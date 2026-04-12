#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Calendar fetch configuration */
#define APP_CALENDAR_FETCH_INTERVAL_S   300     /* 5 minutes */
#define APP_CALENDAR_MAX_EVENTS         20
#define APP_CALENDAR_NVS_NAMESPACE      "calendar"
#define APP_CALENDAR_NVS_KEY_URL        "url"

/* Calendar event */
typedef struct {
    char time[16];       /**< Start time (e.g., "9:00") */
    char title[64];      /**< Event title */
    char color[8];       /**< Hex color (e.g., "#4361ee") */
    int day_offset;      /**< 0 = today, 1 = tomorrow, etc. */
} app_calendar_event_t;

/**
 * @brief Start the calendar fetch background task
 *
 * Fetches calendar events from the configured HTTP endpoint every 5 minutes.
 * Updates Sundar's dashboard calendar widget via ui_sundar_update_calendar().
 */
void app_calendar_start(void);

/**
 * @brief Stop the calendar fetch task
 */
void app_calendar_stop(void);

/**
 * @brief Force an immediate calendar refresh
 * @return ESP_OK if fetch succeeded
 */
esp_err_t app_calendar_refresh(void);

/**
 * @brief Set the calendar API endpoint URL
 * @param url HTTP(S) URL that returns JSON calendar events
 * @return ESP_OK on success
 */
esp_err_t app_calendar_set_url(const char *url);

/**
 * @brief Get the current calendar events
 * @param events Array to fill with events
 * @param max_events Maximum number of events to return
 * @return Number of events copied
 */
int app_calendar_get_events(app_calendar_event_t *events, int max_events);

#ifdef __cplusplus
}
#endif
