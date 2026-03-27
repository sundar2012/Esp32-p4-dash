#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CrowPanel ESP32-P4 LDO and power configuration
 * LDO channel 4 → 3.3V (peripherals, display, touch)
 * LDO channel 3 → 2.5V (MIPI PHY)
 * Backlight PWM on dedicated GPIO */
#define BSP_LDO_CHANNEL_PERIPH      4
#define BSP_LDO_VOLTAGE_PERIPH_MV   3300
#define BSP_LDO_CHANNEL_MIPI        3
#define BSP_LDO_VOLTAGE_MIPI_MV     2500
#define BSP_LCD_BACKLIGHT_PIN       31
#define BSP_LCD_BACKLIGHT_ON_LEVEL  1

/**
 * @brief Initialize power rails (LDO channels) for the CrowPanel
 * @return ESP_OK on success
 */
esp_err_t bsp_extra_power_init(void);

/**
 * @brief Set LCD backlight brightness
 * @param brightness_pct Brightness 0-100
 * @return ESP_OK on success
 */
esp_err_t bsp_extra_lcd_backlight_set(int brightness_pct);

/**
 * @brief Turn LCD backlight on
 * @return ESP_OK on success
 */
esp_err_t bsp_extra_lcd_backlight_on(void);

/**
 * @brief Turn LCD backlight off
 * @return ESP_OK on success
 */
esp_err_t bsp_extra_lcd_backlight_off(void);

#ifdef __cplusplus
}
#endif
