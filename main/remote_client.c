/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Wireless Roku IR remote - see remote_client.h.
 * @details One FreeRTOS task polls GET {base}{REMOTE_BASE_PATH}/pending on a
 *          fast, fixed interval. Each pending command is a single keypress
 *          queued by the PC-side control script - look up its key name
 *          against ir_roku.h's roku_key_t names, fire it through
 *          roku_ir_send(), then POST .../{command_id}/ack to drain it from
 *          the backend's queue. Same "poll task owns the whole request"
 *          shape as notification_client.c's poll_once(), just on a much
 *          shorter interval since this drives live TV navigation instead of
 *          a background status light.
 */

#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ir_roku.h"
#include "remote_client.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h not found. Define BACKEND_BASE_URL there, e.g. #define BACKEND_BASE_URL \"http://192.168.1.31:8000\" - your development laptop's LAN IP, not 127.0.0.1."
#endif

static const char *TAG = "remote_client";

/* Fast enough that a keypress feels immediate (idf.py monitor's old serial
 * keymap was effectively instant); slow enough not to hammer the backend or
 * starve other tasks polling the same wifi link. */
#define REMOTE_POLL_INTERVAL_MS 150
#define REMOTE_REQUEST_TIMEOUT_MS 2000
#define JSON_RESPONSE_BUF_LEN 256
#define COMMAND_ID_LEN 40
#define KEY_NAME_LEN 24

/* Mirrors backend/app/api/remote_schemas.py's ALLOWED_REMOTE_KEYS exactly -
 * that list is the source of truth for which names exist; keep both in
 * sync by hand if either changes. */
static const struct {
    const char *name;
    roku_key_t key;
} s_key_map[] = {
    { "power",      ROKU_KEY_POWER },
    { "power_alt",  ROKU_KEY_POWER_ALT },
    { "home",       ROKU_KEY_HOME },
    { "back",       ROKU_KEY_BACK },
    { "up",         ROKU_KEY_UP },
    { "down",       ROKU_KEY_DOWN },
    { "left",       ROKU_KEY_LEFT },
    { "right",      ROKU_KEY_RIGHT },
    { "ok",         ROKU_KEY_OK },
    { "replay",     ROKU_KEY_REPLAY },
    { "star",       ROKU_KEY_STAR },
    { "rewind",     ROKU_KEY_REWIND },
    { "play_pause", ROKU_KEY_PLAY_PAUSE },
    { "forward",    ROKU_KEY_FORWARD },
    { "vol_up",     ROKU_KEY_VOL_UP },
    { "vol_down",   ROKU_KEY_VOL_DOWN },
    { "mute",       ROKU_KEY_MUTE },
    { "sleep",      ROKU_KEY_SLEEP },
};

/* Single static buffer - poll_once() runs entirely on one task and never
 * overlaps its own GET with its own POST, same reasoning notification_client.c
 * gives for its s_json_buf. */
static char s_json_buf[JSON_RESPONSE_BUF_LEN];
static int s_json_len;

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

static bool key_to_roku(const char *name, roku_key_t *out)
{
    for (size_t i = 0; i < sizeof(s_key_map) / sizeof(s_key_map[0]); i++) {
        if (strcmp(name, s_key_map[i].name) == 0) {
            *out = s_key_map[i].key;
            return true;
        }
    }
    return false;
}

static void ack_command(const char *command_id)
{
    char url[192];
    snprintf(url, sizeof(url), "%s%s/%s/ack", BACKEND_BASE_URL, REMOTE_BASE_PATH, command_id);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = REMOTE_REQUEST_TIMEOUT_MS,
        .event_handler = json_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGW(TAG, "ack: esp_http_client_init failed for url=%s", url);
        return;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, "", 0);

    s_json_len = 0;
    s_json_buf[0] = '\0';

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ack failed: %s", esp_err_to_name(err));
    } else {
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, "ack server error, status=%d", status);
        }
    }
    esp_http_client_cleanup(client);
}

static void poll_once(void)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s/pending", BACKEND_BASE_URL, REMOTE_BASE_PATH);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = REMOTE_REQUEST_TIMEOUT_MS,
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
        /* Backend unreachable is an expected, unremarkable state for a
         * background poll - same reasoning as notification_client.c's own
         * poll_once(). */
        ESP_LOGD(TAG, "pending poll unreachable: %s", esp_err_to_name(err));
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
    if (!cJSON_IsBool(pending_item) || !cJSON_IsTrue(pending_item)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "command_id");
    cJSON *key_item = cJSON_GetObjectItemCaseSensitive(root, "key");
    if (!cJSON_IsString(id_item) || !cJSON_IsString(key_item)) {
        ESP_LOGW(TAG, "pending poll missing command_id/key");
        cJSON_Delete(root);
        return;
    }

    char command_id[COMMAND_ID_LEN];
    char key_name[KEY_NAME_LEN];
    strncpy(command_id, id_item->valuestring, sizeof(command_id) - 1);
    command_id[sizeof(command_id) - 1] = '\0';
    strncpy(key_name, key_item->valuestring, sizeof(key_name) - 1);
    key_name[sizeof(key_name) - 1] = '\0';
    cJSON_Delete(root);

    roku_key_t roku_key;
    if (key_to_roku(key_name, &roku_key)) {
        esp_err_t send_err = roku_ir_send((uint8_t)roku_key);
        if (send_err == ESP_OK) {
            ESP_LOGI(TAG, "-> %-12s (%s)", roku_key_name((uint8_t)roku_key), key_name);
        } else {
            ESP_LOGW(TAG, "roku_ir_send(%s) failed: %s", key_name, esp_err_to_name(send_err));
        }
    } else {
        ESP_LOGW(TAG, "unknown remote key: %s", key_name);
    }

    /* Ack regardless of whether the key was recognized - an unknown key
     * would otherwise wedge the queue on every future poll. */
    ack_command(command_id);
}

static void poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(REMOTE_POLL_INTERVAL_MS));
    }
}

void remote_client_init(void)
{
    esp_err_t err = roku_ir_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "roku_ir_init failed: %s - remote control unavailable this boot", esp_err_to_name(err));
        return;
    }

    if (roku_ir_selftest()) {
        ESP_LOGI(TAG, "IR emitter self-test PASSED");
    } else {
        ESP_LOGW(TAG, "IR emitter self-test did NOT confirm - reflections vary, but if remote");
        ESP_LOGW(TAG, "keys also do nothing, suspect the IR rail before the TV.");
    }

    xTaskCreate(poll_task, "remote_poll", 4096, NULL, 4, NULL);
}
