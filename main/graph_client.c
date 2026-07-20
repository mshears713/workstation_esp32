/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 13 - GO: one-shot backend graph-trigger request - see
 *        graph_client.h.
 * @details Talks to the real POST /runs contract: body {request_id,
 *          notes?} (notes omitted - GO carries no free text this pass),
 *          response {run_id, request_id, status, created_at, duplicate} on
 *          202 (new run), 200 (duplicate request_id) or 409 (another run
 *          already active). Only run_id/duplicate are read back, for the
 *          serial log - the console doesn't poll GET /runs/{run_id} for
 *          completion, it just reports whether the trigger itself landed.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "graph_client.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h not found. Define BACKEND_BASE_URL there, e.g. #define BACKEND_BASE_URL \"http://192.168.1.31:8000\" - your development laptop's LAN IP, not 127.0.0.1."
#endif

static const char *TAG = "graph_client";

#define GRAPH_REQUEST_TIMEOUT_MS 5000
#define RESPONSE_BUF_LEN 256

/* Single static buffer: safe because trigger_graph_run() is a single
 * blocking call with no worker task of its own - only ever one request in
 * flight, same reasoning handshake_client.c documents for its own buffer. */
static char s_response_buf[RESPONSE_BUF_LEN];
static int s_response_len;

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

bool trigger_graph_run(const char *request_id, char *result_out, size_t result_out_len)
{
    if (GRAPH_TRIGGER_PATH[0] == '\0') {
        ESP_LOGW(TAG, "GRAPH_TRIGGER_PATH not configured - not attempting a request");
        snprintf(result_out, result_out_len, "ENDPOINT NOT SET");
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "request_id", request_id);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        ESP_LOGE(TAG, "failed to build request JSON");
        snprintf(result_out, result_out_len, "REQUEST REJECTED");
        return false;
    }

    char url[128];
    snprintf(url, sizeof(url), "%s%s", BACKEND_BASE_URL, GRAPH_TRIGGER_PATH);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = GRAPH_REQUEST_TIMEOUT_MS,
        .event_handler = http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed for url=%s", url);
        free(body);
        snprintf(result_out, result_out_len, "CONNECTION FAILED");
        return false;
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
        if (elapsed_ms >= GRAPH_REQUEST_TIMEOUT_MS - 200) {
            ESP_LOGW(TAG, "graph trigger timed out after %lldms: %s", (long long)elapsed_ms, esp_err_to_name(err));
            snprintf(result_out, result_out_len, "TIMEOUT");
        } else {
            ESP_LOGW(TAG, "graph trigger unreachable after %lldms: %s", (long long)elapsed_ms, esp_err_to_name(err));
            snprintf(result_out, result_out_len, "CONNECTION FAILED");
        }
        return false;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);

    /* {run_id, request_id, status, created_at, duplicate} on 202/200/409
     * alike - parsed best-effort for the log; a body that doesn't parse
     * (or is missing a field) just logs "?" rather than failing the whole
     * call, since the HTTP status code alone is enough to answer true/false. */
    char run_id[32] = "?";
    bool duplicate = false;
    cJSON *resp = cJSON_ParseWithLength(s_response_buf, (size_t)s_response_len);
    if (resp) {
        cJSON *run_id_item = cJSON_GetObjectItemCaseSensitive(resp, "run_id");
        if (cJSON_IsString(run_id_item) && run_id_item->valuestring) {
            strncpy(run_id, run_id_item->valuestring, sizeof(run_id) - 1);
            run_id[sizeof(run_id) - 1] = '\0';
        }
        cJSON *dup_item = cJSON_GetObjectItemCaseSensitive(resp, "duplicate");
        duplicate = cJSON_IsTrue(dup_item);
        cJSON_Delete(resp);
    }

    if (status == 202 || status == 200) {
        ESP_LOGI(TAG, "graph trigger accepted: id=%s run_id=%s status=%d duplicate=%d",
                 request_id, run_id, status, (int)duplicate);
        snprintf(result_out, result_out_len, "GRAPH ACCEPTED");
        return true;
    }
    if (status == 409) {
        ESP_LOGW(TAG, "graph trigger busy: id=%s status=409 (another run active)", request_id);
        snprintf(result_out, result_out_len, "RUN ACTIVE");
        return false;
    }

    ESP_LOGW(TAG, "graph trigger rejected, status=%d body=%s", status, s_response_buf);
    snprintf(result_out, result_out_len, "REQUEST REJECTED %d", status);
    return false;
}
