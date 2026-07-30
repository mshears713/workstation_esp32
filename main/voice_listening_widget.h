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
 *
 *          One exception to "stateless": voice_listening_widget_set_notification()
 *          below lets the caller (status_deck_ui.c, driven by
 *          notification_client_get_status()) recolor the outer ring between
 *          its default blue and orange, to indicate a pending low-confidence
 *          notification without reading anything aloud - see that
 *          function's own doc comment.
 */
#pragma once

#include <stdbool.h>
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

/**
 * Recolors the outer ring - blue (pending=false, the default set at
 * creation) or orange (pending=true) - to indicate whether a low-confidence
 * notification is waiting. Only the thick background ring changes; the
 * traveling cyan-toward-white highlight is untouched, so the "still
 * listening" animation reads the same either way. Cheap enough to call on
 * every UI tick regardless of whether the value actually changed - same
 * "unconditionally re-set the style" convention every render_*_panel in
 * status_deck_ui.c already follows.
 * @param widget   the object returned by voice_listening_widget_create()
 * @param pending  true to show orange, false to show the default blue
 */
void voice_listening_widget_set_notification(lv_obj_t *widget, bool pending);

#ifdef __cplusplus
}
#endif
