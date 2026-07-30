/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Reusable animated "listening" indicator: a rotating blue outer ring
 *        with a bright cyan-toward-white highlight traveling around it, a
 *        thin static light-blue inner ring, a subtle pale-blue halo, and a
 *        dark navy disc with a white microphone icon at the center. No text
 *        of its own - purely the ring/disc/icon graphic.
 * @details Purely decorative and stateless - unlike every render_*_panel in
 *          status_deck_ui.c, it never reads voice_control_get_status() or
 *          any other app state, and animates continuously once created
 *          rather than reacting to anything. See voice_listening_widget.c
 *          for the full layer breakdown.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates one VoiceListeningWidget as a child of `parent` and starts its
 * rotation animation immediately - no separate "start" call needed.
 * @param parent    the widget's parent object
 * @return          the widget's own top-level container (a plain lv_obj_t,
 *                  exactly the ring's own bounding box). Align/position it
 *                  the same way as any other lv_obj_t; nothing else about
 *                  it needs to be touched afterward.
 */
lv_obj_t *voice_listening_widget_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
