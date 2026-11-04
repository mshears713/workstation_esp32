/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Artifacts & Instruments HOME: the interactive Orlando scene.
 * @details Pure LVGL (the simulator builds it). House = main menu, mouse =
 *          guide, van = Tour A at Egypt, plus the radar welcome card. No
 *          weather and no microphone in the exhibit build.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Kept so callers own one HOME instance; the scene keeps its objects itself. */
typedef struct {
    lv_obj_t *page;
} ui_home_t;

/* `talk_cb` is unused in the exhibit build (no microphone); pass NULL. */
void ui_home_build(ui_home_t *h, lv_obj_t *page, lv_event_cb_t talk_cb, lv_event_cb_t menu_cb);

/* Orlando scene owns one timer; suspend it behind pages or tour overlays. */
void ui_home_set_active(bool active);
/* home_cb: unused; tour_cb: the van has left (open Tour A at Egypt);
 * intro_cb: the mouse was tapped (the written guide is already open). */
void ui_home_set_actions(lv_event_cb_t home_cb, lv_event_cb_t tour_cb, lv_event_cb_t intro_cb);
/* Radar welcome card. No-op unless HOME is idle (no guide open, van parked). */
void ui_home_show_welcome(void);
/* Kept for callers of the 1.4 mouse-intro audio path: the guide text is
 * complete whether or not audio plays. */
void ui_home_intro_audio_result(bool accepted);
typedef struct {
 bool active, menu_open, intro_open, sign_visible, wheel_running, castle_running, launch_running, van_running, van_door_open, castle_guest_visible, princess_visible;
 bool drawbridge_down;
 unsigned timers, intro_page;
 int wheel_angle, car_x, peek_x, mouse_x, van_x, princess_y, bridge_frame;
} ui_home_state_t;
ui_home_state_t ui_home_state(void);

#ifdef __cplusplus
}
#endif
