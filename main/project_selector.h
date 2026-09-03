/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief The operator's "which project is this for" hint, as sent to the
 *        backend - see project_selector.c.
 * @details Mission 19 moved the list itself into backend_catalog.h, which
 *          fetches it from the AI-OS Projects database. What used to be a
 *          four-value enum compiled into the firmware (NONE/VAN1/VAN2/
 *          GENERAL) is now whatever is Active or Testing in the AI-OS, and
 *          changing it needs no flash.
 *
 *          What survives here is the one thing the capture clients actually
 *          want: the hint string. entry_client.c (GO) and
 *          voice_inbox_client.c (NOTE/SEND) call project_selector_get_hint()
 *          when building their finish fields, and this file exists so that
 *          neither has to know where the list came from or care that it
 *          changed.
 *
 *          The hint is advisory, not routing. The note lands in the Voice
 *          Inbox either way; the backend resolves the id to a Notion page and
 *          sets the Related Project relation so the downstream agent knows
 *          what the operator had in mind. "none" is a normal answer, not a
 *          failure - see the backend's README.
 *
 *          The selection itself lives in backend_catalog.c and, like the
 *          volume control, is session-only: it resets at boot rather than
 *          being persisted.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The currently selected project's catalog id, or the literal "none" when
 * nothing is selectable or nothing has been chosen. Never NULL, and always
 * present in the request, so the backend never has to guess between "not
 * chosen" and "not sent".
 */
const char *project_selector_get_hint(void);

#ifdef __cplusplus
}
#endif
