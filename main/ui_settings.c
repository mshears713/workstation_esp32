/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief SETTINGS, Hazard Industrial - see ui_settings.h.
 * @details What changed and why:
 *            - the old status row put uptime, API and Wi-Fi as three bare
 *              labels on one line, which collided at their longest; they are
 *              now two status blocks (word on a solid fill) and a band;
 *            - the words and the "no network beats a cached API OK" rule come
 *              from ui_diag_format.c, so SETTINGS and DIAGNOSTICS cannot
 *              disagree; latency is labelled as the last probe's;
 *            - anything not OK sits in hazard tape (shape, not only colour);
 *            - keys are 50-60 px, square, yellow; a key that cannot move
 *              (volume at an end, repository list not loaded) goes outline-only;
 *            - uptime reads "5h 03m", not minutes:seconds.
 *
 *    y   2.. 54   [shell's < HOME]  PROJECTS >  FIRMWARE >
 *    y  58..120   API block | WI-FI block: caption, word, detail
 *    y 124..168   [-]  VOLUME 70 + ten cells  [+]
 *    y 172..216   [<]  GO FILES TO (GITHUB) name  [>]
 *    y 222..238   hazard band: SETTINGS | UP 5h 03m
 */

#include "ui_settings.h"

#include <stdio.h>

#define HZ_BG     lv_color_hex(0x000000)
#define HZ_YELLOW lv_color_hex(0xFFC400)
#define HZ_WHITE  lv_color_hex(0xF5F5F5)
#define HZ_UNLIT  lv_color_hex(0x806300)   /* unlit cells, dead keys: ~3.6:1, still clearly "off" */
#define HZ_OK     lv_color_hex(0x2EDB4B)
#define HZ_BUSY   lv_color_hex(0xFF7A00)
#define HZ_FAIL   lv_color_hex(0xFF3B30)
#define HZ_NODATA lv_color_hex(0x8C8C8C)

void ui_settings_fmt_uptime(uint32_t s, char *b, size_t n)
{
    if (s < 3600u) {
        snprintf(b, n, "%um %02us", (unsigned)(s / 60u), (unsigned)(s % 60u));
    } else if (s < 86400u) {
        snprintf(b, n, "%uh %02um", (unsigned)(s / 3600u), (unsigned)(s / 60u % 60u));
    } else {
        snprintf(b, n, "%ud %02uh", (unsigned)(s / 86400u), (unsigned)(s / 3600u % 24u));
    }
}

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

/* Hazard tape: yellow with black 45-degree bars, drawn by this one object
 * in its draw event rather than built from dozens of line widgets. */
static void tape_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = HZ_BG;
    d.width = 4;
    int32_t h = lv_area_get_height(&a);
    for (int32_t x = a.x1 - h; x < a.x2 + 1; x += 10) {
        d.p1.x = x;
        d.p1.y = a.y2;
        d.p2.x = x + h;
        d.p2.y = a.y1;
        lv_draw_line(layer, &d);
    }
}

static lv_obj_t *tape(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *t = box(parent, x, y, w, h);
    lv_obj_set_style_bg_color(t, HZ_YELLOW, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(t, tape_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return t;
}

static lv_obj_t *key(lv_obj_t *parent, int x, int y, int w, int h, const char *label, const lv_font_t *f,
                     bool solid, lv_event_cb_t cb)
{
    lv_obj_t *k = lv_button_create(parent);
    lv_obj_remove_style_all(k);
    lv_obj_set_pos(k, x, y);
    lv_obj_set_size(k, w, h);
    lv_obj_set_style_border_color(k, HZ_YELLOW, 0);
    lv_obj_set_style_border_width(k, 3, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(k, solid ? HZ_YELLOW : HZ_BG, 0);
    lv_obj_set_style_bg_color(k, HZ_WHITE, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(k, HZ_BG, LV_STATE_DISABLED);
    lv_obj_set_style_border_color(k, HZ_UNLIT, LV_STATE_DISABLED);
    if (cb) {
        lv_obj_add_event_cb(k, cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *l = text(k, label, f, solid ? HZ_BG : HZ_YELLOW);
    lv_obj_set_style_text_color(l, HZ_UNLIT, LV_STATE_DISABLED);
    lv_obj_center(l);
    return k;
}

static void key_enable(lv_obj_t *k, bool on)
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

/* A status block inside its (normally hidden) tape frame. */
static void status_block(lv_obj_t *page, int x, const char *caption, lv_obj_t **tape_out, lv_obj_t **blk,
                         lv_obj_t **word, lv_obj_t **detail)
{
    *tape_out = tape(page, x, 58, 154, 62);
    *blk = box(page, x, 58, 154, 62);
    lv_obj_set_style_bg_opa(*blk, LV_OPA_COVER, 0);
    lv_obj_t *c = text(*blk, caption, &lv_font_montserrat_12, HZ_BG);
    lv_obj_set_pos(c, 8, 4);
    *word = text(*blk, "", &lv_font_montserrat_20, HZ_BG);
    lv_obj_set_pos(*word, 8, 18);
    *detail = text(*blk, "", &lv_font_montserrat_14, HZ_BG);
    lv_obj_set_pos(*detail, 8, 40);
}

void ui_settings_build(ui_settings_page_t *p, lv_obj_t *page, const ui_settings_actions_t *a)
{
    lv_obj_set_style_bg_color(page, HZ_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);

    /* Beside the shell's HOME key: the two places SETTINGS leads to. */
    key(page, 102, 2, 104, 52, "PROJECTS " LV_SYMBOL_RIGHT, &lv_font_montserrat_14, false, a ? a->projects : NULL);
    key(page, 210, 2, 106, 52, "FIRMWARE " LV_SYMBOL_RIGHT, &lv_font_montserrat_14, false, a ? a->firmware : NULL);

    status_block(page, 4, "API", &p->api_tape, &p->api_block, &p->api_word, &p->api_detail);
    status_block(page, 162, "WI-FI", &p->wifi_tape, &p->wifi_block, &p->wifi_word, &p->wifi_detail);

    p->vol_down = key(page, 4, 124, 60, 44, LV_SYMBOL_MINUS, &lv_font_montserrat_20, true, a ? a->volume_down : NULL);
    p->vol_up = key(page, 256, 124, 60, 44, LV_SYMBOL_PLUS, &lv_font_montserrat_20, true, a ? a->volume_up : NULL);
    lv_obj_t *vb = box(page, 68, 124, 184, 44);
    lv_obj_set_style_border_color(vb, HZ_YELLOW, 0);
    lv_obj_set_style_border_width(vb, 2, 0);
    lv_obj_t *vc = text(vb, "VOLUME", &lv_font_montserrat_12, HZ_YELLOW);
    lv_obj_set_pos(vc, 8, 3);
    p->vol_value = text(vb, "", &lv_font_montserrat_20, HZ_WHITE);
    lv_obj_align(p->vol_value, LV_ALIGN_TOP_RIGHT, -8, 1);
    for (int i = 0; i < 10; i++) {
        p->vol_cells[i] = box(vb, 8 + i * 17, 24, 14, 12);
        lv_obj_set_style_bg_opa(p->vol_cells[i], LV_OPA_COVER, 0);
    }

    p->repo_prev = key(page, 4, 172, 60, 44, LV_SYMBOL_LEFT, &lv_font_montserrat_20, true, a ? a->repo_prev : NULL);
    p->repo_next = key(page, 256, 172, 60, 44, LV_SYMBOL_RIGHT, &lv_font_montserrat_20, true, a ? a->repo_next : NULL);
    lv_obj_t *rb = box(page, 68, 172, 184, 44);
    lv_obj_set_style_border_color(rb, HZ_YELLOW, 0);
    lv_obj_set_style_border_width(rb, 2, 0);
    /* GitHub repositories for GO - not the AI-OS projects NOTE uses. */
    lv_obj_t *rc = text(rb, "GO FILES TO (GITHUB)", &lv_font_montserrat_12, HZ_YELLOW);
    lv_obj_set_pos(rc, 8, 3);
    p->repo_name = text(rb, "", &lv_font_montserrat_20, HZ_WHITE);
    lv_obj_set_pos(p->repo_name, 8, 18);

    tape(page, 0, 222, 320, 18);
    lv_obj_t *bb = box(page, 56, 222, 208, 18);
    lv_obj_set_style_bg_color(bb, HZ_BG, 0);
    lv_obj_set_style_bg_opa(bb, LV_OPA_COVER, 0);
    p->band_text = text(bb, "", &lv_font_montserrat_14, HZ_YELLOW);
    lv_obj_center(p->band_text);
}

static void paint_block(lv_obj_t *tp, lv_obj_t *blk, lv_obj_t *word, lv_obj_t *detail, const ui_diag_line_t *l,
                        lv_color_t fill, bool ok)
{
    lv_obj_set_style_bg_color(blk, fill, 0);
    lv_obj_update_layout(tp);   /* its geometry is read below */
    /* OK: the block fills its frame. Anything else: 5 px of tape shows round it. */
    if (ok) {
        lv_obj_add_flag(tp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(blk, lv_obj_get_x(tp), lv_obj_get_y(tp));
        lv_obj_set_size(blk, lv_obj_get_width(tp), lv_obj_get_height(tp));
    } else {
        lv_obj_clear_flag(tp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(blk, lv_obj_get_x(tp) + 4, lv_obj_get_y(tp) + 4);
        lv_obj_set_size(blk, lv_obj_get_width(tp) - 8, lv_obj_get_height(tp) - 8);
    }
    /* caption 15 / word 22 / detail 16 px line boxes, stacked without
     * overlap in the 62 px block or the 54 px inside its tape */
    lv_obj_set_pos(lv_obj_get_child(blk, 0), 8, ok ? 3 : 1);
    lv_label_set_text(word, l->word);
    lv_obj_set_pos(word, 8, ok ? 19 : 16);
    lv_label_set_text(detail, l->detail);
    lv_obj_set_pos(detail, 8, ok ? 42 : 38);
    if (l->detail[0]) {
        lv_obj_clear_flag(detail, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_settings_apply(ui_settings_page_t *p, const ui_settings_view_t *v)
{
    /* The DIAGNOSTICS model decides the words, so the pages agree. */
    ui_diag_view_t d = {
        .wifi = v->wifi, .wifi_reason = v->wifi_reason, .wifi_attempts = v->wifi_attempts,
        .wifi_retry_s = v->wifi_retry_s, .api = v->api, .api_ever_ok = v->api_ever_ok,
        .api_secs_since_ok = v->api_secs_since_ok,
    };
    ui_diag_line_t api = ui_diag_api_line(&d);
    ui_diag_line_t wifi = ui_diag_wifi_line(&d);

    bool api_ok = v->wifi == UI_DIAG_WIFI_ONLINE && v->api == UI_DIAG_API_OK;
    if (api_ok) {
        /* SETTINGS is where latency is read; it is the last 3-second probe's. */
        snprintf(api.detail, sizeof(api.detail), "last %lu ms", (unsigned long)v->api_latency_ms);
    } else if (v->api == UI_DIAG_API_DOWN && v->wifi == UI_DIAG_WIFI_ONLINE && v->api_ever_ok) {
        /* the time since the backend last answered, as DIAGNOSTICS words it */
        char age[16];
        ui_diag_fmt_age_ms(age, sizeof(age), (uint64_t)v->api_secs_since_ok * 1000u);
        snprintf(api.detail, sizeof(api.detail), "ok %s ago", age);
    }
    lv_color_t api_fill = api_ok                                  ? HZ_OK
                        : v->wifi != UI_DIAG_WIFI_ONLINE          ? HZ_NODATA
                        : v->api == UI_DIAG_API_DOWN              ? HZ_FAIL
                                                                  : HZ_NODATA;
    paint_block(p->api_tape, p->api_block, p->api_word, p->api_detail, &api, api_fill, api_ok);

    bool wifi_ok = v->wifi == UI_DIAG_WIFI_ONLINE;
    if (wifi_ok) {
        snprintf(wifi.detail, sizeof(wifi.detail), "IP *%s", v->ip_octet ? v->ip_octet : "?");
    }
    lv_color_t wifi_fill = wifi_ok                              ? HZ_OK
                         : v->wifi == UI_DIAG_WIFI_JOINING      ? HZ_BUSY
                         : v->wifi == UI_DIAG_WIFI_RETRY        ? HZ_FAIL
                                                                : HZ_NODATA;
    paint_block(p->wifi_tape, p->wifi_block, p->wifi_word, p->wifi_detail, &wifi, wifi_fill, wifi_ok);

    int vol = v->volume < 0 ? 0 : v->volume > 100 ? 100 : v->volume;
    lv_label_set_text_fmt(p->vol_value, "%d", vol);
    for (int i = 0; i < 10; i++) {
        lv_obj_set_style_bg_color(p->vol_cells[i], (i + 1) * 10 <= vol ? HZ_YELLOW : HZ_UNLIT, 0);
    }
    key_enable(p->vol_down, vol > 0);
    key_enable(p->vol_up, vol < 100);

    /* Labels are <= 12 characters, but 12 wide ones do not fit at 20 px:
     * step down to 14, and only then cut with dots. */
    lv_label_set_long_mode(p->repo_name, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(p->repo_name, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(p->repo_name, &lv_font_montserrat_20, 0);
    lv_label_set_text(p->repo_name, v->repo ? v->repo : "...");
    lv_obj_update_layout(p->repo_name);
    if (lv_obj_get_width(p->repo_name) > 168) {
        lv_obj_set_style_text_font(p->repo_name, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(p->repo_name, 8, 21);
        lv_obj_update_layout(p->repo_name);
        if (lv_obj_get_width(p->repo_name) > 168) {
            lv_label_set_long_mode(p->repo_name, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_size(p->repo_name, 168, 16);
        }
    } else {
        lv_obj_set_pos(p->repo_name, 8, 18);
    }
    key_enable(p->repo_prev, v->repo_count > 1);
    key_enable(p->repo_next, v->repo_count > 1);

    char up[16];
    ui_settings_fmt_uptime(v->uptime_s, up, sizeof(up));
    lv_label_set_text_fmt(p->band_text, "SETTINGS  |  UP %s", up);
}
