#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Face recognition configuration */
#define APP_FACE_MAX_USERS          10
#define APP_FACE_MAX_NAME_LEN       32
#define APP_FACE_EMBEDDING_DIM      512
#define APP_FACE_MATCH_THRESHOLD    0.6f
#define APP_FACE_NO_FACE_TIMEOUT_S  30
#define APP_FACE_ENROLL_FRAMES      5
#define APP_FACE_NVS_PARTITION      "face_db"
#define APP_FACE_NVS_NAMESPACE      "faces"

/* Face recognition state */
typedef enum {
    FACE_STATE_IDLE,
    FACE_STATE_DETECTING,
    FACE_STATE_RECOGNIZED,
    FACE_STATE_UNKNOWN,
    FACE_STATE_ENROLLING,
} app_face_state_t;

/* Recognized face result */
typedef struct {
    int user_id;                            /**< Internal user ID (0-based) */
    char name[APP_FACE_MAX_NAME_LEN];       /**< User name label */
    float confidence;                       /**< Match confidence (0.0 - 1.0) */
} app_face_result_t;

/* Callback for face recognition events */
typedef void (*app_face_cb_t)(app_face_state_t state, const app_face_result_t *result);

/**
 * @brief Initialize face recognition system
 *
 * Loads face detection and recognition models, and restores enrolled
 * face embeddings from NVS.
 *
 * @return ESP_OK on success
 */
esp_err_t app_face_init(void);

/**
 * @brief Start continuous face scanning
 *
 * Creates a background task that captures camera frames, runs face
 * detection, and matches against enrolled faces.
 */
void app_face_start(void);

/**
 * @brief Stop face scanning
 */
void app_face_stop(void);

/**
 * @brief Begin enrollment for a new user
 * @param name User name to enroll
 * @return ESP_OK if enrollment started
 */
esp_err_t app_face_enroll_start(const char *name);

/**
 * @brief Cancel an in-progress enrollment
 */
void app_face_enroll_cancel(void);

/**
 * @brief Delete an enrolled user
 * @param name User name to delete
 * @return ESP_OK if user found and deleted
 */
esp_err_t app_face_delete_user(const char *name);

/**
 * @brief Get the number of enrolled users
 * @return Number of enrolled users
 */
int app_face_get_user_count(void);

/**
 * @brief Register a callback for face recognition events
 * @param cb Callback function
 */
void app_face_set_callback(app_face_cb_t cb);

/**
 * @brief Get current face recognition state
 * @return Current state
 */
app_face_state_t app_face_get_state(void);

/* Camera preview for enrollment UI */
#define APP_FACE_PREVIEW_W  320
#define APP_FACE_PREVIEW_H  188

/**
 * @brief Get a downscaled camera preview frame + last face bbox
 *
 * Copies the latest downscaled RGB565 preview into out_buf.
 * Returns true if a face was detected in the last frame.
 *
 * @param out_buf     Destination buffer (must be at least PREVIEW_W * PREVIEW_H * 2 bytes)
 * @param face_x      Output: face bbox X (in preview coords), or -1
 * @param face_y      Output: face bbox Y (in preview coords), or -1
 * @param face_w      Output: face bbox width, or 0
 * @param face_h      Output: face bbox height, or 0
 * @return true if preview data was available
 */
bool app_face_get_preview(uint16_t *out_buf, int *face_x, int *face_y, int *face_w, int *face_h);

#ifdef __cplusplus
}
#endif
