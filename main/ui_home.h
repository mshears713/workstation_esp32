/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief HOME's content, built into a page container. Pure LVGL - see
 *        ui_kit.h for why that matters.
 * @details What HOME shows is set afterwards through the widget's own setters
 *          (voice_listening_widget_set_link / _set_notification), called by
 *          status_deck_ui.c from real subsystem state and by the simulator's
 *          HOME target from fixture state.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Builds HOME into `page` and returns the listening widget, which is also
 *  the TALK button (`talk_cb` on click; may be NULL in the simulator). */
lv_obj_t *ui_home_build(lv_obj_t *page, lv_event_cb_t talk_cb);

#ifdef __cplusplus
}
#endif
