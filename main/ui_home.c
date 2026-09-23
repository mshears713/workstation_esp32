/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ui_home.h"
#include "voice_listening_widget.h"

/* Stripped back deliberately. The title, uptime and Wi-Fi row, the
 * project selector and the volume control all moved off (title dropped,
 * the rest to SETTINGS) so this page is one thing: a large microphone
 * that shows whether the workstation is listening and reachable.
 *
 * Tapping it is what TALK used to do. A dedicated TALK button in the
 * nav made sense when the nav was a bar along the bottom; with the
 * microphone filling the page, the microphone *is* the button. */
lv_obj_t *ui_home_build(lv_obj_t *page, lv_event_cb_t talk_cb)
{
    lv_obj_t *widget = voice_listening_widget_create(page);
    lv_obj_align(widget, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(widget, LV_OBJ_FLAG_CLICKABLE);
    if (talk_cb) {
        lv_obj_add_event_cb(widget, talk_cb, LV_EVENT_CLICKED, NULL);
    }
    return widget;
}
