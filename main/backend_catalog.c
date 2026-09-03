/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Repository list fetched from the backend - see repo_selector.h.
 */

#include "repo_selector.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

#include "wifi_manager.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h is required - see backend_config.h.example"
#endif

static const char *TAG = "repo_selector";

/* Retry until it loads, then stop. This is configuration: it changes when
 * someone edits config/projects.json, not continuously, so re-polling it
 * forever would be noise on a link that a recording may be using. A reboot
 * picks up changes, and so does any future explicit refresh. */
#define FETCH_RETRY_INTERVAL_MS 10000
#define FETCH_TIMEOUT_MS 5000

/* The backend caps its own payload well under this (197 bytes for the
 * current catalog, asserted under 1KB for 16 entries). Sized with headroom
 * and hard-capped so a runaway response cannot exhaust the heap. */
#define RESPONSE_BUF_LEN 2048

typedef struct {
    char id[REPO_ID_LEN];
    char label[REPO_LABEL_LEN];
} repo_entry_t;

/* Written once by the fetch task, read by the LVGL task and the audio
 * uploader. s_count is published last and read first, so a reader either
 * sees the old count with consistent entries or the new count with fully
 * written ones - it never walks half-populated storage. */
static repo_entry_t s_repos[REPO_MAX_COUNT];
static volatile int s_count = 0;
static volatile int s_index = 0;
static volatile bool s_ready = false;

int repo_selector_count(void)
{
    return s_count;
}

bool repo_selector_ready(void)
{
    return s_ready;
}

const char *repo_selector_get_label(void)
{
    int count = s_count;
    if (count <= 0) {
        /* Two different states, two different words: the operator should be
         * able to tell "still fetching" from "the catalog is empty". */
        return s_ready ? "NONE" : "...";
    }
    int idx = s_index;
    if (idx < 0 || idx >= count) {
        idx = 0;
    }
    return s_repos[idx].label;
}

const char *repo_selector_get_id(void)
{
    int count = s_count;
    if (count <= 0) {
        return "";
    }
    int idx = s_index;
    if (idx < 0 || idx >= count) {
        idx = 0;
    }
    return s_repos[idx].id;
}

void repo_selector_next(void)
{
    int count = s_count;
    if (count > 0) {
        s_index = (s_index + 1) % count;
    }
}

void repo_selector_prev(void)
{
    int count = s_count;
    if (count > 0) {
        s_index = (s_index + count - 1) % count;
    }
}

/* esp_http_client's event handler, accumulating the body. Same pattern as
 * stream_upload.c's response_event_handler. */
typedef struct {
    char *buf;
    size_t len;
    size_t written;
} response_ctx_t;

static esp_err_t response_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data) {
        return ESP_OK;
    }
    response_ctx_t *ctx = evt->user_data;
    int room = (int)ctx->len - 1 - (int)ctx->written;
    if (room > 0) {
        int copy_len = evt->data_len < room ? evt->data_len : room;
        memcpy(ctx->buf + ctx->written, evt->data, (size_t)copy_len);
        ctx->written += (size_t)copy_len;
        ctx->buf[ctx->written] = '\0';
    }
    return ESP_OK;
}

/* Returns the number of entries stored. Entries that do not fit REPO_ID_LEN
 * or REPO_LABEL_LEN are skipped rather than truncated: a half-written id
 * would resolve to nothing on the backend, so silently sending one would
 * turn a config typo into a confusing runtime failure. */
static int parse_repos(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "catalog response was not valid JSON");
        return -1;
    }

    cJSON *repos = cJSON_GetObjectItemCaseSensitive(root, "repos");
    if (!cJSON_IsArray(repos)) {
        ESP_LOGW(TAG, "catalog response has no repos array");
        cJSON_Delete(root);
        return -1;
    }

    int stored = 0;
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, repos) {
        if (stored >= REPO_MAX_COUNT) {
            ESP_LOGW(TAG, "catalog has more than %d repos - ignoring the rest", REPO_MAX_COUNT);
            break;
        }
        cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id");
        cJSON *label = cJSON_GetObjectItemCaseSensitive(entry, "label");
        if (!cJSON_IsString(id) || !cJSON_IsString(label) || !id->valuestring || !label->valuestring) {
            continue;
        }
        if (strlen(id->valuestring) >= REPO_ID_LEN || strlen(label->valuestring) >= REPO_LABEL_LEN) {
            ESP_LOGW(TAG, "skipping oversized catalog entry: id=%s label=%s",
                     id->valuestring, label->valuestring);
            continue;
        }
        strncpy(s_repos[stored].id, id->valuestring, REPO_ID_LEN - 1);
        s_repos[stored].id[REPO_ID_LEN - 1] = '\0';
        strncpy(s_repos[stored].label, label->valuestring, REPO_LABEL_LEN - 1);
        s_repos[stored].label[REPO_LABEL_LEN - 1] = '\0';
        stored++;
    }

    cJSON_Delete(root);
    return stored;
}

static bool fetch_once(void)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s", BACKEND_BASE_URL, PROJECTS_PATH);

    char *body = malloc(RESPONSE_BUF_LEN);
    if (!body) {
        return false;
    }
    body[0] = '\0';
    response_ctx_t ctx = { .buf = body, .len = RESPONSE_BUF_LEN, .written = 0 };

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = FETCH_TIMEOUT_MS,
        .event_handler = response_event_handler,
        .user_data = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(body);
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);

    bool ok = false;
    if (err == ESP_OK && status == 200) {
        int stored = parse_repos(body);
        if (stored >= 0) {
            /* Count published last - see the note on the statics above. */
            s_index = 0;
            s_count = stored;
            s_ready = true;
            ok = true;
            ESP_LOGI(TAG, "catalog loaded: %d repo(s)", stored);
            for (int i = 0; i < stored; i++) {
                ESP_LOGI(TAG, "  %s (%s)", s_repos[i].label, s_repos[i].id);
            }
        }
    } else {
        ESP_LOGW(TAG, "catalog fetch failed: err=%s status=%d", esp_err_to_name(err), status);
    }

    free(body);
    return ok;
}

static void fetch_task(void *arg)
{
    (void)arg;
    for (;;) {
        wifi_mgr_status_t wst;
        wifi_mgr_get_status(&wst);
        if (wst.state == WIFI_MGR_ONLINE && fetch_once()) {
            break; /* configuration, not telemetry - fetch once and stop */
        }
        vTaskDelay(pdMS_TO_TICKS(FETCH_RETRY_INTERVAL_MS));
    }
    vTaskDelete(NULL);
}

void repo_selector_init(void)
{
    /* Priority 3, same as backend_health: this is a convenience fetch and
     * must never compete with capture or upload for the Wi-Fi path. */
    xTaskCreate(fetch_task, "repo_catalog", 4096, NULL, 3, NULL);
}
