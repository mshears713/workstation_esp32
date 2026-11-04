/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * See backend_health.h for why backend reachability is probed rather than
 * inferred from Wi-Fi state.
 */
#include "backend_health.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_system.h"
#include "nvs.h"

#include "wifi_manager.h"
#include "device_config.h"
#include "firmware_identity.h"
#include "ota_service.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h is required - see backend_config.h.example"
#endif

static const char *TAG = "backend_health";

/* Chosen against the acceptance criterion "reflected on screen within a
 * few seconds": with HEALTH_FAILURES_BEFORE_DOWN below, a backend that
 * dies is shown as down after two consecutive misses, so this interval is
 * roughly half the worst-case detection latency (~3-5s here, vs ~9s at a
 * 5s interval). The probe is a constant-return endpoint - 43ms round trip
 * measured on this LAN - so the extra traffic is negligible next to
 * remote_client.c's 500ms poll. */
#define HEALTH_POLL_INTERVAL_MS 3000

/* Short on purpose. A backend that has not answered a constant-return
 * endpoint in 2s is not healthy, and a long timeout here would only make
 * the indicator slower to tell the truth. */
#define HEALTH_REQUEST_TIMEOUT_MS 2000

/* One missed probe is a blip - a retransmit, or the poll landing while a
 * 480KB chunk upload has the link busy. Two in a row is a real outage.
 * Without this the indicator flickers red during normal NOTE uploads. */
#define HEALTH_FAILURES_BEFORE_DOWN 2

/* 1.2.0: after this many failed probes in a row, try the other base URL
 * (IP <-> clawbox.local). At the 3s poll that is ~15s of silence - long
 * enough not to flip on a blip, short enough that a broken mDNS name costs
 * seconds rather than the whole evening. Uploads' verdicts do not count
 * towards it: only the probe is a like-for-like test of one address. */
#define HEALTH_FAILURES_BEFORE_SWITCH 5

/* 1.2.0 self-recovery: no successful /health (or upload) for this long and
 * the device records why in the Black Box and restarts. Everything that
 * went silent in the incident this release answers - every backend task,
 * with Wi-Fi and ping still up - is the kind of stuck state a restart
 * clears, and nothing else was going to notice. Ten minutes is far past
 * the 90s OTA health gate, so a fresh image rolls back on its own terms
 * long before this could fire, and it is also skipped while on probation. */
#define HEALTH_SELF_RESTART_S 600

/* Black Box keys this module writes. Same namespace and "lastfault" key as
 * status_deck_ui.c's blackbox_save_fault(); "selfrst" is new in 1.2.0 and
 * is read (then erased) by status_deck_ui.c's blackbox_boot_update(), which
 * is how the next boot knows the restart was this one - esp_reset_reason()
 * only says ESP_RST_SW, which an OTA install also says. */
#define BLACKBOX_NVS_NAMESPACE "blackbox"
#define BLACKBOX_KEY_FAULT     "lastfault"
#define BLACKBOX_KEY_SELFRST   "selfrst"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static backend_health_status_t s_status = { .state = BACKEND_HEALTH_UNKNOWN };
static bool s_started = false;

/* Counters and outage bookkeeping, all under s_mux. */
static uint32_t s_probe_failures;
static uint32_t s_outages;
static uint32_t s_longest_outage_s;
static uint32_t s_url_switches;
static bool     s_in_outage;
static uint32_t s_first_fail_s;   /* uptime of the first failure since the last success */
static bool     s_fail_streak;    /* s_first_fail_s is meaningful */
static uint32_t s_probe_fail_run; /* consecutive failed probes, for the URL switch */

static backend_health_event_cb_t s_cb;
static void *s_cb_ctx;

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

void backend_health_get_status(backend_health_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    portEXIT_CRITICAL(&s_mux);
}

void backend_health_get_stats(backend_health_stats_t *out)
{
    uint32_t now = uptime_s();
    portENTER_CRITICAL(&s_mux);
    out->probe_failures   = s_probe_failures;
    out->outages          = s_outages;
    out->longest_outage_s = s_longest_outage_s;
    out->current_outage_s = (s_in_outage && s_fail_streak) ? now - s_first_fail_s : 0;
    out->secs_since_ok    = now - s_status.last_ok_uptime_s; /* 0 = boot when never reached */
    out->url_switches     = s_url_switches;
    out->ever_ok          = s_status.last_ok_uptime_s != 0 || s_status.state == BACKEND_HEALTH_OK;
    portEXIT_CRITICAL(&s_mux);
}

static void emit(backend_health_event_t ev, uint32_t outage_s, const char *message)
{
    ESP_LOGW(TAG, "%s", message);
    if (s_cb) {
        s_cb(ev, outage_s, message, s_cb_ctx);
    }
}

static void record_result(bool ok, uint32_t latency_ms)
{
    uint32_t now = uptime_s();
    bool went_down = false;
    bool came_back = false;
    uint32_t outage_s = 0;

    portENTER_CRITICAL(&s_mux);
    if (ok) {
        if (s_in_outage) {
            came_back = true;
            outage_s = s_fail_streak ? now - s_first_fail_s : 0;
            if (outage_s > s_longest_outage_s) {
                s_longest_outage_s = outage_s;
            }
            s_in_outage = false;
        }
        s_fail_streak = false;
        s_status.state = BACKEND_HEALTH_OK;
        s_status.consecutive_failures = 0;
        /* max(1, now): 0 is reserved for "never reached this boot", and a
         * success in the very first second would otherwise look like that. */
        s_status.last_ok_uptime_s = now ? now : 1;
        if (latency_ms > 0) {
            s_status.latency_ms = latency_ms;
        }
    } else {
        if (!s_fail_streak) {
            s_fail_streak = true;
            s_first_fail_s = now;
        }
        s_status.consecutive_failures++;
        if (s_status.consecutive_failures >= HEALTH_FAILURES_BEFORE_DOWN) {
            s_status.state = BACKEND_HEALTH_DOWN;
            if (!s_in_outage) {
                s_in_outage = true;
                s_outages++;
                went_down = true;
            }
        }
    }
    portEXIT_CRITICAL(&s_mux);

    /* Outside the critical section: the callback takes its own lock. */
    if (went_down) {
        emit(BACKEND_HEALTH_EVT_LOST, 0, "API LOST");
    }
    if (came_back) {
        char msg[32];
        snprintf(msg, sizeof(msg), "API BACK AFTER %lus", (unsigned long)outage_s);
        emit(BACKEND_HEALTH_EVT_BACK, outage_s, msg);
    }
}

void backend_health_report(bool ok)
{
    /* An upload just proved the backend's reachability far more
     * conclusively than a probe would, so it counts the same as one. */
    record_result(ok, 0);
}

/* "http://clawbox.local:8000" -> "clawbox.local:8000", for a Black Box line
 * that has to fit 31 characters. */
static const char *url_host(const char *url)
{
    const char *p = strstr(url, "://");
    return p ? p + 3 : url;
}

/* Called from the poll task only, after a failed probe. */
static void maybe_switch_url(void)
{
    bool do_switch = false;
    portENTER_CRITICAL(&s_mux);
    s_probe_failures++;
    s_probe_fail_run++;
    if (s_probe_fail_run >= HEALTH_FAILURES_BEFORE_SWITCH) {
        s_probe_fail_run = 0;
        do_switch = true;
    }
    portEXIT_CRITICAL(&s_mux);

    if (!do_switch || strcmp(device_config_backend_other_url(), BACKEND_BASE_URL) == 0) {
        return;
    }
    const char *from = BACKEND_BASE_URL;
    bool fallback = device_config_backend_switch();
    portENTER_CRITICAL(&s_mux);
    s_url_switches++;
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGW(TAG, "%d failed probes on %s - switching to %s %s", HEALTH_FAILURES_BEFORE_SWITCH,
             from, fallback ? "fallback" : "primary", BACKEND_BASE_URL);
    char msg[32];
    snprintf(msg, sizeof(msg), "API VIA %.23s", url_host(BACKEND_BASE_URL));
    emit(BACKEND_HEALTH_EVT_SWITCHED, 0, msg);
}

/* Anything that a restart would ruin. Returns a short reason, or NULL. */
static const char *restart_blocker(void)
{
    if (firmware_is_pending_verify()) {
        return "image on probation";   /* the OTA health gate owns this boot */
    }
    ota_status_t ota;
    ota_service_get_status(&ota);
    switch (ota.state) {
    case OTA_STATE_VALIDATING:
    case OTA_STATE_UPDATE_READY:
    case OTA_STATE_DOWNLOADING:
    case OTA_STATE_VERIFYING:
    case OTA_STATE_REBOOTING:
        return "OTA in progress";
    default:
        break;
    }
    return NULL;
}

static void self_restart(uint32_t silent_s)
{
    char fault[32];
    snprintf(fault, sizeof(fault), "WDT NO API %lus", (unsigned long)silent_s);
    ESP_LOGE(TAG, "no backend answer for %lus (api %s) - recording '%s' and restarting",
             (unsigned long)silent_s, BACKEND_BASE_URL, fault);

    nvs_handle_t h;
    if (nvs_open(BLACKBOX_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, BLACKBOX_KEY_FAULT, fault);
        nvs_set_u8(h, BLACKBOX_KEY_SELFRST, 1);
        nvs_commit(h);
        nvs_close(h);
    }
    vTaskDelay(pdMS_TO_TICKS(500));   /* let the log line drain */
    esp_restart();
}

static void check_self_restart(void)
{
    static bool s_deferred_logged = false;

    backend_health_status_t st;
    backend_health_get_status(&st);
    /* 1.6.0 exhibit: only a backend that answered this boot and then went
     * silent is a stuck state a restart can clear. Off the home LAN (an
     * exhibit hall: no CLAWBOX, maybe no Wi-Fi at all) it never answers, and
     * the 1.2.0 rule restarted the exhibit every ten minutes mid-visit. */
    if (st.last_ok_uptime_s == 0) {
        return;
    }
    uint32_t silent_s = uptime_s() - st.last_ok_uptime_s;
    if (silent_s < HEALTH_SELF_RESTART_S) {
        s_deferred_logged = false;
        return;
    }
    const char *blocker = restart_blocker();
    if (blocker != NULL) {
        if (!s_deferred_logged) {
            ESP_LOGW(TAG, "no backend for %lus, restart deferred: %s", (unsigned long)silent_s, blocker);
            s_deferred_logged = true;
        }
        return;
    }
    self_restart(silent_s);
}

const char *backend_health_label(const backend_health_status_t *st)
{
    switch (st->state) {
    case BACKEND_HEALTH_OK:         return "API: OK";
    case BACKEND_HEALTH_DOWN:       return "API: DOWN";
    case BACKEND_HEALTH_NO_NETWORK: return "API: --";
    case BACKEND_HEALTH_UNKNOWN:
    default:                        return "API: ?";
    }
}

static bool probe_once(uint32_t *latency_ms_out)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s", BACKEND_BASE_URL, HEALTH_PATH);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HEALTH_REQUEST_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }

    int64_t start_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);

    *latency_ms_out = elapsed_ms;
    /* Any answer means the backend is up. A non-200 would mean it is
     * running but unhappy, which is still reachable - and /health has no
     * failure mode that returns non-200 today. */
    return err == ESP_OK && status == 200;
}

static void poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        wifi_mgr_status_t wst;
        wifi_mgr_get_status(&wst);

        if (wst.state != WIFI_MGR_ONLINE) {
            /* Don't call an unreachable backend "down" - the network is
             * the thing that is missing, and saying so is the difference
             * between "restart uvicorn" and "check the Wi-Fi". */
            portENTER_CRITICAL(&s_mux);
            s_status.state = BACKEND_HEALTH_NO_NETWORK;
            s_status.consecutive_failures = 0;
            portEXIT_CRITICAL(&s_mux);
        } else {
            uint32_t latency_ms = 0;
            bool ok = probe_once(&latency_ms);
            if (ok) {
                portENTER_CRITICAL(&s_mux);
                s_probe_fail_run = 0;
                portEXIT_CRITICAL(&s_mux);
            }

            backend_health_state_t before;
            portENTER_CRITICAL(&s_mux);
            before = s_status.state;
            portEXIT_CRITICAL(&s_mux);

            record_result(ok, latency_ms);

            backend_health_status_t after;
            backend_health_get_status(&after);
            if (after.state != before) {
                /* Transitions only - this runs every 5s forever and must
                 * not fill the log with "still fine". */
                ESP_LOGI(TAG, "backend %s (%lums) via %s", backend_health_label(&after),
                         (unsigned long)latency_ms, BACKEND_BASE_URL);
            }
            if (!ok) {
                /* After record_result, so "API LOST" lands in the Black Box
                 * before "API VIA ..." does. */
                maybe_switch_url();
            }
        }

        /* Counted whether or not Wi-Fi is up: a Wi-Fi stack that never
         * comes back is exactly as stuck as a backend that never answers. */
        check_self_restart();

        vTaskDelay(pdMS_TO_TICKS(HEALTH_POLL_INTERVAL_MS));
    }
}

void backend_health_init(backend_health_event_cb_t cb, void *user_ctx)
{
    if (s_started) {
        return;
    }
    s_started = true;
    s_cb = cb;
    s_cb_ctx = user_ctx;
    /* Priority 3: below the audio worker/uploader (5) and the notification
     * and remote pollers (4). This is a status indicator - it must never
     * compete with capture or upload for the Wi-Fi path. */
    xTaskCreate(poll_task, "backend_health", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "polling %s%s every %dms", BACKEND_BASE_URL, HEALTH_PATH, HEALTH_POLL_INTERVAL_MS);
}
