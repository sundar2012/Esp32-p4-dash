#include "bsp_uart.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <string.h>

static const char *TAG = "bsp_uart";
static bool s_initialized = false;

esp_err_t bsp_uart_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    uart_config_t uart_config = {
        .baud_rate = BSP_UART_C6_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(BSP_UART_C6_NUM, BSP_UART_C6_BUF_SIZE * 2,
                                         BSP_UART_C6_BUF_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(BSP_UART_C6_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(BSP_UART_C6_NUM, BSP_UART_C6_TX_PIN, BSP_UART_C6_RX_PIN,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "UART to ESP32-C6 initialized (TX=%d, RX=%d, baud=%d)",
             BSP_UART_C6_TX_PIN, BSP_UART_C6_RX_PIN, BSP_UART_C6_BAUD);
    return ESP_OK;
}

int bsp_uart_send(const uint8_t *data, size_t len)
{
    if (!s_initialized || !data) return -1;
    return uart_write_bytes(BSP_UART_C6_NUM, data, len);
}

int bsp_uart_recv(uint8_t *data, size_t max_len, int timeout_ms)
{
    if (!s_initialized || !data) return -1;
    return uart_read_bytes(BSP_UART_C6_NUM, data, max_len, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t bsp_uart_at_cmd(const char *cmd, char *resp, size_t resp_size, int timeout_ms)
{
    if (!s_initialized || !cmd || !resp) return ESP_ERR_INVALID_ARG;

    /* Flush RX buffer before sending command */
    uart_flush_input(BSP_UART_C6_NUM);

    /* Send AT command with CRLF */
    char at_buf[256];
    int cmd_len = snprintf(at_buf, sizeof(at_buf), "%s\r\n", cmd);
    if (cmd_len <= 0 || (size_t)cmd_len >= sizeof(at_buf)) {
        return ESP_ERR_INVALID_SIZE;
    }

    int written = uart_write_bytes(BSP_UART_C6_NUM, at_buf, cmd_len);
    if (written < 0) {
        ESP_LOGE(TAG, "Failed to send AT command: %s", cmd);
        return ESP_FAIL;
    }

    /* Read response */
    memset(resp, 0, resp_size);
    int total_read = 0;
    int remaining_ms = timeout_ms;
    int chunk_ms = 100;

    while (remaining_ms > 0 && (size_t)total_read < resp_size - 1) {
        int read = uart_read_bytes(BSP_UART_C6_NUM,
                                    (uint8_t *)resp + total_read,
                                    resp_size - 1 - total_read,
                                    pdMS_TO_TICKS(chunk_ms));
        if (read > 0) {
            total_read += read;
            resp[total_read] = '\0';

            /* Check for OK or ERROR in response */
            if (strstr(resp, "OK") || strstr(resp, "ERROR") || strstr(resp, "FAIL")) {
                break;
            }
        }
        remaining_ms -= chunk_ms;
    }

    if (strstr(resp, "OK")) {
        ESP_LOGD(TAG, "AT cmd '%s' → OK", cmd);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "AT cmd '%s' → response: %s", cmd, resp);
    return ESP_FAIL;
}

esp_err_t bsp_uart_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    s_initialized = false;
    return uart_driver_delete(BSP_UART_C6_NUM);
}
