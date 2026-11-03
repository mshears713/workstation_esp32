/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief One instrument dial - see ui_dial.h.
 * @details Two lv_arc objects share the face's geometry: the track and the
 *          fill. Both have their knob removed and are not clickable - this is
 *          a gauge, not a control. The sweep runs from 150 degrees (lower
 *          left) clockwise to 30 degrees (lower right), leaving the bottom 120
 *          degrees open for the scale ends.
 */

#include "ui_dial.h"

#include <string.h>

#define SWEEP 240
#define ROT 150

static lv_obj_t *arc_layer(lv_obj_t *face, int size, int w, lv_color_t color, bool track)
{
    lv_obj_t *a = lv_arc_create(face);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, size, size);
    lv_obj_center(a);
    lv_arc_set_rotation(a, ROT);
    lv_arc_set_bg_angles(a, 0, SWEEP);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, track ? DIAL_TRACK : color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, track ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, track ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

void ui_dial_create(ui_dial_t *d, lv_obj_t *parent, const ui_dial_spec_t *spec)
{
    int s = spec->size, w = spec->arc_w;

    d->face = lv_obj_create(parent);
    lv_obj_remove_style_all(d->face);
    lv_obj_set_pos(d->face, spec->x, spec->y);
    lv_obj_set_size(d->face, s, s);
    lv_obj_set_style_radius(d->face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d->face, DIAL_FACE, 0);
    lv_obj_set_style_bg_opa(d->face, LV_OPA_COVER, 0);
    lv_obj_clear_flag(d->face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(d->face, LV_OBJ_FLAG_CLICKABLE);

    int ring = s - 6;                          /* 3 px of face outside the arc */
    d->track = arc_layer(d->face, ring, w, spec->color, true);
    d->fill = arc_layer(d->face, ring, w, spec->color, false);

    d->tick = NULL;
    if (spec->tick_pos >= 0) {
        /* A short radial line across the arc at the reference value. */
        int32_t ang = ROT + spec->tick_pos * SWEEP / 1000;
        int32_t r_out = ring / 2 + 1, r_in = ring / 2 - w - 3;
        int32_t cx = s / 2, cy = s / 2;
        d->tick_pts[0].x = cx + (lv_trigo_cos(ang) * r_out >> LV_TRIGO_SHIFT);
        d->tick_pts[0].y = cy + (lv_trigo_sin(ang) * r_out >> LV_TRIGO_SHIFT);
        d->tick_pts[1].x = cx + (lv_trigo_cos(ang) * r_in >> LV_TRIGO_SHIFT);
        d->tick_pts[1].y = cy + (lv_trigo_sin(ang) * r_in >> LV_TRIGO_SHIFT);
        d->tick = lv_line_create(d->face);
        lv_obj_remove_style_all(d->tick);
        lv_line_set_points(d->tick, d->tick_pts, 2);
        lv_obj_set_style_line_width(d->tick, 2, 0);
        lv_obj_set_style_line_color(d->tick, DIAL_TEXT, 0);
        lv_obj_clear_flag(d->tick, LV_OBJ_FLAG_CLICKABLE);
    }

    d->value = text(d->face, "--", spec->value_font, DIAL_TEXT);
    lv_obj_align(d->value, LV_ALIGN_CENTER, 0, -4);
    d->unit = text(d->face, "", &lv_font_montserrat_14, DIAL_DIM);
    lv_obj_align_to(d->unit, d->value, LV_ALIGN_OUT_BOTTOM_MID, 0, -2);

    /* Scale ends sit in the open bottom of the sweep, under its endpoints. */
    d->lo = text(d->face, spec->lo_text ? spec->lo_text : "", &lv_font_montserrat_12, DIAL_DIM);
    lv_obj_align(d->lo, LV_ALIGN_BOTTOM_LEFT, s / 5, -1);
    d->hi = text(d->face, spec->hi_text ? spec->hi_text : "", &lv_font_montserrat_12, DIAL_DIM);
    lv_obj_align(d->hi, LV_ALIGN_BOTTOM_RIGHT, -s / 5, -1);

    d->name = text(parent, spec->name ? spec->name : "", &lv_font_montserrat_14, DIAL_DIM);
    lv_obj_align_to(d->name, d->face, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    d->word = text(parent, "", &lv_font_montserrat_14, DIAL_DIM);
    lv_obj_align_to(d->word, d->name, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
}

void ui_dial_apply(ui_dial_t *d, const ui_dial_spec_t *spec, const ui_dial_state_t *st)
{
    lv_color_t c = st->current ? spec->color : DIAL_TICK;
    /* Off the dial: the fill runs to the end and turns amber, and the scale
     * end it ran past lights up - the number in the middle stays exact. */
    if (st->current && st->pinned) {
        c = DIAL_AMBER;
    }
    lv_obj_set_style_arc_color(d->fill, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(d->fill, st->have_fill ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_arc_set_value(d->fill, st->have_fill ? st->pos : 0);
    lv_obj_set_style_text_color(d->lo, st->pinned < 0 ? DIAL_AMBER : DIAL_DIM, 0);
    lv_obj_set_style_text_color(d->hi, st->pinned > 0 ? DIAL_AMBER : DIAL_DIM, 0);

    lv_label_set_text(d->value, st->value);
    lv_obj_set_style_text_color(d->value, st->current ? DIAL_TEXT : DIAL_DIM, 0);
    /* A long reading ("-40.0", "104.3") steps down a size rather than
     * running into the arc. Decided by length, not measured width: a live
     * number must not jump between sizes as its digits change. */
    size_t len = strlen(st->value);
    bool long_reading = len > 4 || st->value[0] == '-';
    lv_obj_set_style_text_font(d->value, long_reading ? &lv_font_montserrat_20 : spec->value_font, 0);
    bool unit = st->unit && st->unit[0];
    lv_label_set_text(d->unit, unit ? st->unit : "");
    if (unit) {
        lv_obj_clear_flag(d->unit, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(d->unit, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(d->value, LV_ALIGN_CENTER, 0, unit ? -7 : 0);
    lv_obj_align_to(d->unit, d->value, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    lv_label_set_text(d->word, st->word ? st->word : "");
    lv_obj_set_style_text_color(d->word, st->word_color, 0);
    lv_obj_align_to(d->word, d->name, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
}
