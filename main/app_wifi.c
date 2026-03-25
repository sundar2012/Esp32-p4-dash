#include "app_wifi.h"
#include "bsp_uart.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "app_wifi";
static bool s_connected = false;

/* AT command helpers for ESP32-C6 co-processor */
static esp_err_t wifi_at_test(void)
{
    char resp[256];
    return bsp_uart_at_cmd("AT", resp, sizeof(resp), 2000);
}

static esp_err_t wifi_at_set_mode(int mode)
{
    char cmd[32];
    char resp[256];
    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%d", mode);
    return bsp_uart_at_cmd(cmd, resp, sizeof(resp), 2000);
}

static esp_err_t wifi_at_connect(const char *ssid, const char *password)
{
    char cmd[160];
    char resp[512];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    /* Wi-Fi connection can take several seconds */
    return bsp_uart_at_cmd(cmd, resp, sizeof(resp), 15000);
}

static esp_err_t wifi_at_disconnect(void)
{
    char resp[256];
    return bsp_uart_at_cmd("AT+CWQAP", resp, sizeof(resp), 5000);
}

static esp_err_t wifi_at_get_ip(char *ip_buf, size_t ip_buf_size)
{
    char resp[512];
    esp_err_t ret = bsp_uart_at_cmd("AT+CIFSR", resp, sizeof(resp), 3000);
    if (ret != ESP_OK) return ret;

    /* Parse IP from response: +CIFSR:STAIP,"x.x.x.x" */
    char *ip_start = strstr(resp, "STAIP,\"");
    if (ip_start) {
        ip_start += 7;
        char *ip_end = strchr(ip_start, '"');
        if (ip_end && (size_t)(ip_end - ip_start) < ip_buf_size) {
            memcpy(ip_buf, ip_start, ip_end - ip_start);
            ip_buf[ip_end - ip_start] = '\0';
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

esp_err_t app_wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing Wi-Fi via ESP32-C6 AT commands...");

    /* Initialize UART to C6 */
    esp_err_t ret = bsp_uart_init();
    if (ret != ESP_OK) return ret;

    /* Test AT communication */
    ret = wifi_at_test();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP32-C6 not responding to AT commands");
        return ret;
    }

    /* Set station mode */
    ret = wifi_at_set_mode(1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi station mode");
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi module initialized");
    return ESP_OK;
}

esp_err_t app_wifi_connect(void)
{
    char ssid[APP_WIFI_MAX_SSID_LEN] = {0};
    char password[APP_WIFI_MAX_PASS_LEN] = {0};

    /* Read credentials from NVS */
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No Wi-Fi credentials stored in NVS");
        return ESP_ERR_NOT_FOUND;
    }

    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(password);
    nvs_get_str(nvs, APP_WIFI_NVS_KEY_SSID, ssid, &ssid_len);
    nvs_get_str(nvs, APP_WIFI_NVS_KEY_PASS, password, &pass_len);
    nvs_close(nvs);

    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG, "SSID is empty");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s", ssid);
    ret = wifi_at_connect(ssid, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi");
        s_connected = false;
        return ret;
    }

    /* Verify we got an IP */
    char ip[32] = {0};
    ret = wifi_at_get_ip(ip, sizeof(ip));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi connected, IP: %s", ip);
        s_connected = true;
    } else {
        ESP_LOGW(TAG, "Connected but failed to get IP");
        s_connected = true; /* Optimistic — DHCP may still be in progress */
    }

    return ESP_OK;
}

esp_err_t app_wifi_disconnect(void)
{
    esp_err_t ret = wifi_at_disconnect();
    s_connected = false;
    return ret;
}

bool app_wifi_is_connected(void)
{
    return s_connected;
}

esp_err_t app_wifi_set_credentials(const char *ssid, const char *password)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(nvs, APP_WIFI_NVS_KEY_SSID, ssid);
    if (ret == ESP_OK && password) {
        ret = nvs_set_str(nvs, APP_WIFI_NVS_KEY_PASS, password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);

    ESP_LOGI(TAG, "Wi-Fi credentials saved (SSID: %s)", ssid);
    return ret;
}

esp_err_t app_wifi_get_ssid(char *ssid, size_t max_len)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) return ret;

    ret = nvs_get_str(nvs, APP_WIFI_NVS_KEY_SSID, ssid, &max_len);
    nvs_close(nvs);
    return ret;
}
