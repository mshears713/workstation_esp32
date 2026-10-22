/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief MENU (1.3.0): big buttons to every page that is not HOME. Pure LVGL.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *label;
    lv_event_cb_t cb;
} ui_menu_item_t;

/** Builds MENU into `page`: title, then `count` (up to 6) buttons two to a
 *  row; an odd last one spans the full width. */
void ui_menu_build(lv_obj_t *page, const ui_menu_item_t *items, int count);

#ifdef __cplusplus
}
#endif
