#include "ui_user2.h"
#include "bsp_display.h"
#include "esp_log.h"

static const char *TAG = "ui_user2";

lv_obj_t *ui_user2_create(void)
{
    ESP_LOGI(TAG, "Creating user2 dashboard");

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
    lv_label_set_text(name_lbl, "User 2");
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xe0e0ff), 0);
    lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 15, 0);

    /* Placeholder content — customize per user */
    lv_obj_t *content = lv_label_create(scr);
    lv_label_set_text(content, "User 2 Dashboard");
    lv_obj_set_style_text_font(content, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(content, lv_color_hex(0xe0e0ff), 0);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Customize this dashboard in ui_user2.c");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x808090), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 30);

    return scr;
}
