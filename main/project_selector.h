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
 *          nothing about LVGL. status_deck_ui.c owns the four VAN1/VAN2/
 *          GEN/NONE buttons and calls project_selector_set() when one is
 *          tapped; entry_client.c calls project_selector_get_hint() to
 *          read the current selection when building a GO upload's
 *          multipart fields. Session-only, like the volume control -
 *          resets to the default on reboot, not persisted to NVS.
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
