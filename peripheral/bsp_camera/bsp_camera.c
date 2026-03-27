#include "bsp_camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_sensor.h"
#include "sc2336.h"
#include "esp_sccb_intf.h"
#include "esp_sccb_i2c.h"
#include "driver/isp_core.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

static const char *TAG = "bsp_camera";

static bool s_initialized = false;
static esp_cam_ctlr_handle_t s_cam_handle = NULL;
static isp_proc_handle_t s_isp_proc = NULL;
static i2c_master_bus_handle_t s_sccb_bus = NULL;

/* Frame buffer management */
#define BSP_CAMERA_FB_COUNT  2
#define BSP_CAMERA_FB_SIZE   (BSP_CAMERA_H_RES * BSP_CAMERA_V_RES * 2) /* RGB565 */

static bsp_camera_fb_t s_fb;
static SemaphoreHandle_t s_fb_mutex = NULL;
static QueueHandle_t s_frame_queue = NULL;
static uint8_t *s_frame_buffers[BSP_CAMERA_FB_COUNT] = {NULL};

/* Callback when a frame is received from CSI controller */
static bool camera_trans_done_cb(esp_cam_ctlr_handle_t handle,
                                  esp_cam_ctlr_trans_t *trans,
                                  void *user_data)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_frame_queue, &trans->buffer, &xHigherPriorityTaskWoken);
    return (xHigherPriorityTaskWoken == pdTRUE);
}

/* Initialize SCCB I2C bus and detect camera sensor */
static esp_err_t camera_sensor_init(void)
{
    /* Create separate I2C bus for camera SCCB */
    i2c_master_bus_config_t i2c_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BSP_CAMERA_SCCB_PORT,
        .scl_io_num = BSP_CAMERA_SCCB_SCL,
        .sda_io_num = BSP_CAMERA_SCCB_SDA,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&i2c_config, &s_sccb_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SCCB I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create SCCB interface for sensor communication */
    esp_sccb_io_handle_t sccb_io = NULL;
    sccb_i2c_config_t sccb_config = {
        .scl_speed_hz = BSP_CAMERA_SCCB_FREQ_HZ,
        .device_address = 0x30,  /* SC2336 default address */
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    };
    ret = sccb_new_i2c_io(s_sccb_bus, &sccb_config, &sccb_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SCCB IO: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure and create the camera sensor device */
    esp_cam_sensor_config_t cam_config = {
        .sccb_handle = sccb_io,
        .reset_pin = BSP_CAMERA_RST_PIN,
        .pwdn_pin = BSP_CAMERA_PWDN_PIN,
        .xclk_pin = BSP_CAMERA_XCLK_PIN,
        .sensor_port = ESP_CAM_SENSOR_MIPI_CSI,
    };

    /* Detect the SC2336 sensor on the SCCB bus */
    esp_cam_sensor_device_t *sensor = sc2336_detect(&cam_config);
    if (!sensor) {
        ESP_LOGE(TAG, "SC2336 not detected on SCCB bus (SCL=%d, SDA=%d)",
                 BSP_CAMERA_SCCB_SCL, BSP_CAMERA_SCCB_SDA);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Camera sensor detected: %s", sensor->name);
    return ESP_OK;
}

/* Initialize ISP for RAW8 → RGB565 conversion */
static esp_err_t isp_init(void)
{
    esp_isp_processor_cfg_t isp_config = {
        .clk_hz = 80 * 1000 * 1000,
        .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type = ISP_COLOR_RAW8,
        .output_data_color_type = ISP_COLOR_RGB565,
        .has_line_start_packet = false,
        .has_line_end_packet = false,
        .h_res = BSP_CAMERA_H_RES,
        .v_res = BSP_CAMERA_V_RES,
    };
    esp_err_t ret = esp_isp_new_processor(&isp_config, &s_isp_proc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ISP processor: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_isp_enable(s_isp_proc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable ISP: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ISP initialized (RAW8 → RGB565, %dx%d)", BSP_CAMERA_H_RES, BSP_CAMERA_V_RES);
    return ESP_OK;
}

/* Initialize CSI controller */
static esp_err_t csi_init(void)
{
    esp_cam_ctlr_csi_config_t csi_config = {
        .ctlr_id = 0,
        .h_res = BSP_CAMERA_H_RES,
        .v_res = BSP_CAMERA_V_RES,
        .lane_bit_rate_mbps = BSP_CAMERA_LANE_BITRATE_MBPS,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,
        .data_lane_num = BSP_CAMERA_MIPI_LANES,
        .byte_swap_en = false,
        .queue_items = BSP_CAMERA_FB_COUNT + 1,
    };
    esp_err_t ret = esp_cam_new_csi_ctlr(&csi_config, &s_cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CSI controller: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register frame-done callback */
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_trans_finished = camera_trans_done_cb,
    };
    ret = esp_cam_ctlr_register_event_callbacks(s_cam_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register CSI callbacks: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_cam_ctlr_enable(s_cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable CSI controller: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "CSI controller initialized (%d lanes, %d Mbps)",
             BSP_CAMERA_MIPI_LANES, BSP_CAMERA_LANE_BITRATE_MBPS);
    return ESP_OK;
}

esp_err_t bsp_camera_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Camera already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MIPI-CSI camera (SC2336, %dx%d @ %dfps)...",
             BSP_CAMERA_H_RES, BSP_CAMERA_V_RES, BSP_CAMERA_FPS);

    esp_err_t ret;

    /* Allocate frame buffers in PSRAM */
    s_frame_queue = xQueueCreate(BSP_CAMERA_FB_COUNT, sizeof(uint8_t *));
    s_fb_mutex = xSemaphoreCreateMutex();
    if (!s_frame_queue || !s_fb_mutex) {
        ESP_LOGE(TAG, "Failed to create frame queue/mutex");
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < BSP_CAMERA_FB_COUNT; i++) {
        s_frame_buffers[i] = heap_caps_aligned_calloc(64, 1, BSP_CAMERA_FB_SIZE,
                                                       MALLOC_CAP_SPIRAM);
        if (!s_frame_buffers[i]) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer %d (%d bytes)", i, BSP_CAMERA_FB_SIZE);
            bsp_camera_deinit();
            return ESP_ERR_NO_MEM;
        }
    }

    /* Initialize ISP (must be before CSI on ESP32-P4) */
    ret = isp_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ISP init failed: %s — continuing without ISP", esp_err_to_name(ret));
    }

    /* Initialize CSI controller */
    ret = csi_init();
    if (ret != ESP_OK) {
        bsp_camera_deinit();
        return ret;
    }

    /* Initialize camera sensor via SCCB */
    ret = camera_sensor_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Camera sensor not detected — camera unavailable");
        bsp_camera_deinit();
        return ret;
    }

    /* Submit initial frame buffers to CSI controller for capture */
    for (int i = 0; i < BSP_CAMERA_FB_COUNT; i++) {
        esp_cam_ctlr_trans_t trans = {
            .buffer = s_frame_buffers[i],
            .buflen = BSP_CAMERA_FB_SIZE,
        };
        ret = esp_cam_ctlr_receive(s_cam_handle, &trans, 1000);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to submit frame buffer %d: %s", i, esp_err_to_name(ret));
        }
    }

    /* Start the CSI controller */
    ret = esp_cam_ctlr_start(s_cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start CSI: %s", esp_err_to_name(ret));
        bsp_camera_deinit();
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Camera initialized (SC2336, %dx%d, MIPI-CSI %d lanes)",
             BSP_CAMERA_H_RES, BSP_CAMERA_V_RES, BSP_CAMERA_MIPI_LANES);
    return ESP_OK;
}

bsp_camera_fb_t *bsp_camera_fb_get(void)
{
    if (!s_initialized) return NULL;

    uint8_t *buf = NULL;
    if (xQueueReceive(s_frame_queue, &buf, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (xSemaphoreTake(s_fb_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_fb.data = buf;
            s_fb.len = BSP_CAMERA_FB_SIZE;
            s_fb.width = BSP_CAMERA_H_RES;
            s_fb.height = BSP_CAMERA_V_RES;
            return &s_fb;
        }
    }
    return NULL;
}

void bsp_camera_fb_return(bsp_camera_fb_t *fb)
{
    if (!fb || !s_fb_mutex || !s_cam_handle) return;

    /* Resubmit buffer back to CSI controller for next capture */
    esp_cam_ctlr_trans_t trans = {
        .buffer = fb->data,
        .buflen = fb->len,
    };
    esp_cam_ctlr_receive(s_cam_handle, &trans, 100);

    xSemaphoreGive(s_fb_mutex);
}

esp_err_t bsp_camera_deinit(void)
{
    if (s_cam_handle) {
        esp_cam_ctlr_stop(s_cam_handle);
        esp_cam_ctlr_disable(s_cam_handle);
        esp_cam_ctlr_del(s_cam_handle);
        s_cam_handle = NULL;
    }

    if (s_isp_proc) {
        esp_isp_disable(s_isp_proc);
        esp_isp_del_processor(s_isp_proc);
        s_isp_proc = NULL;
    }

    if (s_sccb_bus) {
        i2c_del_master_bus(s_sccb_bus);
        s_sccb_bus = NULL;
    }

    if (s_frame_queue) {
        vQueueDelete(s_frame_queue);
        s_frame_queue = NULL;
    }

    if (s_fb_mutex) {
        vSemaphoreDelete(s_fb_mutex);
        s_fb_mutex = NULL;
    }

    for (int i = 0; i < BSP_CAMERA_FB_COUNT; i++) {
        if (s_frame_buffers[i]) {
            heap_caps_free(s_frame_buffers[i]);
            s_frame_buffers[i] = NULL;
        }
    }

    s_initialized = false;
    ESP_LOGI(TAG, "Camera deinitialized");
    return ESP_OK;
}
