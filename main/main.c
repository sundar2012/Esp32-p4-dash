#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_camera.h"
#include "app_wifi.h"
#include "app_face.h"
#include "app_dashboard.h"
#include "app_calendar.h"
#include "app_shortcuts.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  CrowPanel ESP32-P4 Face Dashboard      ║");
    ESP_LOGI(TAG, "║  1024x600 MIPI-DSI | 2MP MIPI-CSI       ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    /* Step 1: Initialize NVS (required for Wi-Fi credentials + face DB) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Also initialize the dedicated face_db NVS partition */
    ret = nvs_flash_init_partition("face_db");
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition("face_db");
        ret = nvs_flash_init_partition("face_db");
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "face_db partition init failed: %s (will use default NVS)", esp_err_to_name(ret));
    }

    /* Step 2: Initialize I2C bus (shared by touch controller and camera SCCB) */
    ESP_LOGI(TAG, "Initializing I2C bus...");
    ESP_ERROR_CHECK(bsp_i2c_init());

    /* Step 3: Initialize display (MIPI-DSI + LVGL + touch) */
    ESP_LOGI(TAG, "Initializing display...");
    ESP_ERROR_CHECK(bsp_display_init());

    /* Step 4: Show splash screen while other subsystems initialize */
    if (bsp_display_lock(-1)) {
        app_dashboard_show_splash();
        bsp_display_unlock();
    }

    /* Step 5: Initialize camera (MIPI-CSI) */
    ESP_LOGI(TAG, "Initializing camera...");
    ret = bsp_camera_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Camera init failed — face recognition disabled");
    }

    /* Step 6: Initialize Wi-Fi via ESP32-C6 */
    ESP_LOGI(TAG, "Initializing Wi-Fi...");
    ret = app_wifi_init();
    if (ret == ESP_OK) {
        ret = app_wifi_connect();
        if (ret == ESP_OK) {
            /* Start background services that need network */
            app_calendar_start();
        } else {
            ESP_LOGW(TAG, "Wi-Fi connection failed — calendar/shortcuts unavailable until configured");
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi init failed — running in offline mode");
    }

    /* Step 7: Initialize shortcuts module */
    app_shortcuts_init();

    /* Step 8: Initialize face recognition */
    ESP_LOGI(TAG, "Starting face recognition...");
    bool face_available = false;
    ret = app_face_init();
    if (ret == ESP_OK) {
        face_available = true;
        ESP_LOGI(TAG, "Face recognition models loaded successfully");
    } else {
        ESP_LOGW(TAG, "Face recognition unavailable: %s", esp_err_to_name(ret));
    }

    /* Show the screensaver after splash, then start face scanning */
    vTaskDelay(pdMS_TO_TICKS(1500));  /* Show splash for 1.5s */
    if (bsp_display_lock(-1)) {
        if (face_available) {
            app_dashboard_show(DASHBOARD_SCREENSAVER);
        } else {
            /* No face recognition — go straight to Sundar's dashboard */
            app_dashboard_show(DASHBOARD_SUNDAR);
        }
        bsp_display_unlock();
    }

    /* Start face scanning in background */
    if (face_available) {
        app_face_start();
        ESP_LOGI(TAG, "Face scanning active — waiting for faces...");
    }

    ESP_LOGI(TAG, "System initialization complete");
    ESP_LOGI(TAG, "Free heap: %lu bytes, free PSRAM: %lu bytes",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
