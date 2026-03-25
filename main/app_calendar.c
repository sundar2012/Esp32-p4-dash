#include "app_calendar.h"
#include "app_wifi.h"
#include "ui_sundar.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "app_calendar";

static TaskHandle_t s_fetch_task = NULL;
static bool s_running = false;
static app_calendar_event_t s_events[APP_CALENDAR_MAX_EVENTS];
static int s_event_count = 0;
static char s_calendar_url[256] = "http://192.168.1.100:8080/calendar/events";

#define CALENDAR_TASK_STACK     (8 * 1024)
#define CALENDAR_TASK_PRIORITY  3
#define HTTP_RESP_BUF_SIZE      4096

/* ──── HTTP response handler ──── */

typedef struct {
    char *buffer;
    int buffer_len;
    int content_length;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *resp = (http_response_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (resp->buffer && resp->buffer_len + evt->data_len < HTTP_RESP_BUF_SIZE - 1) {
                memcpy(resp->buffer + resp->buffer_len, evt->data, evt->data_len);
                resp->buffer_len += evt->data_len;
                resp->buffer[resp->buffer_len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

/* ──── JSON parser (simple, no dependency on cJSON for size) ──── */

/*
 * Expected JSON format from the calendar endpoint:
 * {
 *   "events": [
 *     {"time": "9:00", "title": "Team Standup", "color": "#4361ee", "day": 0},
 *     {"time": "10:30", "title": "Design Review", "color": "#7209b7", "day": 0},
 *     ...
 *   ]
 * }
 *
 * This is a minimal parser. For production, consider using cJSON component.
 */

static int parse_string_field(const char *json, const char *field, char *out, int out_size)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", field);
    const char *pos = strstr(json, pattern);
    if (!pos) return -1;

    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t') pos++;
    if (*pos != '"') return -1;
    pos++;

    int i = 0;
    while (*pos && *pos != '"' && i < out_size - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return i;
}

static int parse_int_field(const char *json, const char *field)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", field);
    const char *pos = strstr(json, pattern);
    if (!pos) return 0;
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t') pos++;
    return atoi(pos);
}

static void parse_calendar_json(const char *json)
{
    s_event_count = 0;

    /* Find events array */
    const char *arr_start = strstr(json, "\"events\"");
    if (!arr_start) return;
    arr_start = strchr(arr_start, '[');
    if (!arr_start) return;

    const char *cursor = arr_start + 1;
    while (s_event_count < APP_CALENDAR_MAX_EVENTS) {
        const char *obj_start = strchr(cursor, '{');
        if (!obj_start) break;
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;

        /* Extract a single event object */
        int obj_len = obj_end - obj_start + 1;
        char obj_buf[256];
        if (obj_len >= (int)sizeof(obj_buf)) {
            cursor = obj_end + 1;
            continue;
        }
        memcpy(obj_buf, obj_start, obj_len);
        obj_buf[obj_len] = '\0';

        app_calendar_event_t *ev = &s_events[s_event_count];
        if (parse_string_field(obj_buf, "time", ev->time, sizeof(ev->time)) > 0 &&
            parse_string_field(obj_buf, "title", ev->title, sizeof(ev->title)) > 0) {
            parse_string_field(obj_buf, "color", ev->color, sizeof(ev->color));
            ev->day_offset = parse_int_field(obj_buf, "day");
            s_event_count++;
        }

        cursor = obj_end + 1;
    }

    ESP_LOGI(TAG, "Parsed %d calendar events", s_event_count);
}

/* ──── Fetch task ──── */

static esp_err_t do_fetch(void)
{
    if (!app_wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    char *resp_buf = malloc(HTTP_RESP_BUF_SIZE);
    if (!resp_buf) return ESP_ERR_NO_MEM;

    http_response_t resp = {
        .buffer = resp_buf,
        .buffer_len = 0,
    };

    esp_http_client_config_t config = {
        .url = s_calendar_url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 && resp.buffer_len > 0) {
            parse_calendar_json(resp.buffer);

            /* Update the UI on the LVGL thread */
            if (bsp_display_lock(1000)) {
                ui_sundar_update_calendar(resp.buffer);
                bsp_display_unlock();
            }
        } else {
            ESP_LOGW(TAG, "Calendar fetch: HTTP %d, %d bytes", status, resp.buffer_len);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Calendar fetch failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(resp_buf);
    return err;
}

static void calendar_fetch_task(void *pvParam)
{
    (void)pvParam;
    ESP_LOGI(TAG, "Calendar fetch task started (interval: %ds)", APP_CALENDAR_FETCH_INTERVAL_S);

    /* Initial delay to let Wi-Fi stabilize */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (s_running) {
        do_fetch();

        /* Sleep for the fetch interval, checking every second for stop signal */
        for (int i = 0; i < APP_CALENDAR_FETCH_INTERVAL_S && s_running; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "Calendar fetch task stopped");
    vTaskDelete(NULL);
}

/* ──── Public API ──── */

void app_calendar_start(void)
{
    if (s_running) return;

    /* Load URL from NVS if available */
    nvs_handle_t nvs;
    if (nvs_open(APP_CALENDAR_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t url_len = sizeof(s_calendar_url);
        nvs_get_str(nvs, APP_CALENDAR_NVS_KEY_URL, s_calendar_url, &url_len);
        nvs_close(nvs);
    }

    s_running = true;
    xTaskCreatePinnedToCore(calendar_fetch_task, "cal_fetch", CALENDAR_TASK_STACK,
                            NULL, CALENDAR_TASK_PRIORITY, &s_fetch_task, 0);
}

void app_calendar_stop(void)
{
    s_running = false;
}

esp_err_t app_calendar_refresh(void)
{
    return do_fetch();
}

esp_err_t app_calendar_set_url(const char *url)
{
    if (!url) return ESP_ERR_INVALID_ARG;

    strncpy(s_calendar_url, url, sizeof(s_calendar_url) - 1);
    s_calendar_url[sizeof(s_calendar_url) - 1] = '\0';

    /* Persist to NVS */
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(APP_CALENDAR_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret == ESP_OK) {
        nvs_set_str(nvs, APP_CALENDAR_NVS_KEY_URL, s_calendar_url);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    ESP_LOGI(TAG, "Calendar URL set: %s", s_calendar_url);
    return ESP_OK;
}

int app_calendar_get_events(app_calendar_event_t *events, int max_events)
{
    if (!events) return 0;
    int count = (s_event_count < max_events) ? s_event_count : max_events;
    memcpy(events, s_events, count * sizeof(app_calendar_event_t));
    return count;
}
