#include "app_face.h"
#include "app_dashboard.h"
#include "bsp_camera.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

/* ESP-DL face detection and recognition headers */
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"

static const char *TAG = "app_face";

/* ESP-DL model instances */
static HumanFaceDetect *s_detector = nullptr;
static HumanFaceRecognizer *s_recognizer = nullptr;

/* RGB888 conversion buffer (allocated in PSRAM) */
static uint8_t *s_rgb888_buf = nullptr;
#define RGB888_BUF_SIZE (BSP_CAMERA_H_RES * BSP_CAMERA_V_RES * 3)

/* SPIFFS mount point for face DB */
#define FACE_DB_MOUNT_POINT "/spiffs"
#define FACE_DB_PATH        "/spiffs/face.db"

/*
 * ID-to-name mapping stored in NVS.
 * HumanFaceRecognizer assigns auto-incrementing uint16_t IDs.
 * We map those IDs to user names in NVS.
 */
typedef struct {
    bool valid;
    uint16_t face_id;
    char name[APP_FACE_MAX_NAME_LEN];
} face_name_entry_t;

static face_name_entry_t s_name_map[APP_FACE_MAX_USERS];
static int s_user_count = 0;

/* State */
static app_face_state_t s_state = FACE_STATE_IDLE;
static app_face_cb_t s_callback = NULL;
static TaskHandle_t s_scan_task = NULL;
static bool s_scan_running = false;

/* Enrollment state */
static bool s_enrolling = false;
static char s_enroll_name[APP_FACE_MAX_NAME_LEN];

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
        dst[i * 3 + 0] = (pixel >> 8) & 0xF8;
        dst[i * 3 + 1] = (pixel >> 3) & 0xFC;
        dst[i * 3 + 2] = (pixel << 3) & 0xF8;
    }
}

/* ──── NVS: ID-to-name mapping persistence ──── */

static esp_err_t name_map_save(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open_from_partition(APP_FACE_NVS_PARTITION, APP_FACE_NVS_NAMESPACE,
                                             NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ret = nvs_open(APP_FACE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
        if (ret != ESP_OK) return ret;
    }

    nvs_set_i32(nvs, "count", s_user_count);
    for (int i = 0; i < APP_FACE_MAX_USERS; i++) {
        char kv[16], kn[16], ki[16];
        snprintf(kv, sizeof(kv), "v%d", i);
        snprintf(kn, sizeof(kn), "n%d", i);
        snprintf(ki, sizeof(ki), "i%d", i);

        nvs_set_u8(nvs, kv, s_name_map[i].valid ? 1 : 0);
        if (s_name_map[i].valid) {
            nvs_set_str(nvs, kn, s_name_map[i].name);
            nvs_set_u16(nvs, ki, s_name_map[i].face_id);
        }
    }

    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Name map saved (%d users)", s_user_count);
    return ESP_OK;
}

static esp_err_t name_map_load(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open_from_partition(APP_FACE_NVS_PARTITION, APP_FACE_NVS_NAMESPACE,
                                             NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        ret = nvs_open(APP_FACE_NVS_NAMESPACE, NVS_READONLY, &nvs);
        if (ret != ESP_OK) {
            ESP_LOGI(TAG, "No saved name map found");
            return ESP_ERR_NOT_FOUND;
        }
    }

    int32_t count = 0;
    nvs_get_i32(nvs, "count", &count);
    s_user_count = 0;

    for (int i = 0; i < APP_FACE_MAX_USERS; i++) {
        char kv[16], kn[16], ki[16];
        snprintf(kv, sizeof(kv), "v%d", i);
        snprintf(kn, sizeof(kn), "n%d", i);
        snprintf(ki, sizeof(ki), "i%d", i);

        uint8_t valid = 0;
        nvs_get_u8(nvs, kv, &valid);
        s_name_map[i].valid = (valid != 0);

        if (s_name_map[i].valid) {
            size_t name_len = sizeof(s_name_map[i].name);
            nvs_get_str(nvs, kn, s_name_map[i].name, &name_len);
            nvs_get_u16(nvs, ki, &s_name_map[i].face_id);
            s_user_count++;
        }
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "Name map loaded (%d users)", s_user_count);
    return ESP_OK;
}

static const char *name_for_id(uint16_t face_id)
{
    for (int i = 0; i < APP_FACE_MAX_USERS; i++) {
        if (s_name_map[i].valid && s_name_map[i].face_id == face_id) {
            return s_name_map[i].name;
        }
    }
    return NULL;
}

static int name_map_add(uint16_t face_id, const char *name)
{
    for (int i = 0; i < APP_FACE_MAX_USERS; i++) {
        if (!s_name_map[i].valid) {
            s_name_map[i].valid = true;
            s_name_map[i].face_id = face_id;
            strncpy(s_name_map[i].name, name, APP_FACE_MAX_NAME_LEN - 1);
            s_name_map[i].name[APP_FACE_MAX_NAME_LEN - 1] = '\0';
            s_user_count++;
            name_map_save();
            return i;
        }
    }
    return -1;
}

/* ──── SPIFFS for face DB file ──── */

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = FACE_DB_MOUNT_POINT;
    conf.partition_label = "storage";
    conf.max_files = 5;
    conf.format_if_mount_failed = true;

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %d/%d bytes used", (int)used, (int)total);
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

        /* Convert camera RGB565 frame to RGB888 for ESP-DL models */
        rgb565_to_rgb888(fb->data, s_rgb888_buf, fb->width, fb->height);
        int img_width = fb->width;
        int img_height = fb->height;
        bsp_camera_fb_return(fb);

        /* Create ESP-DL image descriptor */
        dl::image::img_t img = {};
        img.data = s_rgb888_buf;
        img.width = img_width;
        img.height = img_height;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;

        /* Run face detection */
        auto &detections = s_detector->run(img);

        if (!detections.empty()) {
            s_last_face_time = esp_timer_get_time();

            auto &det = detections.front();
            ESP_LOGD(TAG, "Face at (%.0f,%.0f)-(%.0f,%.0f), score=%.2f",
                     det.box[0], det.box[1], det.box[2], det.box[3], det.score);

            if (s_enrolling) {
                /* Enroll the detected face into the recognizer's DB */
                esp_err_t err = s_recognizer->enroll(img, detections);
                if (err == ESP_OK) {
                    /* Get the ID that was just assigned (it's the latest entry) */
                    int num_feats = s_recognizer->get_num_feats();
                    /* The last enrolled ID = num_feats (1-based in ESP-DL DB) */
                    uint16_t new_id = (uint16_t)(num_feats);
                    int slot = name_map_add(new_id, s_enroll_name);
                    ESP_LOGI(TAG, "Enrolled '%s' as face ID %d (slot %d)",
                             s_enroll_name, new_id, slot);
                } else {
                    ESP_LOGW(TAG, "Enrollment failed: %s", esp_err_to_name(err));
                }
                s_enrolling = false;
                s_state = FACE_STATE_DETECTING;
            } else {
                /* Try to recognize the face */
                auto results = s_recognizer->recognize(img, detections);

                if (!results.empty() && results[0].similarity >= APP_FACE_MATCH_THRESHOLD) {
                    auto &best = results[0];
                    const char *name = name_for_id(best.id);

                    s_state = FACE_STATE_RECOGNIZED;
                    app_face_result_t result = {};
                    result.user_id = best.id;
                    result.confidence = best.similarity;
                    if (name) {
                        strncpy(result.name, name, APP_FACE_MAX_NAME_LEN - 1);
                    } else {
                        snprintf(result.name, APP_FACE_MAX_NAME_LEN, "user_%d", best.id);
                    }

                    ESP_LOGI(TAG, "Recognized: %s (ID=%d, similarity=%.2f)",
                             result.name, best.id, best.similarity);

                    if (s_callback) {
                        s_callback(FACE_STATE_RECOGNIZED, &result);
                    }

                    /* Route to appropriate dashboard */
                    if (bsp_display_lock(100)) {
                        if (name && strcmp(name, "sundar") == 0) {
                            app_dashboard_show(DASHBOARD_SUNDAR);
                        } else if (name && strcmp(name, "user2") == 0) {
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

    memset(s_name_map, 0, sizeof(s_name_map));
    s_user_count = 0;

    /* Mount SPIFFS for face DB file */
    esp_err_t ret = init_spiffs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS init failed — face recognition disabled");
        return ret;
    }

    /* Load ID-to-name mapping from NVS */
    name_map_load();

    /* Allocate RGB888 conversion buffer in PSRAM */
    s_rgb888_buf = (uint8_t *)heap_caps_malloc(RGB888_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_rgb888_buf) {
        ESP_LOGE(TAG, "Failed to allocate RGB888 buffer (%d bytes)", RGB888_BUF_SIZE);
        return ESP_ERR_NO_MEM;
    }

    /* Initialize face detection model */
    ESP_LOGI(TAG, "Loading face detection model...");
    s_detector = new (std::nothrow) HumanFaceDetect();
    if (!s_detector) {
        ESP_LOGE(TAG, "Failed to create face detector");
        heap_caps_free(s_rgb888_buf);
        s_rgb888_buf = nullptr;
        return ESP_ERR_NO_MEM;
    }

    /* Initialize face recognizer (detection + feature extraction + DB) */
    ESP_LOGI(TAG, "Loading face recognition model...");
    s_recognizer = new (std::nothrow) HumanFaceRecognizer(FACE_DB_PATH);
    if (!s_recognizer) {
        ESP_LOGE(TAG, "Failed to create face recognizer");
        delete s_detector;
        s_detector = nullptr;
        heap_caps_free(s_rgb888_buf);
        s_rgb888_buf = nullptr;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Face recognition initialized (%d enrolled users, %d feats in DB)",
             s_user_count, s_recognizer->get_num_feats());
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
    if (s_user_count >= APP_FACE_MAX_USERS) {
        ESP_LOGE(TAG, "Face DB full (%d users)", APP_FACE_MAX_USERS);
        return ESP_ERR_NO_MEM;
    }

    strncpy(s_enroll_name, name, APP_FACE_MAX_NAME_LEN - 1);
    s_enroll_name[APP_FACE_MAX_NAME_LEN - 1] = '\0';
    s_enrolling = true;
    s_state = FACE_STATE_ENROLLING;

    ESP_LOGI(TAG, "Enrollment started for '%s'", name);
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
        if (s_name_map[i].valid && strcmp(s_name_map[i].name, name) == 0) {
            if (s_recognizer) {
                s_recognizer->delete_feat(s_name_map[i].face_id);
            }
            s_name_map[i].valid = false;
            memset(s_name_map[i].name, 0, sizeof(s_name_map[i].name));
            s_user_count--;
            name_map_save();
            ESP_LOGI(TAG, "Deleted user '%s'", name);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

extern "C" int app_face_get_user_count(void)
{
    return s_user_count;
}

extern "C" void app_face_set_callback(app_face_cb_t cb)
{
    s_callback = cb;
}

extern "C" app_face_state_t app_face_get_state(void)
{
    return s_state;
}
