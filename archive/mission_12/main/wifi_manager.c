/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 08 — Connection Deck: Wi-Fi station lifecycle
 * @details Treats Wi-Fi as an explicit state machine (DISCONNECTED ->
 *          CONNECTING -> ONLINE, with RETRY_WAIT entered on any failure)
 *          rather than a connected/disconnected boolean, per the Mission 08
 *          directive. Runs entirely on the ESP-IDF default event loop task;
 *          the only cross-task contract with the UI is wifi_mgr_get_status()
 *          (mutex-protected snapshot) and the wifi_mgr_event_cb_t callback,
 *          which the caller must treat as running on a foreign task - the
 *          same rule status_deck_ui.c's sensor_event_handler already
 *          follows for the IMU.
 *
 *          Retries use a small bounded backoff (see kRetryDelaysMs) capped
 *          at its last entry, so reconnection continues indefinitely at a
 *          fixed cadence rather than escalating forever or spinning in a
 *          tight loop. Retries never stop on their own - a van driving back
 *          into Wi-Fi range is normal operation, not a terminal failure -
 *          so there is no separate persistent FAILED state; failure is a
 *          reason attached to the RETRY_WAIT the device enters anyway.
 *
 *          Manual reconnect (wifi_mgr_request_reconnect) while already
 *          ONLINE goes through the real WIFI_EVENT_STA_DISCONNECTED event
 *          rather than assuming esp_wifi_disconnect() completes
 *          synchronously - see s_manual_reconnect_pending.
 */

#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "wifi_manager.h"

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#else
#error "main/wifi_credentials.h not found. Copy main/wifi_credentials.h.example to main/wifi_credentials.h and fill in your real Wi-Fi SSID/password. That file is git-ignored (unlike sdkconfig), so credentials never reach git history."
#endif

static const char *TAG = "wifi_mgr";

/* Bounded, increasing retry backoff. Index caps at the last entry so
 * retries continue forever at a fixed 10s cadence instead of escalating
 * without limit or hammering the AP in a tight loop. */
static const uint32_t kRetryDelaysMs[] = { 3000, 5000, 10000 };
#define RETRY_DELAYS_LEN (sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0]))

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static wifi_mgr_status_t s_status = {
    .state = WIFI_MGR_DISCONNECTED,
    .ip_addr = "",
    .reason_text = "",
    .attempt_count = 0,
    .retry_remaining_s = 0,
};
static int64_t s_retry_deadline_us = 0; /* absolute; valid only while state==RETRY_WAIT */
static volatile bool s_manual_reconnect_pending = false;

static wifi_mgr_event_cb_t s_cb = NULL;
static void *s_cb_ctx = NULL;
static esp_timer_handle_t s_retry_timer = NULL;

static void notify(wifi_mgr_state_t new_state, const char *message)
{
    if (s_cb) {
        s_cb(new_state, message, s_cb_ctx);
    }
}

static void begin_connect_attempt(void)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = WIFI_MGR_CONNECTING;
    s_status.attempt_count++;
    s_status.retry_remaining_s = 0;
    s_status.reason_text[0] = '\0';
    portEXIT_CRITICAL(&s_mux);

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    }
    notify(WIFI_MGR_CONNECTING, "WIFI CONNECTING");
}

static void retry_timer_cb(void *arg)
{
    (void)arg;
    begin_connect_attempt();
}

/* retry_timer_cb fires on the esp_timer task, a different task from the
 * event-loop task that normally calls this - so unlike most of this file,
 * s_status.attempt_count and s_retry_deadline_us genuinely need the same
 * critical section covering the read, the derived writes, and the
 * timestamp together, not just the final state/retry_remaining_s write. */
static void schedule_retry(void)
{
    uint32_t delay_ms;

    portENTER_CRITICAL(&s_mux);
    uint32_t idx = s_status.attempt_count < RETRY_DELAYS_LEN ? s_status.attempt_count : RETRY_DELAYS_LEN - 1;
    delay_ms = kRetryDelaysMs[idx];
    s_status.state = WIFI_MGR_RETRY_WAIT;
    s_status.retry_remaining_s = (delay_ms + 999) / 1000;
    s_retry_deadline_us = esp_timer_get_time() + (int64_t)delay_ms * 1000;
    portEXIT_CRITICAL(&s_mux);

    esp_timer_stop(s_retry_timer); /* harmless if not currently running */
    esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000);
}

typedef enum { REASON_NO_AP, REASON_AUTH, REASON_LOST, REASON_OTHER } reason_kind_t;

/* NO_AP_FOUND and the auth-related codes are stable, long-standing
 * wifi_err_reason_t values. "was_online" always wins - losing an
 * established link is a more useful operator category than whatever
 * low-level reason code accompanies the drop. */
static reason_kind_t categorize_reason(uint8_t reason, bool was_online)
{
    if (was_online) {
        return REASON_LOST;
    }
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return REASON_NO_AP;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return REASON_AUTH;
    default:
        return REASON_OTHER;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;
    if (base != WIFI_EVENT) {
        return;
    }

    if (id == WIFI_EVENT_STA_START) {
        notify(WIFI_MGR_CONNECTING, "WIFI START");
        begin_connect_attempt();
        return;
    }

    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)event_data;

        if (s_manual_reconnect_pending) {
            s_manual_reconnect_pending = false;
            notify(WIFI_MGR_CONNECTING, "MANUAL RECONNECT");
            begin_connect_attempt();
            return;
        }

        bool was_online;
        portENTER_CRITICAL(&s_mux);
        was_online = (s_status.state == WIFI_MGR_ONLINE);
        s_status.ip_addr[0] = '\0';
        portEXIT_CRITICAL(&s_mux);

        reason_kind_t kind = categorize_reason(evt->reason, was_online);
        const char *reason_str =
            kind == REASON_NO_AP ? "NO AP FOUND" :
            kind == REASON_AUTH  ? "AUTH FAILED" :
            kind == REASON_LOST  ? "WIFI LOST"   : "RETRYING";

        portENTER_CRITICAL(&s_mux);
        strncpy(s_status.reason_text, reason_str, sizeof(s_status.reason_text) - 1);
        s_status.reason_text[sizeof(s_status.reason_text) - 1] = '\0';
        portEXIT_CRITICAL(&s_mux);

        schedule_retry();

        char msg[32];
        uint32_t remaining_s;
        portENTER_CRITICAL(&s_mux);
        remaining_s = s_status.retry_remaining_s;
        portEXIT_CRITICAL(&s_mux);
        snprintf(msg, sizeof(msg), "%s, RETRY %lus", reason_str, (unsigned long)remaining_s);
        notify(WIFI_MGR_RETRY_WAIT, msg);
        return;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;
    if (base != IP_EVENT || id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;

    portENTER_CRITICAL(&s_mux);
    s_status.state = WIFI_MGR_ONLINE;
    s_status.retry_remaining_s = 0;
    s_status.reason_text[0] = '\0';
    snprintf(s_status.ip_addr, sizeof(s_status.ip_addr), IPSTR, IP2STR(&evt->ip_info.ip));
    portEXIT_CRITICAL(&s_mux);

    notify(WIFI_MGR_ONLINE, "WIFI ONLINE");
}

void wifi_mgr_init(wifi_mgr_event_cb_t cb, void *user_ctx)
{
    s_cb = cb;
    s_cb_ctx = user_ctx;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, NULL));

    const esp_timer_create_args_t timer_args = {
        .callback = &retry_timer_cb,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_retry_timer));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, WIFI_CREDENTIALS_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_CREDENTIALS_PASS, sizeof(wifi_config.sta.password) - 1);
    /* "Accept AP if security >= WPA2_PSK" - matches ESP-IDF's own station
     * example default. A WPA3-only network is a known gap; nothing here
     * detects or reports that case specially. */
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* esp_wifi_start() triggers WIFI_EVENT_STA_START asynchronously on the
     * event task; that handler is what actually issues the first connect. */
}

void wifi_mgr_get_status(wifi_mgr_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    if (out->state == WIFI_MGR_RETRY_WAIT) {
        int64_t remaining_us = s_retry_deadline_us - esp_timer_get_time();
        out->retry_remaining_s = remaining_us > 0 ? (uint32_t)((remaining_us + 999999) / 1000000) : 0;
    }
    portEXIT_CRITICAL(&s_mux);
}

void wifi_mgr_request_reconnect(void)
{
    wifi_mgr_state_t cur;
    portENTER_CRITICAL(&s_mux);
    cur = s_status.state;
    portEXIT_CRITICAL(&s_mux);

    if (cur == WIFI_MGR_ONLINE) {
        /* Completed asynchronously in wifi_event_handler's
         * WIFI_EVENT_STA_DISCONNECTED branch - esp_wifi_disconnect() is not
         * guaranteed to have finished by the time this call returns. */
        s_manual_reconnect_pending = true;
        esp_wifi_disconnect();
        return;
    }

    esp_timer_stop(s_retry_timer); /* harmless if not currently running */
    begin_connect_attempt();
}
