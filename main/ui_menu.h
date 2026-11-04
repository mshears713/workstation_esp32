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

/** Builds a menu page into `page`: `title`, then `count` (up to 6) buttons
 *  two to a row (an odd last one spans the full width). `corner` (may be
 *  NULL) is a key in the title band's right end - "MORE >" on the first
 *  page, "< MENU" on the second - so six full-size buttons stay six. */
void ui_menu_build(lv_obj_t *page, const char *title, const ui_menu_item_t *items, int count,
                   const ui_menu_item_t *corner);

#ifdef __cplusplus
}
#endif

/* Picture-card variant extends the existing checked-in LVGL menu builder. */
typedef struct { const char *label; const lv_image_dsc_t *image; int scale; lv_event_cb_t cb; } ui_menu_picture_item_t;
void ui_menu_build_pictures(lv_obj_t *,const char *,const ui_menu_picture_item_t *,int,const ui_menu_item_t *);
