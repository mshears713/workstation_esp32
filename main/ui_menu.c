/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ui_menu.h"

#include "ui_kit.h"
#include "ui_theme.h"

/* The spec asked for a scrollable list. Five destinations fit on one screen
 * as 150x54 buttons, which is better at arm's length than a list you have to
 * scroll: every page is one look and one tap away, and the buttons are as
 * big as the corner nav ones they replace. The sixth slot went to DIAG; past
 * six, a second page reached by a key in the title band ("MORE >") keeps
 * every button full size instead of shrinking them into a list.
 *
 *    y  58..112   row 1
 *    y 118..172   row 2
 *    y 178..232   row 3 (full width when the count is odd)
 */
#define MENU_TOP 58
#define MENU_ROW_H 54
#define MENU_GAP 6
#define MENU_BTN_W 151

void ui_menu_build(lv_obj_t *page, const char *title, const ui_menu_item_t *items, int count,
                   const ui_menu_item_t *corner)
{
    ui_page_title(page, title);

    if (corner) {
        /* Title band, right end: the same 52 px height as HOME on the left. */
        lv_obj_t *k = lv_button_create(page);
        lv_obj_set_size(k, 96, 52);
        lv_obj_set_pos(k, 320 - 2 - 96, 2);
        lv_obj_set_style_bg_color(k, UI_PANEL, 0);
        lv_obj_set_style_border_width(k, 2, 0);
        lv_obj_set_style_border_color(k, UI_ACCENT, 0);
        lv_obj_set_style_radius(k, 10, 0);
        if (corner->cb) {
            lv_obj_add_event_cb(k, corner->cb, LV_EVENT_CLICKED, NULL);
        }
        lv_obj_t *l = lv_label_create(k);
        lv_label_set_text(l, corner->label);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(l, UI_TEXT, 0);
        lv_obj_center(l);
    }

    for (int i = 0; i < count && i < 6; i++) {
        bool alone = (i == count - 1) && (count % 2 == 1);
        lv_obj_t *btn = lv_button_create(page);
        lv_obj_set_size(btn, alone ? 2 * MENU_BTN_W + MENU_GAP : MENU_BTN_W, MENU_ROW_H);
        lv_obj_set_pos(btn, 6 + (i % 2) * (MENU_BTN_W + MENU_GAP), MENU_TOP + (i / 2) * (MENU_ROW_H + MENU_GAP));
        lv_obj_set_style_bg_color(btn, UI_PANEL, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, UI_ACCENT, 0);
        lv_obj_set_style_radius(btn, 10, 0);
        if (items[i].cb) {
            lv_obj_add_event_cb(btn, items[i].cb, LV_EVENT_CLICKED, NULL);
        }
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, items[i].label);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(l, UI_TEXT, 0);
        lv_obj_center(l);
    }
}
