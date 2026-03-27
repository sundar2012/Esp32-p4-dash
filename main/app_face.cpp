#include "app_face.h"
#include "app_dashboard.h"
#include "bsp_camera.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

/* ESP-DL face detection and recognition headers */
#include "human_face_detect.hpp"
#include "human_face_feat.hpp"

static const char *TAG = "app_face";

/* ESP-DL model instances */
static HumanFaceDetect *s_detector = nullptr;
static HumanFaceFeat *s_recognizer = nullptr;

/* RGB888 conversion buffer (allocated in PSRAM) */
static uint8_t *s_rgb888_buf = nullptr;
#define RGB888_BUF_SIZE (BSP_CAMERA_H_RES * BSP_CAMERA_V_RES * 3)

/* Enrolled face database (in-memory, persisted to NVS) */
typedef struct {
    bool valid;
    char name[APP_FACE_MAX_NAME_LEN];
    float embedding[APP_FACE_EMBEDDING_DIM];
} face_entry_t;

static face_entry_t s_face_db[APP_FACE_MAX_USERS];
static int s_face_count = 0;

/* State */
static app_face_state_t s_state = FACE_STATE_IDLE;
static app_face_cb_t s_callback = NULL;
static TaskHandle_t s_scan_task = NULL;
static bool s_scan_running = false;

/* Enrollment state */
static bool s_enrolling = false;
static char s_enroll_name[APP_FACE_MAX_NAME_LEN];
static int s_enroll_frame_count = 0;
static float s_enroll_accum[APP_FACE_EMBEDDING_DIM];

/* Timing */
static int64_t s_last_face_time = 0;

#define FACE_SCAN_TASK_STACK    (32 * 1024)
#define FACE_SCAN_TASK_PRIORITY 4

/* ──── RGB565 → RGB888 conversion ──── */

static void rgb565_to_rgb888(const uint8_t *src, uint8_t *dst, int width, int height)
{
    const uint16_t *src16 = (const uint16_t *)src;
    int total = width * height;
    for (int i = 0; i < total; i++) {
        uint16_t pixel = src16[i];
        /* RGB565: RRRRRGGGGGGBBBBB (big-endian in memory from CSI) */
        dst[i * 3 + 0] = (pixel >> 8) & 0xF8;         /* R: top 5 bits → 8 bits */
        dst[i * 3 + 1] = (pixel >> 3) & 0xFC;          /* G: mid 6 bits → 8 bits */
        dst[i * 3 + 2] = (pixel << 3) & 0xF8;          /* B: low 5 bits → 8 bits */
    }
}

/* ──── NVS persistence ──── */

static esp_err_t face_db_save(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open_from_partition(APP_FACE_NVS_PARTITION, APP_FACE_NVS_NAMESPACE,
                                             NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ret = nvs_open(APP_FACE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
        if (ret != ESP_OK) return ret;
    }

    nvs_set_i32(nvs, "count", s_face_count);

    for (int i = 0; i < APP_FACE_MAX_USERS; i++) {
        char key_valid[16], key_name[16], key_embed[16];
        snprintf(key_valid, sizeof(key_valid), "v%d", i);
        snprintf(key_name, sizeof(key_name), "n%d", i);
        snprintf(key_embed, sizeof(key_embed), "e%d", i);

        nvs_set_u8(nvs, key_valid, s_face_db[i].valid ? 1 : 0);
        if (s_face_db[i].valid) {
            nvs_set_str(nvs, key_name, s_face_db[i].name);
            nvs_set_blob(nvs, key_embed, s_face_db[i].embedding,
                         sizeof(s_face_db[i].embedding));
        }
    }

    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Face DB saved (%d users)", s_face_count);
    return ESP_OK;
}

static esp_err_t face_db_load(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open_from_partition(APP_FACE_NVS_PARTITION, APP_FACE_NVS_NAMESPACE,
                                             NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        ret = nvs_open(APP_FACE_NVS_NAMESPACE, NVS_READONLY, &nvs);
        if (ret != ESP_OK) {
            ESP_LOGI(TAG, "No saved face DB found");
            return ESP_ERR_NOT_FOUND;
        }
    }

    int32_t count = 0;
    nvs_get_i32(nvs, "count", &count);
    s_face_count = 0;

    for (int i = 0; i < APP_FACE_MAX_USERS; i++) {
        char key_valid[16], key_name[16], key_embed[16];
        snprintf(key_valid, sizeof(key_valid), "v%d", i);
        snprintf(key_name, sizeof(key_name), "n%d", i);
        snprintf(key_embed, sizeof(key_embed), "e%d", i);

        uint8_t valid = 0;
        nvs_get_u8(nvs, key_valid, &valid);
        s_face_db[i].valid = (valid != 0);

        if (s_face_db[i].valid) {
            size_t name_len = sizeof(s_face_db[i].name);
            nvs_get_str(nvs, key_name, s_face_db[i].name, &name_len);

            size_t embed_len = sizeof(s_face_db[i].embedding);
            nvs_get_blob(nvs, key_embed, s_face_db[i].embedding, &embed_len);
            s_face_count++;
        }
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "Face DB loaded (%d users)", s_face_count);
    return ESP_OK;
}

/* ──── Face matching (cosine similarity) ──── */

static float cosine_similarity(const float *a, const float *b, int dim)
{
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (int i = 0; i < dim; i++) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    if (norm_a == 0.0f || norm_b == 0.0f) return 0.0f;
    return dot / (sqrtf(norm_a) * sqrtf(norm_b));
}

static int face_match(const float *embedding, float *out_confidence)
{
    int best_idx = -1;
    float best_score = APP_FACE_MATCH_THRESHOLD;

    for (int i = 0; i < APP_FACE_MAX_USERS; i++) {
        if (!s_face_db[i].valid) continue;
        float score = cosine_similarity(embedding, s_face_db[i].embedding, APP_FACE_EMBEDDING_DIM);
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    *out_confidence = best_score;
    return best_idx;
}

/* ──── Face detection + recognition pipeline using ESP-DL ──── */

typedef struct {
    bool face_detected;
    int x, y, w, h;
    float embedding[APP_FACE_EMBEDDING_DIM];
} face_detect_result_t;

static esp_err_t face_detect_and_recognize(const bsp_camera_fb_t *fb, face_detect_result_t *result)
{
    result->face_detected = false;

    if (!s_detector || !s_recognizer || !s_rgb888_buf) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Convert camera RGB565 frame to RGB888 for ESP-DL models */
    rgb565_to_rgb888(fb->data, s_rgb888_buf, fb->width, fb->height);

    /* Create ESP-DL image tensor: HWC layout, RGB888 */
    dl::image::img_t img;
    img.data = s_rgb888_buf;
    img.width = fb->width;
    img.height = fb->height;
    img.channel = 3;

    /* Run face detection */
    auto &detections = s_detector->run(img);

    if (detections.empty()) {
        return ESP_OK;
    }

    /* Use the first (highest-confidence) detected face */
    auto &det = detections.front();
    result->x = (int)det.box[0];
    result->y = (int)det.box[1];
    result->w = (int)(det.box[2] - det.box[0]);
    result->h = (int)(det.box[3] - det.box[1]);

    ESP_LOGD(TAG, "Face detected at (%d,%d) %dx%d, score=%.2f",
             result->x, result->y, result->w, result->h, det.score);

    /* Extract face feature embedding using the detected keypoints */
    auto &feats = s_recognizer->run(img, detections);

    if (!feats.empty()) {
        result->face_detected = true;
        /* Copy the embedding vector */
        const auto &feat = feats.front();
        int dim = feat.size();
        if (dim > APP_FACE_EMBEDDING_DIM) dim = APP_FACE_EMBEDDING_DIM;
        for (int i = 0; i < dim; i++) {
            result->embedding[i] = feat[i];
        }
        /* Zero-pad if embedding is shorter than expected */
        for (int i = dim; i < APP_FACE_EMBEDDING_DIM; i++) {
            result->embedding[i] = 0.0f;
        }
    }

    return ESP_OK;
}

/* ──── Face scan task ──── */

static void face_scan_task(void *pvParam)
{
    (void)pvParam;
    ESP_LOGI(TAG, "Face scan task started");
    s_last_face_time = esp_timer_get_time();

    while (s_scan_running) {
        /* Capture a frame */
        bsp_camera_fb_t *fb = bsp_camera_fb_get();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Run face detection + recognition */
        face_detect_result_t det = {};
        face_detect_and_recognize(fb, &det);
        bsp_camera_fb_return(fb);

        if (det.face_detected) {
            s_last_face_time = esp_timer_get_time();

            if (s_enrolling) {
                /* Accumulate embeddings for enrollment */
                for (int i = 0; i < APP_FACE_EMBEDDING_DIM; i++) {
                    s_enroll_accum[i] += det.embedding[i];
                }
                s_enroll_frame_count++;
                ESP_LOGI(TAG, "Enrollment frame %d/%d for '%s'",
                         s_enroll_frame_count, APP_FACE_ENROLL_FRAMES, s_enroll_name);

                if (s_enroll_frame_count >= APP_FACE_ENROLL_FRAMES) {
                    /* Average the embeddings and store */
                    int slot = -1;
                    for (int i = 0; i < APP_FACE_MAX_USERS; i++) {
                        if (!s_face_db[i].valid) { slot = i; break; }
                    }
                    if (slot >= 0) {
                        for (int i = 0; i < APP_FACE_EMBEDDING_DIM; i++) {
                            s_face_db[slot].embedding[i] = s_enroll_accum[i] / s_enroll_frame_count;
                        }
                        strncpy(s_face_db[slot].name, s_enroll_name, APP_FACE_MAX_NAME_LEN - 1);
                        s_face_db[slot].valid = true;
                        s_face_count++;
                        face_db_save();
                        ESP_LOGI(TAG, "Enrolled user '%s' in slot %d", s_enroll_name, slot);
                    }
                    s_enrolling = false;
                    s_state = FACE_STATE_DETECTING;
                }
            } else {
                /* Try to match against enrolled faces */
                float confidence = 0.0f;
                int match = face_match(det.embedding, &confidence);

                if (match >= 0) {
                    s_state = FACE_STATE_RECOGNIZED;
                    app_face_result_t result = {};
                    result.user_id = match;
                    result.confidence = confidence;
                    strncpy(result.name, s_face_db[match].name, APP_FACE_MAX_NAME_LEN - 1);

                    ESP_LOGI(TAG, "Recognized: %s (confidence: %.2f)", result.name, confidence);

                    if (s_callback) {
                        s_callback(FACE_STATE_RECOGNIZED, &result);
                    }

                    /* Route to appropriate dashboard */
                    if (bsp_display_lock(100)) {
                        if (strcmp(result.name, "sundar") == 0) {
                            app_dashboard_show(DASHBOARD_SUNDAR);
                        } else if (strcmp(result.name, "user2") == 0) {
                            app_dashboard_show(DASHBOARD_USER2);
                        } else {
                            app_dashboard_show(DASHBOARD_GUEST);
                        }
                        bsp_display_unlock();
                    }

                    /* Pause scanning briefly after recognition */
                    vTaskDelay(pdMS_TO_TICKS(3000));
                } else {
                    s_state = FACE_STATE_UNKNOWN;
                    if (s_callback) {
                        s_callback(FACE_STATE_UNKNOWN, NULL);
                    }
                }
            }
        } else {
            /* No face detected — check for timeout */
            int64_t elapsed_us = esp_timer_get_time() - s_last_face_time;
            if (elapsed_us > (int64_t)APP_FACE_NO_FACE_TIMEOUT_S * 1000000) {
                if (s_state != FACE_STATE_IDLE) {
                    ESP_LOGI(TAG, "No face for %ds — showing screensaver", APP_FACE_NO_FACE_TIMEOUT_S);
                    s_state = FACE_STATE_IDLE;
                    if (bsp_display_lock(100)) {
                        app_dashboard_show(DASHBOARD_SCREENSAVER);
                        bsp_display_unlock();
                    }
                }
            }
        }

        /* ~5 FPS scan rate (face detection + recognition takes ~110ms on P4) */
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    ESP_LOGI(TAG, "Face scan task stopped");
    vTaskDelete(NULL);
}

/* ──── Public API ──── */

extern "C" esp_err_t app_face_init(void)
{
    ESP_LOGI(TAG, "Initializing face recognition system...");

    memset(s_face_db, 0, sizeof(s_face_db));
    s_face_count = 0;

    /* Load enrolled faces from NVS */
    face_db_load();

    /* Allocate RGB888 conversion buffer in PSRAM */
    s_rgb888_buf = (uint8_t *)heap_caps_malloc(RGB888_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_rgb888_buf) {
        ESP_LOGE(TAG, "Failed to allocate RGB888 buffer (%d bytes)", RGB888_BUF_SIZE);
        return ESP_ERR_NO_MEM;
    }

    /* Initialize face detection model (two-stage, optimized for ESP32-P4) */
    ESP_LOGI(TAG, "Loading face detection model...");
    s_detector = new (std::nothrow) HumanFaceDetect();
    if (!s_detector) {
        ESP_LOGE(TAG, "Failed to create face detector");
        heap_caps_free(s_rgb888_buf);
        s_rgb888_buf = nullptr;
        return ESP_ERR_NO_MEM;
    }

    /* Initialize face recognition / feature extraction model */
    ESP_LOGI(TAG, "Loading face recognition model...");
    s_recognizer = new (std::nothrow) HumanFaceFeat();
    if (!s_recognizer) {
        ESP_LOGE(TAG, "Failed to create face recognizer");
        delete s_detector;
        s_detector = nullptr;
        heap_caps_free(s_rgb888_buf);
        s_rgb888_buf = nullptr;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Face recognition initialized (%d enrolled users)", s_face_count);
    ESP_LOGI(TAG, "Free PSRAM after model load: %lu bytes",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return ESP_OK;
}

extern "C" void app_face_start(void)
{
    if (s_scan_running) return;
    if (!s_detector || !s_recognizer) {
        ESP_LOGE(TAG, "Cannot start scan — models not loaded");
        return;
    }

    s_scan_running = true;
    s_state = FACE_STATE_DETECTING;
    xTaskCreatePinnedToCore(face_scan_task, "face_scan", FACE_SCAN_TASK_STACK,
                            NULL, FACE_SCAN_TASK_PRIORITY, &s_scan_task, 1);
}

extern "C" void app_face_stop(void)
{
    s_scan_running = false;
    s_state = FACE_STATE_IDLE;
}

extern "C" esp_err_t app_face_enroll_start(const char *name)
{
    if (!name || strlen(name) == 0) return ESP_ERR_INVALID_ARG;
    if (s_face_count >= APP_FACE_MAX_USERS) {
        ESP_LOGE(TAG, "Face DB full (%d users)", APP_FACE_MAX_USERS);
        return ESP_ERR_NO_MEM;
    }

    strncpy(s_enroll_name, name, APP_FACE_MAX_NAME_LEN - 1);
    s_enroll_name[APP_FACE_MAX_NAME_LEN - 1] = '\0';
    memset(s_enroll_accum, 0, sizeof(s_enroll_accum));
    s_enroll_frame_count = 0;
    s_enrolling = true;
    s_state = FACE_STATE_ENROLLING;

    ESP_LOGI(TAG, "Enrollment started for '%s' — capture %d frames", name, APP_FACE_ENROLL_FRAMES);
    return ESP_OK;
}

extern "C" void app_face_enroll_cancel(void)
{
    s_enrolling = false;
    s_state = FACE_STATE_DETECTING;
    ESP_LOGI(TAG, "Enrollment cancelled");
}

extern "C" esp_err_t app_face_delete_user(const char *name)
{
    for (int i = 0; i < APP_FACE_MAX_USERS; i++) {
        if (s_face_db[i].valid && strcmp(s_face_db[i].name, name) == 0) {
            s_face_db[i].valid = false;
            memset(s_face_db[i].name, 0, sizeof(s_face_db[i].name));
            memset(s_face_db[i].embedding, 0, sizeof(s_face_db[i].embedding));
            s_face_count--;
            face_db_save();
            ESP_LOGI(TAG, "Deleted user '%s'", name);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

extern "C" int app_face_get_user_count(void)
{
    return s_face_count;
}

extern "C" void app_face_set_callback(app_face_cb_t cb)
{
    s_callback = cb;
}

extern "C" app_face_state_t app_face_get_state(void)
{
    return s_state;
}
