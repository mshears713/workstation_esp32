/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief LOG as a phosphor terminal - see ui_log.h.
 * @details Why it changed: the recorder keeps 32 events, the old page drew
 *          the newest 8 into a box with room for about 4 and a half, and never
 *          said an event's type. Here every retained line is reachable, a page
 *          at a time, and nothing is cut: a long message wraps under itself.
 *
 *    y   0.. 56   LOG title beside HOME; FPS/CPU (device) and position at the right
 *    y  58..188   lines: "12:04 NET WIFI ONLINE", a fault in amber with '!'
 *    y 192..236   soft keys: NEW (newer page)  OLD (older page)  NET  RPT  CLR
 *
 *          Green on black, uppercase, no scanlines or glow (layers cost
 *          redraw on this panel). Status is never colour alone: a fault line
 *          also starts with '!'.
 */

#include "ui_log.h"

#include <stdio.h>
#include "ui_kit.h"
#include "ui_log_format.h"

#define PH_BG      lv_color_hex(0x000000)
#define PH_GREEN   lv_color_hex(0x33FF66)
#define PH_DIM     lv_color_hex(0x1FAA4A)   /* times, tags, captions: ~7:1 on black */
#define PH_AMBER   lv_color_hex(0xFFB000)
#define PH_KEY_BG  lv_color_hex(0x062A12)

#define LIST_Y 60
#define LIST_BOTTOM 188
#define MSG_X 84            /* time (5 chars) + tag (3 chars) column */

static lv_obj_t *text(lv_obj_t *parent, const char *s, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static lv_obj_t *soft_key(lv_obj_t *page, int x, int w, const char *label, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *k = lv_button_create(page);
    lv_obj_remove_style_all(k);
    lv_obj_set_pos(k, x, 192);
    lv_obj_set_size(k, w, 44);
    lv_obj_set_style_bg_color(k, PH_KEY_BG, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(k, PH_GREEN, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(k, PH_GREEN, 0);
    lv_obj_set_style_border_width(k, 2, 0);
    lv_obj_set_style_border_color(k, PH_DIM, LV_STATE_DISABLED);
    lv_obj_set_style_radius(k, 2, 0);
    if (cb) {
        lv_obj_add_event_cb(k, cb, LV_EVENT_CLICKED, ud);
    }
    lv_obj_t *l = text(k, label, PH_GREEN);
    lv_obj_set_style_text_color(l, PH_BG, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(l, PH_DIM, LV_STATE_DISABLED);
    lv_obj_center(l);
    return k;
}

static void set_enabled(lv_obj_t *k, bool on)
{
    lv_obj_t *l = lv_obj_get_child(k, 0);
    if (on) {
        lv_obj_clear_state(k, LV_STATE_DISABLED);
        lv_obj_clear_state(l, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(k, LV_STATE_DISABLED);
        lv_obj_add_state(l, LV_STATE_DISABLED);
    }
}

/* Lays out lines from p->offset down until the next would not fit. */
static void draw(ui_log_page_t *p)
{
    lv_obj_clean(p->list);
    int y = 0, i = p->offset;
    int avail = LIST_BOTTOM - LIST_Y;
    if (p->count == 0) {
        lv_obj_t *l = text(p->list, "> NO EVENTS SINCE THE LAST CLEAR_", PH_DIM);
        lv_obj_set_pos(l, 0, 0);
    }
    for (; i < p->count; i++) {
        const ui_log_entry_t *e = &p->entries[i];
        bool fault = ui_log_is_fault(e->type, e->message);
        lv_color_t c = fault ? PH_AMBER : PH_GREEN;

        char when[12], head[20];
        ui_log_fmt_time(e->uptime_s, when, sizeof(when));
        snprintf(head, sizeof(head), "%5s %s", when, ui_log_tag(e->type));
        lv_obj_t *msg = text(p->list, "", c);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(msg, 304 - MSG_X);
        lv_label_set_text_fmt(msg, "%s%s", fault ? "! " : "", e->message ? e->message : "");
        lv_obj_update_layout(msg);
        int h = lv_obj_get_height(msg);
        if (y + h > avail && i > p->offset) {
            lv_obj_delete(msg);
            break;
        }
        lv_obj_set_pos(msg, MSG_X, y);
        lv_obj_t *hd = text(p->list, head, fault ? PH_AMBER : PH_DIM);
        lv_obj_set_pos(hd, 0, y);
        y += h + 2;
    }
    p->shown = i - p->offset;

    if (p->count > 0) {
        lv_label_set_text_fmt(p->position, "%d-%d of %d, since boot", p->offset + 1, p->offset + p->shown, p->count);
    } else {
        lv_label_set_text(p->position, "0 of 0");
    }
    set_enabled(p->older, p->offset + p->shown < p->count);
    set_enabled(p->newer, p->offset > 0);
    if (p->unseen > 0 && p->offset > 0) {
        lv_label_set_text_fmt(p->newer_label, LV_SYMBOL_UP " %d NEW", p->unseen);
    } else {
        lv_label_set_text(p->newer_label, LV_SYMBOL_UP " NEW");
    }
}

static void older_cb(lv_event_t *e)
{
    ui_log_page_t *p = lv_event_get_user_data(e);
    if (p->offset + p->shown >= p->count) {
        return;
    }
    if (p->depth < UI_LOG_MAX_PAGES) {
        p->back[p->depth++] = p->offset;
    }
    p->offset += p->shown;
    draw(p);
}

static void newer_cb(lv_event_t *e)
{
    ui_log_page_t *p = lv_event_get_user_data(e);
    if (p->offset == 0) {
        return;
    }
    /* back to where the previous page started, shifted by what arrived */
    p->offset = p->depth > 0 ? p->back[--p->depth] : 0;
    if (p->depth == 0) {
        p->offset = 0;     /* the first page always shows the newest */
        p->unseen = 0;
    }
    draw(p);
}

void ui_log_build(ui_log_page_t *p, lv_obj_t *page, const ui_log_actions_t *a)
{
    *p = (ui_log_page_t){ 0 };
    lv_obj_set_style_bg_color(page, PH_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "LOG");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, PH_GREEN, 0);
    lv_obj_set_pos(title, 104, 12);

    /* LVGL's FPS/CPU readout is reparented here on the device
     * (status_deck_ui). It is two lines ("NN FPS, NN% CPU" / "NN ms (..)"),
     * so the slot is two lines tall. */
    p->perf_slot = lv_obj_create(page);
    lv_obj_remove_style_all(p->perf_slot);
    lv_obj_set_size(p->perf_slot, 132, 38);
    lv_obj_align(p->perf_slot, LV_ALIGN_TOP_RIGHT, -4, 1);
    lv_obj_clear_flag(p->perf_slot, LV_OBJ_FLAG_CLICKABLE);
    /* "13-19 of 32, since boot": where you are, and what the time column is. */
    p->position = text(page, "", PH_DIM);
    lv_obj_set_style_text_font(p->position, &lv_font_montserrat_12, 0);
    lv_obj_align(p->position, LV_ALIGN_TOP_RIGHT, -8, 41);

    lv_obj_t *rule = lv_obj_create(page);
    lv_obj_remove_style_all(rule);
    lv_obj_set_pos(rule, 8, 57);
    lv_obj_set_size(rule, 304, 1);
    lv_obj_set_style_bg_color(rule, PH_DIM, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    p->list = lv_obj_create(page);
    lv_obj_remove_style_all(p->list);
    lv_obj_set_pos(p->list, 8, LIST_Y);
    lv_obj_set_size(p->list, 304, LIST_BOTTOM - LIST_Y);
    lv_obj_clear_flag(p->list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(p->list, LV_OBJ_FLAG_CLICKABLE);

    p->newer = soft_key(page, 4, 72, LV_SYMBOL_UP " NEW", newer_cb, p);
    p->newer_label = lv_obj_get_child(p->newer, 0);
    p->older = soft_key(page, 80, 72, LV_SYMBOL_DOWN " OLD", older_cb, p);
    soft_key(page, 160, 50, "NET", a ? a->net : NULL, NULL);
    soft_key(page, 213, 50, "RPT", a ? a->rpt : NULL, NULL);
    soft_key(page, 266, 50, "CLR", a ? a->clr : NULL, NULL);
}

void ui_log_show(ui_log_page_t *p, const ui_log_entry_t *entries, int count, uint32_t seq)
{
    int added = (int)(seq - p->seq);
    if (p->seq == 0 || added < 0) {
        added = 0;                       /* first show, or the counter restarted */
    }
    p->seq = seq;
    p->entries = entries;
    p->count = count;
    if (p->offset > 0) {
        /* Keep the same events on screen: they moved down the ring. */
        p->offset += added;
        for (int i = 0; i < p->depth; i++) {
            p->back[i] = p->back[i] ? p->back[i] + added : 0;
        }
        p->unseen += added;
    }
    if (count == 0 || p->offset >= count) {
        p->offset = 0;
        p->depth = 0;
        p->unseen = 0;
    }
    draw(p);
}
