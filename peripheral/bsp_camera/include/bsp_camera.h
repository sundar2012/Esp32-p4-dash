#pragma once

#include "esp_err.h"
#include "esp_cam_ctlr_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CrowPanel 2MP MIPI-CSI camera configuration (SC2336 sensor) */
#define BSP_CAMERA_H_RES        1024
#define BSP_CAMERA_V_RES        600
#define BSP_CAMERA_FPS          30
#define BSP_CAMERA_MIPI_LANES   2
#define BSP_CAMERA_LANE_BITRATE_MBPS  200

/* SCCB (I2C) interface for camera sensor — separate bus from touch */
#define BSP_CAMERA_SCCB_PORT    1
#define BSP_CAMERA_SCCB_SCL     13
#define BSP_CAMERA_SCCB_SDA     12
#define BSP_CAMERA_SCCB_FREQ_HZ 100000

/* Camera control pins (not used on CrowPanel) */
#define BSP_CAMERA_RST_PIN      (-1)
#define BSP_CAMERA_PWDN_PIN     (-1)
#define BSP_CAMERA_XCLK_PIN     (-1)

/**
 * @brief Camera frame buffer handle
 */
typedef struct {
    uint8_t *data;      /**< Pointer to frame data (RGB565) */
    size_t len;         /**< Frame data length in bytes */
    uint16_t width;     /**< Frame width */
    uint16_t height;    /**< Frame height */
} bsp_camera_fb_t;

/**
 * @brief Initialize the MIPI-CSI camera (SC2336 sensor)
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
