/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief SETTINGS (gallery concept 04 Hazard Industrial): backend and Wi-Fi as
 *        solid status blocks, volume and the GO repository on big keys, the
 *        links to PROJECTS and FIRMWARE. Pure LVGL; status_deck_ui.c fills
 *        the view from the subsystems, tools/uisim/targets/settings.c from
 *        fixtures. The global "< HOME" button stays the app's own.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui_diag_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t uptime_s;
    ui_diag_wifi_t wifi;          /* wifi_mgr state */
    const char *ip_octet;         /* ".57" while online, else NULL */
    uint32_t wifi_attempts;
    const char *wifi_reason;      /* "NO AP FOUND" / "AUTH FAILED" / "WIFI LOST" / "RETRYING" / "" */
    uint32_t wifi_retry_s;
    ui_diag_api_t api;            /* backend_health state */
    uint32_t api_latency_ms;      /* last successful probe (every 3 s) */
    bool api_ever_ok;
    uint32_t api_secs_since_ok;
    int volume;                   /* 0..100, steps of 10 */
    const char *repo;             /* catalog label (<= 12 chars), "..." loading, "NONE" empty */
    int repo_count;               /* 0 while loading or empty: the arrows cannot move */
} ui_settings_view_t;

typedef struct {
    lv_event_cb_t volume_down, volume_up, repo_prev, repo_next, projects, firmware;
} ui_settings_actions_t;

typedef struct {
    lv_obj_t *api_tape, *api_block, *api_word, *api_detail;
    lv_obj_t *wifi_tape, *wifi_block, *wifi_word, *wifi_detail;
    lv_obj_t *vol_value, *vol_cells[10], *vol_down, *vol_up;
    lv_obj_t *repo_name, *repo_prev, *repo_next;
    lv_obj_t *band_text;
} ui_settings_page_t;

void ui_settings_build(ui_settings_page_t *p, lv_obj_t *page, const ui_settings_actions_t *a);
void ui_settings_apply(ui_settings_page_t *p, const ui_settings_view_t *v);

/** "42m 17s" / "5h 03m" / "3d 00h" / "412d 07h". */
void ui_settings_fmt_uptime(uint32_t s, char *b, size_t n);

#ifdef __cplusplus
}
#endif
