/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 09 — Earthside Handshake: backend request lifecycle
 * @details One persistent FreeRTOS worker task blocks on a length-1 queue
 *          and performs each handshake request synchronously via
 *          esp_http_client_perform() - deliberately off the LVGL task. A
 *          length-1 queue is enough because handshake_client_send's
 *          in-flight guard (state == HS_SENDING) already ensures at most
 *          one request is ever outstanding.
 *
 *          Keeps three kinds of evidence separate, per the Mission 09
 *          directive:
 *            - transport result: did esp_http_client_perform even succeed,
 *              and did it fail fast or ride out the deliberate timeout
 *              ceiling (see the elapsed_ms check in perform_request);
 *            - HTTP status code: did the server respond, and how;
 *            - parsed application response: accepted / event_id.
 *
 *          Same foreign-task-callback contract as wifi_manager.h: the
 *          caller must bridge into the LVGL task itself, exactly as
 *          status_deck_ui.c already does for Wi-Fi.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "handshake_client.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h not found. Define BACKEND_BASE_URL there, e.g. #define BACKEND_BASE_URL \"http://192.168.1.31:8000\" - your development laptop's LAN IP, not 127.0.0.1."
#endif

static const char *TAG = "handshake";

#define HANDSHAKE_PATH "/api/v1/handshake"
#define REQUEST_TIMEOUT_MS 5000
#define RESPONSE_BUF_LEN 512

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static handshake_status_t s_status = {
    .state = HS_IDLE,
    .last_http_status = 0,
    .last_event_id = "",
    .last_result_us = 0,
};

static handshake_event_cb_t s_cb = NULL;
static void *s_cb_ctx = NULL;
static QueueHandle_t s_request_queue = NULL;

/* Single static buffer: safe because handshake_client_send's in-flight
 * guard ensures at most one request is ever being performed at a time, so
 * there is never a second writer. */
static char s_response_buf[RESPONSE_BUF_LEN];
static int s_response_len;

static void notify(handshake_state_t new_state, const char *message)
{
    if (s_cb) {
        s_cb(new_state, message, s_cb_ctx);
    }
}

static void set_result(handshake_state_t state, int http_status, const char *event_id_or_null)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = state;
    s_status.last_http_status = http_status;
    if (event_id_or_null) {
        strncpy(s_status.last_event_id, event_id_or_null, sizeof(s_status.last_event_id) - 1);
        s_status.last_event_id[sizeof(s_status.last_event_id) - 1] = '\0';
    }
    s_status.last_result_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_mux);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && !esp_http_client_is_chunked_response(evt->client)) {
        int room = RESPONSE_BUF_LEN - 1 - s_response_len;
        int copy_len = evt->data_len < room ? evt->data_len : room;
        if (copy_len > 0) {
            memcpy(s_response_buf + s_response_len, evt->data, (size_t)copy_len);
            s_response_len += copy_len;
            s_response_buf[s_response_len] = '\0';
        }
    }
    return ESP_OK;
}

/* Parses {"accepted": bool, "event_id": str, ...}. Only "accepted" and
 * "event_id" matter to the state machine - server_time/message are for the
 * operator's own curiosity via serial logs, not tracked in status. */
static bool parse_response(char *event_id_out, size_t event_id_out_len)
{
    cJSON *root = cJSON_ParseWithLength(s_response_buf, (size_t)s_response_len);
    if (!root) {
        return false;
    }
    cJSON *accepted = cJSON_GetObjectItemCaseSensitive(root, "accepted");
    cJSON *event_id = cJSON_GetObjectItemCaseSensitive(root, "event_id");
    bool ok = cJSON_IsBool(accepted) && cJSON_IsTrue(accepted) && cJSON_IsString(event_id) && event_id->valuestring;
    if (ok) {
        strncpy(event_id_out, event_id->valuestring, event_id_out_len - 1);
        event_id_out[event_id_out_len - 1] = '\0';
    }
    cJSON_Delete(root);
    return ok;
}

static void perform_request(const handshake_payload_t *payload)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", payload->device_id);
    cJSON_AddStringToObject(root, "event_type", payload->event_type);
    cJSON_AddStringToObject(root, "mission", payload->mission);
    cJSON_AddNumberToObject(root, "device_uptime_ms", payload->device_uptime_ms);
    cJSON_AddNumberToObject(root, "sequence", payload->sequence);
    cJSON_AddNumberToObject(root, "sample_interval_ms", payload->sample_interval_ms);

    /* Real samples only - accel_sample_count is however many the ring
     * buffer actually had (0..HANDSHAKE_MAX_ACCEL_SAMPLES), never padded. */
    cJSON *samples = cJSON_AddArrayToObject(root, "accel_samples_g");
    for (int i = 0; i < payload->accel_sample_count; i++) {
        cJSON_AddItemToArray(samples, cJSON_CreateNumber((double)payload->accel_samples_g[i]));
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        ESP_LOGE(TAG, "failed to build request JSON");
        set_result(HS_BAD_RESPONSE, 0, NULL);
        notify(HS_BAD_RESPONSE, "BAD RESPONSE");
        return;
    }

    char url[96];
    snprintf(url, sizeof(url), "%s%s", BACKEND_BASE_URL, HANDSHAKE_PATH);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = REQUEST_TIMEOUT_MS,
        .event_handler = http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        /* Only realistic cause: a malformed BACKEND_BASE_URL. */
        ESP_LOGE(TAG, "esp_http_client_init failed for url=%s", url);
        free(body);
        set_result(HS_NETWORK_ERROR, 0, NULL);
        notify(HS_NETWORK_ERROR, "BACKEND UNREACHABLE");
        return;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    s_response_len = 0;
    s_response_buf[0] = '\0';

    int64_t start_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(body);
        /* No HTTP status was ever received - transport-level failure. A
         * fast failure (connection refused: backend process not running)
         * reads as NETWORK_ERROR; riding out (close to) the full timeout
         * budget reads as TIMEOUT (host unreachable / packets silently
         * dropped) - esp_http_client_perform doesn't expose that
         * distinction directly, so this is inferred from elapsed time. */
        if (elapsed_ms >= REQUEST_TIMEOUT_MS - 200) {
            ESP_LOGW(TAG, "handshake timed out after %lldms: %s", (long long)elapsed_ms, esp_err_to_name(err));
            set_result(HS_TIMEOUT, 0, NULL);
            notify(HS_TIMEOUT, "HTTP TIMEOUT");
        } else {
            ESP_LOGW(TAG, "handshake unreachable after %lldms: %s", (long long)elapsed_ms, esp_err_to_name(err));
            set_result(HS_NETWORK_ERROR, 0, NULL);
            notify(HS_NETWORK_ERROR, "BACKEND UNREACHABLE");
        }
        return;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);

    if (status != 200) {
        ESP_LOGW(TAG, "handshake server error, status=%d body=%s", status, s_response_buf);
        set_result(HS_SERVER_ERROR, status, NULL);
        char msg[24];
        snprintf(msg, sizeof(msg), "SERVER ERROR %d", status);
        notify(HS_SERVER_ERROR, msg);
        return;
    }

    char event_id[sizeof(s_status.last_event_id)];
    if (!parse_response(event_id, sizeof(event_id))) {
        ESP_LOGW(TAG, "handshake bad response body=%s", s_response_buf);
        set_result(HS_BAD_RESPONSE, status, NULL);
        notify(HS_BAD_RESPONSE, "BAD RESPONSE");
        return;
    }

    ESP_LOGI(TAG, "handshake accepted, event_id=%s, samples=%d", event_id, payload->accel_sample_count);
    set_result(HS_ACCEPTED, status, event_id);
    char msg[40];
    snprintf(msg, sizeof(msg), "SENT x%d OK #%.8s", payload->accel_sample_count, event_id);
    notify(HS_ACCEPTED, msg);
}

static void worker_task(void *arg)
{
    (void)arg;
    handshake_payload_t payload;
    for (;;) {
        if (xQueueReceive(s_request_queue, &payload, portMAX_DELAY) == pdTRUE) {
            perform_request(&payload);
        }
    }
}

void handshake_client_init(handshake_event_cb_t cb, void *user_ctx)
{
    s_cb = cb;
    s_cb_ctx = user_ctx;
    s_request_queue = xQueueCreate(1, sizeof(handshake_payload_t));
    xTaskCreate(worker_task, "handshake_worker", 6144, NULL, 5, NULL);
}

void handshake_client_get_status(handshake_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    portEXIT_CRITICAL(&s_mux);
}

bool handshake_client_send(const handshake_payload_t *payload)
{
    portENTER_CRITICAL(&s_mux);
    bool busy = (s_status.state == HS_SENDING);
    if (!busy) {
        s_status.state = HS_SENDING;
    }
    portEXIT_CRITICAL(&s_mux);

    if (busy) {
        return false; /* in-flight guard: duplicate press while sending */
    }

    notify(HS_SENDING, "HANDSHAKE START");

    if (xQueueSend(s_request_queue, payload, 0) != pdTRUE) {
        /* Should not happen given the guard above (queue depth 1, and only
         * this function ever enqueues) - stay safe if it somehow does. */
        set_result(HS_NETWORK_ERROR, 0, NULL);
        notify(HS_NETWORK_ERROR, "BACKEND UNREACHABLE");
        return false;
    }
    return true;
}
