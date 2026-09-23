/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief FIRMWARE page: what is running, and what the updater is doing.
 * @details The first page split along the view-model seam:
 *
 *            real subsystems -> ui_firmware_view_t -> ui_firmware_page_apply()
 *            fixture JSON    -> ui_firmware_view_t -> ui_firmware_page_apply()
 *
 *          status_deck_ui.c fills the view from firmware_identity / ota_service
 *          / device_config; the host simulator fills it from a fixture. The
 *          wording and layout live only here, so both show the same page.
 *          Pure LVGL - see ui_kit.h.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Everything the page shows. Strings are borrowed for the duration of the
 *  apply call only (LVGL copies label text), so the view costs no RAM. */
typedef struct {
    const char *version;        /* "1.2.2+2d189af" */
    const char *built_at;       /* "2026-09-23 15:06" */
    const char *backend_url;    /* "http://192.168.1.71:8000" */
    bool backend_fallback;      /* running on the fallback address */
    const char *slot;           /* "ota_0" */
    bool pending_verify;        /* new image still on probation */
    const char *ota_state;      /* ota_service_state_label() */
    const char *ota_message;    /* ota_status_t.message, "" when none */
    bool ota_downloading;
    int ota_progress_pct;
} ui_firmware_view_t;

/** The labels apply() writes to. Zero-initialise before build. */
typedef struct {
    lv_obj_t *version;
    lv_obj_t *build;
    lv_obj_t *backend;
    lv_obj_t *slot;
    lv_obj_t *ota;
} ui_firmware_page_t;

/** Builds the page into `page` (a ui_page_create() container). `check_cb` is
 *  the CHECK CLAWBOX NOW button's handler; may be NULL in the simulator. */
void ui_firmware_page_build(ui_firmware_page_t *w, lv_obj_t *page, lv_event_cb_t check_cb);

/** Writes the view into the labels. No-op before build. */
void ui_firmware_page_apply(const ui_firmware_page_t *w, const ui_firmware_view_t *v);

#ifdef __cplusplus
}
#endif
