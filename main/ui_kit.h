/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief The shared visual furniture every page uses: the full-screen page
 *        container, the backing panel, the page title, and the four corner
 *        nav buttons.
 * @details Pure LVGL - this file (and every ui_*.c) includes nothing from
 *          ESP-IDF, the BSP or the app's subsystems. That is the whole point
 *          of the split: the same builders compile into the firmware and into
 *          the host LVGL simulator (/opt/workstation/lvgl-sim, driven by the
 *          workstation-ui MCP), so what an agent renders on CLAWBOX is built by
 *          the code the device runs, not by a hand-maintained copy.
 *
 *          The rule for anything added here: take plain values, callbacks and
 *          parent objects as arguments; never read app state. status_deck_ui.c
 *          reads the subsystems and passes what it found.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The corner buttons, in the same order as the first four app pages. */
typedef enum {
    UI_NAV_HOME = 0,
    UI_NAV_SENS,
    UI_NAV_LOG,
    UI_NAV_SET,
    UI_NAV_COUNT,
} ui_nav_slot_t;

/** A full-screen (320x240), transparent, non-scrolling page container. The
 *  corner buttons float above it; each page keeps its content clear of them. */
lv_obj_t *ui_page_create(lv_obj_t *scr);

/** Bordered, filled "block" look used behind grouped content. */
void ui_panel_style(lv_obj_t *obj);

/** A 304px-wide backing panel at y, created first so it sits behind the
 *  page's content. */
lv_obj_t *ui_page_panel(lv_obj_t *page, int y, int h);

/** A sub-page's name, in the band beside the HOME button. */
void ui_page_title(lv_obj_t *page, const char *text);

/** Creates the four 96x52 corner buttons on `scr` and stores them in `out`.
 *  `cbs[i]` is the click handler for slot i. */
void ui_nav_create(lv_obj_t *scr, lv_obj_t *out[UI_NAV_COUNT], const lv_event_cb_t cbs[UI_NAV_COUNT]);

/** Colours and shows/hides the nav for the page now on screen. `active_page`
 *  uses the app's page numbering, whose first four values match
 *  ui_nav_slot_t; pages beyond those (PROJECTS, FIRMWARE) highlight nothing.
 *  All four corners show on HOME, HOME alone everywhere else. */
void ui_nav_set_active(lv_obj_t *const nav[UI_NAV_COUNT], int active_page);

#ifdef __cplusplus
}
#endif
