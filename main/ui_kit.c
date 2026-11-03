/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Page container, panel, title and corner nav - moved out of
 *        status_deck_ui.c unchanged so the host simulator can build them too.
 *        See ui_kit.h.
 */

#include "ui_kit.h"
#include "ui_theme.h"

lv_obj_t *ui_page_create(lv_obj_t *scr)
{
    lv_obj_t *page = lv_obj_create(scr);
    /* Full screen. The corner buttons float on top; each page is
     * responsible for keeping its own content clear of them. */
    lv_obj_set_size(page, 320, 240);
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

/* The panel look, in one function.
 *
 * It started on GO's repository chooser, which read better than everything
 * around it - a bordered, filled block with its content grouped inside,
 * rather than labels floating on the page background. Applied per page it
 * would drift the way the command hints did, so it lives here and the pages
 * call it.
 *
 * Used as a *backing* panel: created before a page's content and never
 * reparenting it, so existing layout coordinates keep working and this is
 * purely a visual layer behind them. */
void ui_panel_style(lv_obj_t *obj)
{
    /* 1.3.0 dark look: charcoal block on a black screen, no border. The old
     * blue-grey panel was the backdrop behind the ~1.6:1 FIRMWARE text. */
    lv_obj_set_style_bg_color(obj, UI_PANEL, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

/* A backing panel filling a page's safe band - the region clear of the
 * corner nav buttons. Created first so it sits behind everything the page
 * adds afterwards. */
lv_obj_t *ui_page_panel(lv_obj_t *page, int y, int h)
{
    lv_obj_t *panel = lv_obj_create(page);
    lv_obj_set_size(panel, 304, h);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, y);
    ui_panel_style(panel);
    return panel;
}

/* A sub-page's name, sitting in the band beside the HOME button.
 *
 * It earns its place because of what the nav change took away: with only HOME
 * showing on a sub-page, nothing else says which page you are on - the
 * highlighted tab that used to answer that is gone with the other three
 * buttons. SETTINGS already had a centred title of its own; this makes it the
 * rule rather than the exception, and puts it where the corner button leaves a
 * gap instead of competing with the content below.
 *
 * x=104 clears the 96px-wide button plus a margin; y=12 centers a 32px line
 * against the button's own y 2..54. Content on every page starts at y=58,
 * below both. */
void ui_page_title(lv_obj_t *page, const char *text)
{
    lv_obj_t *label = lv_label_create(page);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(label, UI_TITLE, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 104, 12);
}

/* 1.3.0: the four corner buttons are gone - HOME is now the weather and the
 * microphone, and everything else is one tap away through MENU. What is left
 * is the way back, in the same top-left corner (and at the same 96x52, with a
 * montserrat_20 label read at arm's length) that HOME always occupied, so the
 * one button present on every sub-page is still where the hand expects it.
 * From a sub-page the only move is home: SENS -> LOG is HOME, MENU, LOG. */
lv_obj_t *ui_back_button_create(lv_obj_t *scr, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 96, 52);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_set_style_bg_color(btn, UI_ACCENT, 0);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, LV_SYMBOL_LEFT " HOME");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(btn_label, UI_TEXT, 0);
    lv_obj_center(btn_label);
    return btn;
}
