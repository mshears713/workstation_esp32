/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief DIAGNOSTICS page model: raw values in, words + severity out.
 * @details Plain C (snprintf only - no LVGL, no ESP-IDF), so the same rules
 *          run in the firmware, in the host simulator, and in
 *          tools/hosttest under ASan/UBSan. ui_diagnostics.c draws what these
 *          functions decide; status_deck_ui.c fills ui_diag_view_t from the
 *          subsystems; tools/uisim/targets/diagnostics.c fills it from
 *          fixtures.
 *
 *          The rules that keep it truthful:
 *            - nothing reads as good until its source has said so this boot
 *              (never-probed is WAITING / UNKNOWN, not OK);
 *            - every non-nominal state has a word; colour only reinforces it;
 *            - a sensor whose init failed is ERROR, not "absent": a failed
 *              probe does not prove the hardware is missing;
 *            - internal heap only (PSRAM is never the constraint), numbers
 *              without verdicts - "LOW" appears only under a documented
 *              reference, and nothing here says "OK" about memory.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* mirrors wifi_mgr_state_t */
typedef enum { UI_DIAG_WIFI_OFF = 0, UI_DIAG_WIFI_JOINING, UI_DIAG_WIFI_ONLINE, UI_DIAG_WIFI_RETRY } ui_diag_wifi_t;
/* mirrors backend_health_state_t */
typedef enum { UI_DIAG_API_UNKNOWN = 0, UI_DIAG_API_OK, UI_DIAG_API_DOWN, UI_DIAG_API_NO_NET } ui_diag_api_t;
/* derived by ui_diag_freshness() from the boot probe and the last-good timestamp */
typedef enum {
    UI_DIAG_SRC_UNKNOWN = 0, /* probe not run yet */
    UI_DIAG_SRC_FAILED,      /* bsp_sensor_init / iot_sensor_start returned an error */
    UI_DIAG_SRC_WAITING,     /* probe ok, no sample yet */
    UI_DIAG_SRC_LIVE,        /* last sample within the stale threshold */
    UI_DIAG_SRC_STALE,       /* last sample older than the threshold */
} ui_diag_src_t;

typedef enum { UI_DIAG_SEV_NONE = 0, UI_DIAG_SEV_NOM, UI_DIAG_SEV_CAU, UI_DIAG_SEV_ALM } ui_diag_sev_t;

/* Documented references, not health claims (diagnostics-review/DATA-SOURCES.md):
 *   min-free under 2 KiB: 1.2.0 panicked at 1,207 B; workstation-watch alerts under 2 KB
 *   largest under 7 KiB:  CLAUDE.md "OTA needs ~7 KB contiguous" */
#define UI_DIAG_HEAP_MIN_REF   2048u
#define UI_DIAG_HEAP_BLOCK_REF 7168u

/** Everything the page shows. Strings are borrowed for the apply call only. */
typedef struct {
    ui_diag_wifi_t wifi;
    bool have_rssi;             /* esp_wifi_sta_get_ap_info() answered */
    int rssi_dbm;
    const char *wifi_reason;    /* wifi_mgr reason_text, "" or NULL when none */
    uint32_t wifi_attempts;
    uint32_t wifi_retry_s;

    ui_diag_api_t api;
    bool api_ever_ok;
    uint32_t api_secs_since_ok;

    ui_diag_src_t accel;
    uint64_t accel_age_ms;
    ui_diag_src_t env;          /* AHT30: temperature + humidity share one freshness */
    uint64_t env_age_ms;

    uint64_t uptime_s;

    uint32_t heap_free;         /* heap_caps_*(MALLOC_CAP_INTERNAL), bytes */
    uint32_t heap_largest;
    uint32_t heap_min;          /* minimum free since boot */
} ui_diag_view_t;

typedef struct {
    char word[24];              /* drawn at 20 px; sized for -Werror=format-truncation */
    char detail[32];            /* drawn at 14 px */
    ui_diag_sev_t sev;
} ui_diag_line_t;

/** Freshness from the boot probe and timestamps (esp_timer microseconds;
 *  last_good_us == 0 means no sample yet). Writes the age in ms, saturated,
 *  never negative. */
ui_diag_src_t ui_diag_freshness(bool probed, bool present, int64_t last_good_us, int64_t now_us,
                                int64_t stale_after_us, uint64_t *age_ms);

void ui_diag_fmt_age_ms(char *b, size_t n, uint64_t ms);   /* "<1 s" "42 s" "2m 14s" "3h 05m" "51d 7h" */
void ui_diag_fmt_days(char *b, size_t n, uint64_t up_s);   /* "1 day" "412 days" */
void ui_diag_fmt_hm(char *b, size_t n, uint64_t up_s);     /* "07 h 12 m" */
void ui_diag_fmt_kib(char *b, size_t n, uint32_t bytes);   /* "41.2" (KiB), "1024" at and above 1000 KiB */

ui_diag_line_t ui_diag_wifi_line(const ui_diag_view_t *v);
ui_diag_line_t ui_diag_api_line(const ui_diag_view_t *v);
/** `onboard`: the ICM42670 is on the board, so its failure is an alarm; the
 *  AHT30 is on the optional SENSOR dock, so its failure is a caution. */
ui_diag_line_t ui_diag_src_line(ui_diag_src_t s, uint64_t age_ms, bool onboard);

ui_diag_sev_t ui_diag_heap_min_sev(const ui_diag_view_t *v);
ui_diag_sev_t ui_diag_heap_block_sev(const ui_diag_view_t *v);

#ifdef __cplusplus
}
#endif
