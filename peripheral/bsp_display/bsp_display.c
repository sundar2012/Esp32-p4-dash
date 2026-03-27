#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_extra.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ek79007.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "bsp_display";

static lv_display_t *s_display = NULL;
static lv_indev_t *s_indev = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static TaskHandle_t s_lvgl_task_handle = NULL;

#define BSP_LVGL_TASK_STACK_SIZE    (8 * 1024)
#define BSP_LVGL_TASK_PRIORITY      5
#define BSP_LVGL_TICK_MS            5
#define BSP_LVGL_BUF_HEIGHT         50  /* Partial buffer: 1024 * 50 * 2 bytes = 100KB per buf */

/* LVGL flush callback — sends buffer to MIPI-DSI panel */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    int x_start = area->x1;
    int y_start = area->y1;
    int x_end = area->x2 + 1;
    int y_end = area->y2 + 1;

    esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, px_map);
    lv_display_flush_ready(disp);
}

/* Touch read callback for GT911 */
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    /* TODO: Read GT911 touch data via I2C
     * For now, report no touch — actual GT911 driver integration
     * will read touch coordinates from the controller registers */
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = 0;
    data->point.y = 0;
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

    /* Step 4: Create the EK79007 panel with DPI video mode
     * Timing values from Elecrow CrowPanel 7" ESP32-P4 reference code */
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

    /* Use the EK79007 panel driver — sends vendor init commands via DBI
     * then starts DPI video mode */
    ek79007_vendor_config_t vendor_config = {
        .init_cmds = NULL,      /* Use built-in default init sequence */
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

    /* Step 6: Initialize LVGL */
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();

    /* Create LVGL display */
    s_display = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_display_set_user_data(s_display, s_panel_handle);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);

    /* Allocate draw buffers in PSRAM for large display */
    size_t buf_size = BSP_LCD_H_RES * BSP_LVGL_BUF_HEIGHT * sizeof(lv_color16_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers (%zu bytes)", buf_size);
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(s_display, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

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
