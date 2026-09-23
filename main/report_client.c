/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * See report_client.h. Shaped like handshake_client.c - one persistent
 * worker, one request in flight, a status snapshot the UI polls - because
 * that pattern is already proven on this device and the UI already knows
 * how to live with it.
 */
#include "report_client.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "backend_config.h"
#include "backend_health.h"
#include "firmware_identity.h"
#include "ota_service.h"
#include "wifi_manager.h"

static const char *TAG = "report";

/* Generous: the backend writes one small file and answers. A report is sent
 * at the moment the link has just come back, or by hand while something
 * looks wrong, so this is allowed to be patient. */
#define REPORT_TIMEOUT_MS 8000

/* ~32 events x ~90 bytes + ~1.5KB of the rest. 12KB leaves room for every
 * message being escaped at worst case. PSRAM, so the size costs nothing
 * that matters. */
#define REPORT_BODY_MAX (12 * 1024)
#define REPORT_RESPONSE_MAX 192

/* Internal RAM, not PSRAM: the worker reads the OTA state from flash
 * (firmware_is_pending_verify), which a task on a PSRAM stack must not do.
 * Plain HTTP, no TLS, and the body is built with snprintf into PSRAM, so
 * this is the same budget backend_health's GET task runs on plus margin. */
#define REPORT_TASK_STACK 5120

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static report_status_t s_status = { .state = REPORT_IDLE };
static report_request_t *s_req;   /* PSRAM */
static char *s_body;              /* PSRAM */
static char *s_resp;              /* PSRAM */
static int s_resp_len;
static TaskHandle_t s_task;

/* ---- tiny JSON writer into s_body -------------------------------------- */

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} jbuf_t;

static void jb_raw(jbuf_t *j, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void jb_raw(jbuf_t *j, const char *fmt, ...)
{
    if (j->overflow) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(j->buf + j->len, j->cap - j->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= j->cap - j->len) {
        j->overflow = true;
        return;
    }
    j->len += (size_t)n;
}

static void jb_putc(jbuf_t *j, char c)
{
    if (j->overflow || j->len + 1 >= j->cap) {
        j->overflow = true;
        return;
    }
    j->buf[j->len++] = c;
    j->buf[j->len] = '\0';
}

/* A JSON string literal, quotes included. Escapes what JSON requires;
 * everything the device logs is ASCII, so no UTF-8 handling is needed. */
static void jb_str(jbuf_t *j, const char *s)
{
    jb_putc(j, '"');
    for (const char *p = s ? s : ""; *p && !j->overflow; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            jb_putc(j, '\\');
            jb_putc(j, (char)c);
        } else if (c < 0x20) {
            jb_raw(j, "\\u%04x", c);
        } else {
            jb_putc(j, (char)c);
        }
    }
    jb_putc(j, '"');
}

static void jb_key_str(jbuf_t *j, const char *key, const char *val)
{
    jb_raw(j, "\"%s\":", key);
    jb_str(j, val);
    jb_raw(j, ",");
}

static void jb_key_u32(jbuf_t *j, const char *key, uint32_t val)
{
    jb_raw(j, "\"%s\":%" PRIu32 ",", key, val);
}

static void jb_key_bool(jbuf_t *j, const char *key, bool val)
{
    jb_raw(j, "\"%s\":%s,", key, val ? "true" : "false");
}

/* Replaces a trailing comma before a closing bracket - the writers above
 * always leave one. */
static void jb_close(jbuf_t *j, char closer)
{
    if (j->overflow) {
        return;
    }
    if (j->len > 0 && j->buf[j->len - 1] == ',') {
        j->len--;
    }
    jb_raw(j, "%c,", closer);
}

static const char *wifi_state_word(wifi_mgr_state_t s)
{
    switch (s) {
    case WIFI_MGR_ONLINE:      return "online";
    case WIFI_MGR_CONNECTING:  return "connecting";
    case WIFI_MGR_RETRY_WAIT:  return "retry-wait";
    case WIFI_MGR_DISCONNECTED:
    default:                   return "disconnected";
    }
}

static bool build_body(const report_request_t *r, jbuf_t *j)
{
    uint8_t mac[6] = { 0 };
    esp_efuse_mac_get_default(mac);
    char device_id[24];
    snprintf(device_id, sizeof(device_id), "esp32-%02x%02x%02x", mac[3], mac[4], mac[5]);

    ota_status_t ota;
    ota_service_get_status(&ota);
    backend_health_status_t bh;
    backend_health_get_status(&bh);
    backend_health_stats_t bs;
    backend_health_get_stats(&bs);
    wifi_mgr_status_t w;
    wifi_mgr_get_status(&w);

    int rssi = 0;
    bool have_rssi = false;
    if (w.state == WIFI_MGR_ONLINE) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            rssi = ap.rssi;
            have_rssi = true;
        }
    }

    jb_raw(j, "{");
    jb_key_str(j, "trigger", r->trigger);
    jb_key_str(j, "device_id", device_id);
    jb_key_u32(j, "uptime_s", (uint32_t)(esp_timer_get_time() / 1000000));
    jb_key_u32(j, "boot_count", r->boot_count);
    jb_key_str(j, "reset_reason", r->reset_reason);
    jb_key_str(j, "prev_reset_reason", r->prev_reset_reason);
    jb_key_bool(j, "prev_was_self_restart", r->prev_was_self_restart);
    jb_key_str(j, "last_fault", r->last_fault);
    jb_key_str(j, "last_cmd", r->last_cmd);

    jb_raw(j, "\"firmware\":{");
    jb_key_str(j, "version", firmware_version());
    jb_key_str(j, "slot", firmware_running_partition_label());
    jb_key_bool(j, "pending_verify", firmware_is_pending_verify());
    jb_key_str(j, "ota_state", ota_service_state_label(ota.state));
    jb_key_str(j, "ota_message", ota.message);
    jb_key_str(j, "ota_desired", ota.desired_version);
    jb_key_u32(j, "ota_checks", ota.checks);
    jb_key_u32(j, "ota_failures", ota.failures);
    jb_key_str(j, "ota_primary_url", device_config_ota_base_url());
    jb_close(j, '}');

    jb_raw(j, "\"backend\":{");
    jb_key_str(j, "url", BACKEND_BASE_URL);
    jb_key_bool(j, "on_fallback", device_config_backend_on_fallback());
    jb_key_str(j, "other_url", device_config_backend_other_url());
    jb_key_bool(j, "url_from_nvs", device_config_backend_from_nvs());
    jb_key_str(j, "state", backend_health_label(&bh));
    jb_key_u32(j, "latency_ms", bh.latency_ms);
    jb_key_u32(j, "consecutive_failures", bh.consecutive_failures);
    jb_key_u32(j, "failures_since_boot", bs.probe_failures);
    jb_key_u32(j, "outages_since_boot", bs.outages);
    jb_key_u32(j, "longest_outage_s", bs.longest_outage_s);
    jb_key_u32(j, "current_outage_s", bs.current_outage_s);
    jb_key_u32(j, "secs_since_last_ok", bs.secs_since_ok);
    jb_key_u32(j, "url_switches", bs.url_switches);
    jb_key_bool(j, "ever_ok", bs.ever_ok);
    jb_close(j, '}');

    jb_raw(j, "\"memory\":{");
    jb_key_u32(j, "internal_free", (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    jb_key_u32(j, "internal_largest", (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    jb_key_u32(j, "internal_min_free", (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    jb_key_u32(j, "psram_free", (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    jb_close(j, '}');

    jb_raw(j, "\"wifi\":{");
    jb_key_str(j, "state", wifi_state_word(w.state));
    jb_key_str(j, "ip", w.ip_addr);
    if (have_rssi) {
        jb_raw(j, "\"rssi\":%d,", rssi);
    } else {
        jb_raw(j, "\"rssi\":null,");
    }
    jb_key_u32(j, "connect_attempts", w.attempt_count);
    jb_close(j, '}');

    jb_raw(j, "\"events\":[");
    for (int i = 0; i < r->event_count && i < REPORT_MAX_EVENTS; i++) {
        jb_raw(j, "{");
        jb_key_u32(j, "t", r->events[i].uptime_s);
        jb_key_str(j, "type", r->events[i].type);
        jb_key_str(j, "msg", r->events[i].message);
        jb_close(j, '}');
    }
    jb_close(j, ']');
    jb_close(j, '}');
    if (!j->overflow && j->len > 0 && j->buf[j->len - 1] == ',') {
        j->len--;             /* the outermost object's own trailing comma */
        j->buf[j->len] = '\0';
    }
    return !j->overflow;
}

/* ---- HTTP -------------------------------------------------------------- */

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && s_resp != NULL) {
        int room = REPORT_RESPONSE_MAX - 1 - s_resp_len;
        int n = evt->data_len < room ? evt->data_len : room;
        if (n > 0) {
            memcpy(s_resp + s_resp_len, evt->data, (size_t)n);
            s_resp_len += n;
            s_resp[s_resp_len] = '\0';
        }
    }
    return ESP_OK;
}

static void settle(report_state_t state, const char *id, const char *message)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = state;
    s_status.seq++;
    if (id) {
        strlcpy(s_status.id, id, sizeof(s_status.id));
    }
    strlcpy(s_status.message, message, sizeof(s_status.message));
    portEXIT_CRITICAL(&s_mux);
}

static void send_report(void)
{
    jbuf_t j = { .buf = s_body, .cap = REPORT_BODY_MAX, .len = 0, .overflow = false };
    s_body[0] = '\0';
    if (!build_body(s_req, &j)) {
        ESP_LOGE(TAG, "report body overflowed %d bytes", REPORT_BODY_MAX);
        settle(REPORT_FAILED, NULL, "REPORT FAIL: TOO BIG");
        return;
    }

    char url[DEVICE_CONFIG_URL_MAX + 32];
    snprintf(url, sizeof(url), "%s%s", BACKEND_BASE_URL, REPORT_PATH);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = REPORT_TIMEOUT_MS,
        .event_handler = http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        settle(REPORT_FAILED, NULL, "REPORT FAIL: CLIENT");
        return;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, s_body, (int)j.len);

    s_resp_len = 0;
    s_resp[0] = '\0';
    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "POST %s failed: %s", url, esp_err_to_name(err));
        backend_health_report(false);
        /* "ESP_ERR_HTTP_CONNECT" -> "HTTP_CONNECT": the part that fits. */
        const char *name = esp_err_to_name(err);
        if (strncmp(name, "ESP_ERR_", 8) == 0) {
            name += 8;
        }
        char msg[32];
        snprintf(msg, sizeof(msg), "REPORT FAIL: %.18s", name);
        settle(REPORT_FAILED, NULL, msg);
        return;
    }
    backend_health_report(true);
    if (status != 200 && status != 201) {
        ESP_LOGW(TAG, "POST %s -> HTTP %d: %s", url, status, s_resp);
        char msg[32];
        snprintf(msg, sizeof(msg), "REPORT FAIL: HTTP %d", status);
        settle(REPORT_FAILED, NULL, msg);
        return;
    }

    char id[24] = "?";
    cJSON *root = cJSON_ParseWithLength(s_resp, (size_t)s_resp_len);
    if (root != NULL) {
        const cJSON *jid = cJSON_GetObjectItemCaseSensitive(root, "id");
        if (cJSON_IsString(jid) && jid->valuestring != NULL) {
            strlcpy(id, jid->valuestring, sizeof(id));
        } else if (cJSON_IsNumber(jid)) {
            snprintf(id, sizeof(id), "%ld", (long)jid->valuedouble);
        }
        cJSON_Delete(root);
    }
    ESP_LOGI(TAG, "report (%s) stored as #%s, %u bytes", s_req->trigger, id, (unsigned)j.len);
    char msg[32];
    snprintf(msg, sizeof(msg), "REPORT SENT #%.18s", id);
    settle(REPORT_SENT, id, msg);
}

static void worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        send_report();
    }
}

void report_client_init(void)
{
    if (s_task != NULL) {
        return;
    }
    s_req  = heap_caps_calloc(1, sizeof(report_request_t), MALLOC_CAP_SPIRAM);
    s_body = heap_caps_malloc(REPORT_BODY_MAX, MALLOC_CAP_SPIRAM);
    s_resp = heap_caps_malloc(REPORT_RESPONSE_MAX, MALLOC_CAP_SPIRAM);
    if (s_req == NULL || s_body == NULL || s_resp == NULL) {
        ESP_LOGE(TAG, "no PSRAM for report buffers - reports unavailable this boot");
        return;
    }
    /* Priority 3, same as backend_health: reporting must never compete with
     * audio capture or upload (5) for the link. */
    if (xTaskCreate(worker_task, "report", REPORT_TASK_STACK, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "could not start the report task - reports unavailable this boot");
    }
}

report_request_t *report_client_begin(void)
{
    if (s_task == NULL) {
        return NULL;
    }
    bool busy;
    portENTER_CRITICAL(&s_mux);
    busy = (s_status.state == REPORT_SENDING);
    if (!busy) {
        s_status.state = REPORT_SENDING;
    }
    portEXIT_CRITICAL(&s_mux);
    if (busy) {
        return NULL;
    }
    memset(s_req, 0, sizeof(*s_req));
    return s_req;
}

void report_client_commit(void)
{
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

void report_client_get_status(report_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    portEXIT_CRITICAL(&s_mux);
}
