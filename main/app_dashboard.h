#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dashboard IDs */
typedef enum {
    DASHBOARD_SPLASH,
    DASHBOARD_SCREENSAVER,
    DASHBOARD_GUEST,
    DASHBOARD_SUNDAR,
    DASHBOARD_USER2,
    DASHBOARD_ENROLLMENT,
    DASHBOARD_SETTINGS,
    DASHBOARD_MAX,
} dashboard_id_t;

/**
 * @brief Show a specific dashboard screen
 *
 * Must be called with the LVGL mutex held.
 *
 * @param id Dashboard to show
 */
void app_dashboard_show(dashboard_id_t id);

/**
 * @brief Show the splash/boot screen
 *
 * Must be called with the LVGL mutex held.
 */
void app_dashboard_show_splash(void);

/**
 * @brief Get the currently active dashboard ID
 * @return Current dashboard ID
 */
dashboard_id_t app_dashboard_get_current(void);

#ifdef __cplusplus
}
#endif
