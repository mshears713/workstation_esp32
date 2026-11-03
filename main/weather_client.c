/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief HOME's weather poller - see weather_client.h.
 */

#include "weather_client.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

#include "wifi_manager.h"
#include "ota_service.h"
#include "backend_config.h"

static const char *TAG = "weather";

/* The backend caches Open-Meteo for ten minutes, so polling faster would only
 * re-read the same answer. A failed fetch retries after a minute. */
#define POLL_INTERVAL_MS (10 * 60 * 1000)
#define RETRY_INTERVAL_MS (60 * 1000)
#define FETCH_TIMEOUT_MS 5000
/* The answer is ~450 bytes. Explicitly PSRAM: anything under 4096 would
 * otherwise land in internal RAM (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL). */
#define RESPONSE_BUF_LEN 1024
/* PSRAM stack (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY). This task never
 * touches flash - no NVS writes - which is the one thing a PSRAM-stack task
 * must not do. */
#define TASK_STACK 6144
/* If the updater never started (ESP_ERR_NO_MEM at boot, by design non-fatal)
 * its check count stays 0; do not wait for it forever. */
#define WEATHER_FIRST_FETCH_MAX_S 180

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static weather_status_t s_status;
static int64_t s_fetched_us;
static TaskHandle_t s_task;
static bool s_manual_pending;
static int64_t s_last_manual_us;

void weather_client_get(weather_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    int64_t fetched = s_fetched_us;
    portEXIT_CRITICAL(&s_mux);
    if (out->valid) {
        out->age_s += (uint32_t)((esp_timer_get_time() - fetched) / 1000000);
    }
}

/* ---- Parsing -------------------------------------------------------- */

static void copy_str(char *dst, size_t len, const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring) {
        strlcpy(dst, v->valuestring, len);
    } else {
        dst[0] = '\0';
    }
}

static int get_int(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valueint : 0;
}

/* All-or-nothing: the snapshot is only replaced by a complete answer, so a
 * shape change on the backend shows as stale data, never as half of one. */
static bool parse(const char *body, weather_status_t *w, bool manual)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return false;
    }
    if (manual && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "fresh"))) {
        cJSON_Delete(root);
        return false;
    }
    bool ok = false;
    const cJSON *now = cJSON_GetObjectItemCaseSensitive(root, "now");
    const cJSON *slots = cJSON_GetObjectItemCaseSensitive(root, "slots");
    const cJSON *tmrw = cJSON_GetObjectItemCaseSensitive(root, "tomorrow");
    if (cJSON_IsObject(now) && cJSON_IsArray(slots) && cJSON_GetArraySize(slots) >= WEATHER_SLOTS &&
        cJSON_IsObject(tmrw)) {
        memset(w, 0, sizeof(*w));
        copy_str(w->location, sizeof(w->location), root, "location");
        copy_str(w->as_of, sizeof(w->as_of), root, "as_of");
        w->age_s = (uint32_t)get_int(root, "age_s");
        w->now_temp = get_int(now, "temp");
        copy_str(w->now_icon, sizeof(w->now_icon), now, "icon");
        copy_str(w->now_text, sizeof(w->now_text), now, "text");
        w->now_rain = get_int(now, "rain");
        for (int i = 0; i < WEATHER_SLOTS; i++) {
            const cJSON *s = cJSON_GetArrayItem(slots, i);
            copy_str(w->slot[i].label, sizeof(w->slot[i].label), s, "label");
            w->slot[i].temp = get_int(s, "temp");
            copy_str(w->slot[i].icon, sizeof(w->slot[i].icon), s, "icon");
            w->slot[i].rain = get_int(s, "rain");
        }
        w->tmrw_hi = get_int(tmrw, "hi");
        w->tmrw_lo = get_int(tmrw, "lo");
        copy_str(w->tmrw_icon, sizeof(w->tmrw_icon), tmrw, "icon");
        w->tmrw_rain = get_int(tmrw, "rain");
        w->valid = true;
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

/* ---- Fetch ---------------------------------------------------------- */

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

static bool fetch_once(char *body, bool manual)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s%s", BACKEND_BASE_URL, WEATHER_PATH, manual ? "/refresh" : "");
    body[0] = '\0';
    response_ctx_t ctx = { .buf = body, .len = RESPONSE_BUF_LEN, .written = 0 };

    esp_http_client_config_t config = {
        .url = url,
        .method = manual ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = FETCH_TIMEOUT_MS,
        .event_handler = response_event_handler,
        .user_data = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "fetch failed: err=%s status=%d", esp_err_to_name(err), status);
        return false;
    }
    weather_status_t w;
    if (!parse(body, &w, manual)) {
        ESP_LOGW(TAG, "answer did not parse (%u bytes)", (unsigned)ctx.written);
        return false;
    }
    portENTER_CRITICAL(&s_mux);
    w.seq = s_status.seq + 1;
    w.manual_success = s_status.manual_success + (manual ? 1 : 0);
    w.manual_failure = s_status.manual_failure;
    s_status = w;
    s_fetched_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "%s %dF %s, %s %dF, %s %dF, tmrw %d/%dF (backend age %lus)",
             w.location, w.now_temp, w.now_text, w.slot[0].label, w.slot[0].temp,
             w.slot[1].label, w.slot[1].temp, w.tmrw_hi, w.tmrw_lo, (unsigned long)w.age_s);
    return true;
}

/* True once the updater has finished one manifest check (it latches): an
 * update can always land before this task does any backend work. */
static bool first_ota_check_done(void)
{
    ota_status_t o;
    ota_service_get_status(&o);
    if (o.checks > 0 && (o.state == OTA_STATE_IDLE || o.state == OTA_STATE_FAILED)) {
        return true;
    }
    return esp_timer_get_time() >= (int64_t)WEATHER_FIRST_FETCH_MAX_S * 1000000;
}

weather_refresh_request_t weather_client_request_refresh(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    weather_refresh_request_t result;
    if (!s_task) result=WEATHER_REFRESH_UNAVAILABLE;
    else if (s_manual_pending) result=WEATHER_REFRESH_PENDING;
    else if (s_last_manual_us && now-s_last_manual_us < 30000000) result=WEATHER_REFRESH_COOLDOWN;
    else result=WEATHER_REFRESH_ACCEPTED;
    TaskHandle_t task = s_task;
    if (result==WEATHER_REFRESH_ACCEPTED) { s_manual_pending=true; s_last_manual_us=now; }
    portEXIT_CRITICAL(&s_mux);
    if (result==WEATHER_REFRESH_ACCEPTED) xTaskNotifyGive(task);
    return result;
}

static void poll_task(void *arg)
{
    char *body = arg;
    while (!first_ota_check_done()) vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "first OTA check done, starting weather polls");
    bool manual=false;
    for (;;) {
        wifi_mgr_status_t wst; wifi_mgr_get_status(&wst);
        bool ok = wst.state == WIFI_MGR_ONLINE && fetch_once(body, manual);
        if (manual) {
            portENTER_CRITICAL(&s_mux);
            if (!ok) s_status.manual_failure++;
            s_manual_pending=false;
            portEXIT_CRITICAL(&s_mux);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ok ? POLL_INTERVAL_MS : RETRY_INTERVAL_MS));
        portENTER_CRITICAL(&s_mux);
        manual=s_manual_pending;
        portEXIT_CRITICAL(&s_mux);
    }
}

void weather_client_init(void)
{
    char *body = heap_caps_malloc(RESPONSE_BUF_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        ESP_LOGE(TAG, "no PSRAM for the response buffer - weather off this boot");
        return;
    }
    /* Priority 3, same as backend_catalog: a convenience fetch that must never
     * compete with capture or upload for the Wi-Fi path. */
    if (xTaskCreateWithCaps(poll_task, "weather", TASK_STACK, body, 3, &s_task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "task create failed - weather off this boot");
        heap_caps_free(body);
    }
}
