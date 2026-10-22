/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief SENSORS dial cluster - see ui_sensors.h.
 * @details Three equal dials, one per physical quantity, read left to right.
 *          What each dial says, and why:
 *            - the number is the last real sample, never clamped to the dial;
 *            - the arc fills to it; off the dial it pins, turns amber and
 *              lights that end's scale number;
 *            - the thin inner ring spans the recent low..high (10 s for
 *              acceleration, 60 s for temperature/humidity - the windows the
 *              old trend charts covered);
 *            - under it, a word from the same freshness rules DIAGNOSTICS
 *              uses: LIVE, STALE + age (number and arc turn grey: a held
 *              reading is not a current one), WAITING, ERROR (init failed -
 *              not "absent", a failed probe does not prove that), UNKNOWN.
 *
 *    y  58..158   three 100 px dials at x 4 / 110 / 216
 *    y 160..196   name, state word
 *    y 214..230   what the inner ring means
 */

#include "ui_sensors.h"

#include <stdio.h>
#include <string.h>
#include "ui_kit.h"
#include "ui_theme.h"

void ui_sensors_dial_state(ui_sens_id_t id, const ui_sens_reading_t *r, ui_dial_state_t *st,
                           char *value_buf, size_t value_len, char *word_buf, size_t word_len)
{
    const ui_sens_info_t *info = ui_sens_info(id);
    memset(st, 0, sizeof(*st));
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
    if (shown && r->have_range) {
        st->have_band = true;
        st->band_lo = ui_sens_dial_pos(id, r->lo, NULL);
        st->band_hi = ui_sens_dial_pos(id, r->hi, NULL);
    }

    /* The words DIAGNOSTICS uses, with the age folded in when it matters. */
    ui_diag_line_t l = ui_diag_src_line(r->src, r->age_ms, id == UI_SENS_ACCEL);
    if (r->src == UI_DIAG_SRC_STALE) {
        char age[16];
        ui_diag_fmt_age_ms(age, sizeof(age), r->age_ms);
        snprintf(word_buf, word_len, "STALE %s", age);
    } else if (r->src == UI_DIAG_SRC_FAILED) {
        snprintf(word_buf, word_len, "INIT FAILED");
    } else {
        snprintf(word_buf, word_len, "%s", l.word);
    }
    st->word = word_buf;
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
        p->spec[i] = (ui_dial_spec_t){
            .x = 4 + i * 106, .y = 58, .size = 100, .arc_w = 9,
            .value_font = &lv_font_montserrat_32,
            .name = info->name,
            .lo_text = ends[i][0], .hi_text = ends[i][1],
            .tick_pos = info->rest == INT32_MIN ? -1 : ui_sens_dial_pos((ui_sens_id_t)i, info->rest, NULL),
            .color = DIAL_CYAN,
        };
        ui_dial_create(&p->dial[i], page, &p->spec[i]);
    }

    lv_obj_t *legend = lv_label_create(page);
    lv_label_set_text(legend, "Inner ring: low to high, last 10 s / 60 s");
    lv_obj_set_style_text_font(legend, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(legend, DIAL_DIM, 0);
    lv_obj_align(legend, LV_ALIGN_TOP_MID, 0, 216);
}

void ui_sensors_apply(ui_sensors_page_t *p, const ui_sensors_view_t *v)
{
    for (int i = 0; i < UI_SENS_COUNT; i++) {
        char value[16], word[24];
        ui_dial_state_t st;
        ui_sensors_dial_state((ui_sens_id_t)i, &v->r[i], &st, value, sizeof(value), word, sizeof(word));
        ui_dial_apply(&p->dial[i], &p->spec[i], &st);
    }
}
