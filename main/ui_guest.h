#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the guest/default dashboard screen
 * @return LVGL screen object
 */
lv_obj_t *ui_guest_create(void);

#ifdef __cplusplus
}
#endif
