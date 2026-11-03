/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief LOG page (gallery concept 03 Phosphor Terminal): the Black Box as
 *        terminal lines you page through, plus NET / RPT / CLR. Pure LVGL;
 *        status_deck_ui.c passes the real event ring, the simulator a fixture.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int type;              /* event_type_t, see ui_log_format.h */
    uint32_t uptime_s;     /* when, as seconds since boot */
    const char *message;   /* <= 31 characters in the firmware */
} ui_log_entry_t;

typedef struct {
    lv_event_cb_t net, rpt, clr;   /* the existing LOG commands */
} ui_log_actions_t;

#define UI_LOG_MAX_PAGES 12   /* 32 events at >= 3 lines a page: at most 11 pages */

typedef struct {
    lv_obj_t *list;          /* the lines live here, rebuilt on every show */
    lv_obj_t *position;      /* "1-7 of 32" */
    lv_obj_t *newer, *older;
    lv_obj_t *newer_label;
    lv_obj_t *perf_slot;     /* the app parks LVGL's FPS/CPU readout here */
    const ui_log_entry_t *entries;   /* newest first; borrowed until the next show */
    int count;
    uint32_t seq;            /* events recorded since boot, as of the last show */
    int offset;              /* index of the top line on screen */
    int shown;               /* how many fit on this page */
    int unseen;              /* newer events that arrived while paged back */
    int back[UI_LOG_MAX_PAGES];
    int depth;               /* page offsets to return to with NEWER */
} ui_log_page_t;

void ui_log_build(ui_log_page_t *p, lv_obj_t *page, const ui_log_actions_t *actions);

/** Shows the ring (newest first, `count` entries). `seq` counts events
 *  recorded since boot (it keeps rising past the ring's size): while you are
 *  paged back, new events do not move the page under you - they add to the
 *  NEWER key's count instead. On the first page, new lines just appear. */
void ui_log_show(ui_log_page_t *p, const ui_log_entry_t *entries, int count, uint32_t seq);

#ifdef __cplusplus
}
#endif
