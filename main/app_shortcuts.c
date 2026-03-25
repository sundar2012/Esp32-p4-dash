#include "app_shortcuts.h"
#include "app_wifi.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "app_shortcuts";
static char s_mac_ip[32] = "192.168.1.50";

/* URL-encode a string (minimal: spaces and special chars) */
static void url_encode(const char *src, char *dst, size_t dst_size)
{
    size_t di = 0;
    for (size_t si = 0; src[si] && di < dst_size - 4; si++) {
        char c = src[si];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = c;
        } else if (c == ' ') {
            dst[di++] = '%';
            dst[di++] = '2';
            dst[di++] = '0';
        } else {
            int written = snprintf(dst + di, dst_size - di, "%%%02X", (unsigned char)c);
            if (written > 0) di += written;
        }
    }
    dst[di] = '\0';
}

static esp_err_t http_trigger(const char *url)
{
    if (!app_wifi_is_connected()) {
        ESP_LOGW(TAG, "Wi-Fi not connected — cannot trigger");
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Trigger → HTTP %d: %s", status, url);
        if (status < 200 || status >= 300) {
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Trigger failed: %s → %s", esp_err_to_name(err), url);
    }

    esp_http_client_cleanup(client);
    return err;
}

void app_shortcuts_init(void)
{
    /* Load Mac IP from NVS */
    nvs_handle_t nvs;
    if (nvs_open(APP_SHORTCUTS_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t ip_len = sizeof(s_mac_ip);
        nvs_get_str(nvs, APP_SHORTCUTS_NVS_KEY_MAC_IP, s_mac_ip, &ip_len);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "Shortcuts module initialized (Mac IP: %s)", s_mac_ip);
}

esp_err_t app_shortcuts_trigger_btt(const char *trigger_name)
{
    if (!trigger_name) return ESP_ERR_INVALID_ARG;

    char encoded_name[128];
    url_encode(trigger_name, encoded_name, sizeof(encoded_name));

    char url[256];
    snprintf(url, sizeof(url),
             "http://%s:%d/trigger_named/?trigger_name=%s",
             s_mac_ip, APP_SHORTCUTS_BTT_PORT, encoded_name);

    ESP_LOGI(TAG, "BTT trigger: %s", trigger_name);
    return http_trigger(url);
}

esp_err_t app_shortcuts_trigger_shortcut(const char *shortcut_name)
{
    if (!shortcut_name) return ESP_ERR_INVALID_ARG;

    char encoded_name[128];
    url_encode(shortcut_name, encoded_name, sizeof(encoded_name));

    char url[256];
    snprintf(url, sizeof(url),
             "http://%s:%d/trigger?shortcut=%s",
             s_mac_ip, APP_SHORTCUTS_BRIDGE_PORT, encoded_name);

    ESP_LOGI(TAG, "Shortcut trigger: %s", shortcut_name);
    return http_trigger(url);
}

esp_err_t app_shortcuts_set_mac_ip(const char *ip)
{
    if (!ip) return ESP_ERR_INVALID_ARG;

    strncpy(s_mac_ip, ip, sizeof(s_mac_ip) - 1);
    s_mac_ip[sizeof(s_mac_ip) - 1] = '\0';

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(APP_SHORTCUTS_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret == ESP_OK) {
        nvs_set_str(nvs, APP_SHORTCUTS_NVS_KEY_MAC_IP, s_mac_ip);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    ESP_LOGI(TAG, "Mac IP set to: %s", s_mac_ip);
    return ESP_OK;
}

esp_err_t app_shortcuts_get_mac_ip(char *ip, size_t max_len)
{
    if (!ip) return ESP_ERR_INVALID_ARG;
    strncpy(ip, s_mac_ip, max_len - 1);
    ip[max_len - 1] = '\0';
    return ESP_OK;
}
