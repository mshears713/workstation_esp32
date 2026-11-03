/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief FIRMWARE page: what is running, and where the updater is in its
 *        known stages (gallery concept 10 Timeline).
 * @details The first page split along the view-model seam:
 *            real subsystems -> ui_firmware_view_t -> ui_firmware_page_apply()
 *            fixture JSON    -> ui_firmware_view_t -> ui_firmware_page_apply()
 *          status_deck_ui.c fills the view from firmware_identity / ota_service
 *          / device_config; the host simulator fills it from a fixture. Which
 *          stage is done, current or failed is decided by ui_fw_timeline.c
 *          from the updater's own state and message. Pure LVGL - see ui_kit.h.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "ui_fw_timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Everything the page shows. Strings are borrowed for the duration of the
 *  apply call only (LVGL copies label text), so the view costs no RAM. */
typedef struct {
    const char *version;        /* "1.2.2+2d189af" */
    const char *built_at;       /* "Sep 23 2026 15:06:22" */
    const char *backend_url;    /* "http://192.168.1.71:8000" */
    bool backend_fallback;      /* running on the fallback address */
    const char *slot;           /* "ota_0" */
    bool pending_verify;        /* new image still on probation */
    ui_fw_state_t ota_state;    /* ota_status_t.state */
    const char *ota_message;    /* ota_status_t.message, "" when none */
    const char *desired_version;/* ota_status_t.desired_version, "" until read */
    int ota_progress_pct;       /* measured, while downloading */
    bool rolled_back;           /* this boot came after a failed image */
} ui_firmware_view_t;

/** The objects apply() writes to. Zero-initialise before build. */
typedef struct {
    lv_obj_t *version;
    lv_obj_t *slot;
    lv_obj_t *node[UI_FW_STAGES], *node_mark[UI_FW_STAGES], *node_name[UI_FW_STAGES], *link[UI_FW_STAGES - 1];
    lv_obj_t *headline;
    lv_obj_t *detail;
    lv_obj_t *bar;
    lv_obj_t *backend;
    lv_obj_t *build;
} ui_firmware_page_t;

/** Builds the page into `page` (a ui_page_create() container). `check_cb` is
 *  the CHECK NOW key's handler; may be NULL in the simulator. */
void ui_firmware_page_build(ui_firmware_page_t *w, lv_obj_t *page, lv_event_cb_t check_cb);

/** Writes the view into the page. No-op before build. */
void ui_firmware_page_apply(const ui_firmware_page_t *w, const ui_firmware_view_t *v);

#ifdef __cplusplus
}
#endif
