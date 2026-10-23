/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Low-confidence spoken-notification client - see
 *        notification_client.h.
 * @details One persistent FreeRTOS task polls GET {base}/pending on a fixed
 *          interval and stores the result in a thread-safe status struct -
 *          same shape as voice_control.c's detect_task publishing
 *          voice_status_t, just on a timer instead of driven by AFE frames.
 *          fetch_audio()/ack() are separate one-shot blocking calls made
 *          directly by voice_control.c's detect_task when "yes" is
 *          recognized - not routed through the poll task, since only one
 *          of "poll /pending" or "fetch+ack a specific notification" is
 *          ever happening at a time in practice (playback pauses WakeNet
 *          listening, so nothing else on this device is driving concurrent
 *          traffic), and adding a request queue for that would be
 *          complexity with no real caller needing it.
 */

#include <string.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "notification_client.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h not found. Define BACKEND_BASE_URL there, e.g. #define BACKEND_BASE_URL \"http://192.168.1.31:8000\" - your development laptop's LAN IP, not 127.0.0.1."
#endif

static const char *TAG = "notification_client";

/* "Lightly poll" per the directive - nothing else in this codebase polls
 * the backend periodically (every other HTTP call here is user/event
 * triggered), so this is a new pattern kept deliberately gentle. */
#define NOTIFICATION_POLL_INTERVAL_MS 5000
#define NOTIFICATION_REQUEST_TIMEOUT_MS 5000
#define NOTIFICATION_AUDIO_TIMEOUT_MS 15000
#define JSON_RESPONSE_BUF_LEN 512

/* NOTIFICATION_AUDIO_SAMPLE_RATE_HZ/BITS_PER_SAMPLE/CHANNELS come from
 * notification_client.h - see that header's comment for why they live
 * there instead of here. */

/* 30s ceiling - comfortably above the 5-20s clips notifications are
 * expected to produce, same "generous but bounded, not a measured limit"
 * reasoning audio_capture.c documents for AUDIO_NOTE_MAX_DURATION_MS. A
 * clip longer than this is truncated (see the download event handler's room
 * check below), not rejected outright. */
#define NOTIFICATION_AUDIO_MAX_MS 30000
#define NOTIFICATION_AUDIO_BUFFER_BYTES \
    ((NOTIFICATION_AUDIO_SAMPLE_RATE_HZ * NOTIFICATION_AUDIO_MAX_MS / 1000) * \
     (NOTIFICATION_AUDIO_BITS_PER_SAMPLE / 8) * NOTIFICATION_AUDIO_CHANNELS)

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static notification_status_t s_status = {
    .pending = false,
    .count = 0,
    .notification_id = "",
};

/* Single static buffer, safe for the same reason handshake_client.c's own
 * response buffer is: fetch_audio()/ack()/the poll task's own GET are never
 * called concurrently with each other (poll runs on its own task on a
 * timer; fetch/ack are one-shot calls from voice_control.c's detect_task,
 * which only ever makes one at a time). */
static char s_json_buf[JSON_RESPONSE_BUF_LEN];
static int s_json_len;

/* Audio download buffer - allocated once at init (PSRAM, same
 * heap_caps_malloc/MALLOC_CAP_SPIRAM choice audio_capture.c makes for its
 * own capture buffer), reused by every fetch_audio() call. */
/* Detail fetch (the front message's text): its own buffer, in PSRAM, so it
 * never shares s_json_buf with the fetch/ack calls on detect_task. */
#define DETAIL_BUF_LEN 2048
static char *s_detail_buf;
static int s_detail_len;
static char s_detail_id[NOTIFICATION_ID_LEN];   /* id the text belongs to */
static char *s_text;                             /* front message text, PSRAM, under s_mux */

static uint8_t *s_audio_buf = NULL;
static size_t s_audio_len;
static bool s_audio_buf_available = false;

static esp_err_t json_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && !esp_http_client_is_chunked_response(evt->client)) {
        int room = JSON_RESPONSE_BUF_LEN - 1 - s_json_len;
        int copy_len = evt->data_len < room ? evt->data_len : room;
        if (copy_len > 0) {
            memcpy(s_json_buf + s_json_len, evt->data, (size_t)copy_len);
            s_json_len += copy_len;
            s_json_buf[s_json_len] = '\0';
        }
    }
    return ESP_OK;
}

static esp_err_t audio_download_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && !esp_http_client_is_chunked_response(evt->client)) {
        size_t room = NOTIFICATION_AUDIO_BUFFER_BYTES - s_audio_len;
        size_t copy_len = (size_t)evt->data_len < room ? (size_t)evt->data_len : room;
        if (copy_len > 0) {
            memcpy(s_audio_buf + s_audio_len, evt->data, copy_len);
            s_audio_len += copy_len;
        }
    }
    return ESP_OK;
}

static esp_err_t detail_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && !esp_http_client_is_chunked_response(evt->client)) {
        int room = DETAIL_BUF_LEN - 1 - s_detail_len;
        int copy_len = evt->data_len < room ? evt->data_len : room;
        if (copy_len > 0) {
            memcpy(s_detail_buf + s_detail_len, evt->data, (size_t)copy_len);
            s_detail_len += copy_len;
            s_detail_buf[s_detail_len] = '\0';
        }
    }
    return ESP_OK;
}

/* The value of "key": "..." in `json`, unescaped into out (truncated to
 * fit). No allocation: a JSON parser here would take its nodes from
 * internal RAM. \uXXXX outside ASCII becomes '?' (the fonts are ASCII). */
static bool json_string_field(const char *json, const char *key, char *out, size_t out_len)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return false;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == ':') {
        p++;
    }
    if (*p != '"') {
        return false;                 /* null, or not a string */
    }
    p++;
    size_t o = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            char e = *p++;
            c = e == 'n' ? ' ' : e == 't' ? ' ' : e;
            if (e == 'u') {
                c = '?';
                for (int i = 0; i < 4 && *p; i++) {
                    p++;
                }
            }
        }
        if (o + 1 < out_len) {
            out[o++] = c;
        }
    }
    out[o] = '\0';
    return *p == '"';                 /* false if the body was cut off mid-string */
}

/* GET {base}/{id} for the front message's spoken_message. Poll task only,
 * once per new front id. */
static void fetch_text(const char *id)
{
    char text[NOTIFICATION_TEXT_LEN] = "";
    uint8_t state = 2;
    if (s_detail_buf) {
        char url[192];
        snprintf(url, sizeof(url), "%s%s/%s", BACKEND_BASE_URL, NOTIFICATIONS_BASE_PATH, id);
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = NOTIFICATION_REQUEST_TIMEOUT_MS,
            .event_handler = detail_event_handler,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client) {
            s_detail_len = 0;
            s_detail_buf[0] = '\0';
            esp_err_t err = esp_http_client_perform(client);
            int status = esp_http_client_get_status_code(client);
            esp_http_client_cleanup(client);
            if (err == ESP_OK && status == 200 &&
                json_string_field(s_detail_buf, "spoken_message", text, sizeof(text)) && text[0]) {
                state = 1;
            } else {
                ESP_LOGI(TAG, "text for %s not loaded (err=%s status=%d len=%d)", id,
                         esp_err_to_name(err), status, s_detail_len);
            }
        }
    }
    portENTER_CRITICAL(&s_mux);
    if (strcmp(s_status.notification_id, id) == 0) {   /* still the front one */
        if (s_text) {
            strncpy(s_text, text, NOTIFICATION_TEXT_LEN - 1);
            s_text[NOTIFICATION_TEXT_LEN - 1] = '\0';
        }
        s_status.text_state = s_text ? state : 2;
    }
    portEXIT_CRITICAL(&s_mux);
}

static void set_status(bool pending, int count, const char *notification_id)
{
    portENTER_CRITICAL(&s_mux);
    s_status.polled = true;
    if (!notification_id || strcmp(s_status.notification_id, notification_id) != 0) {
        if (s_text) {
            s_text[0] = '\0';         /* a different front message: its text is not known yet */
        }
        s_status.text_state = 0;
    }
    s_status.pending = pending;
    s_status.count = count;
    if (notification_id) {
        strncpy(s_status.notification_id, notification_id, sizeof(s_status.notification_id) - 1);
        s_status.notification_id[sizeof(s_status.notification_id) - 1] = '\0';
    } else {
        s_status.notification_id[0] = '\0';
    }
    portEXIT_CRITICAL(&s_mux);
}

/* One blocking GET {base}/pending -> updates s_status. Used by both the
 * background poll task and notification_client_refresh_now(). */
static void poll_once(bool with_text)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s/pending", BACKEND_BASE_URL, NOTIFICATIONS_BASE_PATH);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = NOTIFICATION_REQUEST_TIMEOUT_MS,
        .event_handler = json_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGW(TAG, "esp_http_client_init failed for url=%s", url);
        return;
    }

    s_json_len = 0;
    s_json_buf[0] = '\0';

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        /* Backend unreachable/asleep is an expected, unremarkable state for
         * a background poll (unlike a user-triggered request) - logged at
         * INFO, not WARN, and status is left as-is rather than forced to
         * "not pending" on a transient failure. */
        ESP_LOGI(TAG, "pending poll unreachable: %s", esp_err_to_name(err));
        return;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (status != 200) {
        ESP_LOGW(TAG, "pending poll server error, status=%d", status);
        return;
    }

    cJSON *root = cJSON_ParseWithLength(s_json_buf, (size_t)s_json_len);
    if (!root) {
        ESP_LOGW(TAG, "pending poll bad response body=%s", s_json_buf);
        return;
    }

    cJSON *pending_item = cJSON_GetObjectItemCaseSensitive(root, "pending");
    cJSON *count_item = cJSON_GetObjectItemCaseSensitive(root, "count");
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "notification_id");

    bool pending = cJSON_IsBool(pending_item) && cJSON_IsTrue(pending_item);
    int count = cJSON_IsNumber(count_item) ? count_item->valueint : 0;
    const char *notification_id = (pending && cJSON_IsString(id_item)) ? id_item->valuestring : NULL;

    set_status(pending, count, notification_id);
    char want[NOTIFICATION_ID_LEN] = "";
    if (!notification_id) {
        /* Queue empty: forget whose text we had, so the same id coming back
         * to the front is fetched again rather than left "not tried". */
        if (with_text) {
            s_detail_id[0] = '\0';
        }
    } else if (with_text && strcmp(notification_id, s_detail_id) != 0) {
        strncpy(want, notification_id, sizeof(want) - 1);
    }
    cJSON_Delete(root);
    if (want[0]) {
        /* Only the poll task (with_text) fetches text or touches s_detail_*,
         * so they need no lock. refresh_now() runs on detect_task between
         * playback and reopening the mic, and must not add a GET there. */
        strncpy(s_detail_id, want, sizeof(s_detail_id) - 1);
        fetch_text(want);
    }
}

static void poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        poll_once(true);
        vTaskDelay(pdMS_TO_TICKS(NOTIFICATION_POLL_INTERVAL_MS));
    }
}

void notification_client_init(void)
{
    /* MALLOC_CAP_SPIRAM first, MALLOC_CAP_8BIT fallback - same order
     * audio_capture.c uses for its own PSRAM-sized buffer, and for the same
     * reason: this project's sdkconfig has CONFIG_SPIRAM=y, so the fallback
     * is not expected to be exercised on this board, only kept as a defined
     * outcome instead of an unchecked assumption. */
    s_audio_buf = heap_caps_malloc(NOTIFICATION_AUDIO_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_audio_buf) {
        s_audio_buf = heap_caps_malloc(NOTIFICATION_AUDIO_BUFFER_BYTES, MALLOC_CAP_8BIT);
    }
    if (!s_audio_buf) {
        ESP_LOGE(TAG, "audio buffer alloc failed (%d bytes) - notification playback unavailable this boot",
                 NOTIFICATION_AUDIO_BUFFER_BYTES);
        s_audio_buf_available = false;
    } else {
        s_audio_buf_available = true;
        ESP_LOGI(TAG, "notification audio buffer ready: %d bytes (~%ds at %dHz/%dbit/%dch)",
                 NOTIFICATION_AUDIO_BUFFER_BYTES, NOTIFICATION_AUDIO_MAX_MS / 1000,
                 NOTIFICATION_AUDIO_SAMPLE_RATE_HZ, NOTIFICATION_AUDIO_BITS_PER_SAMPLE, NOTIFICATION_AUDIO_CHANNELS);
    }

    s_detail_buf = heap_caps_malloc(DETAIL_BUF_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_text = heap_caps_calloc(1, NOTIFICATION_TEXT_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_detail_buf) {
        ESP_LOGW(TAG, "no PSRAM for notification text; cards will say it was not loaded");
    }

    xTaskCreate(poll_task, "notif_poll", 4096, NULL, 4, NULL);
}

void notification_client_get_status(notification_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    portEXIT_CRITICAL(&s_mux);
}

void notification_client_get_text(char *out, size_t out_len)
{
    portENTER_CRITICAL(&s_mux);
    if (s_text && s_status.text_state == 1) {
        strncpy(out, s_text, out_len - 1);
        out[out_len - 1] = '\0';
    } else {
        out[0] = '\0';
    }
    portEXIT_CRITICAL(&s_mux);
}

void notification_client_refresh_now(void)
{
    poll_once(false);   /* count/front only; the poll task loads the text */
}

bool notification_client_fetch_audio(const char *notification_id,
                                      const uint8_t **pcm_out, size_t *len_out,
                                      char *fail_reason_out, size_t fail_reason_out_len)
{
    if (!s_audio_buf_available) {
        snprintf(fail_reason_out, fail_reason_out_len, "NO AUDIO BUFFER");
        return false;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s%s/%s/audio", BACKEND_BASE_URL, NOTIFICATIONS_BASE_PATH, notification_id);

    s_audio_len = 0;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = NOTIFICATION_AUDIO_TIMEOUT_MS,
        .event_handler = audio_download_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed for url=%s", url);
        snprintf(fail_reason_out, fail_reason_out_len, "CONNECTION FAILED");
        return false;
    }

    int64_t start_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        if (elapsed_ms >= NOTIFICATION_AUDIO_TIMEOUT_MS - 200) {
            ESP_LOGW(TAG, "audio fetch timed out after %lldms: %s", (long long)elapsed_ms, esp_err_to_name(err));
            snprintf(fail_reason_out, fail_reason_out_len, "TIMEOUT");
        } else {
            ESP_LOGW(TAG, "audio fetch unreachable after %lldms: %s", (long long)elapsed_ms, esp_err_to_name(err));
            snprintf(fail_reason_out, fail_reason_out_len, "UNREACHABLE");
        }
        return false;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGW(TAG, "audio fetch server error, status=%d", status);
        snprintf(fail_reason_out, fail_reason_out_len, "FETCH ERR %d", status);
        return false;
    }
    if (s_audio_len == 0) {
        ESP_LOGW(TAG, "audio fetch returned 0 bytes");
        snprintf(fail_reason_out, fail_reason_out_len, "EMPTY AUDIO");
        return false;
    }

    ESP_LOGI(TAG, "audio fetch complete: id=%s %u bytes, %lldms",
             notification_id, (unsigned)s_audio_len, (long long)elapsed_ms);
    *pcm_out = s_audio_buf;
    *len_out = s_audio_len;
    return true;
}

bool notification_client_ack(const char *notification_id)
{
    char url[192];
    snprintf(url, sizeof(url), "%s%s/%s/ack", BACKEND_BASE_URL, NOTIFICATIONS_BASE_PATH, notification_id);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = NOTIFICATION_REQUEST_TIMEOUT_MS,
        .event_handler = json_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed for url=%s", url);
        return false;
    }
    /* Empty body - the notification_id is already in the URL path. */
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, "", 0);

    s_json_len = 0;
    s_json_buf[0] = '\0';

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        ESP_LOGW(TAG, "ack failed: %s", esp_err_to_name(err));
        return false;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "ack server error, status=%d", status);
        return false;
    }

    ESP_LOGI(TAG, "ack complete: id=%s", notification_id);
    return true;
}
