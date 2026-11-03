/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief DIAGNOSTICS page - see ui_diagnostics.h.
 * @details Four square blocks, one per question: am I connected, are the
 *          sensors fresh, how long have I been up, how much internal RAM is
 *          left. A block's border turns orange/red (2 px) when something in
 *          it needs a look; inside, the status WORD is the 20 px line and the
 *          name and supporting value sit above it at 14 px. Chosen over a
 *          ledger and a verdict-first layout (tools/uisim/experiments/
 *          diagnostics/, diagnostics-review/STATE.md).
 *
 *            y   2.. 54  global < HOME | DIAGNOSTICS (20 px)    rule at y=58
 *            y  62..147  LINK: Wi-Fi, backend  | SENSORS: accel, temp/RH
 *            y 151..236  UPTIME: days / h m    | INTERNAL HEAP KiB: free, largest, min
 */

#include "ui_diagnostics.h"

#include <stdio.h>
#include "ui_theme.h"

/* Mission Control Dense, local to this page (other pages keep the 1.3.0 look):
 * square blocks, hairline rules, no fills that are not data. */
#define D_TILE  lv_color_hex(0x0E151D)
#define D_RULE  lv_color_hex(0x24323F)
#define D_HEAD  lv_color_hex(0x6FA8C8)   /* block captions and signal names */
#define D_TXT   lv_color_hex(0xE6EDF2)
#define D_NOM   lv_color_hex(0x3DD68C)
#define D_CAU   lv_color_hex(0xFFA726)
#define D_ALM   lv_color_hex(0xFF4D4D)

#define Q_W 155
#define Q_H 86
#define Q_X0 4
#define Q_X1 161
#define Q_Y0 62
#define Q_Y1 151

static lv_color_t sev_color(ui_diag_sev_t s)
{
    switch (s) {
    case UI_DIAG_SEV_ALM: return D_ALM;
    case UI_DIAG_SEV_CAU: return D_CAU;
    case UI_DIAG_SEV_NOM: return D_NOM;
    default:              return UI_TEXT_DIM;   /* unknown / waiting: silver, never green */
    }
}

static ui_diag_sev_t sev_max(ui_diag_sev_t a, ui_diag_sev_t b)
{
    return a > b ? a : b;
}

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static lv_obj_t *block(lv_obj_t *page, int x, int y)
{
    lv_obj_t *q = lv_obj_create(page);
    lv_obj_remove_style_all(q);
    lv_obj_set_pos(q, x, y);
    lv_obj_set_size(q, Q_W, Q_H);
    lv_obj_set_style_bg_color(q, D_TILE, 0);
    lv_obj_set_style_bg_opa(q, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(q, D_RULE, 0);
    lv_obj_set_style_border_width(q, 1, 0);
    lv_obj_clear_flag(q, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(q, LV_OBJ_FLAG_CLICKABLE);
    return q;
}

static void block_sev(lv_obj_t *q, ui_diag_sev_t s)
{
    bool hot = s >= UI_DIAG_SEV_CAU;
    lv_obj_set_style_border_color(q, hot ? sev_color(s) : D_RULE, 0);
    lv_obj_set_style_border_width(q, hot ? 2 : 1, 0);
}

/* 42 px: name (left) and detail (right) at 14, the word under them at 20.
 * The detail is a fixed one-line box from the end of the measured name to the
 * block edge, so a 19-character Wi-Fi cause ends in dots instead of running
 * into the name. */
static void signal_build(ui_diag_signal_t *s, lv_obj_t *q, int y, const char *name)
{
    lv_obj_t *n = text(q, name, &lv_font_montserrat_14, D_HEAD);
    lv_obj_set_pos(n, 7, y);
    lv_obj_update_layout(n);
    s->detail = text(q, "", &lv_font_montserrat_14, UI_TEXT_DIM);
    /* Placed from the name, not aligned to the block's right edge: the edge
     * moves in when the block's border thickens for a caution, which took
     * the gap to "AUTH FAILED" from 8 px to 4 (the UI-suite tight-gap check
     * caught it). 8 px keeps the two reading as name and detail, and still
     * leaves the longest reason, "AUTH FAILED", its full 95 px. */
    int dx = 7 + lv_obj_get_width(n) + 8;
    lv_obj_set_size(s->detail, Q_W - dx - 5, 16);
    lv_label_set_long_mode(s->detail, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s->detail, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s->detail, dx, y);
    s->word = text(q, "", &lv_font_montserrat_20, D_TXT);
    lv_obj_set_pos(s->word, 7, y + 16);
}

static void set_or_hide(lv_obj_t *label, const char *s)
{
    lv_label_set_text(label, s);
    if (s[0]) {
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void signal_apply(const ui_diag_signal_t *s, const ui_diag_line_t *l)
{
    lv_label_set_text(s->word, l->word);
    lv_obj_set_style_text_color(s->word, sev_color(l->sev), 0);
    set_or_hide(s->detail, l->detail);
}


void ui_diagnostics_build(ui_diagnostics_page_t *w, lv_obj_t *page)
{
    /* Title at 20, not ui_page_title()'s 32: "DIAGNOSTICS" at 32 would run off
     * the screen from x=104. Vertically centred on the HOME button (y 2..54). */
    lv_obj_t *title = text(page, "DIAGNOSTICS", &lv_font_montserrat_20, UI_TITLE);
    lv_obj_set_pos(title, 108, 17);

    lv_obj_t *rule = lv_obj_create(page);
    lv_obj_remove_style_all(rule);
    lv_obj_set_pos(rule, 4, 58);
    lv_obj_set_size(rule, 312, 1);
    lv_obj_set_style_bg_color(rule, D_HEAD, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    w->q_link = block(page, Q_X0, Q_Y0);
    w->q_sens = block(page, Q_X1, Q_Y0);
    w->q_up = block(page, Q_X0, Q_Y1);
    w->q_heap = block(page, Q_X1, Q_Y1);

    signal_build(&w->wifi, w->q_link, 2, "WI-FI");
    signal_build(&w->api, w->q_link, 43, "BACKEND");
    signal_build(&w->accel, w->q_sens, 2, "ACCEL");
    signal_build(&w->env, w->q_sens, 43, "TEMP/RH");

    /* Uptime on two lines so a year-long run (and uint32 seconds' 49,710
     * days) still fits at 20 px. */
    lv_obj_set_pos(text(w->q_up, "UPTIME", &lv_font_montserrat_14, D_HEAD), 7, 2);
    w->days = text(w->q_up, "", &lv_font_montserrat_20, D_TXT);
    lv_obj_set_pos(w->days, 7, 22);
    w->hm = text(w->q_up, "", &lv_font_montserrat_20, D_TXT);
    lv_obj_set_pos(w->hm, 7, 48);

    /* Internal heap only: the unit is in the heading so each number can be
     * the big thing. MIN is the lowest free seen since boot. */
    lv_obj_set_pos(text(w->q_heap, "INT HEAP KiB", &lv_font_montserrat_14, D_HEAD), 7, 2);
    w->heap_word = text(w->q_heap, "", &lv_font_montserrat_14, D_ALM);
    lv_obj_align(w->heap_word, LV_ALIGN_TOP_RIGHT, -7, 2);
    static const char *const names[3] = { "FREE", "LARGEST", "MIN" };
    for (int i = 0; i < 3; i++) {
        /* 22 px pitch = the 20 px line height, so the values never touch */
        lv_obj_set_pos(text(w->q_heap, names[i], &lv_font_montserrat_14, UI_TEXT_DIM), 7, 22 + i * 22);
        w->heap_val[i] = text(w->q_heap, "", &lv_font_montserrat_20, D_TXT);
        lv_obj_align(w->heap_val[i], LV_ALIGN_TOP_RIGHT, -7, 18 + i * 22);
    }
}

void ui_diagnostics_apply(const ui_diagnostics_page_t *w, const ui_diag_view_t *v)
{
    if (w->q_link == NULL) {
        return;
    }
    ui_diag_line_t wifi = ui_diag_wifi_line(v);
    ui_diag_line_t api = ui_diag_api_line(v);
    ui_diag_line_t accel = ui_diag_src_line(v->accel, v->accel_age_ms, true);
    ui_diag_line_t env = ui_diag_src_line(v->env, v->env_age_ms, false);
    signal_apply(&w->wifi, &wifi);
    signal_apply(&w->api, &api);
    signal_apply(&w->accel, &accel);
    signal_apply(&w->env, &env);
    block_sev(w->q_link, sev_max(wifi.sev, api.sev));
    block_sev(w->q_sens, sev_max(accel.sev, env.sev));

    char b[24];
    ui_diag_fmt_days(b, sizeof b, v->uptime_s);
    lv_label_set_text(w->days, b);
    ui_diag_fmt_hm(b, sizeof b, v->uptime_s);
    lv_label_set_text(w->hm, b);

    /* Numbers, not verdicts: a value under its documented reference turns
     * orange/red and the heading says LOW - the word carries it, not colour
     * alone. Nothing here says memory is fine. */
    uint32_t vals[3] = { v->heap_free, v->heap_largest, v->heap_min };
    ui_diag_sev_t sev[3] = { UI_DIAG_SEV_NONE, ui_diag_heap_block_sev(v), ui_diag_heap_min_sev(v) };
    for (int i = 0; i < 3; i++) {
        ui_diag_fmt_kib(b, sizeof b, vals[i]);
        lv_label_set_text(w->heap_val[i], b);
        lv_obj_set_style_text_color(w->heap_val[i], sev[i] >= UI_DIAG_SEV_CAU ? sev_color(sev[i]) : D_TXT, 0);
    }
    ui_diag_sev_t heap = sev_max(sev[1], sev[2]);
    set_or_hide(w->heap_word, heap >= UI_DIAG_SEV_CAU ? "LOW" : "");
    lv_obj_set_style_text_color(w->heap_word, sev_color(heap), 0);
    block_sev(w->q_heap, heap);
}
