/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Backend reachability, tracked separately from Wi-Fi state.
 *
 * Wi-Fi being up says nothing about whether the FastAPI backend is
 * answering, and the two fail independently in practice: the router is
 * fine while the laptop running uvicorn is asleep, closed, or has the
 * process stopped. Before this module the console could only show Wi-Fi,
 * so a workstation that would silently fail every upload looked identical
 * to a healthy one.
 *
 * Deliberately a real probe rather than an inference. The listening ring
 * turning orange is a *content* signal (a notification is pending) and
 * notification_client.c leaves its status unchanged when the backend is
 * unreachable, so it cannot stand in for reachability.
 *
 * Two inputs feed the state:
 *   - a periodic GET HEALTH_PATH on this module's own low-priority task
 *   - backend_health_report(), called by the upload path, so a real
 *     failed upload marks the backend down immediately instead of the
 *     screen staying green until the next poll
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BACKEND_HEALTH_UNKNOWN = 0, /* not probed yet this boot */
    BACKEND_HEALTH_OK,          /* backend answered */
    BACKEND_HEALTH_DOWN,        /* reachable network, no answer from the backend */
    BACKEND_HEALTH_NO_NETWORK,  /* Wi-Fi is not online, so nothing was attempted */
} backend_health_state_t;

typedef struct {
    backend_health_state_t state;
    uint32_t latency_ms;           /* round trip of the last successful probe */
    uint32_t consecutive_failures; /* 0 whenever state is OK */
    uint32_t last_ok_uptime_s;     /* 0 if never reached this boot */
} backend_health_status_t;

/* Connection history since boot (1.2.0). Kept here because this module is
 * the one that already sees every probe and every upload's verdict. */
typedef struct {
    uint32_t probe_failures;    /* failed /health probes since boot */
    uint32_t outages;           /* times the backend went DOWN since boot */
    uint32_t longest_outage_s;  /* longest finished outage this boot */
    uint32_t current_outage_s;  /* 0 unless an outage is in progress */
    uint32_t secs_since_ok;     /* since the last success, or since boot if none */
    uint32_t url_switches;      /* primary<->fallback flips this boot */
    bool     ever_ok;           /* the backend has answered at least once */
} backend_health_stats_t;

typedef enum {
    BACKEND_HEALTH_EVT_LOST = 0, /* went DOWN */
    BACKEND_HEALTH_EVT_BACK,     /* answered again after being DOWN */
    BACKEND_HEALTH_EVT_SWITCHED, /* now using the other base URL */
} backend_health_event_t;

/* Runs on whichever task observed the change (the poll task, or an upload
 * task via backend_health_report) - never the LVGL task. Bridge it, the same
 * way status_deck_ui.c bridges wifi_manager's callback. `outage_s` is the
 * finished outage's length for EVT_BACK, else 0. `message` fits the Black
 * Box's 32-byte records. */
typedef void (*backend_health_event_cb_t)(backend_health_event_t ev, uint32_t outage_s,
                                          const char *message, void *user_ctx);

/* Starts the poll task. Safe to call once, after wifi_mgr_init(). The
 * callback may be NULL. */
void backend_health_init(backend_health_event_cb_t cb, void *user_ctx);

/* Snapshot of the counters above. Safe from any task. */
void backend_health_get_stats(backend_health_stats_t *out);

/* Snapshot, mutex-free (single word writes under a portMUX). Safe from any
 * task, including the LVGL timer. */
void backend_health_get_status(backend_health_status_t *out);

/* Fed by the upload path so a genuine transport failure updates the
 * indicator immediately rather than up to one poll interval later. `ok`
 * means the backend answered at all - an HTTP error response still counts
 * as reachable, since something replied. */
void backend_health_report(bool ok);

/* Short label for the status row, e.g. "API: OK". Never NULL. */
const char *backend_health_label(const backend_health_status_t *st);
