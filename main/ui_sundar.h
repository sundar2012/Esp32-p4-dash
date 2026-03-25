#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create Sundar's dashboard screen
 *
 * Layout (1024x600):
 *   - Top: Status bar (user name, time, date, settings/lock icons)
 *   - Left: Calendar widget (upcoming events)
 *   - Right: Stream Deck grid (4x3 action buttons)
 *
 * @return LVGL screen object
 */
lv_obj_t *ui_sundar_create(void);

/**
 * @brief Update the calendar events display
 * @param events_json JSON string with calendar events
 */
void ui_sundar_update_calendar(const char *events_json);

/**
 * @brief Update the status bar clock
 * @param time_str Time string (e.g., "12:45 PM")
 * @param date_str Date string (e.g., "Wed Mar 25")
 */
void ui_sundar_update_time(const char *time_str, const char *date_str);

#ifdef __cplusplus
}
#endif
