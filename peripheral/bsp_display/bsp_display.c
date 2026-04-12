#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_extra.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ek79007.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "bsp_display";

static lv_display_t *s_display = NULL;
static lv_indev_t *s_indev = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static TaskHandle_t s_lvgl_task_handle = NULL;

/* GT911 touch controller handle */
static i2c_master_dev_handle_t s_gt911_handle = NULL;

#define BSP_LVGL_TASK_STACK_SIZE    (8 * 1024)
#define BSP_LVGL_TASK_PRIORITY      5
#define BSP_LVGL_TICK_MS            5

/* ──── GT911 Touch Controller Driver ──── */

/* GT911 register addresses */
#define GT911_REG_STATUS    0x814E
#define GT911_REG_POINT1    0x8150

static esp_err_t gt911_read_reg(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t reg_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(s_gt911_handle, reg_buf, 2, data, len, 50);
}

static esp_err_t gt911_write_reg(uint16_t reg, uint8_t val)
{
    uint8_t buf[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val };
    return i2c_master_transmit(s_gt911_handle, buf, 3, 50);
}

static esp_err_t gt911_init(void)
{
    i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_handle();
    if (!i2c_bus) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Reset GT911 with INT low → selects I2C address 0x5D */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BSP_TOUCH_RST_PIN) | (1ULL << BSP_TOUCH_INT_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level((gpio_num_t)BSP_TOUCH_INT_PIN, 0);
    gpio_set_level((gpio_num_t)BSP_TOUCH_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)BSP_TOUCH_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Release INT pin to input after reset (address is latched) */
    gpio_set_direction((gpio_num_t)BSP_TOUCH_INT_PIN, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Add GT911 I2C device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_TOUCH_I2C_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_gt911_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GT911 device: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Read product ID to verify communication */
    uint8_t product_id[4] = {0};
    ret = gt911_read_reg(0x8140, product_id, 4);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "GT911 product ID: %c%c%c%c", product_id[0], product_id[1],
                 product_id[2], product_id[3]);
    } else {
        ESP_LOGW(TAG, "GT911 read failed: %s (touch may not work)", esp_err_to_name(ret));
    }

    return ESP_OK;
}

/* LVGL flush callback — sends buffer to MIPI-DSI DPI panel */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

/* Touch read callback for GT911 */
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = 0;
    data->point.y = 0;

    if (!s_gt911_handle) return;

    /* Read status register */
    uint8_t status = 0;
    if (gt911_read_reg(GT911_REG_STATUS, &status, 1) != ESP_OK) return;

    /* Bit 7: buffer ready, bits 3:0: number of touch points */
    if (!(status & 0x80)) return;

    int touch_count = status & 0x0F;

    if (touch_count > 0 && touch_count <= 5) {
        /* Read first touch point (8 bytes: id, x_l, x_h, y_l, y_h, size_l, size_h, reserved) */
        uint8_t point_data[7];
        if (gt911_read_reg(GT911_REG_POINT1, point_data, 7) == ESP_OK) {
            uint16_t x = (uint16_t)point_data[1] | ((uint16_t)point_data[2] << 8);
            uint16_t y = (uint16_t)point_data[3] | ((uint16_t)point_data[4] << 8);

            /* Clamp to display bounds */
            if (x >= BSP_LCD_H_RES) x = BSP_LCD_H_RES - 1;
            if (y >= BSP_LCD_V_RES) y = BSP_LCD_V_RES - 1;

            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = x;
            data->point.y = y;
        }
    }

    /* Clear status register — must write 0 to acknowledge */
    gt911_write_reg(GT911_REG_STATUS, 0);
}

/* LVGL tick provider */
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(BSP_LVGL_TICK_MS);
}

/* LVGL task — runs the LVGL timer handler in a loop */
static void lvgl_task(void *pvParam)
{
    (void)pvParam;
    ESP_LOGI(TAG, "LVGL task started");
    while (1) {
        if (bsp_display_lock(-1)) {
            uint32_t next_ms = lv_timer_handler();
            bsp_display_unlock();
            if (next_ms < BSP_LVGL_TICK_MS) {
                next_ms = BSP_LVGL_TICK_MS;
            }
            vTaskDelay(pdMS_TO_TICKS(next_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(BSP_LVGL_TICK_MS));
        }
    }
}

esp_err_t bsp_display_init(void)
{
    if (s_display != NULL) {
        ESP_LOGW(TAG, "Display already initialized");
        return ESP_OK;
    }

    esp_err_t ret;

    /* Step 1: Power on the display via LDO channels */
    ret = bsp_extra_power_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Power init failed");
        return ret;
    }

    /* Step 2: Initialize MIPI-DSI bus */
    ESP_LOGI(TAG, "Initializing MIPI-DSI bus...");
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = BSP_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BSP_MIPI_DSI_LANE_BITRATE / 1000000,
    };
    ret = esp_lcd_new_dsi_bus(&bus_config, &dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MIPI-DSI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Step 3: Create DBI interface for sending commands to the panel */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DBI IO: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Step 4: Create the EK79007 panel with DPI video mode */
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BSP_LCD_PIXEL_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,
        .video_timing = {
            .h_size = BSP_LCD_H_RES,
            .v_size = BSP_LCD_V_RES,
            .hsync_back_porch = 160,
            .hsync_pulse_width = 70,
            .hsync_front_porch = 160,
            .vsync_back_porch = 23,
            .vsync_pulse_width = 10,
            .vsync_front_porch = 12,
        },
        .flags.use_dma2d = true,
    };

    ek79007_vendor_config_t vendor_config = {
        .init_cmds = NULL,
        .init_cmds_size = 0,
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = BSP_MIPI_DSI_LANE_NUM,
        },
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ret = esp_lcd_new_panel_ek79007(io_handle, &panel_config, &s_panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create EK79007 panel: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_reset(s_panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_init(s_panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Step 5: Turn on backlight */
    bsp_extra_lcd_backlight_on();

    /* Step 6: Initialize GT911 touch controller */
    ESP_LOGI(TAG, "Initializing GT911 touch...");
    ret = gt911_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GT911 init failed — touch disabled");
    }

    /* Step 7: Initialize LVGL */
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();

    s_display = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_display_set_user_data(s_display, s_panel_handle);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);

    void *fb0 = NULL;
    void *fb1 = NULL;
    esp_lcd_dpi_panel_get_frame_buffer(s_panel_handle, 2, &fb0, &fb1);
    if (!fb0 || !fb1) {
        ESP_LOGE(TAG, "Failed to get DPI frame buffers");
        return ESP_ERR_NO_MEM;
    }
    size_t fb_size = BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(lv_color16_t);
    lv_display_set_buffers(s_display, fb0, fb1, fb_size, LV_DISPLAY_RENDER_MODE_DIRECT);

    /* Create LVGL touch input device */
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, lvgl_touch_read_cb);

    /* Create LVGL tick timer */
    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    esp_timer_create(&tick_timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, BSP_LVGL_TICK_MS * 1000);

    /* Create LVGL mutex */
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_lvgl_mutex) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Create LVGL task */
    BaseType_t xret = xTaskCreatePinnedToCore(
        lvgl_task, "lvgl", BSP_LVGL_TASK_STACK_SIZE,
        NULL, BSP_LVGL_TASK_PRIORITY, &s_lvgl_task_handle, 0
    );
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Display initialized: %dx%d, MIPI-DSI %d lanes (EK79007)",
             BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_MIPI_DSI_LANE_NUM);
    return ESP_OK;
}

lv_display_t *bsp_display_get(void)
{
    return s_display;
}

lv_indev_t *bsp_display_get_indev(void)
{
    return s_indev;
}

bool bsp_display_lock(int timeout_ms)
{
    if (!s_lvgl_mutex) return false;
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mutex, ticks) == pdTRUE;
}

void bsp_display_unlock(void)
{
    if (s_lvgl_mutex) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}
