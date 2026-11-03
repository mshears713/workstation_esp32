/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief FIRMWARE page as a timeline - see ui_firmware_page.h.
 * @details
 *    y   0.. 56   shell's HOME, FIRMWARE title
 *    y  56.. 94   RUNNING ... slot + CONFIRMED / ON PROBATION, then the version
 *    y  92..132   CHECK - GET - VERIFY - BOOT - HEALTH - KEPT
 *    y 136..186   what is happening now (20), the updater's words (14),
 *                 a bar only while downloading (its percent is measured)
 *    y 194..238   backend address + build time | CHECK NOW
 *
 *          CHECK NOW still only asks CLAWBOX again (ota_service_request_check):
 *          it never forces an install - the device installs only the version
 *          CLAWBOX names as desired.
 */

#include <stdio.h>
#include <string.h>
#include "ui_firmware_page.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define NODE 22
#define NODE_Y 94
#define NODE_X0 30
#define NODE_PITCH 52

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void ui_firmware_page_build(ui_firmware_page_t *w, lv_obj_t *page, lv_event_cb_t check_cb)
{
    ui_page_title(page, "FIRMWARE");

    lv_obj_t *cap = text(page, "RUNNING", &lv_font_montserrat_12, UI_TEXT_DIM);
    lv_obj_set_pos(cap, 12, 56);
    /* the slot on the caption row: a "-dirty" version needs the whole width */
    w->slot = text(page, "", &lv_font_montserrat_14, UI_TEXT_DIM);
    lv_obj_align(w->slot, LV_ALIGN_TOP_RIGHT, -12, 55);
    w->version = text(page, "...", &lv_font_montserrat_20, UI_TEXT);
    lv_obj_set_pos(w->version, 12, 71);

    for (int i = 0; i < UI_FW_STAGES; i++) {
        int cx = NODE_X0 + i * NODE_PITCH;
        if (i > 0) {
            w->link[i - 1] = box(page, cx - NODE_PITCH + NODE / 2, NODE_Y + NODE / 2 - 1, NODE_PITCH - NODE, 3);
            lv_obj_set_style_bg_opa(w->link[i - 1], LV_OPA_COVER, 0);
        }
        w->node[i] = box(page, cx - NODE / 2, NODE_Y, NODE, NODE);
        lv_obj_set_style_radius(w->node[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(w->node[i], 2, 0);
        w->node_mark[i] = text(w->node[i], "", &lv_font_montserrat_12, UI_TEXT);
        lv_obj_center(w->node_mark[i]);
        w->node_name[i] = text(page, ui_fw_stage_name((ui_fw_stage_t)i), &lv_font_montserrat_12, UI_TEXT_DIM);
        lv_obj_align(w->node_name[i], LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_update_layout(w->node_name[i]);
        lv_obj_set_pos(w->node_name[i], cx - lv_obj_get_width(w->node_name[i]) / 2, NODE_Y + NODE + 3);
    }

    w->headline = text(page, "", &lv_font_montserrat_20, UI_TEXT);
    lv_obj_set_pos(w->headline, 12, 136);
    w->detail = text(page, "", &lv_font_montserrat_14, UI_TEXT_DIM);
    lv_label_set_long_mode(w->detail, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(w->detail, 296);
    lv_obj_set_pos(w->detail, 12, 159);

    w->bar = lv_bar_create(page);
    lv_obj_remove_style_all(w->bar);
    lv_obj_set_pos(w->bar, 12, 180);
    lv_obj_set_size(w->bar, 296, 8);
    lv_bar_set_range(w->bar, 0, 100);
    lv_obj_set_style_bg_color(w->bar, UI_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(w->bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w->bar, UI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(w->bar, 4, LV_PART_INDICATOR);

    w->backend = text(page, "", &lv_font_montserrat_14, UI_TEXT_DIM);
    lv_obj_set_pos(w->backend, 12, 198);
    w->build = text(page, "", &lv_font_montserrat_12, UI_TEXT_DIM);
    lv_obj_set_pos(w->build, 12, 218);

    lv_obj_t *check_btn = lv_button_create(page);
    lv_obj_set_size(check_btn, 124, 44);
    lv_obj_set_pos(check_btn, 188, 194);
    lv_obj_set_style_bg_color(check_btn, UI_ACCENT, 0);
    if (check_cb) {
        lv_obj_add_event_cb(check_btn, check_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *check_lbl = text(check_btn, LV_SYMBOL_REFRESH " CHECK NOW", &lv_font_montserrat_14, UI_TEXT);
    lv_obj_center(check_lbl);
}

void ui_firmware_page_apply(const ui_firmware_page_t *w, const ui_firmware_view_t *v)
{
    if (w->version == NULL) {
        return;   /* page not built yet */
    }
    char buf[96];
    lv_label_set_text(w->version, v->version);
    snprintf(buf, sizeof(buf), "%s  %s", v->slot, v->pending_verify ? "ON PROBATION" : "CONFIRMED");
    lv_label_set_text(w->slot, buf);
    lv_obj_set_style_text_color(w->slot, v->pending_verify ? UI_WARN : UI_OK, 0);

    ui_fw_timeline_t t;
    ui_fw_timeline(v->ota_state, v->ota_message, v->desired_version, v->pending_verify, v->rolled_back, &t);
    for (int i = 0; i < UI_FW_STAGES; i++) {
        lv_color_t fill = UI_BG, edge = lv_color_hex(0x4A515B), name = UI_TEXT_DIM;
        const char *mark = "";
        switch (t.stage[i]) {
        case UI_FW_ST_DONE:    fill = UI_OK; edge = UI_OK; mark = LV_SYMBOL_OK; break;   /* black tick: white on green is 2.8:1 */
        case UI_FW_ST_CURRENT: fill = UI_ACCENT; edge = UI_TITLE; name = UI_TEXT; break;
        case UI_FW_ST_FAILED:  fill = UI_ERR; edge = UI_ERR; mark = LV_SYMBOL_CLOSE; name = UI_ERR; break;
        default: break;
        }
        lv_obj_set_style_bg_color(w->node[i], fill, 0);
        lv_obj_set_style_bg_opa(w->node[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(w->node[i], edge, 0);
        lv_label_set_text(w->node_mark[i], mark);
        lv_obj_set_style_text_color(w->node_mark[i], t.stage[i] == UI_FW_ST_DONE ? UI_BG : UI_TEXT, 0);
        if (mark[0]) {
            lv_obj_clear_flag(w->node_mark[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(w->node_mark[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_text_color(w->node_name[i], name, 0);
        if (i > 0) {
            lv_obj_set_style_bg_color(w->link[i - 1], t.stage[i] == UI_FW_ST_PENDING ? lv_color_hex(0x2E333A) : UI_OK, 0);
        }
    }

    lv_label_set_text(w->headline, t.headline);
    lv_obj_set_style_text_color(w->headline, t.failed ? UI_ERR : t.in_flight ? UI_TEXT : UI_TEXT_DIM, 0);
    if (t.show_percent) {
        /* the updater's own count of bytes written, not an estimate */
        lv_label_set_text_fmt(w->detail, "%d%% written  -  %s", v->ota_progress_pct, t.detail);
        lv_bar_set_value(w->bar, v->ota_progress_pct, LV_ANIM_OFF);
        lv_obj_clear_flag(w->bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(w->detail, t.detail);
        lv_obj_add_flag(w->bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (t.failed && !t.failed_stage_known) {
        lv_obj_set_style_text_color(w->detail, UI_ERR, 0);
    } else {
        lv_obj_set_style_text_color(w->detail, UI_TEXT_DIM, 0);
    }

    /* The address without its scheme: the part that tells you which
     * machine this is. "FB" in place of "api" = running on the fallback
     * address (first, so it is never the part that gets cut). */
    const char *url = v->backend_url ? v->backend_url : "";
    if (strncmp(url, "http://", 7) == 0) {
        url += 7;
    }
    snprintf(buf, sizeof(buf), "%s %s", v->backend_fallback ? "FB" : "api", url);
    lv_label_set_text(w->backend, buf);
    lv_obj_set_style_text_color(w->backend, v->backend_fallback ? UI_WARN : UI_TEXT_DIM, 0);
    snprintf(buf, sizeof(buf), "built %s", v->built_at);
    lv_label_set_text(w->build, buf);
}
