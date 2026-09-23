/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Device report (1.2.0): one POST of everything the device knows about its
 * own health, to BACKEND_BASE_URL + REPORT_PATH, so that the next time it
 * loses the server there is evidence on CLAWBOX instead of a guess.
 *
 * Sent when:
 *   - the RPT button on the LOG page is tapped (trigger "button"),
 *   - the backend answers again after an outage longer than 2 minutes
 *     (trigger "auto-reconnect"),
 *   - once after boot, if the previous reset was backend_health's
 *     self-recovery restart (trigger "auto-watchdog").
 *   - 1.2.1: once after a PANIC/watchdog/brownout reset, retried each minute
 *     until stored (trigger "after-crash"), and hourly (trigger
 *     "heartbeat"). Every report also carries diag.h's counters, the
 *     previous boot's crash breadcrumb (`crash`, `last_words`) and task
 *     stack high-water marks (`stack_free_min`).
 *
 * Threading: status_deck_ui.c (the LVGL task) claims the request with
 * report_client_begin(), fills in the parts only it owns - the Black Box
 * ring and summary - and hands it over with report_client_commit(). The
 * HTTP work happens on this module's own worker task. Everything else in
 * the report (heap, Wi-Fi, OTA, backend counters) is read by the worker
 * from modules that are safe to read from any task.
 *
 * Memory: the request and the JSON body live in PSRAM, allocated once at
 * init. Internal RAM is the scarce resource on this board.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REPORT_PATH "/api/v1/device/report"

#define REPORT_MAX_EVENTS 32
#define REPORT_EVENT_MSG_LEN 32

typedef struct {
    uint32_t uptime_s;
    char type[12];                       /* "NETWORK", "BACKEND", ... */
    char message[REPORT_EVENT_MSG_LEN];
} report_event_t;

typedef struct {
    char trigger[24];                    /* "button", "auto-reconnect", "auto-watchdog", "after-crash", "heartbeat" */
    uint32_t boot_count;
    char reset_reason[20];               /* this boot */
    char prev_reset_reason[20];          /* the boot before */
    bool prev_was_self_restart;          /* previous reset was the no-backend watchdog */
    char last_fault[32];
    char last_cmd[16];
    int event_count;                     /* newest first */
    report_event_t events[REPORT_MAX_EVENTS];
} report_request_t;

typedef enum {
    REPORT_IDLE = 0,
    REPORT_SENDING,
    REPORT_SENT,
    REPORT_FAILED,
} report_state_t;

typedef struct {
    report_state_t state;
    uint32_t seq;          /* increments each time a send settles (SENT or FAILED) */
    char id[24];           /* backend-assigned id of the last SENT report */
    char message[32];      /* Black Box line for the last result */
} report_status_t;

/* Allocates the PSRAM buffers and starts the worker. Call once. */
void report_client_init(void);

/* Claims the single request slot. NULL if a report is already in flight or
 * init failed. The caller must fill it and call report_client_commit(). */
report_request_t *report_client_begin(void);

/* Hands the claimed request to the worker. */
void report_client_commit(void);

void report_client_get_status(report_status_t *out);

#ifdef __cplusplus
}
#endif
