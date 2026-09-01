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

#include "wifi_manager.h"

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
 * remote_client.c's existing 150ms poll. */
#define HEALTH_POLL_INTERVAL_MS 3000

/* Short on purpose. A backend that has not answered a constant-return
 * endpoint in 2s is not healthy, and a long timeout here would only make
 * the indicator slower to tell the truth. */
#define HEALTH_REQUEST_TIMEOUT_MS 2000

/* One missed probe is a blip - a retransmit, or the poll landing while a
 * 480KB chunk upload has the link busy. Two in a row is a real outage.
 * Without this the indicator flickers red during normal NOTE uploads. */
#define HEALTH_FAILURES_BEFORE_DOWN 2

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static backend_health_status_t s_status = { .state = BACKEND_HEALTH_UNKNOWN };
static bool s_started = false;

void backend_health_get_status(backend_health_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    portEXIT_CRITICAL(&s_mux);
}

static void record_result(bool ok, uint32_t latency_ms)
{
    portENTER_CRITICAL(&s_mux);
    if (ok) {
        s_status.state = BACKEND_HEALTH_OK;
        s_status.consecutive_failures = 0;
        s_status.last_ok_uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
        if (latency_ms > 0) {
            s_status.latency_ms = latency_ms;
        }
    } else {
        s_status.consecutive_failures++;
        if (s_status.consecutive_failures >= HEALTH_FAILURES_BEFORE_DOWN) {
            s_status.state = BACKEND_HEALTH_DOWN;
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

void backend_health_report(bool ok)
{
    /* An upload just proved the backend's reachability far more
     * conclusively than a probe would, so it counts the same as one. */
    record_result(ok, 0);
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
                ESP_LOGI(TAG, "backend %s (%lums)", backend_health_label(&after),
                         (unsigned long)latency_ms);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(HEALTH_POLL_INTERVAL_MS));
    }
}

void backend_health_init(void)
{
    if (s_started) {
        return;
    }
    s_started = true;
    /* Priority 3: below the audio worker/uploader (5) and the notification
     * and remote pollers (4). This is a status indicator - it must never
     * compete with capture or upload for the Wi-Fi path. */
    xTaskCreate(poll_task, "backend_health", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "polling %s%s every %dms", BACKEND_BASE_URL, HEALTH_PATH, HEALTH_POLL_INTERVAL_MS);
}
