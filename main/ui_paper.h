/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Calm Paper building blocks shared by the survey and the music remote.
 *        Pure LVGL (see ui_kit.h for the rule).
 * @details The look, from the gallery's 02 Calm Paper notes: warm paper, ink
 *          type, hairline rules, outlined radius-6 keys, one filled ink key
 *          for the main action. These widgets drop the default (dark) theme's
 *          styles entirely - it would otherwise paint blue buttons with a
 *          grow-on-press transform onto the paper.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Full-screen paper sheet on `scr`, above everything already there. */
lv_obj_t *ui_paper_sheet(lv_obj_t *scr);

/** A label in ink (or `color`), at a font, no theme styling. */
lv_obj_t *ui_paper_text(lv_obj_t *parent, const char *s, const lv_font_t *font, lv_color_t color);

/** A key: outlined (secondary) or filled ink (the main action). Pressed
 *  darkens it; LV_STATE_DISABLED shows it faded and it ignores taps.
 *  `cb` gets LV_EVENT_CLICKED with `user_data`. The label is child 0. */
lv_obj_t *ui_paper_key(lv_obj_t *parent, const char *text, const lv_font_t *font, bool filled,
                       lv_event_cb_t cb, void *user_data);

/** Fill or outline an existing key - a choice tile showing it is selected. */
void ui_paper_key_set_filled(lv_obj_t *key, bool filled);

/** Enable or disable a key. Disabled reads as faded outline + faded label and
 *  ignores taps. (A label does not inherit its button's state, so setting
 *  LV_STATE_DISABLED on the key alone leaves the label in its normal colour -
 *  paper-on-paper for a filled key.) */
void ui_paper_key_set_enabled(lv_obj_t *key, bool enabled);

/** A 1 px horizontal rule, ink or hairline. */
lv_obj_t *ui_paper_rule(lv_obj_t *parent, int x, int y, int w, lv_color_t color);

#ifdef __cplusplus
}
#endif
