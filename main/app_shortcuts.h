#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shortcut/BTT trigger configuration */
#define APP_SHORTCUTS_NVS_NAMESPACE     "shortcuts"
#define APP_SHORTCUTS_NVS_KEY_MAC_IP    "mac_ip"
#define APP_SHORTCUTS_BTT_PORT          12345
#define APP_SHORTCUTS_BRIDGE_PORT       8765

/**
 * @brief Initialize the shortcuts module
 *
 * Loads Mac IP and port configuration from NVS.
 */
void app_shortcuts_init(void);

/**
 * @brief Trigger a BetterTouchTool named trigger
 * @param trigger_name BTT trigger name
 * @return ESP_OK if HTTP request sent successfully
 */
esp_err_t app_shortcuts_trigger_btt(const char *trigger_name);

/**
 * @brief Trigger a macOS Shortcut via the bridge server
 * @param shortcut_name macOS Shortcut name
 * @return ESP_OK if HTTP request sent successfully
 */
esp_err_t app_shortcuts_trigger_shortcut(const char *shortcut_name);

/**
 * @brief Set the Mac's IP address for trigger requests
 * @param ip IP address string (e.g., "192.168.1.50")
 * @return ESP_OK on success
 */
esp_err_t app_shortcuts_set_mac_ip(const char *ip);

/**
 * @brief Get the configured Mac IP address
 * @param ip Buffer to store IP string
 * @param max_len Buffer size
 * @return ESP_OK if IP is configured
 */
esp_err_t app_shortcuts_get_mac_ip(char *ip, size_t max_len);

#ifdef __cplusplus
}
#endif
