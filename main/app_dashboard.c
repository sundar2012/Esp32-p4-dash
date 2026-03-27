#include "app_dashboard.h"
#include "app_face.h"
#include "bsp_display.h"
#include "ui_sundar.h"
#include "ui_guest.h"
#include "ui_user2.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "app_dashboard";
static dashboard_id_t s_current = DASHBOARD_SPLASH;
static lv_obj_t *s_screens[DASHBOARD_MAX] = {NULL};

/* ──── Forward declarations ──── */
static lv_obj_t *create_passcode_screen(void);

/* ──── Passcode state ──── */
#define PASSCODE_LEN 4
static const char CORRECT_PASSCODE[PASSCODE_LEN + 1] = "3635";
static char s_passcode_input[PASSCODE_LEN + 1] = {0};
static int s_passcode_pos = 0;
static lv_obj_t *s_passcode_dots[PASSCODE_LEN] = {NULL};
static lv_obj_t *s_passcode_status_label = NULL;

/* Enrollment name selection */
static char s_enroll_name[32] = {0};

/* ──── Splash screen ──── */

static lv_obj_t *create_splash_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "CrowPanel Face Dashboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xe0e0ff), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Initializing...");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x808090), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t *spinner = lv_spinner_create(scr);
    lv_spinner_set_anim_params(spinner, 1000, 200);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 80);

    return scr;
}

/* ──── Screensaver / Lock screen ──── */

static void screensaver_tap_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Screen tapped — unlocking to guest dashboard");
    app_dashboard_show(DASHBOARD_GUEST);
}

static void settings_gear_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Settings gear tapped — showing passcode screen");
    app_dashboard_show(DASHBOARD_SETTINGS);
}

static lv_obj_t *create_screensaver_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    /* Make entire screen tappable to unlock */
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, screensaver_tap_cb, LV_EVENT_CLICKED, NULL);

    /* Clock display */
    lv_obj_t *clock_lbl = lv_label_create(scr);
    lv_label_set_text(clock_lbl, "12:00");
    lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(clock_lbl, lv_color_hex(0x404060), 0);
    lv_obj_align(clock_lbl, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Tap screen or face the camera to unlock");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x303040), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 30);

    /* Settings gear icon — top right corner */
    lv_obj_t *gear_btn = lv_btn_create(scr);
    lv_obj_set_size(gear_btn, 50, 50);
    lv_obj_align(gear_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(gear_btn, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(gear_btn, LV_OPA_80, 0);
    lv_obj_set_style_radius(gear_btn, 25, 0);
    lv_obj_set_style_border_width(gear_btn, 0, 0);
    lv_obj_set_style_shadow_width(gear_btn, 0, 0);
    lv_obj_add_event_cb(gear_btn, settings_gear_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *gear_icon = lv_label_create(gear_btn);
    lv_label_set_text(gear_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gear_icon, lv_color_hex(0x606080), 0);
    lv_obj_set_style_text_font(gear_icon, &lv_font_montserrat_20, 0);
    lv_obj_center(gear_icon);

    return scr;
}

/* ──── Passcode screen (Settings entry gate) ──── */

static void passcode_reset(void)
{
    s_passcode_pos = 0;
    memset(s_passcode_input, 0, sizeof(s_passcode_input));
    for (int i = 0; i < PASSCODE_LEN; i++) {
        if (s_passcode_dots[i]) {
            lv_obj_set_style_bg_color(s_passcode_dots[i], lv_color_hex(0x404060), 0);
        }
    }
    if (s_passcode_status_label) {
        lv_label_set_text(s_passcode_status_label, "");
    }
}

static void passcode_wrong_timer_cb(lv_timer_t *t)
{
    passcode_reset();
    lv_timer_delete(t);
}

static void passcode_digit_cb(lv_event_t *e)
{
    const char *digit_str = (const char *)lv_event_get_user_data(e);
    if (s_passcode_pos >= PASSCODE_LEN) return;

    s_passcode_input[s_passcode_pos] = digit_str[0];

    /* Light up the dot */
    if (s_passcode_dots[s_passcode_pos]) {
        lv_obj_set_style_bg_color(s_passcode_dots[s_passcode_pos], lv_color_hex(0x4080ff), 0);
    }
    s_passcode_pos++;

    /* Check when all digits entered */
    if (s_passcode_pos == PASSCODE_LEN) {
        if (strcmp(s_passcode_input, CORRECT_PASSCODE) == 0) {
            ESP_LOGI(TAG, "Passcode correct — entering enrollment");
            passcode_reset();
            app_dashboard_show(DASHBOARD_ENROLLMENT);
        } else {
            ESP_LOGW(TAG, "Wrong passcode");
            if (s_passcode_status_label) {
                lv_label_set_text(s_passcode_status_label, "Wrong code");
                lv_obj_set_style_text_color(s_passcode_status_label, lv_color_hex(0xff4040), 0);
            }
            /* Reset after a short visual delay via LVGL timer */
            lv_timer_create(passcode_wrong_timer_cb, 800, NULL);
        }
    }
}

static void passcode_backspace_cb(lv_event_t *e)
{
    (void)e;
    if (s_passcode_pos <= 0) return;
    s_passcode_pos--;
    s_passcode_input[s_passcode_pos] = '\0';
    if (s_passcode_dots[s_passcode_pos]) {
        lv_obj_set_style_bg_color(s_passcode_dots[s_passcode_pos], lv_color_hex(0x404060), 0);
    }
}

static void passcode_cancel_cb(lv_event_t *e)
{
    (void)e;
    passcode_reset();
    app_dashboard_show(DASHBOARD_SCREENSAVER);
}

/* Digit strings must be static so the pointer remains valid */
static const char digit_strs[10][2] = {"0","1","2","3","4","5","6","7","8","9"};

static lv_obj_t *create_passcode_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a1e), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Enter Passcode");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xc0c0e0), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* Passcode dots */
    lv_obj_t *dot_container = lv_obj_create(scr);
    lv_obj_set_size(dot_container, 200, 40);
    lv_obj_align(dot_container, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_opa(dot_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dot_container, 0, 0);
    lv_obj_set_style_pad_all(dot_container, 0, 0);
    lv_obj_set_flex_flow(dot_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dot_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dot_container, 20, 0);

    for (int i = 0; i < PASSCODE_LEN; i++) {
        lv_obj_t *dot = lv_obj_create(dot_container);
        lv_obj_set_size(dot, 24, 24);
        lv_obj_set_style_radius(dot, 12, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x404060), 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x6060a0), 0);
        lv_obj_set_style_border_width(dot, 2, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        s_passcode_dots[i] = dot;
    }

    /* Status label (wrong code feedback) */
    s_passcode_status_label = lv_label_create(scr);
    lv_label_set_text(s_passcode_status_label, "");
    lv_obj_set_style_text_font(s_passcode_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_passcode_status_label, LV_ALIGN_TOP_MID, 0, 130);

    /* Numpad: 3 cols x 4 rows (1-9, backspace, 0, enter) */
    lv_obj_t *numpad = lv_obj_create(scr);
    lv_obj_set_size(numpad, 300, 340);
    lv_obj_align(numpad, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_opa(numpad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(numpad, 0, 0);
    lv_obj_set_style_pad_all(numpad, 5, 0);
    lv_obj_set_flex_flow(numpad, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(numpad, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(numpad, 8, 0);
    lv_obj_set_style_pad_column(numpad, 8, 0);

    /* Digits 1-9 */
    for (int d = 1; d <= 9; d++) {
        lv_obj_t *btn = lv_btn_create(numpad);
        lv_obj_set_size(btn, 85, 70);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x252545), 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_add_event_cb(btn, passcode_digit_cb, LV_EVENT_CLICKED, (void *)digit_strs[d]);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, digit_strs[d]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xe0e0ff), 0);
        lv_obj_center(lbl);
    }

    /* Row 4: Backspace, 0, Cancel */
    /* Backspace */
    lv_obj_t *bksp = lv_btn_create(numpad);
    lv_obj_set_size(bksp, 85, 70);
    lv_obj_set_style_bg_color(bksp, lv_color_hex(0x352535), 0);
    lv_obj_set_style_radius(bksp, 12, 0);
    lv_obj_add_event_cb(bksp, passcode_backspace_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bksp_lbl = lv_label_create(bksp);
    lv_label_set_text(bksp_lbl, LV_SYMBOL_BACKSPACE);
    lv_obj_set_style_text_font(bksp_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(bksp_lbl, lv_color_hex(0xff8080), 0);
    lv_obj_center(bksp_lbl);

    /* 0 */
    lv_obj_t *zero_btn = lv_btn_create(numpad);
    lv_obj_set_size(zero_btn, 85, 70);
    lv_obj_set_style_bg_color(zero_btn, lv_color_hex(0x252545), 0);
    lv_obj_set_style_radius(zero_btn, 12, 0);
    lv_obj_add_event_cb(zero_btn, passcode_digit_cb, LV_EVENT_CLICKED, (void *)digit_strs[0]);
    lv_obj_t *zero_lbl = lv_label_create(zero_btn);
    lv_label_set_text(zero_lbl, "0");
    lv_obj_set_style_text_font(zero_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(zero_lbl, lv_color_hex(0xe0e0ff), 0);
    lv_obj_center(zero_lbl);

    /* Cancel */
    lv_obj_t *cancel = lv_btn_create(numpad);
    lv_obj_set_size(cancel, 85, 70);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x352525), 0);
    lv_obj_set_style_radius(cancel, 12, 0);
    lv_obj_add_event_cb(cancel, passcode_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(0xff6060), 0);
    lv_obj_center(cancel_lbl);

    return scr;
}

/* ──── Enrollment screen ──── */

static void enroll_name_cb(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    strncpy(s_enroll_name, name, sizeof(s_enroll_name) - 1);
    ESP_LOGI(TAG, "Starting enrollment for '%s'", s_enroll_name);
    app_face_enroll_start(s_enroll_name);

    /* Switch to the enrollment progress screen */
    /* For now, show a simple status and go back to screensaver */
    app_dashboard_show(DASHBOARD_SCREENSAVER);
}

static void enroll_cancel_cb(lv_event_t *e)
{
    (void)e;
    app_dashboard_show(DASHBOARD_SCREENSAVER);
}

static const char *enroll_names[] = {"sundar", "user2", "guest"};

static lv_obj_t *create_enrollment_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a1a2e), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN "  Face Enrollment");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xe0e0ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* Instructions */
    lv_obj_t *instr = lv_label_create(scr);
    lv_label_set_text(instr, "Select a user to enroll, then face the camera.");
    lv_obj_set_style_text_font(instr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(instr, lv_color_hex(0x808090), 0);
    lv_obj_align(instr, LV_ALIGN_TOP_MID, 0, 75);

    /* Enrolled users count */
    lv_obj_t *count_lbl = lv_label_create(scr);
    char count_text[64];
    snprintf(count_text, sizeof(count_text), "Enrolled users: %d / %d",
             app_face_get_user_count(), APP_FACE_MAX_USERS);
    lv_label_set_text(count_lbl, count_text);
    lv_obj_set_style_text_font(count_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(count_lbl, lv_color_hex(0x606080), 0);
    lv_obj_align(count_lbl, LV_ALIGN_TOP_MID, 0, 110);

    /* Name buttons */
    int num_names = sizeof(enroll_names) / sizeof(enroll_names[0]);
    for (int i = 0; i < num_names; i++) {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_set_size(btn, 250, 60);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, -60 + i * 80);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a2a4e), 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_add_event_cb(btn, enroll_name_cb, LV_EVENT_CLICKED, (void *)enroll_names[i]);

        lv_obj_t *lbl = lv_label_create(btn);
        char btn_text[48];
        snprintf(btn_text, sizeof(btn_text), LV_SYMBOL_PLUS "  Enroll \"%s\"", enroll_names[i]);
        lv_label_set_text(lbl, btn_text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xe0e0ff), 0);
        lv_obj_center(lbl);
    }

    /* Cancel button */
    lv_obj_t *cancel_btn = lv_btn_create(scr);
    lv_obj_set_size(cancel_btn, 150, 50);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x3a2020), 0);
    lv_obj_set_style_radius(cancel_btn, 10, 0);
    lv_obj_add_event_cb(cancel_btn, enroll_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_LEFT "  Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(0xff8080), 0);
    lv_obj_center(cancel_lbl);

    return scr;
}

/* ──── Dashboard show/switch ──── */

void app_dashboard_show(dashboard_id_t id)
{
    if (id >= DASHBOARD_MAX) {
        ESP_LOGE(TAG, "Invalid dashboard ID: %d", id);
        return;
    }

    /* The settings screen is now the passcode entry gate */
    /* Enrollment screen is re-created each time to refresh counts */
    if (id == DASHBOARD_ENROLLMENT && s_screens[id]) {
        lv_obj_delete(s_screens[id]);
        s_screens[id] = NULL;
    }
    /* Reset passcode state when showing passcode screen */
    if (id == DASHBOARD_SETTINGS) {
        passcode_reset();
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
                s_screens[id] = create_passcode_screen();
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
