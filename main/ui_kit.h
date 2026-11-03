/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief The shared visual furniture every page uses: the full-screen page
 *        container, the backing panel, the page title, and the back button.
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

/** A full-screen (320x240), transparent, non-scrolling page container. The
 *  back button floats above it; each page keeps its content clear of it. */
lv_obj_t *ui_page_create(lv_obj_t *scr);

/** Bordered, filled "block" look used behind grouped content. */
void ui_panel_style(lv_obj_t *obj);

/** A 304px-wide backing panel at y, created first so it sits behind the
 *  page's content. */
lv_obj_t *ui_page_panel(lv_obj_t *page, int y, int h);

/** A sub-page's name, in the band beside the HOME button. */
void ui_page_title(lv_obj_t *page, const char *text);

/** 1.3.0: the one nav control left - a 96x52 "< HOME" button in the top-left
 *  corner of `scr`, shown on every page except HOME (the caller hides it
 *  there). HOME reaches everything else through MENU. */
lv_obj_t *ui_back_button_create(lv_obj_t *scr, lv_event_cb_t cb);

#ifdef __cplusplus
}
#endif
