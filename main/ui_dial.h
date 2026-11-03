/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief One instrument dial (gallery concept 06 Dial Cluster). Pure LVGL.
 * @details A 240-degree flat arc on a dark face that fills to the reading, a
 *          short tick at a reference value (1 g at rest), the scale's ends in
 *          the open bottom of the sweep, the number in the middle, and a name
 *          and state word underneath. Positions are per mille of the sweep
 *          (0..1000), so the dial knows nothing about units -
 *          ui_sensor_model.c does that.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dial Cluster palette (gallery firmware/06-dials notes) */
#define DIAL_FACE    lv_color_hex(0x14171C)
#define DIAL_TRACK   lv_color_hex(0x2A3039)
#define DIAL_TICK    lv_color_hex(0x8D96A1)
#define DIAL_TEXT    lv_color_hex(0xE8EAED)
#define DIAL_DIM     lv_color_hex(0xA7AFB9)   /* captions; ~8:1 on the face */
#define DIAL_CYAN    lv_color_hex(0x38C6FF)
#define DIAL_AMBER   lv_color_hex(0xFFB020)
#define DIAL_RED     lv_color_hex(0xFF5252)
#define DIAL_GREEN   lv_color_hex(0x3DDC84)

typedef struct {
    lv_obj_t *face;
    lv_obj_t *track;      /* full sweep, dark */
    lv_obj_t *fill;       /* 0 .. reading */
    lv_obj_t *tick;       /* reference mark (lv_line) or NULL */
    lv_obj_t *value;
    lv_obj_t *unit;
    lv_obj_t *name;
    lv_obj_t *word;
    lv_obj_t *lo;
    lv_obj_t *hi;
    lv_point_precise_t tick_pts[2];
} ui_dial_t;

typedef struct {
    int x, y, size, arc_w;           /* the face's box in the parent */
    const lv_font_t *value_font;     /* steps down to 20 when a reading is too wide */
    const char *name;                /* under the dial */
    const char *lo_text, *hi_text;   /* scale ends, printed in the bottom gap */
    int32_t tick_pos;                /* per mille, or -1 for no reference tick */
    lv_color_t color;                /* the reading's colour when current */
} ui_dial_spec_t;

typedef struct {
    const char *value;   /* "1.01" or "--" */
    const char *unit;    /* "g"; "" hides it */
    bool have_fill;      /* false: no reading to show, track only */
    int32_t pos;         /* per mille */
    int pinned;          /* -1 / +1: the reading is off the dial at that end */
    const char *word;    /* "LIVE", "STALE 4 s" ... */
    lv_color_t word_color;
    bool current;        /* false: drawn in grey - a held or absent reading */
} ui_dial_state_t;

void ui_dial_create(ui_dial_t *d, lv_obj_t *parent, const ui_dial_spec_t *spec);
void ui_dial_apply(ui_dial_t *d, const ui_dial_spec_t *spec, const ui_dial_state_t *st);

#ifdef __cplusplus
}
#endif
