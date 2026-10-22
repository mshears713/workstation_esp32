/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief HOME (1.3.0): Orlando weather down the left, the microphone (link
 *        status + tap to record) top-right, MENU bottom-right. Pure LVGL - see
 *        ui_kit.h for why that matters.
 * @details status_deck_ui.c fills a ui_home_weather_t from weather_client and
 *          calls ui_home_apply_weather(); the simulator's HOME target fills the
 *          same struct from fixtures. Link state goes through
 *          ui_home_set_link(), which drives both the widget and the caption
 *          under it.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "voice_listening_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_HOME_SLOTS 2   /* +2 h and +6 h */

/** What the weather column shows. Strings are borrowed for the call only. */
typedef struct {
    bool valid;                 /* false: "--" and `status` instead */
    const char *status;         /* when !valid: "Loading weather" / "No weather" */
    const char *location;       /* "Orlando" */
    const char *stale_as_of;    /* non-NULL: data is old, show "as of 11:40 PM" */
    int now_temp;
    const char *now_icon;       /* sun moon partly cloud fog rain storm snow */
    const char *now_text;       /* "Partly cloudy" */
    int now_rain;
    struct {
        const char *label;      /* "3 PM" */
        int temp;
        const char *icon;
        int rain;
    } slot[UI_HOME_SLOTS];
    int tmrw_hi;
    int tmrw_lo;
    const char *tmrw_icon;
    int tmrw_rain;
} ui_home_weather_t;

/** HOME's live objects, owned by the caller (status_deck_ui.c keeps one). */
typedef struct {
    lv_obj_t *mic;              /* the listening widget - also the TALK button */
    lv_obj_t *link_label;
    lv_obj_t *now_icon;
    lv_obj_t *now_temp;
    lv_obj_t *now_text;
    lv_obj_t *now_where;        /* location, or "as of ..." when stale */
    lv_obj_t *now_rain;
    struct {
        lv_obj_t *label;
        lv_obj_t *icon;
        lv_obj_t *temp;
        lv_obj_t *rain;
    } row[UI_HOME_SLOTS + 1];   /* the two slots, then tomorrow */
    lv_obj_t *tmrw_lo;
} ui_home_t;

/** Builds HOME into `page`. `talk_cb` fires on a tap of the microphone,
 *  `menu_cb` on MENU (either may be NULL in the simulator). Starts with the
 *  weather in its "Loading weather" state. */
void ui_home_build(ui_home_t *h, lv_obj_t *page, lv_event_cb_t talk_cb, lv_event_cb_t menu_cb);

void ui_home_apply_weather(ui_home_t *h, const ui_home_weather_t *w);

/** Backend reachability: the ring/mic colour and the caption under it. */
void ui_home_set_link(ui_home_t *h, voice_widget_link_t link);

#ifdef __cplusplus
}
#endif
