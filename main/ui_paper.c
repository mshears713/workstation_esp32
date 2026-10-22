/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Calm Paper keys, text and rules - see ui_paper.h.
 */

#include "ui_paper.h"
#include "ui_theme.h"

lv_obj_t *ui_paper_sheet(lv_obj_t *scr)
{
    lv_obj_t *sheet = lv_obj_create(scr);
    lv_obj_remove_style_all(sheet);
    lv_obj_set_size(sheet, 320, 240);
    lv_obj_set_pos(sheet, 0, 0);
    lv_obj_set_style_bg_color(sheet, PAPER_BG, 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    /* Clickable on purpose: a tap that misses every key lands here instead
     * of falling through to whatever page is underneath. */
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
    return sheet;
}

lv_obj_t *ui_paper_text(lv_obj_t *parent, const char *s, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

void ui_paper_key_set_filled(lv_obj_t *key, bool filled)
{
    lv_obj_t *label = lv_obj_get_child(key, 0);
    lv_obj_set_style_bg_color(key, filled ? PAPER_INK : PAPER_BG, 0);
    lv_obj_set_style_bg_color(key, filled ? lv_color_hex(0x3A3733) : PAPER_PRESSED, LV_STATE_PRESSED);
    if (label) {
        lv_obj_set_style_text_color(label, filled ? PAPER_BG : PAPER_INK, 0);
    }
}

void ui_paper_key_set_enabled(lv_obj_t *key, bool enabled)
{
    lv_obj_t *label = lv_obj_get_child(key, 0);
    if (enabled) {
        lv_obj_clear_state(key, LV_STATE_DISABLED);
        if (label) {
            lv_obj_clear_state(label, LV_STATE_DISABLED);
        }
    } else {
        lv_obj_add_state(key, LV_STATE_DISABLED);
        if (label) {
            lv_obj_add_state(label, LV_STATE_DISABLED);
        }
    }
}

lv_obj_t *ui_paper_key(lv_obj_t *parent, const char *text, const lv_font_t *font, bool filled,
                       lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *key = lv_button_create(parent);
    lv_obj_remove_style_all(key);
    lv_obj_set_style_radius(key, 6, 0);
    lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(key, PAPER_INK, 0);
    lv_obj_set_style_border_width(key, 2, 0);
    lv_obj_set_style_border_color(key, PAPER_HAIRLINE, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(key, PAPER_BG, LV_STATE_DISABLED);
    lv_obj_clear_flag(key, LV_OBJ_FLAG_SCROLLABLE);
    if (cb) {
        lv_obj_add_event_cb(key, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *label = lv_label_create(key);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, PAPER_FADED, LV_STATE_DISABLED);
    lv_obj_center(label);
    ui_paper_key_set_filled(key, filled);
    return key;
}

lv_obj_t *ui_paper_rule(lv_obj_t *parent, int x, int y, int w, lv_color_t color)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_size(r, w, 1);
    lv_obj_set_style_bg_color(r, color, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
    return r;
}
