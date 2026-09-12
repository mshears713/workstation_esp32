/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "ota_service.h"
#include "device_config.h"
#include "firmware_identity.h"
#include "wifi_manager.h"
#include "backend_health.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_app_desc.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"

static const char *TAG = "ota";

/* ---- tuning -------------------------------------------------------------
 * All of these are "boring on purpose" numbers. None is load-bearing; they
 * exist so the device is not surprising rather than because a measurement
 * demanded them. */

/* How often the device asks CLAWBOX what it should be running. A minute is
 * slow enough to be invisible (one small HTTPS GET) and quick enough that a
 * deploy feels immediate rather than scheduled. */
#define OTA_POLL_INTERVAL_MS      60000

/* Longest the health gate will wait for a freshly installed image to prove
 * itself before rolling back. Has to comfortably cover a cold Wi-Fi
 * association plus one backend poll interval; 90s is roughly triple the
 * worst case observed on this board. */
#define OTA_HEALTH_TIMEOUT_MS     90000
#define OTA_HEALTH_POLL_MS        500

/* Read buffer for the image download. 4KB matches the flash sector size
 * esp_ota_write() deals in, and costs little internal RAM - which is the
 * scarce resource on this board (see sdkconfig.defaults). */
#define OTA_RECV_BUF_SZ           4096

#define OTA_HTTP_TIMEOUT_MS       15000
#define OTA_MANIFEST_MAX_BYTES    2048

/* CLAWBOX's certificate, compiled in. See main/CMakeLists.txt EMBED_TXTFILES
 * and doc/OTA.md for how it is generated and what to do when it is replaced. */
extern const uint8_t clawbox_ota_ca_pem_start[] asm("_binary_clawbox_ota_ca_pem_start");

/* ---- module state ------------------------------------------------------- */

typedef struct {
    char     version[40];
    char     sha256[65];   /* lowercase hex, NUL-terminated */
    char     url[192];     /* absolute, or path relative to the OTA base URL */
    uint32_t size;
} ota_manifest_t;

static ota_status_t       s_status;
static SemaphoreHandle_t  s_lock;
static TaskHandle_t       s_task;
static volatile bool      s_check_requested;
static volatile bool      s_update_requested;

/* The status mutex does not exist until ota_service_start() runs, and the UI
 * is built before that - status_deck_ui() draws the FIRMWARE page, which
 * reads this status, during app_main. Taking a NULL semaphore is an assert
 * failure and a boot loop, so every use goes through these two. */
static bool lock_take(void)
{
    return s_lock != NULL && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void lock_give(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static void status_note_check(void)
{
    if (lock_take()) { s_status.checks++; lock_give(); }
}

static void status_note_failure(void)
{
    if (lock_take()) { s_status.failures++; lock_give(); }
}

static void status_set_desired(const char *version)
{
    if (lock_take()) {
        strlcpy(s_status.desired_version, version, sizeof(s_status.desired_version));
        lock_give();
    }
}

static void status_clear_rollback(void)
{
    if (lock_take()) { s_status.rolled_back = false; lock_give(); }
}

static void status_set(ota_state_t state, const char *fmt, ...)
{
    va_list ap;
    bool locked = lock_take();
    s_status.state = state;
    if (fmt != NULL) {
        va_start(ap, fmt);
        vsnprintf(s_status.message, sizeof(s_status.message), fmt, ap);
        va_end(ap);
    }
    if (locked) {
        lock_give();
    }
    if (fmt != NULL) {
        ESP_LOGI(TAG, "[%s] %s", ota_service_state_label(state), s_status.message);
    }
}

static void status_set_progress(int pct)
{
    if (!lock_take()) {
        return;
    }
    s_status.progress_pct = pct;
    lock_give();
}

void ota_service_get_status(ota_status_t *out)
{
    if (out == NULL) {
        return;
    }
    if (!lock_take()) {
        /* Called before the OTA task exists - which the UI does, on the way
         * up. Report the honest answer rather than asserting. */
        memset(out, 0, sizeof(*out));
        out->state = OTA_STATE_IDLE;
        strlcpy(out->message, "not started", sizeof(out->message));
        return;
    }
    *out = s_status;
    lock_give();
}

const char *ota_service_state_label(ota_state_t state)
{
    switch (state) {
    case OTA_STATE_IDLE:         return "idle";
    case OTA_STATE_VALIDATING:   return "validating";
    case OTA_STATE_CHECKING:     return "checking";
    case OTA_STATE_UPDATE_READY: return "update ready";
    case OTA_STATE_DOWNLOADING:  return "downloading";
    case OTA_STATE_VERIFYING:    return "verifying";
    case OTA_STATE_REBOOTING:    return "rebooting";
    case OTA_STATE_FAILED:       return "failed";
    default:                     return "?";
    }
}

void ota_service_request_check(void)
{
    s_check_requested = true;
}

void ota_service_request_update(void)
{
    s_update_requested = true;
}

/* ---- the rollback health gate -------------------------------------------
 *
 * What counts as "this firmware works well enough to keep". The list is
 * short on purpose: every item is something that, if broken, makes the
 * device useless in a way the previous image was not, AND can be observed
 * from here without inventing a test framework.
 *
 *   - NVS opened                : device_config_is_ready()
 *   - the UI came up            : passed in by app_main after the display
 *                                 and LVGL are running
 *   - Wi-Fi associated          : wifi_mgr_get_status() == ONLINE
 *   - the backend answered once : backend_health_get_status() == OK
 *
 * The last two are what a bad image usually breaks (a linker change that
 * starves internal RAM so esp_wifi_start() fails is exactly the failure
 * this project has hit before). A crash loop needs no gate at all - the
 * bootloader rolls back on its own when the image never marks itself valid.
 */

static volatile bool s_ui_ready;

void ota_service_report_ui_ready(void)
{
    s_ui_ready = true;
}

static bool health_gate_pass(char *why, size_t why_sz)
{
    if (!device_config_is_ready()) {
        snprintf(why, why_sz, "config not loaded");
        return false;
    }
    if (!s_ui_ready) {
        snprintf(why, why_sz, "UI not up");
        return false;
    }
    wifi_mgr_status_t w;
    wifi_mgr_get_status(&w);
    if (w.state != WIFI_MGR_ONLINE) {
        snprintf(why, why_sz, "wifi not online");
        return false;
    }
    backend_health_status_t h;
    backend_health_get_status(&h);
    if (h.state != BACKEND_HEALTH_OK) {
        snprintf(why, why_sz, "backend not answering");
        return false;
    }
    snprintf(why, why_sz, "ok");
    return true;
}

static void run_health_gate(void)
{
    char why[48] = "";
    status_set(OTA_STATE_VALIDATING, "new image on probation, validating");

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(OTA_HEALTH_TIMEOUT_MS);
    while (xTaskGetTickCount() < deadline) {
        if (health_gate_pass(why, sizeof(why))) {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                status_set(OTA_STATE_IDLE, "image marked valid");
            } else {
                /* Could not cancel the rollback. Say so loudly: the device
                 * will revert on the next reset and somebody should know
                 * why rather than discovering it later. */
                status_set(OTA_STATE_FAILED, "mark-valid failed: %s", esp_err_to_name(err));
            }
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(OTA_HEALTH_POLL_MS));
    }

    ESP_LOGE(TAG, "health gate timed out after %dms (%s) - rolling back",
             OTA_HEALTH_TIMEOUT_MS, why);
    status_set(OTA_STATE_FAILED, "unhealthy (%s), rolling back", why);
    /* Marks this slot invalid and reboots into the previous one. Does not
     * return on success. */
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

/* ---- HTTP helpers -------------------------------------------------------- */

/* Percent-encodes the one character our version strings contain that a
 * query string would otherwise misread. Not a general URL encoder, and not
 * pretending to be one. */
static void escape_plus(const char *src, char *dst, size_t dst_sz)
{
    size_t o = 0;
    for (size_t i = 0; src[i] != '\0' && o + 4 < dst_sz; i++) {
        if (src[i] == '+') {
            dst[o++] = '%'; dst[o++] = '2'; dst[o++] = 'B';
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = '\0';
}

static void build_url(char *dst, size_t dst_sz, const char *maybe_relative)
{
    if (strncmp(maybe_relative, "http://", 7) == 0 ||
        strncmp(maybe_relative, "https://", 8) == 0) {
        strlcpy(dst, maybe_relative, dst_sz);
    } else {
        /* Manifests publish a path, not a host, so the same artifact stays
         * correct if CLAWBOX's address changes. */
        snprintf(dst, dst_sz, "%s%s%s",
                 device_config_ota_base_url(),
                 maybe_relative[0] == '/' ? "" : "/",
                 maybe_relative);
    }
}

static esp_http_client_handle_t open_get(const char *url, int *content_length_out)
{
    esp_http_client_config_t cfg = {
        .url             = url,
        .cert_pem        = (const char *)clawbox_ota_ca_pem_start,
        .timeout_ms      = OTA_HTTP_TIMEOUT_MS,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return NULL;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open %s: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return NULL;
    }
    int len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "GET %s -> HTTP %d", url, status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return NULL;
    }
    if (content_length_out) {
        *content_length_out = len;
    }
    return client;
}

static void close_client(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

/* ---- manifest ------------------------------------------------------------ */

static bool fetch_manifest(ota_manifest_t *out)
{
    /* The device reports its own state in the query string of the request
     * it was going to make anyway. CLAWBOX has no other way to ask - the
     * device runs no server - and nginx logs query strings, so its access
     * log doubles as the check-in record `workstation status` reads. The
     * "+" in a version has to be escaped or it decodes as a space. */
    char ver[64];
    escape_plus(firmware_version(), ver, sizeof(ver));
    char path[200];
    snprintf(path, sizeof(path),
             "/api/v1/firmware/desired?current=%s&slot=%s&state=%s",
             ver, firmware_running_partition_label(),
             firmware_is_pending_verify() ? "pending-verify" : "valid");

    char url[288];
    build_url(url, sizeof(url), path);

    int len = 0;
    esp_http_client_handle_t client = open_get(url, &len);
    if (client == NULL) {
        return false;
    }

    char body[OTA_MANIFEST_MAX_BYTES];
    int got = esp_http_client_read_response(client, body, sizeof(body) - 1);
    close_client(client);
    if (got <= 0) {
        ESP_LOGE(TAG, "manifest: empty response");
        return false;
    }
    body[got] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        ESP_LOGE(TAG, "manifest: not JSON");
        return false;
    }

    bool ok = false;
    const cJSON *jv   = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *jh   = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    const cJSON *ju   = cJSON_GetObjectItemCaseSensitive(root, "url");
    const cJSON *js   = cJSON_GetObjectItemCaseSensitive(root, "size");

    if (cJSON_IsString(jv) && cJSON_IsString(jh) && cJSON_IsString(ju) && cJSON_IsNumber(js) &&
        strlen(jh->valuestring) == 64 && js->valuedouble > 0) {
        memset(out, 0, sizeof(*out));
        strlcpy(out->version, jv->valuestring, sizeof(out->version));
        strlcpy(out->sha256,  jh->valuestring, sizeof(out->sha256));
        strlcpy(out->url,     ju->valuestring, sizeof(out->url));
        out->size = (uint32_t)js->valuedouble;
        ok = true;
    } else {
        ESP_LOGE(TAG, "manifest: missing or malformed fields");
    }
    cJSON_Delete(root);
    return ok;
}

/* ---- download + verify + switch ------------------------------------------ */

static void hex_of(const uint8_t *digest, char *dst /* >=65 */)
{
    static const char *h = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        dst[i * 2]     = h[digest[i] >> 4];
        dst[i * 2 + 1] = h[digest[i] & 0x0f];
    }
    dst[64] = '\0';
}

static bool install_manifest(const ota_manifest_t *m)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        status_set(OTA_STATE_FAILED, "no inactive OTA slot");
        return false;
    }
    if (m->size > target->size) {
        status_set(OTA_STATE_FAILED, "image %lu > slot %lu",
                   (unsigned long)m->size, (unsigned long)target->size);
        return false;
    }

    char url[224];
    build_url(url, sizeof(url), m->url);
    status_set(OTA_STATE_DOWNLOADING, "%s -> %s", m->version, target->label);
    status_set_progress(0);

    int content_length = 0;
    esp_http_client_handle_t client = open_get(url, &content_length);
    if (client == NULL) {
        status_set(OTA_STATE_FAILED, "cannot fetch image");
        return false;
    }
    if (content_length > 0 && (uint32_t)content_length != m->size) {
        /* The manifest and the artifact disagree. Refuse rather than guess:
         * one of the two is stale and installing either is a coin flip. */
        status_set(OTA_STATE_FAILED, "size mismatch: manifest %lu, server %d",
                   (unsigned long)m->size, content_length);
        close_client(client);
        return false;
    }

    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(target, m->size, &ota);
    if (err != ESP_OK) {
        status_set(OTA_STATE_FAILED, "esp_ota_begin: %s", esp_err_to_name(err));
        close_client(client);
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0 /* SHA-256, not SHA-224 */);

    char *buf = malloc(OTA_RECV_BUF_SZ);
    if (buf == NULL) {
        status_set(OTA_STATE_FAILED, "out of memory");
        mbedtls_sha256_free(&sha);
        esp_ota_abort(ota);
        close_client(client);
        return false;
    }

    uint32_t written = 0;
    bool transport_ok = true;
    int last_pct = -1;

    while (written < m->size) {
        int n = esp_http_client_read(client, buf, OTA_RECV_BUF_SZ);
        if (n < 0) {
            ESP_LOGE(TAG, "read error after %lu bytes", (unsigned long)written);
            transport_ok = false;
            break;
        }
        if (n == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            continue;
        }
        mbedtls_sha256_update(&sha, (const unsigned char *)buf, n);
        err = esp_ota_write(ota, buf, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
            transport_ok = false;
            break;
        }
        written += n;

        int pct = (int)((uint64_t)written * 100 / m->size);
        if (pct != last_pct) {
            status_set_progress(pct);
            last_pct = pct;
            /* The UI reads progress from the status snapshot; yielding here
             * keeps this task from monopolising its core during a ~2.5MB
             * download so the display and touch stay responsive. */
            taskYIELD();
        }
    }
    free(buf);
    close_client(client);

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (!transport_ok || written != m->size) {
        status_set(OTA_STATE_FAILED, "download incomplete (%lu/%lu)",
                   (unsigned long)written, (unsigned long)m->size);
        esp_ota_abort(ota);
        return false;
    }

    /* esp_ota_end validates the image structure (magic, header, checksum).
     * It does NOT know what CLAWBOX intended - that is the next check. */
    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        status_set(OTA_STATE_FAILED, "image rejected: %s", esp_err_to_name(err));
        return false;
    }

    status_set(OTA_STATE_VERIFYING, "checking hash");
    char got_hex[65];
    hex_of(digest, got_hex);
    if (strcasecmp(got_hex, m->sha256) != 0) {
        /* Written, structurally valid, and not the artifact we were told to
         * install. Do not switch the boot slot. The slot keeps the bad image
         * until the next attempt overwrites it, which is harmless: nothing
         * boots from it. */
        ESP_LOGE(TAG, "sha256 mismatch\n  want %s\n  got  %s", m->sha256, got_hex);
        status_set(OTA_STATE_FAILED, "hash mismatch, not installing");
        return false;
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        status_set(OTA_STATE_FAILED, "set_boot_partition: %s", esp_err_to_name(err));
        return false;
    }

    status_set(OTA_STATE_REBOOTING, "installed %s, rebooting", m->version);
    ESP_LOGW(TAG, "rebooting into %s (%s)", target->label, m->version);
    vTaskDelay(pdMS_TO_TICKS(1500));   /* let the log drain and the UI show it */
    esp_restart();
    return true;                       /* not reached */
}

/* ---- the task ------------------------------------------------------------ */

static void ota_task(void *arg)
{
    (void)arg;

    /* Probation first. Nothing else matters until we know whether this image
     * is staying. */
    if (firmware_is_pending_verify()) {
        status_clear_rollback();
        run_health_gate();
    } else {
        status_set(OTA_STATE_IDLE, "running %s", firmware_version());
    }

    TickType_t next_poll = xTaskGetTickCount();

    for (;;) {
        bool due = (xTaskGetTickCount() >= next_poll);
        if (s_check_requested) {
            s_check_requested = false;
            due = true;
        }

        if (due) {
            next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(OTA_POLL_INTERVAL_MS);

            wifi_mgr_status_t w;
            wifi_mgr_get_status(&w);
            if (w.state == WIFI_MGR_ONLINE) {
                ota_manifest_t m;
                status_set(OTA_STATE_CHECKING, NULL);
                status_note_check();

                if (fetch_manifest(&m)) {
                    status_set_desired(m.version);

                    if (strcmp(m.version, firmware_version()) == 0) {
                        status_set(OTA_STATE_IDLE, "up to date (%s)", m.version);
                    } else {
                        /* CLAWBOX has named a different version as desired.
                         * That IS the deploy instruction - publishing alone
                         * never reaches this point. */
                        status_set(OTA_STATE_UPDATE_READY, "desired %s", m.version);
                        if (!install_manifest(&m)) {
                            status_note_failure();
                        }
                    }
                } else {
                    status_set(OTA_STATE_IDLE, "no manifest from CLAWBOX");
                }
            }
        }

        if (s_update_requested) {
            s_update_requested = false;
            s_check_requested = true;   /* handled on the next pass */
            next_poll = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t ota_service_start(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = OTA_STATE_IDLE;
    strlcpy(s_status.message, "starting", sizeof(s_status.message));

    /* 6KB stack: mbedTLS' TLS handshake is the peak consumer here. Priority
     * 4 keeps it below the audio and LVGL work - a firmware update must
     * never be the reason a recording drops samples. */
    BaseType_t ok = xTaskCreate(ota_task, "ota", 6144, NULL, 4, &s_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
