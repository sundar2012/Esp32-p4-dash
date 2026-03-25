#include "bsp_extra.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "esp_ldo_regulator.h"

static const char *TAG = "bsp_extra";
static esp_ldo_channel_handle_t s_ldo_periph = NULL;
static esp_ldo_channel_handle_t s_ldo_mipi = NULL;
static bool s_backlight_initialized = false;

#define BSP_BACKLIGHT_LEDC_TIMER    LEDC_TIMER_0
#define BSP_BACKLIGHT_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BSP_BACKLIGHT_LEDC_FREQ_HZ  5000
#define BSP_BACKLIGHT_LEDC_DUTY_RES LEDC_TIMER_10_BIT

esp_err_t bsp_extra_power_init(void)
{
    /* Initialize LDO channel 4 — 3.3V for peripherals and display */
    esp_ldo_channel_config_t ldo_periph_cfg = {
        .chan_id = BSP_LDO_CHANNEL_PERIPH,
        .voltage_mv = BSP_LDO_VOLTAGE_PERIPH_MV,
    };
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_periph_cfg, &s_ldo_periph);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init LDO ch%d: %s", BSP_LDO_CHANNEL_PERIPH, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "LDO ch%d → %dmV OK", BSP_LDO_CHANNEL_PERIPH, BSP_LDO_VOLTAGE_PERIPH_MV);

    /* Initialize LDO channel 3 — 2.5V for MIPI PHY */
    esp_ldo_channel_config_t ldo_mipi_cfg = {
        .chan_id = BSP_LDO_CHANNEL_MIPI,
        .voltage_mv = BSP_LDO_VOLTAGE_MIPI_MV,
    };
    ret = esp_ldo_acquire_channel(&ldo_mipi_cfg, &s_ldo_mipi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init LDO ch%d: %s", BSP_LDO_CHANNEL_MIPI, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "LDO ch%d → %dmV OK", BSP_LDO_CHANNEL_MIPI, BSP_LDO_VOLTAGE_MIPI_MV);

    return ESP_OK;
}

static esp_err_t backlight_init(void)
{
    if (s_backlight_initialized) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BSP_BACKLIGHT_LEDC_TIMER,
        .duty_resolution = BSP_BACKLIGHT_LEDC_DUTY_RES,
        .freq_hz = BSP_BACKLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) return ret;

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BSP_BACKLIGHT_LEDC_CHANNEL,
        .timer_sel = BSP_BACKLIGHT_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BSP_LCD_BACKLIGHT_PIN,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&ch_cfg);
    if (ret == ESP_OK) {
        s_backlight_initialized = true;
    }
    return ret;
}

esp_err_t bsp_extra_lcd_backlight_set(int brightness_pct)
{
    esp_err_t ret = backlight_init();
    if (ret != ESP_OK) return ret;

    if (brightness_pct < 0) brightness_pct = 0;
    if (brightness_pct > 100) brightness_pct = 100;

    uint32_t max_duty = (1 << BSP_BACKLIGHT_LEDC_DUTY_RES) - 1;
    uint32_t duty = (max_duty * brightness_pct) / 100;

    if (!BSP_LCD_BACKLIGHT_ON_LEVEL) {
        duty = max_duty - duty;
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, BSP_BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BSP_BACKLIGHT_LEDC_CHANNEL);

    ESP_LOGI(TAG, "Backlight set to %d%%", brightness_pct);
    return ESP_OK;
}

esp_err_t bsp_extra_lcd_backlight_on(void)
{
    return bsp_extra_lcd_backlight_set(100);
}

esp_err_t bsp_extra_lcd_backlight_off(void)
{
    return bsp_extra_lcd_backlight_set(0);
}
