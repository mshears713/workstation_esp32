/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief What the workstation is allowed to select, fetched from the backend
 *        rather than compiled in - see backend_catalog.c.
 * @details Was repo_selector.h, which only held the GitHub repositories GO
 *          files against. It now holds both lists, because both arrive in the
 *          same GET /api/v1/projects response and fetching it twice would be
 *          silly:
 *
 *            projects - the AI-OS routing hint attached to a NOTE. Sourced
 *                       from the AI-OS Projects database (Active + Testing),
 *                       so starting a project in Notion makes it selectable
 *                       here without a reflash. This is Mission 19.
 *            repos    - where a GO-captured GitHub issue is filed.
 *
 *          Still two lists, not one. An AI-OS project and a GitHub repository
 *          are different things chosen in different flows, and merging them
 *          would invite sending one where the other belongs - even though a
 *          project with a GitHub Repo property now appears in both.
 *
 *          The device only ever holds short ids and labels. Never an
 *          "owner/name" slug, never a Notion page id, never a token. It sends
 *          the selected id back and the backend resolves it against its own
 *          allowlist, so a compromised device cannot file an issue against an
 *          arbitrary repository or attach a note to an arbitrary page.
 *
 *          Each project also carries its Cue - the AI-OS's own "2-6 word
 *          memory hook". Twelve characters of label is not enough to be sure
 *          you picked the right project when five of them start with "VAN";
 *          the UI shows the cue when the label is tapped.
 *
 *          App-agnostic: knows nothing about LVGL. status_deck_ui.c owns the
 *          on-screen scrollers.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sized against the backend's own limits (projects_catalog.MAX_ID_LEN 32,
 * MAX_LABEL_LEN 12, MAX_CUE_LEN 40) plus a nul. A Notion page id with its
 * dashes removed is exactly 32 characters, which is what MAX_ID_LEN was
 * chosen to fit. Entries that would not fit are dropped at parse time rather
 * than truncated - a half-written id would resolve to nothing on the backend
 * anyway, turning a config problem into a confusing runtime failure. */
#define CATALOG_ID_LEN 33
#define CATALOG_LABEL_LEN 13
#define CATALOG_CUE_LEN 41

/* A ceiling, not an expectation. Bounded so a mistake in the catalog cannot
 * make the device allocate without limit; the scroller is also unusable long
 * before this. Applies to each list separately. */
#define CATALOG_MAX_COUNT 12

/**
 * Starts the background fetch. Safe to call once, after wifi_mgr_init() and
 * after NVS is initialised.
 *
 * Publishes the NVS-cached catalog immediately if there is one, so a boot
 * with the backend down still offers the list it last saw rather than "...".
 * Then fetches, and keeps re-fetching on a slow timer - unlike the original
 * fetch-once behaviour, because the whole point of Mission 19 is that adding
 * a project in the AI-OS reaches the workbench without a reboot.
 */
void backend_catalog_init(void);

/** True once a catalog has been published, from the network or from NVS. */
bool backend_catalog_ready(void);

/* ---- Projects: the AI-OS routing hint on a NOTE --------------------- */

/** How many projects are selectable. 0 until a catalog is published. */
int backend_catalog_project_count(void);

/**
 * Short label for the current project, e.g. "VAN FLIP". Never NULL - returns
 * "..." while nothing has loaded and "NONE" if the catalog loaded empty, so
 * the UI always has something honest to render.
 */
const char *backend_catalog_project_label(void);

/**
 * The AI-OS Cue for the current project - a short memory hook such as
 * "Fleet van to trusted camper". Never NULL; empty when the project has no
 * cue set, or when nothing is selectable.
 */
const char *backend_catalog_project_cue(void);

/**
 * Catalog id for the current project, as sent to the backend in
 * `project_hint`. Never NULL; empty string when nothing is selectable, which
 * project_selector_get_hint() turns into "none".
 */
const char *backend_catalog_project_id(void);

/** Moves the project selection, wrapping. No-op when the list is empty. */
void backend_catalog_project_next(void);
void backend_catalog_project_prev(void);

/* ---- Repositories: where a GO issue is filed ------------------------ */

/** How many repositories are available. 0 until a catalog is published. */
int backend_catalog_repo_count(void);

/** Short label for the current repository, e.g. "WKSTN". Never NULL. */
const char *backend_catalog_repo_label(void);

/**
 * Catalog id for the current repository. Never NULL; empty string when
 * nothing is selectable, which issue_client treats as "do not submit".
 */
const char *backend_catalog_repo_id(void);

/** Moves the repository selection, wrapping. No-op when the list is empty. */
void backend_catalog_repo_next(void);
void backend_catalog_repo_prev(void);

#ifdef __cplusplus
}
#endif
