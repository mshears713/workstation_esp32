/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 09 — Earthside Handshake: backend request lifecycle
 * @details Public interface for the HTTP request state machine described in
 *          handshake_client.c. Deliberately app-agnostic (it knows nothing
 *          about app_state or LVGL), mirroring wifi_manager.h's boundary.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HS_IDLE = 0,
    HS_SENDING,
    HS_ACCEPTED,
    HS_TIMEOUT,
    HS_NETWORK_ERROR,
    HS_SERVER_ERROR,
    HS_BAD_RESPONSE,
} handshake_state_t;

/* Max samples a payload can carry - matches the "last 10 measurements"
 * ring buffer status_deck_ui.c keeps. accel_sample_count may be less than
 * this (e.g. right after boot, or if the IMU isn't present) - never
 * padded with fabricated zeros, only whatever real samples exist. */
#define HANDSHAKE_MAX_ACCEL_SAMPLES 10

/* Built by the caller (status_deck_ui.c) from its own app_state - this
 * module has no idea what a "mission" or "sequence" means, it just ships
 * these fields as JSON. Never put Wi-Fi credentials or other secrets in
 * here. */
typedef struct {
    char device_id[24];
    char event_type[24];
    char mission[16];
    uint32_t device_uptime_ms;
    uint32_t sequence;
    float accel_samples_g[HANDSHAKE_MAX_ACCEL_SAMPLES]; /* oldest first */
    int accel_sample_count;                             /* 0..HANDSHAKE_MAX_ACCEL_SAMPLES */
    uint32_t sample_interval_ms;                         /* nominal spacing between samples */
} handshake_payload_t;

typedef struct {
    handshake_state_t state;
    int last_http_status;     /* 0 if no HTTP response was ever received */
    char last_event_id[40];   /* "" until the first ACCEPTED; kept across later failures */
    int64_t last_result_us;   /* esp_timer_get_time() when state last settled; meaningless while SENDING */
} handshake_status_t;

/**
 * Called on the handshake worker task - never the LVGL task. Implementations
 * must not block and must not touch LVGL. `blackbox_message` is short,
 * ready-to-log, and never contains the response body or credentials.
 */
typedef void (*handshake_event_cb_t)(handshake_state_t new_state, const char *blackbox_message, void *user_ctx);

/** Starts the persistent worker task. Call once, from any task. */
void handshake_client_init(handshake_event_cb_t cb, void *user_ctx);

/** Thread-safe snapshot of current status for rendering. */
void handshake_client_get_status(handshake_status_t *out);

/**
 * Enqueues one handshake request. Returns false and does nothing if a
 * request is already in flight (the in-flight guard) - callers should treat
 * that as "button press had no effect," not an error worth reporting.
 */
bool handshake_client_send(const handshake_payload_t *payload);

#ifdef __cplusplus
}
#endif
