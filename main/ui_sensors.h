/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief SENSORS page as a dial cluster: acceleration, temperature, humidity,
 *        each a dial with its recent trend underneath. Pure LVGL;
 *        status_deck_ui.c fills it from the real sensors,
 *        tools/uisim/targets/sensors.c from fixtures.
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
} ui_sens_reading_t;

typedef struct {
    ui_sens_reading_t r[UI_SENS_COUNT];
} ui_sensors_view_t;

typedef struct {
    ui_dial_t dial[UI_SENS_COUNT];
    ui_dial_spec_t spec[UI_SENS_COUNT];
    lv_obj_t *chart[UI_SENS_COUNT];
    lv_chart_series_t *series[UI_SENS_COUNT];
} ui_sensors_page_t;

void ui_sensors_build(ui_sensors_page_t *p, lv_obj_t *page);
/** Current state of all three: dials, words, and the trend's colour. */
void ui_sensors_apply(ui_sensors_page_t *p, const ui_sensors_view_t *v);
/** One new sample onto a trend (call at the sensor's acquisition rate, so the
 *  trend's width is time: 10 s for acceleration, 60 s for the others). */
void ui_sensors_push(ui_sensors_page_t *p, ui_sens_id_t id, int32_t value);

#ifdef __cplusplus
}
#endif
