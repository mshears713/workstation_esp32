/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief DIAGNOSTICS page: link, sensor freshness, uptime, internal heap.
 * @details Same seam as ui_firmware_page.h:
 *
 *            real subsystems -> ui_diag_view_t -> ui_diagnostics_apply()
 *            fixture JSON    -> ui_diag_view_t -> ui_diagnostics_apply()
 *
 *          The words and severities come from ui_diag_format.c; this file only
 *          draws them. The global "< HOME" button (ui_back_button_create) is
 *          not part of the page - it floats above every page - so the page
 *          leaves the top-left 100x56 clear for it. Pure LVGL.
 */
#pragma once

#include "lvgl.h"
#include "ui_diag_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One signal: 14 px name + 14 px detail, then the 20 px word. */
typedef struct {
    lv_obj_t *detail;
    lv_obj_t *word;
} ui_diag_signal_t;

/** The objects apply() writes to. Zero-initialise before build. */
typedef struct {
    lv_obj_t *q_link, *q_sens, *q_up, *q_heap;
    ui_diag_signal_t wifi, api, accel, env;
    lv_obj_t *days, *hm;
    lv_obj_t *heap_word;
    lv_obj_t *heap_val[3];      /* free, largest, min */
} ui_diagnostics_page_t;

/** Builds the page into `page` (a ui_page_create() container). */
void ui_diagnostics_build(ui_diagnostics_page_t *w, lv_obj_t *page);

/** Writes the view into the page. No-op before build. */
void ui_diagnostics_apply(const ui_diagnostics_page_t *w, const ui_diag_view_t *v);

#ifdef __cplusplus
}
#endif
