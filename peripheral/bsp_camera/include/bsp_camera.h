#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CrowPanel 2MP MIPI-CSI camera configuration */
#define BSP_CAMERA_H_RES        640
#define BSP_CAMERA_V_RES        480
#define BSP_CAMERA_FPS          15
#define BSP_CAMERA_MIPI_LANES   2

/* Camera MIPI-CSI GPIO (active-low reset, PWDN) — per CrowPanel schematic */
#define BSP_CAMERA_RST_PIN      (-1)  /* Connected internally or not used */
#define BSP_CAMERA_PWDN_PIN     (-1)

/* SCCB (I2C) interface for camera sensor configuration */
#define BSP_CAMERA_SCCB_SCL     8     /* Shared I2C bus with touch */
#define BSP_CAMERA_SCCB_SDA     7

/**
 * @brief Camera frame buffer handle
 */
typedef struct {
    uint8_t *data;      /**< Pointer to frame data */
    size_t len;         /**< Frame data length in bytes */
    uint16_t width;     /**< Frame width */
    uint16_t height;    /**< Frame height */
} bsp_camera_fb_t;

/**
 * @brief Initialize the MIPI-CSI camera
 * @return ESP_OK on success
 */
esp_err_t bsp_camera_init(void);

/**
 * @brief Capture a single frame from the camera
 * @return Pointer to frame buffer, or NULL on failure
 */
bsp_camera_fb_t *bsp_camera_fb_get(void);

/**
 * @brief Return a frame buffer after use
 * @param fb Frame buffer to return
 */
void bsp_camera_fb_return(bsp_camera_fb_t *fb);

/**
 * @brief Deinitialize the camera
 * @return ESP_OK on success
 */
esp_err_t bsp_camera_deinit(void);

#ifdef __cplusplus
}
#endif
