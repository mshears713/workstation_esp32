/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief SENSORS page model - see ui_sensor_model.h.
 */

#include "ui_sensor_model.h"

#include <stdio.h>

static const ui_sens_info_t info[UI_SENS_COUNT] = {
    [UI_SENS_ACCEL] = { "ACCEL", "g", 0, 200, 100, 2, 100 },
    [UI_SENS_TEMP]  = { "TEMP", "\xC2\xB0" "F", 600, 900, INT32_MIN, 1, 60 },
    [UI_SENS_HUMI]  = { "HUMIDITY", "%", 0, 100, INT32_MIN, 0, 60 },
};

const ui_sens_info_t *ui_sens_info(ui_sens_id_t id)
{
    return &info[id < UI_SENS_COUNT ? id : 0];
}

void ui_sens_fmt_value(ui_sens_id_t id, int32_t v, char *b, size_t n)
{
    const ui_sens_info_t *s = ui_sens_info(id);
    int64_t x = v;
    const char *sign = x < 0 ? "-" : "";
    uint64_t m = (uint64_t)(x < 0 ? -x : x);
    if (s->decimals == 2) {
        snprintf(b, n, "%s%llu.%02llu", sign, (unsigned long long)(m / 100), (unsigned long long)(m % 100));
    } else if (s->decimals == 1) {
        snprintf(b, n, "%s%llu.%llu", sign, (unsigned long long)(m / 10), (unsigned long long)(m % 10));
    } else {
        snprintf(b, n, "%s%llu", sign, (unsigned long long)m);
    }
}

void ui_sens_fmt_age(uint64_t ms, char *b, size_t n)
{
    uint64_t s = ms / 1000u;
    if (s < 1u) {
        snprintf(b, n, "<1 s");
    } else if (s < 60u) {
        snprintf(b, n, "%u s", (unsigned)s);
    } else if (s < 3600u) {
        snprintf(b, n, "%u m", (unsigned)(s / 60u));
    } else if (s < 86400u) {
        snprintf(b, n, "%u h", (unsigned)(s / 3600u));
    } else {
        snprintf(b, n, "%llu d", (unsigned long long)(s / 86400u));
    }
}

int32_t ui_sens_dial_pos(ui_sens_id_t id, int32_t v, int *pinned)
{
    const ui_sens_info_t *s = ui_sens_info(id);
    int p = v < s->scale_lo ? -1 : v > s->scale_hi ? 1 : 0;
    if (pinned) {
        *pinned = p;
    }
    if (p < 0) {
        return 0;
    }
    if (p > 0) {
        return 1000;
    }
    return (int32_t)((int64_t)(v - s->scale_lo) * 1000 / (s->scale_hi - s->scale_lo));
}

int32_t ui_sens_fixed(ui_sens_id_t id, float v)
{
    static const float mul[3] = { 1.0f, 10.0f, 100.0f };
    float x = v * mul[ui_sens_info(id)->decimals];
    return (int32_t)(x < 0 ? x - 0.5f : x + 0.5f);
}
