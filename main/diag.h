/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Diagnostics (1.2.1): evidence that survives a crash.
 *
 * 1.2.0's report showed the boot before last ended in a PANIC, and that was
 * all it could show - there is no core dump and no free flash for one. This
 * module keeps three things:
 *
 *   Breadcrumb   An RTC_NOINIT struct (RTC slow memory, not internal DRAM)
 *                that survives a panic or watchdog reset: a ~2 KB ring of the
 *                most recent log lines (esp_log_set_vprintf hook, chained to
 *                the original), the page on screen, the last command, and
 *                internal heap free/min as of the last 1 s tick. Magic + CRC
 *                over the header decide whether it is trustworthy. After an
 *                abnormal reset it is copied to PSRAM, cleared, and sent as
 *                `last_words` in an `after-crash` report.
 *
 *   Counters     NVS namespace "diag": panics, watchdog resets, self-restarts
 *                (backend_health's no-API restart) and brownouts, each with
 *                the boot number of the last one. Boot numbers are the Black
 *                Box's `bootcnt`. The device has no wall clock at boot, so the
 *                time of each is the time the backend received the
 *                after-crash report. Written only when one of them changes.
 *
 *   Stacks       High-water marks of the app tasks, by name, for reports.
 *
 * Threading: diag_init() runs once from app_main. The diag_set_* / tick
 * calls are safe from any task (a spinlock around a few dozen bytes). The
 * getters return data fixed at diag_init() and are safe from any task.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Commissioning aid: holding the LOG page's RPT button for 5 s calls
 * diag_test_crash(), which abort()s, so the after-crash report can be proven
 * end to end. Set to 0 (or delete the block in status_deck_ui.c marked
 * DIAG_TEST_CRASH) once commissioning is done. */
#define DIAG_TEST_CRASH 1
#define DIAG_TEST_CRASH_HOLD_MS 5000

/* Hourly heartbeat report (trigger "heartbeat"). */
#define DIAG_HEARTBEAT_S 3600

typedef struct {
    uint32_t boot;              /* this boot's number (Black Box bootcnt) */
    uint32_t counting_since;    /* boot number the counters started at (first 1.2.1 boot) */
    uint32_t panics;            /* PANIC and CPU_LOCKUP resets */
    uint32_t panic_last_boot;   /* boot that STARTED after the last one; 0 = never */
    uint32_t wdt_resets;        /* INT_WDT, TASK_WDT, other WDT */
    uint32_t wdt_last_boot;
    uint32_t self_restarts;     /* backend_health's no-API watchdog restart */
    uint32_t self_restart_last_boot;
    uint32_t brownouts;         /* BROWNOUT and PWR_GLITCH */
    uint32_t brownout_last_boot;
    uint32_t report_crashes;    /* 1.2.2: abnormal resets with a report in flight */
    uint32_t report_crash_last_boot;
} diag_counters_t;

typedef struct {
    bool happened;              /* this boot followed an abnormal reset */
    const char *kind;           /* "panic", "watchdog", "brownout" */
    bool breadcrumb_valid;      /* magic + CRC held; the fields below are real */
    uint32_t boot;              /* boot number that crashed */
    uint32_t uptime_s;          /* its uptime at the last tick */
    char page[12];
    char cmd[16];
    uint32_t internal_free;     /* at the last tick */
    uint32_t internal_min_free;
    uint32_t internal_largest;
    uint32_t psram_free;
    const char *last_words;     /* PSRAM, NUL-terminated, oldest line first; "" if none */
} diag_crash_t;

/** Once, from app_main, after ota_service_start(). Reads the breadcrumb and
 *  counters, updates the counters, clears the breadcrumb and installs the
 *  log hook. Allocates only in PSRAM, and only after an abnormal reset. */
void diag_init(void);

/** Breadcrumb updates. Cheap, never block, safe from any task. */
void diag_set_page(const char *name);
void diag_set_cmd(const char *name);
void diag_tick(void);          /* heap numbers + uptime; call about once a second */

const diag_counters_t *diag_counters(void);
const diag_crash_t *diag_crash(void);

/** True until an after-crash report has been stored by the backend. */
bool diag_after_crash_pending(void);
void diag_after_crash_delivered(void);

/** Stack high-water marks (bytes never used) of the known app tasks.
 *  Calls cb once per task that exists. Hourly use; walks the task lists. */
typedef void (*diag_stack_cb_t)(const char *task, uint32_t free_bytes, void *ctx);
void diag_stack_hwm(diag_stack_cb_t cb, void *ctx);

/* ---- report loop guard (1.2.2) ------------------------------------------
 * 1.2.1 crash-looped: the report path aborted (see diag_stack_hwm in diag.c),
 * and the after-crash report it scheduled on every following boot aborted
 * the same way, ~25 s after each boot, before OTA could land a fix.
 *
 * report_client.c brackets every report attempt (automatic or RPT) with
 * begin/end. The marker lives in RTC_NOINIT memory, so it survives the
 * reset. If diag_init() finds it set after an abnormal reset, it counts a
 * report crash (NVS "diag"/rptcrash) and automatic reports - after-crash,
 * heartbeat, auto-reconnect, auto-watchdog - stay off for this boot. The
 * RPT button still works. The next boot re-enables them. So a report path
 * that always crashes costs at most one reset, then the device stays up. */
void diag_report_guard_begin(const char *trigger);
void diag_report_guard_end(void);
/** True for the whole boot if the previous one died inside a report. */
bool diag_auto_reports_off(void);
/** The trigger of the report that was in flight then ("" if none). */
const char *diag_report_crash_trigger(void);

#if DIAG_TEST_CRASH
/** Commissioning only: logs why, then abort(). Never returns. */
void diag_test_crash(const char *why) __attribute__((noreturn));
#endif

#ifdef __cplusplus
}
#endif
