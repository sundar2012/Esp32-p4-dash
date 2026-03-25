#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wi-Fi credentials stored in NVS */
#define APP_WIFI_NVS_NAMESPACE  "wifi_cfg"
#define APP_WIFI_NVS_KEY_SSID   "ssid"
#define APP_WIFI_NVS_KEY_PASS   "password"
#define APP_WIFI_MAX_SSID_LEN   32
#define APP_WIFI_MAX_PASS_LEN   64

/**
 * @brief Initialize Wi-Fi via ESP32-C6 co-processor (AT commands over UART)
 * @return ESP_OK on success
 */
esp_err_t app_wifi_init(void);

/**
 * @brief Connect to the configured Wi-Fi network
 * @return ESP_OK if connected successfully
 */
esp_err_t app_wifi_connect(void);

/**
 * @brief Disconnect from Wi-Fi
 * @return ESP_OK on success
 */
esp_err_t app_wifi_disconnect(void);

/**
 * @brief Check if Wi-Fi is connected
 * @return true if connected
 */
bool app_wifi_is_connected(void);

/**
 * @brief Store Wi-Fi credentials in NVS
 * @param ssid Network SSID
 * @param password Network password
 * @return ESP_OK on success
 */
esp_err_t app_wifi_set_credentials(const char *ssid, const char *password);

/**
 * @brief Get stored Wi-Fi SSID from NVS
 * @param ssid Buffer to store SSID
 * @param max_len Buffer size
 * @return ESP_OK if credentials found
 */
esp_err_t app_wifi_get_ssid(char *ssid, size_t max_len);

#ifdef __cplusplus
}
#endif
