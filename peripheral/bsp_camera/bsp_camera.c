#include "bsp_camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "bsp_camera";

/* Double-buffered frame storage in PSRAM */
#define BSP_CAMERA_FB_COUNT  2
static bsp_camera_fb_t s_fb[BSP_CAMERA_FB_COUNT];
static uint8_t *s_fb_mem[BSP_CAMERA_FB_COUNT] = {NULL};
static SemaphoreHandle_t s_fb_mutex = NULL;
static bool s_initialized = false;

/* Frame buffer size: RGB565 format */
#define BSP_CAMERA_FB_SIZE  (BSP_CAMERA_H_RES * BSP_CAMERA_V_RES * 2)

esp_err_t bsp_camera_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Camera already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MIPI-CSI camera (%dx%d @ %dfps)...",
             BSP_CAMERA_H_RES, BSP_CAMERA_V_RES, BSP_CAMERA_FPS);

    /* Allocate frame buffers in PSRAM */
    for (int i = 0; i < BSP_CAMERA_FB_COUNT; i++) {
        s_fb_mem[i] = heap_caps_calloc(1, BSP_CAMERA_FB_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_fb_mem[i]) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer %d (%d bytes)", i, BSP_CAMERA_FB_SIZE);
            bsp_camera_deinit();
            return ESP_ERR_NO_MEM;
        }
        s_fb[i].data = s_fb_mem[i];
        s_fb[i].len = BSP_CAMERA_FB_SIZE;
        s_fb[i].width = BSP_CAMERA_H_RES;
        s_fb[i].height = BSP_CAMERA_V_RES;
    }

    s_fb_mutex = xSemaphoreCreateMutex();
    if (!s_fb_mutex) {
        ESP_LOGE(TAG, "Failed to create frame buffer mutex");
        bsp_camera_deinit();
        return ESP_ERR_NO_MEM;
    }

    /* TODO: Initialize the actual MIPI-CSI hardware via esp_cam_sensor APIs.
     *
     * The ESP32-P4 MIPI-CSI driver flow is:
     *   1. Configure CSI host (lane count, clock)
     *   2. Configure camera sensor via SCCB/I2C (resolution, format, FPS)
     *   3. Start CSI reception into DMA-linked frame buffers
     *
     * The CrowPanel uses a specific camera sensor (likely OV2640 or SC2336)
     * connected via MIPI-CSI. The exact sensor model determines the
     * initialization sequence.
     *
     * For now, we set up the frame buffer infrastructure. The actual
     * hardware init will be completed when building against the full
     * ESP-IDF + ESP-WHO toolchain with the CrowPanel BSP. */

    s_initialized = true;
    ESP_LOGI(TAG, "Camera initialized (frame buffer %d x %d bytes in PSRAM)",
             BSP_CAMERA_FB_COUNT, BSP_CAMERA_FB_SIZE);
    return ESP_OK;
}

bsp_camera_fb_t *bsp_camera_fb_get(void)
{
    if (!s_initialized) return NULL;

    /* TODO: In production, this triggers a CSI frame capture and waits
     * for DMA completion. For now, return the first frame buffer
     * (will contain zeros / test pattern). */
    if (xSemaphoreTake(s_fb_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        return &s_fb[0];
    }
    return NULL;
}

void bsp_camera_fb_return(bsp_camera_fb_t *fb)
{
    if (!fb || !s_fb_mutex) return;
    xSemaphoreGive(s_fb_mutex);
}

esp_err_t bsp_camera_deinit(void)
{
    s_initialized = false;

    if (s_fb_mutex) {
        vSemaphoreDelete(s_fb_mutex);
        s_fb_mutex = NULL;
    }

    for (int i = 0; i < BSP_CAMERA_FB_COUNT; i++) {
        if (s_fb_mem[i]) {
            heap_caps_free(s_fb_mem[i]);
            s_fb_mem[i] = NULL;
            s_fb[i].data = NULL;
        }
    }

    ESP_LOGI(TAG, "Camera deinitialized");
    return ESP_OK;
}
