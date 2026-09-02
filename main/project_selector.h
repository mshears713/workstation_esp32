/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Current "which project is this for" selection - see
 *        project_selector.c.
 * @details App-agnostic like audio_playback.h/notification_client.h: knows
 *          nothing about LVGL. status_deck_ui.c owns the on-screen
 *          up/down selector - one of the four values shown at a time, not
 *          four separate buttons as this said previously - and calls
 *          project_selector_set() when it changes. It appears both on HOME
 *          and on the recording overlay, so a project can be chosen either
 *          before starting or while a NOTE is running.
 *
 *          entry_client.c (GO) and voice_inbox_client.c (NOTE/SEND) both
 *          call project_selector_get_hint() when building their finish
 *          fields; it returns "none" when nothing is selected, so the
 *          field is always present and the backend never has to guess
 *          between "not chosen" and "not sent".
 *
 *          Session-only, like the volume control - resets to the default
 *          on reboot, not persisted to NVS.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROJECT_SELECTION_NONE = 0, /* default: not part of the van-build project */
    PROJECT_SELECTION_VAN1,
    PROJECT_SELECTION_VAN2,
    PROJECT_SELECTION_GENERAL,
} project_selection_t;

/** Sets the current selection. Called from the LVGL task (button taps). */
void project_selector_set(project_selection_t selection);

/** Current selection - defaults to PROJECT_SELECTION_NONE at boot. */
project_selection_t project_selector_get(void);

/**
 * The current selection as the short lowercase string sent to the backend's
 * /api/v1/entries `project_hint` form field ("none"/"van1"/"van2"/
 * "general") - see entry_client.c.
 */
const char *project_selector_get_hint(void);

#ifdef __cplusplus
}
#endif
