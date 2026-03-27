#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CrowPanel 7" display parameters */
#define BSP_LCD_H_RES           1024
#define BSP_LCD_V_RES           600
#define BSP_LCD_PIXEL_CLK_MHZ   40

/* MIPI-DSI configuration for the CrowPanel's ILI9881C-based panel */
#define BSP_MIPI_DSI_LANE_NUM       2
#define BSP_MIPI_DSI_LANE_BITRATE   (1000 * 1000 * 1000)  /* 1Gbps per lane */

/* Touch controller: Goodix GT911 on I2C */
#define BSP_TOUCH_I2C_ADDR      0x5D
#define BSP_TOUCH_RST_PIN       6
#define BSP_TOUCH_INT_PIN       5

/**
 * @brief Initialize the MIPI-DSI display and touch controller
 *
 * Initializes the MIPI-DSI bus, LCD panel (ILI9881C), and GT911 touch.
 * Also sets up LVGL display and input drivers.
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_display_init(void);

/**
 * @brief Get the LVGL display object
 * @return LVGL display pointer, or NULL if not initialized
 */
lv_display_t *bsp_display_get(void);

/**
 * @brief Get the LVGL input device (touch)
 * @return LVGL indev pointer, or NULL if not initialized
 */
lv_indev_t *bsp_display_get_indev(void);

/**
 * @brief Lock the LVGL mutex before accessing LVGL APIs
 * @param timeout_ms Timeout in ms, -1 for infinite
 * @return true if lock acquired
 */
bool bsp_display_lock(int timeout_ms);

/**
 * @brief Unlock the LVGL mutex
 */
void bsp_display_unlock(void);

#ifdef __cplusplus
}
#endif
