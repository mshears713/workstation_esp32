/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief SENSORS page as a dial cluster: acceleration, temperature, humidity.
 *        Pure LVGL; status_deck_ui.c fills ui_sensors_view_t from the real
 *        sensors, tools/uisim/targets/sensors.c from fixtures.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui_dial.h"
#include "ui_diag_format.h"
#include "ui_sensor_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ui_diag_src_t src;   /* ui_diag_freshness(): unknown / failed / waiting / live / stale */
    uint64_t age_ms;     /* since the last good sample */
    bool have_value;     /* a sample has arrived this boot (kept while stale) */
    int32_t value;       /* fixed-point, see ui_sensor_model.h */
    bool have_range;     /* recent low/high available */
    int32_t lo, hi;
} ui_sens_reading_t;

typedef struct {
    ui_sens_reading_t r[UI_SENS_COUNT];
} ui_sensors_view_t;

typedef struct {
    ui_dial_t dial[UI_SENS_COUNT];
    ui_dial_spec_t spec[UI_SENS_COUNT];
} ui_sensors_page_t;

void ui_sensors_build(ui_sensors_page_t *p, lv_obj_t *page);
void ui_sensors_apply(ui_sensors_page_t *p, const ui_sensors_view_t *v);

/** What one dial shows for a reading: number, fill, band, state word.
 *  `value_buf`/`word_buf` hold the strings the state points at. Exposed for
 *  layout experiments that place the same dials differently. */
void ui_sensors_dial_state(ui_sens_id_t id, const ui_sens_reading_t *r, ui_dial_state_t *st,
                           char *value_buf, size_t value_len, char *word_buf, size_t word_len);

#ifdef __cplusplus
}
#endif
