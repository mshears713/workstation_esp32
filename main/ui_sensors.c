/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief SENSORS dial cluster - see ui_sensors.h.
 * @details Three equal instruments, read left to right. Chosen over a
 *          temperature-hero layout (its small dials' labels ran off the
 *          screen) and over a ring showing the recent low..high (invisible at
 *          this size); the evidence is in the run's review/sensors sheets.
 *          What each instrument says, and why:
 *            - the number is the last real sample, never clamped to the dial;
 *            - the arc fills to it; off the dial it pins, turns amber and
 *              lights that end's scale number;
 *            - the line underneath is the trend the old page's charts showed,
 *              on the dial's own scale (100 points = 10 s of acceleration,
 *              60 = 60 s of temperature/humidity);
 *            - under the name, a word from the same freshness rules
 *              DIAGNOSTICS uses: LIVE, STALE + age (number, arc and trend
 *              turn grey: a held reading is not a current one), WAITING,
 *              INIT FAILED (not "absent": a failed probe does not prove the
 *              dock is missing), UNKNOWN.
 *
 *    y  58..150   three 92 px dials at x 8 / 114 / 220
 *    y 152..186   name, state word
 *    y 194..222   trends
 */

#include "ui_sensors.h"

#include <stdio.h>
#include "ui_kit.h"

#define DIAL_SIZE 92
#define COL_PITCH 106

static void dial_state(ui_sens_id_t id, const ui_sens_reading_t *r, ui_dial_state_t *st,
                       char *value_buf, size_t value_len, char *word_buf, size_t word_len)
{
    const ui_sens_info_t *info = ui_sens_info(id);
    *st = (ui_dial_state_t){ 0 };
    bool shown = r->have_value && (r->src == UI_DIAG_SRC_LIVE || r->src == UI_DIAG_SRC_STALE);
    if (shown) {
        ui_sens_fmt_value(id, r->value, value_buf, value_len);
        st->unit = info->unit;
        st->have_fill = true;
        st->pos = ui_sens_dial_pos(id, r->value, &st->pinned);
    } else {
        snprintf(value_buf, value_len, "--");
        st->unit = "";
    }
    st->value = value_buf;
    st->current = shown && r->src == UI_DIAG_SRC_LIVE;

    /* DIAGNOSTICS' words; the age is the short form so two stale words
     * side by side stay two words. */
    if (r->src == UI_DIAG_SRC_STALE) {
        char age[12];
        ui_sens_fmt_age(r->age_ms, age, sizeof(age));
        snprintf(word_buf, word_len, "STALE %s", age);
    } else if (r->src == UI_DIAG_SRC_FAILED) {
        snprintf(word_buf, word_len, "INIT FAILED");
    } else {
        snprintf(word_buf, word_len, "%s", ui_diag_src_line(r->src, r->age_ms, id == UI_SENS_ACCEL).word);
    }
    st->word = word_buf;
    /* The IMU is on the board, so its failure is an alarm; the AHT30 is on an
     * optional dock, so its failure is a caution (as in DIAGNOSTICS). */
    st->word_color = r->src == UI_DIAG_SRC_LIVE    ? DIAL_GREEN
                   : r->src == UI_DIAG_SRC_STALE   ? DIAL_AMBER
                   : r->src == UI_DIAG_SRC_FAILED  ? (id == UI_SENS_ACCEL ? DIAL_RED : DIAL_AMBER)
                                                   : DIAL_DIM;
}

void ui_sensors_build(ui_sensors_page_t *p, lv_obj_t *page)
{
    ui_page_title(page, "SENSORS");

    static const char *const ends[UI_SENS_COUNT][2] = { { "0", "2" }, { "60", "90" }, { "0", "100" } };
    for (int i = 0; i < UI_SENS_COUNT; i++) {
        const ui_sens_info_t *info = ui_sens_info((ui_sens_id_t)i);
        int x = 8 + i * COL_PITCH;
        p->spec[i] = (ui_dial_spec_t){
            .x = x, .y = 58, .size = DIAL_SIZE, .arc_w = 7,
            .value_font = &lv_font_montserrat_32,
            .name = info->name,
            .lo_text = ends[i][0], .hi_text = ends[i][1],
            .tick_pos = info->rest == INT32_MIN ? -1 : ui_sens_dial_pos((ui_sens_id_t)i, info->rest, NULL),
            .color = DIAL_CYAN,
        };
        ui_dial_create(&p->dial[i], page, &p->spec[i]);

        lv_obj_t *c = lv_chart_create(page);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, DIAL_SIZE, 28);
        lv_obj_set_pos(c, x, 194);
        lv_obj_set_style_bg_color(c, DIAL_FACE, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(c, 4, 0);
        lv_obj_set_style_pad_ver(c, 3, 0);
        lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);   /* a line, no point markers */
        lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_chart_set_type(c, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(c, info->history);
        lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_SHIFT);
        lv_chart_set_div_line_count(c, 0, 0);
        lv_chart_set_axis_range(c, LV_CHART_AXIS_PRIMARY_Y, info->scale_lo, info->scale_hi);
        p->series[i] = lv_chart_add_series(c, DIAL_CYAN, LV_CHART_AXIS_PRIMARY_Y);
        p->chart[i] = c;
    }
}

void ui_sensors_apply(ui_sensors_page_t *p, const ui_sensors_view_t *v)
{
    for (int i = 0; i < UI_SENS_COUNT; i++) {
        char value[16], word[24];
        ui_dial_state_t st;
        dial_state((ui_sens_id_t)i, &v->r[i], &st, value, sizeof(value), word, sizeof(word));
        ui_dial_apply(&p->dial[i], &p->spec[i], &st);
        lv_chart_set_series_color(p->chart[i], p->series[i], st.current ? DIAL_CYAN : DIAL_TICK);
    }
}

void ui_sensors_push(ui_sensors_page_t *p, ui_sens_id_t id, int32_t value)
{
    lv_chart_set_next_value(p->chart[id], p->series[id], value);
}
