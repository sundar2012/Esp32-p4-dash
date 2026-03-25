#include "ui_sundar.h"
#include "app_shortcuts.h"
#include "app_dashboard.h"
#include "app_face.h"
#include "bsp_display.h"
#include "esp_log.h"

static const char *TAG = "ui_sundar";

/* UI element references for dynamic updates */
static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_date_label = NULL;
static lv_obj_t *s_calendar_container = NULL;

/* ──── Color theme ──── */
#define COLOR_BG            lv_color_hex(0x0f0f23)
#define COLOR_STATUS_BAR    lv_color_hex(0x1a1a3e)
#define COLOR_PANEL_BG      lv_color_hex(0x16213e)
#define COLOR_CARD_BG       lv_color_hex(0x1a1a4e)
#define COLOR_TEXT_PRIMARY   lv_color_hex(0xe0e0ff)
#define COLOR_TEXT_SECONDARY lv_color_hex(0x808090)
#define COLOR_ACCENT_BLUE    lv_color_hex(0x4361ee)
#define COLOR_ACCENT_PURPLE  lv_color_hex(0x7209b7)
#define COLOR_ACCENT_GREEN   lv_color_hex(0x06d6a0)
#define COLOR_ACCENT_ORANGE  lv_color_hex(0xf77f00)
#define COLOR_BTN_BG        lv_color_hex(0x1e2a4a)
#define COLOR_BTN_PRESSED   lv_color_hex(0x2a3a6a)

/* ──── Stream Deck button configuration ──── */
typedef struct {
    const char *label;
    const char *icon;     /* LV_SYMBOL or emoji fallback label */
    const char *type;     /* "btt" or "shortcut" */
    const char *action;   /* BTT trigger name or Shortcut name */
    lv_color_t color;     /* Accent color for button icon */
} deck_button_t;

static const deck_button_t s_deck_buttons[] = {
    {"Home",  LV_SYMBOL_HOME,      "btt",      "ShowDesktop",            {.blue=0xee, .green=0x61, .red=0x43}},
    {"Lights",LV_SYMBOL_CHARGE,    "shortcut", "Toggle Office Lights",   {.blue=0xa0, .green=0xd6, .red=0x06}},
    {"Vol +", LV_SYMBOL_VOLUME_MAX,"btt",      "VolumeUp",               {.blue=0x00, .green=0x7f, .red=0xf7}},
    {"Camera",LV_SYMBOL_IMAGE,     "shortcut", "Security Camera View",   {.blue=0xb7, .green=0x09, .red=0x72}},
    {"Music", LV_SYMBOL_AUDIO,     "btt",      "PlayPause",              {.blue=0xee, .green=0x61, .red=0x43}},
    {"Coffee",LV_SYMBOL_BELL,      "shortcut", "Coffee Machine",         {.blue=0x00, .green=0x7f, .red=0xf7}},
    {"Mail",  LV_SYMBOL_ENVELOPE,  "shortcut", "Open Mail",              {.blue=0xa0, .green=0xd6, .red=0x06}},
    {"Desktop",LV_SYMBOL_DRIVE,    "btt",      "SwitchDesktop",          {.blue=0xb7, .green=0x09, .red=0x72}},
    {"Lock",  LV_SYMBOL_EYE_CLOSE, "btt",      "LockScreen",            {.blue=0xee, .green=0x61, .red=0x43}},
    {"DND",   LV_SYMBOL_CLOSE,     "shortcut", "Toggle DND",             {.blue=0xa0, .green=0xd6, .red=0x06}},
    {"Files", LV_SYMBOL_DIRECTORY, "shortcut", "Open Files",             {.blue=0x00, .green=0x7f, .red=0xf7}},
    {"Power", LV_SYMBOL_POWER,     "btt",      "SleepDisplay",           {.blue=0x00, .green=0x00, .red=0xff}},
};
#define DECK_BUTTON_COUNT  (sizeof(s_deck_buttons) / sizeof(s_deck_buttons[0]))

/* ──── Button press callback ──── */

static void deck_btn_event_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || (size_t)idx >= DECK_BUTTON_COUNT) return;

    const deck_button_t *btn = &s_deck_buttons[idx];
    ESP_LOGI(TAG, "Deck button pressed: %s → %s:%s", btn->label, btn->type, btn->action);

    if (strcmp(btn->type, "btt") == 0) {
        app_shortcuts_trigger_btt(btn->action);
    } else {
        app_shortcuts_trigger_shortcut(btn->action);
    }
}

/* Settings gear callback */
static void settings_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Settings button pressed");
    app_dashboard_show(DASHBOARD_SETTINGS);
}

/* Lock button callback */
static void lock_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Lock button pressed");
    app_dashboard_show(DASHBOARD_SCREENSAVER);
}

/* ──── Status bar ──── */

static lv_obj_t *create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, BSP_LCD_H_RES, 40);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, COLOR_STATUS_BAR, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_left(bar, 15, 0);
    lv_obj_set_style_pad_right(bar, 15, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* User name */
    lv_obj_t *name = lv_label_create(bar);
    lv_label_set_text(name, "Sundar");
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

    /* Time */
    s_time_label = lv_label_create(bar);
    lv_label_set_text(s_time_label, "12:00 PM");
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_time_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, -60, 0);

    /* Date */
    s_date_label = lv_label_create(bar);
    lv_label_set_text(s_date_label, "Tue Mar 25");
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_date_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 60, 0);

    /* Settings gear */
    lv_obj_t *settings_btn = lv_btn_create(bar);
    lv_obj_set_size(settings_btn, 36, 36);
    lv_obj_align(settings_btn, LV_ALIGN_RIGHT_MID, -45, 0);
    lv_obj_set_style_bg_opa(settings_btn, LV_OPA_0, 0);
    lv_obj_set_style_shadow_width(settings_btn, 0, 0);
    lv_obj_t *gear_icon = lv_label_create(settings_btn);
    lv_label_set_text(gear_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gear_icon, COLOR_TEXT_SECONDARY, 0);
    lv_obj_center(gear_icon);
    lv_obj_add_event_cb(settings_btn, settings_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* Lock */
    lv_obj_t *lock_btn = lv_btn_create(bar);
    lv_obj_set_size(lock_btn, 36, 36);
    lv_obj_align(lock_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(lock_btn, LV_OPA_0, 0);
    lv_obj_set_style_shadow_width(lock_btn, 0, 0);
    lv_obj_t *lock_icon = lv_label_create(lock_btn);
    lv_label_set_text(lock_icon, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_color(lock_icon, COLOR_TEXT_SECONDARY, 0);
    lv_obj_center(lock_icon);
    lv_obj_add_event_cb(lock_btn, lock_btn_event_cb, LV_EVENT_CLICKED, NULL);

    return bar;
}

/* ──── Calendar widget ──── */

static lv_obj_t *create_calendar_widget(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 380, BSP_LCD_V_RES - 50);
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 5, 45);
    lv_obj_set_style_bg_color(panel, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 15, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 4, 0);

    s_calendar_container = panel;

    /* Section: Today */
    lv_obj_t *today_lbl = lv_label_create(panel);
    lv_label_set_text(today_lbl, "Today");
    lv_obj_set_style_text_font(today_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(today_lbl, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_pad_bottom(today_lbl, 8, 0);

    /* Placeholder events — will be replaced by calendar fetch */
    static const struct { const char *time; const char *title; uint32_t color; } events[] = {
        {"9:00",  "Team Standup",   0x4361ee},
        {"10:30", "Design Review",  0x7209b7},
        {"12:00", "Lunch",          0x06d6a0},
        {"14:00", "Phoenix Sync",   0xf77f00},
        {"16:00", "1:1 w/ Delilah", 0x4361ee},
    };

    for (int i = 0; i < 5; i++) {
        lv_obj_t *row = lv_obj_create(panel);
        lv_obj_set_size(row, LV_PCT(100), 40);
        lv_obj_set_style_bg_color(row, COLOR_CARD_BG, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(row, 3, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(events[i].color), 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_left(row, 12, 0);
        lv_obj_set_style_pad_ver(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *time_lbl = lv_label_create(row);
        lv_label_set_text(time_lbl, events[i].time);
        lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(time_lbl, lv_color_hex(events[i].color), 0);
        lv_obj_align(time_lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *title_lbl = lv_label_create(row);
        lv_label_set_text(title_lbl, events[i].title);
        lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(title_lbl, COLOR_TEXT_PRIMARY, 0);
        lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 60, 0);
    }

    /* Section: Tomorrow */
    lv_obj_t *tomorrow_lbl = lv_label_create(panel);
    lv_label_set_text(tomorrow_lbl, "\nTomorrow");
    lv_obj_set_style_text_font(tomorrow_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tomorrow_lbl, COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *tmrw_row = lv_obj_create(panel);
    lv_obj_set_size(tmrw_row, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(tmrw_row, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(tmrw_row, 0, 0);
    lv_obj_set_style_border_side(tmrw_row, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(tmrw_row, 3, 0);
    lv_obj_set_style_border_color(tmrw_row, lv_color_hex(0x7209b7), 0);
    lv_obj_set_style_radius(tmrw_row, 6, 0);
    lv_obj_set_style_pad_left(tmrw_row, 12, 0);
    lv_obj_clear_flag(tmrw_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tmrw_time = lv_label_create(tmrw_row);
    lv_label_set_text(tmrw_time, "8:30");
    lv_obj_set_style_text_font(tmrw_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tmrw_time, lv_color_hex(0x7209b7), 0);
    lv_obj_align(tmrw_time, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *tmrw_title = lv_label_create(tmrw_row);
    lv_label_set_text(tmrw_title, "Board Prep");
    lv_obj_set_style_text_font(tmrw_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tmrw_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(tmrw_title, LV_ALIGN_LEFT_MID, 60, 0);

    return panel;
}

/* ──── Stream Deck grid ──── */

static lv_obj_t *create_stream_deck(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, BSP_LCD_H_RES - 395, BSP_LCD_V_RES - 50);
    lv_obj_align(panel, LV_ALIGN_TOP_RIGHT, -5, 45);
    lv_obj_set_style_bg_color(panel, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 15, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 4 columns x 3 rows grid */
    static int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_set_grid_dsc_array(panel, col_dsc, row_dsc);
    lv_obj_set_style_pad_column(panel, 10, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_layout(panel, LV_LAYOUT_GRID);

    for (size_t i = 0; i < DECK_BUTTON_COUNT; i++) {
        int col = i % 4;
        int row = i / 4;

        lv_obj_t *btn = lv_btn_create(panel);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col, 1,
                             LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_set_style_bg_color(btn, COLOR_BTN_BG, 0);
        lv_obj_set_style_bg_color(btn, COLOR_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x2a3a6a), 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        /* Icon */
        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, s_deck_buttons[i].icon);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(icon, s_deck_buttons[i].color, 0);

        /* Label */
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, s_deck_buttons[i].label);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, COLOR_TEXT_SECONDARY, 0);

        /* Event handler */
        lv_obj_add_event_cb(btn, deck_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    return panel;
}

/* ──── Public API ──── */

lv_obj_t *ui_sundar_create(void)
{
    ESP_LOGI(TAG, "Creating Sundar's dashboard (1024x600)");

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);

    create_status_bar(scr);
    create_calendar_widget(scr);
    create_stream_deck(scr);

    return scr;
}

void ui_sundar_update_calendar(const char *events_json)
{
    if (!s_calendar_container || !events_json) return;

    /* TODO: Parse JSON events and rebuild calendar widget entries.
     * Expected JSON format:
     * [{"time":"9:00","title":"Team Standup","color":"#4361ee"}, ...]
     *
     * For now, this is a placeholder. The app_calendar module will call
     * this function after each successful fetch. */
    ESP_LOGI(TAG, "Calendar update received (JSON len=%d)", (int)strlen(events_json));
}

void ui_sundar_update_time(const char *time_str, const char *date_str)
{
    if (s_time_label && time_str) {
        lv_label_set_text(s_time_label, time_str);
    }
    if (s_date_label && date_str) {
        lv_label_set_text(s_date_label, date_str);
    }
}
