#include "ui_guest.h"
#include "bsp_display.h"
#include "app_dashboard.h"
#include "esp_log.h"

static const char *TAG = "ui_guest";

lv_obj_t *ui_guest_create(void)
{
    ESP_LOGI(TAG, "Creating guest dashboard");

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f23), 0);

    /* Status bar */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, BSP_LCD_H_RES, 40);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1a1a3e), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_lbl = lv_label_create(bar);
    lv_label_set_text(name_lbl, "Guest");
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xe0e0ff), 0);
    lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 15, 0);

    /* Welcome message */
    lv_obj_t *welcome = lv_label_create(scr);
    lv_label_set_text(welcome, "Welcome, Guest");
    lv_obj_set_style_text_font(welcome, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(welcome, lv_color_hex(0xe0e0ff), 0);
    lv_obj_align(welcome, LV_ALIGN_CENTER, 0, -60);

    /* Info panel */
    lv_obj_t *info_panel = lv_obj_create(scr);
    lv_obj_set_size(info_panel, 500, 200);
    lv_obj_align(info_panel, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(info_panel, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(info_panel, 0, 0);
    lv_obj_set_style_radius(info_panel, 12, 0);
    lv_obj_set_style_pad_all(info_panel, 20, 0);

    lv_obj_t *info_text = lv_label_create(info_panel);
    lv_label_set_text(info_text,
        "This is the CrowPanel ESP32-P4 Face Dashboard.\n\n"
        "To set up your personalized dashboard:\n"
        "1. Press the Settings gear icon\n"
        "2. Tap 'Enroll New Face'\n"
        "3. Follow the on-screen instructions\n\n"
        "Your dashboard will appear automatically\n"
        "when you're recognized by the camera.");
    lv_obj_set_style_text_font(info_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info_text, lv_color_hex(0xc0c0e0), 0);
    lv_label_set_long_mode(info_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info_text, 460);

    return scr;
}
