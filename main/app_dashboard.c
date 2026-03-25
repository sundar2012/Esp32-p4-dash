#include "app_dashboard.h"
#include "bsp_display.h"
#include "ui_sundar.h"
#include "ui_guest.h"
#include "ui_user2.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "app_dashboard";
static dashboard_id_t s_current = DASHBOARD_SPLASH;
static lv_obj_t *s_screens[DASHBOARD_MAX] = {NULL};

/* ──── Splash screen ──── */

static lv_obj_t *create_splash_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "CrowPanel Face Dashboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xe0e0ff), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    /* Subtitle */
    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Initializing...");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x808090), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 20);

    /* Spinner */
    lv_obj_t *spinner = lv_spinner_create(scr);
    lv_spinner_set_anim_params(spinner, 1000, 200);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 80);

    return scr;
}

/* ──── Screensaver / Lock screen ──── */

static lv_obj_t *create_screensaver_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    /* Clock display */
    lv_obj_t *clock_lbl = lv_label_create(scr);
    lv_label_set_text(clock_lbl, "12:00");
    lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(clock_lbl, lv_color_hex(0x404060), 0);
    lv_obj_align(clock_lbl, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Face the camera to unlock");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x303040), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 30);

    return scr;
}

/* ──── Enrollment screen ──── */

static lv_obj_t *create_enrollment_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    /* Camera preview area (placeholder) */
    lv_obj_t *cam_area = lv_obj_create(scr);
    lv_obj_set_size(cam_area, 320, 240);
    lv_obj_align(cam_area, LV_ALIGN_CENTER, 0, -60);
    lv_obj_set_style_bg_color(cam_area, lv_color_hex(0x202040), 0);
    lv_obj_set_style_border_color(cam_area, lv_color_hex(0x4040ff), 0);
    lv_obj_set_style_border_width(cam_area, 2, 0);
    lv_obj_set_style_radius(cam_area, 8, 0);

    lv_obj_t *cam_label = lv_label_create(cam_area);
    lv_label_set_text(cam_label, "Camera Preview");
    lv_obj_set_style_text_color(cam_label, lv_color_hex(0x606080), 0);
    lv_obj_center(cam_label);

    /* Instruction */
    lv_obj_t *instr = lv_label_create(scr);
    lv_label_set_text(instr, "Look at the camera and slowly turn your head");
    lv_obj_set_style_text_font(instr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(instr, lv_color_hex(0xc0c0e0), 0);
    lv_obj_align(instr, LV_ALIGN_CENTER, 0, 80);

    /* Progress */
    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 300, 20);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 120);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    return scr;
}

/* ──── Settings screen ──── */

static lv_obj_t *create_settings_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x16213e), 0);

    /* Title bar */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xe0e0ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 10);

    /* Wi-Fi section */
    lv_obj_t *wifi_section = lv_obj_create(scr);
    lv_obj_set_size(wifi_section, 480, 150);
    lv_obj_align(wifi_section, LV_ALIGN_TOP_LEFT, 20, 60);
    lv_obj_set_style_bg_color(wifi_section, lv_color_hex(0x1a1a3e), 0);
    lv_obj_set_style_radius(wifi_section, 8, 0);

    lv_obj_t *wifi_title = lv_label_create(wifi_section);
    lv_label_set_text(wifi_title, LV_SYMBOL_WIFI "  Wi-Fi Configuration");
    lv_obj_set_style_text_font(wifi_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_title, lv_color_hex(0xc0c0e0), 0);

    /* Face management section */
    lv_obj_t *face_section = lv_obj_create(scr);
    lv_obj_set_size(face_section, 480, 150);
    lv_obj_align(face_section, LV_ALIGN_TOP_LEFT, 20, 230);
    lv_obj_set_style_bg_color(face_section, lv_color_hex(0x1a1a3e), 0);
    lv_obj_set_style_radius(face_section, 8, 0);

    lv_obj_t *face_title = lv_label_create(face_section);
    lv_label_set_text(face_title, LV_SYMBOL_EYE_OPEN "  Face Enrollment");
    lv_obj_set_style_text_font(face_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(face_title, lv_color_hex(0xc0c0e0), 0);

    /* Enroll button */
    lv_obj_t *enroll_btn = lv_btn_create(face_section);
    lv_obj_align(enroll_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *enroll_lbl = lv_label_create(enroll_btn);
    lv_label_set_text(enroll_lbl, "Enroll New Face");

    /* Back button */
    lv_obj_t *back_btn = lv_btn_create(scr);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Back");

    return scr;
}

/* ──── Dashboard show/switch ──── */

void app_dashboard_show(dashboard_id_t id)
{
    if (id >= DASHBOARD_MAX) {
        ESP_LOGE(TAG, "Invalid dashboard ID: %d", id);
        return;
    }

    /* Create screen lazily on first show */
    if (s_screens[id] == NULL) {
        switch (id) {
            case DASHBOARD_SPLASH:
                s_screens[id] = create_splash_screen();
                break;
            case DASHBOARD_SCREENSAVER:
                s_screens[id] = create_screensaver_screen();
                break;
            case DASHBOARD_GUEST:
                s_screens[id] = ui_guest_create();
                break;
            case DASHBOARD_SUNDAR:
                s_screens[id] = ui_sundar_create();
                break;
            case DASHBOARD_USER2:
                s_screens[id] = ui_user2_create();
                break;
            case DASHBOARD_ENROLLMENT:
                s_screens[id] = create_enrollment_screen();
                break;
            case DASHBOARD_SETTINGS:
                s_screens[id] = create_settings_screen();
                break;
            default:
                break;
        }
    }

    if (s_screens[id]) {
        lv_screen_load_anim(s_screens[id], LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
        s_current = id;
        ESP_LOGI(TAG, "Dashboard switched to: %d", id);
    }
}

void app_dashboard_show_splash(void)
{
    app_dashboard_show(DASHBOARD_SPLASH);
}

dashboard_id_t app_dashboard_get_current(void)
{
    return s_current;
}
