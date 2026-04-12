#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CrowPanel ESP32-P4 I2C bus configuration
 * I2C0: Touch controller (GT911) + other peripherals
 * GPIO pins per CrowPanel schematic */
#define BSP_I2C_NUM         I2C_NUM_0
#define BSP_I2C_SCL_PIN     46
#define BSP_I2C_SDA_PIN     45
#define BSP_I2C_FREQ_HZ     400000

/**
 * @brief Initialize the I2C master bus
 * @return ESP_OK on success
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Get the I2C master bus handle
 * @return I2C master bus handle, or NULL if not initialized
 */
i2c_master_bus_handle_t bsp_i2c_get_handle(void);

/**
 * @brief Deinitialize the I2C bus
 * @return ESP_OK on success
 */
esp_err_t bsp_i2c_deinit(void);

#ifdef __cplusplus
}
#endif
