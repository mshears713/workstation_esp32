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
    lv_obj_set_style_bg_color(obj, lv_palette_darken(LV_PALETTE_BLUE_GREY, 3), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_palette_darken(LV_PALETTE_BLUE, 2), 0);
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
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 104, 12);
}

/* Four corners rather than a bottom bar. That frees the whole middle of
 * the screen for the listening widget, which is what HOME is actually
 * for, and it gives the fourth page somewhere to live now that TALK is
 * no longer a button - tapping the microphone does that job.
 *
 * 96x52 in each corner with montserrat_20 labels - these are read and
 * hit at arm's length, so the text carries as much as the target size
 * does. The pages are full-screen underneath
 * rather than squeezed into the gap between them. That matters because
 * HOME is a circle: the corners of its bounding box are transparent, so
 * big corner buttons and a big microphone can share the same 320x240
 * without touching. SENS and LOG do have rectangular content, so they
 * keep theirs inside y=48..192 by their own offsets. */
void ui_nav_create(lv_obj_t *scr, lv_obj_t *out[UI_NAV_COUNT], const lv_event_cb_t cbs[UI_NAV_COUNT])
{
    static const struct {
        const char *label;
        lv_align_t align;
        int x;
        int y;
    } defs[UI_NAV_COUNT] = {
        [UI_NAV_HOME] = { "HOME", LV_ALIGN_TOP_LEFT,      2,  2 },
        [UI_NAV_SENS] = { "SENS", LV_ALIGN_TOP_RIGHT,    -2,  2 },
        [UI_NAV_LOG]  = { "LOG",  LV_ALIGN_BOTTOM_LEFT,   2, -2 },
        [UI_NAV_SET]  = { "SET",  LV_ALIGN_BOTTOM_RIGHT, -2, -2 },
    };
    for (int i = 0; i < UI_NAV_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_set_size(btn, 96, 52);
        lv_obj_align(btn, defs[i].align, defs[i].x, defs[i].y);
        if (cbs && cbs[i]) {
            lv_obj_add_event_cb(btn, cbs[i], LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, defs[i].label);
        lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_20, 0);
        lv_obj_center(btn_label);

        out[i] = btn;
    }
}

void ui_nav_set_active(lv_obj_t *const nav[UI_NAV_COUNT], int active_page)
{
    for (int i = 0; i < UI_NAV_COUNT; i++) {
        lv_obj_set_style_bg_color(nav[i], i == active_page ? lv_palette_main(LV_PALETTE_BLUE)
                                                           : lv_palette_main(LV_PALETTE_GREY), 0);
    }

    /* Nav visibility: all four corners on HOME, HOME alone everywhere else.
     * Four buttons on every page was too much furniture - on a 320x240 panel
     * they crowd the content the page exists to show, and three of the four
     * are always wrong for where you already are. So HOME is the hub: from it
     * you pick a destination, and from a destination the only move is back.
     * That costs one extra tap to go SENS -> LOG, which is rare, and buys the
     * whole screen back on the pages that actually need it.
     *
     * HOME stays in the top-left corner rather than moving to a "back" slot,
     * so the one button that is always present is always in the same place. */
    for (int i = 0; i < UI_NAV_COUNT; i++) {
        if (active_page == UI_NAV_HOME || i == UI_NAV_HOME) {
            lv_obj_clear_flag(nav[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(nav[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}
