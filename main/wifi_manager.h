/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 08 — Connection Deck: Wi-Fi station lifecycle
 * @details Public interface for the Wi-Fi state machine described in
 *          wifi_manager.c. Kept deliberately free of LVGL/UI types so a
 *          future Mission 09 backend module can depend on it the same way
 *          status_deck_ui.c does.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MGR_DISCONNECTED = 0,
    WIFI_MGR_CONNECTING,
    WIFI_MGR_ONLINE,
    WIFI_MGR_RETRY_WAIT,
} wifi_mgr_state_t;

typedef struct {
    wifi_mgr_state_t state;
    char ip_addr[16];           /* "" until ONLINE */
    char reason_text[20];       /* "" or a short cause, e.g. "AUTH FAILED" */
    uint32_t attempt_count;     /* esp_wifi_connect() calls issued since boot */
    uint32_t retry_remaining_s; /* seconds left in current RETRY_WAIT, else 0 */
} wifi_mgr_status_t;

/**
 * Called on the ESP-IDF Wi-Fi/event task (or, for a manually-triggered
 * reconnect, whichever task called wifi_mgr_request_reconnect) - never the
 * LVGL task. Implementations must not block and must not touch LVGL.
 * `blackbox_message` is a short, ready-to-log, non-credential string valid
 * only for the duration of the call.
 */
typedef void (*wifi_mgr_event_cb_t)(wifi_mgr_state_t new_state, const char *blackbox_message, void *user_ctx);

/**
 * Brings up the Wi-Fi station using the credentials in
 * main/wifi_credentials.h (git-ignored - see wifi_credentials.h.example)
 * and starts the connect/retry lifecycle. Call once, from any task.
 */
void wifi_mgr_init(wifi_mgr_event_cb_t cb, void *user_ctx);

/** Thread-safe snapshot of current status for rendering. */
void wifi_mgr_get_status(wifi_mgr_status_t *out);

/**
 * Manual RETRY/CONNECT control: cancels any pending retry wait and starts
 * a fresh connection attempt, regardless of current state.
 */
void wifi_mgr_request_reconnect(void);

#ifdef __cplusplus
}
#endif
