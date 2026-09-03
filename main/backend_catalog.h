/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Which repository a GO capture files its issue against, fetched
 *        from the backend rather than compiled in - see repo_selector.c.
 * @details The whole point of this module is that adding a repository is a
 *          backend edit, not a reflash. The list comes from
 *          GET /api/v1/projects (PROJECTS_PATH); the device only ever holds
 *          short ids and labels, never an "owner/name" slug and never a
 *          GitHub token. It sends the selected id back and the backend
 *          resolves it against its own allowlist.
 *
 *          Contrast project_selector.h, which is the same idea for NOTE's
 *          AI-OS routing hint but still uses a compiled-in list. The two are
 *          deliberately separate: an AI-OS project and a GitHub repository
 *          are different things chosen in different flows, and merging them
 *          would invite sending one where the other belongs.
 *
 *          App-agnostic like project_selector.h - knows nothing about LVGL.
 *          status_deck_ui.c owns the on-screen scroller.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sized against the backend's own limits (projects_catalog.MAX_ID_LEN 32,
 * MAX_LABEL_LEN 12) plus a nul. Entries that would not fit are dropped at
 * parse time rather than truncated - a half-written id would resolve to
 * nothing on the backend anyway. */
#define REPO_ID_LEN 33
#define REPO_LABEL_LEN 13

/* A ceiling, not an expectation. Bounded so a mistake in the catalog cannot
 * make the device allocate without limit; the scroller is also unusable long
 * before this. */
#define REPO_MAX_COUNT 12

/**
 * Starts the background fetch. Safe to call once, after wifi_mgr_init().
 * Retries on its own until the catalog loads, then stops - this is
 * configuration, not telemetry, so it does not need re-polling.
 */
void repo_selector_init(void);

/** How many repositories are available. 0 until the catalog loads. */
int repo_selector_count(void);

/**
 * Short label for the current selection, e.g. "WKSTN". Never NULL - returns
 * "..." while the catalog has not loaded yet and "NONE" if it loaded empty,
 * so the UI always has something honest to render.
 */
const char *repo_selector_get_label(void);

/**
 * Catalog id for the current selection, e.g. "workstation". Never NULL;
 * empty string when nothing is selectable, which issue_client treats as
 * "do not submit".
 */
const char *repo_selector_get_id(void);

/** Moves the selection, wrapping. No-op when the catalog is empty. */
void repo_selector_next(void);
void repo_selector_prev(void);

/** True once the catalog has been fetched successfully at least once. */
bool repo_selector_ready(void);

#ifdef __cplusplus
}
#endif
