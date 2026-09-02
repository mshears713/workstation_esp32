/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Current "which project is this for" selection - see
 *        project_selector.h.
 */

#include "project_selector.h"

/* volatile because this is written from the LVGL task (the on-screen
 * selector) and read from the audio worker task (when a client builds its
 * finish body). A single enum-sized word is atomic on this target, so no
 * lock is needed - but without volatile the compiler is free to cache the
 * read, and the reader would keep seeing a stale selection. Every other
 * cross-task value in this firmware is already either volatile or under a
 * portMUX; this one was the exception. */
static volatile project_selection_t s_selection = PROJECT_SELECTION_NONE;

void project_selector_set(project_selection_t selection)
{
    s_selection = selection;
}

project_selection_t project_selector_get(void)
{
    return s_selection;
}

const char *project_selector_get_hint(void)
{
    switch (s_selection) {
        case PROJECT_SELECTION_VAN1:
            return "van1";
        case PROJECT_SELECTION_VAN2:
            return "van2";
        case PROJECT_SELECTION_GENERAL:
            return "general";
        case PROJECT_SELECTION_NONE:
        default:
            return "none";
    }
}
