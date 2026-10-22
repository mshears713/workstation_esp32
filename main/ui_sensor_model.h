/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief SENSORS page model: fixed-point readings, their dial scales, a small
 *        recent-history ring per sensor, and the text the page shows.
 * @details Plain C (snprintf only - no LVGL, no ESP-IDF), so the firmware, the
 *          simulator adapter and tools/hosttest share one set of rules.
 *          Freshness comes from ui_diag_freshness() (ui_diag_format.h) with the
 *          firmware's own thresholds, so SENSORS and DIAGNOSTICS can never
 *          disagree about whether a reading is current.
 *
 *          Units and scales, and where each comes from:
 *            ACCEL  |a| in centi-g. The ICM42670 is set to +-2 g per axis
 *                   (sensor_hub's icm42670 impl, ACCE_FS_2G), so |a| can
 *                   reach ~3.46 g. The dial spans 0-2 g so 1 g (at rest,
 *                   any orientation) sits at the top; past 2 g it pins and
 *                   says so. The number is never clamped.
 *            TEMP   deci-degrees F (the AHT30 reports C; status_deck_ui.c
 *                   converts once). Dial 60-90 F: the indoor band the old
 *                   chart used (Mission 19). Outside it the dial pins.
 *            HUMI   whole percent RH, 0-100.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { UI_SENS_ACCEL = 0, UI_SENS_TEMP, UI_SENS_HUMI, UI_SENS_COUNT } ui_sens_id_t;

typedef struct {
    const char *name;     /* "ACCEL" */
    const char *unit;     /* "g", "°F", "%" */
    int32_t scale_lo;     /* dial range, fixed-point like the readings */
    int32_t scale_hi;
    int32_t rest;         /* a reference tick (1 g at rest) or INT32_MIN for none */
    int decimals;         /* fixed-point decimals: 2, 1, 0 */
    uint32_t window_s;    /* how much history the recent-range band covers */
} ui_sens_info_t;

const ui_sens_info_t *ui_sens_info(ui_sens_id_t id);

/** Ring of the last `cap` samples. The caller owns `buf` (PSRAM in the
 *  firmware). Samples arrive at the sensor's acquisition rate, so cap fixes
 *  the time window: 100 at 10 Hz = 10 s (ACCEL), 60 at 1 Hz = 60 s (env). */
typedef struct {
    int32_t *buf;
    uint16_t cap;
    uint16_t len;
    uint16_t head;   /* next write index */
} ui_sens_hist_t;

void ui_sens_hist_init(ui_sens_hist_t *h, int32_t *buf, uint16_t cap);
void ui_sens_hist_push(ui_sens_hist_t *h, int32_t v);
/** Lowest and highest sample held; false while empty. */
bool ui_sens_hist_range(const ui_sens_hist_t *h, int32_t *lo, int32_t *hi);

/** "1.01", "76.3", "48" - the number alone, sign kept, never clamped. */
void ui_sens_fmt_value(ui_sens_id_t id, int32_t v, char *b, size_t n);

/** Where on its dial a value sits, 0..1000 (per mille), clamped; *pinned
 *  says it was outside the dial (-1 below, +1 above, 0 inside). */
int32_t ui_sens_dial_pos(ui_sens_id_t id, int32_t v, int *pinned);

/** Float reading -> fixed-point for that sensor, rounded half away from 0. */
int32_t ui_sens_fixed(ui_sens_id_t id, float v);

#ifdef __cplusplus
}
#endif
