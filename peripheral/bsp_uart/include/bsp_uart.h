#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UART to ESP32-C6 co-processor configuration
 * The ESP32-C6 handles Wi-Fi 6 / BLE 5.3 via AT commands over UART */
#define BSP_UART_C6_NUM         1
#define BSP_UART_C6_TX_PIN      24
#define BSP_UART_C6_RX_PIN      25
#define BSP_UART_C6_BAUD        115200
#define BSP_UART_C6_BUF_SIZE    2048

/**
 * @brief Initialize UART to the ESP32-C6 co-processor
 * @return ESP_OK on success
 */
esp_err_t bsp_uart_init(void);

/**
 * @brief Send data to the ESP32-C6
 * @param data Data buffer to send
 * @param len Length of data
 * @return Number of bytes written, or -1 on error
 */
int bsp_uart_send(const uint8_t *data, size_t len);

/**
 * @brief Receive data from the ESP32-C6
 * @param data Buffer to receive into
 * @param max_len Maximum bytes to read
 * @param timeout_ms Read timeout in ms
 * @return Number of bytes read, or -1 on error
 */
int bsp_uart_recv(uint8_t *data, size_t max_len, int timeout_ms);

/**
 * @brief Send an AT command and wait for response
 * @param cmd AT command string (without trailing \\r\\n)
 * @param resp Response buffer
 * @param resp_size Response buffer size
 * @param timeout_ms Timeout for response
 * @return ESP_OK if "OK" received, ESP_FAIL otherwise
 */
esp_err_t bsp_uart_at_cmd(const char *cmd, char *resp, size_t resp_size, int timeout_ms);

/**
 * @brief Deinitialize the UART
 * @return ESP_OK on success
 */
esp_err_t bsp_uart_deinit(void);

#ifdef __cplusplus
}
#endif
