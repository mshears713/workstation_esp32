/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Recording overlay with a health rail - see ui_recording.h.
 * @details The rail and the main area are computed from the same view, so
 *          they cannot tell different stories: a stalled uploader is
 *          "PAUSED" + "WAITING ON BACKEND" in the middle, API SLOW, MIC HELD
 *          and UPL WAIT on the rail; a failed upload is the red outline card
 *          and API/UPL ERR; no endpoint is "NOT SENT" and UPL OFF.
 *
 *          Controls follow what the capture can still do, not habit:
 *          CANCEL shows only while ARMING/RECORDING (audio_capture checks the
 *          cancel flag only inside its recording loop; before the worker
 *          starts the flag is reset, after it it is ignored), SEND only for
 *          a long-form NOTE, the selector only until the upload starts.
 *
 *    x 0..44     rail: NET  API  MIC  UPL (glyph + word, fill = problem)
 *    y   0.. 40  command (32) + destination, status word right
 *    y  44..100  red dot + timer (48) | "of 15s" / "cap 20:00" + how it ends
 *    y 102..126  progress bar (bounded) or the stall band
 *    y  44..126  outcome card instead, once the recording is over
 *    y 128..184  project (NOTE) / repository (GO) chooser
 *    y 188..236  CANCEL | SEND
 */

#include "ui_recording.h"

#include <stdio.h>
#include <string.h>

#define RC_BG    lv_color_hex(0x12171A)
#define RC_PANEL lv_color_hex(0x1E272C)
#define RC_INK   lv_color_hex(0x0A0D0F)
#define RC_TEXT  lv_color_hex(0xECEFF1)
#define RC_DIM   lv_color_hex(0x9AA7AE)
#define RC_OK    lv_color_hex(0x2ECC71)
#define RC_WARN  lv_color_hex(0xFFA726)
#define RC_FAIL  lv_color_hex(0xEF5350)
#define RC_NONE  lv_color_hex(0x78909C)
#define RC_INFO  lv_color_hex(0x4FC3F7)

#define X0 52
#define W 264

typedef enum { SEG_OK, SEG_INFO, SEG_NONE, SEG_WARN, SEG_FAIL } seg_kind_t;

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

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static lv_obj_t *key(lv_obj_t *parent, int x, int y, int w, int h, const char *label, lv_color_t fill,
                     lv_color_t ink, lv_event_cb_t cb)
{
    lv_obj_t *k = lv_button_create(parent);
    lv_obj_remove_style_all(k);
    lv_obj_set_pos(k, x, y);
    lv_obj_set_size(k, w, h);
    lv_obj_set_style_radius(k, 6, 0);
    lv_obj_set_style_bg_color(k, fill, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(k, RC_TEXT, LV_STATE_PRESSED);
    if (cb) {
        lv_obj_add_event_cb(k, cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *l = text(k, label, &lv_font_montserrat_20, ink);
    lv_obj_center(l);
    return k;
}

static void show(lv_obj_t *o, bool on)
{
    if (on) {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_recording_build(ui_recording_t *r, lv_obj_t *scr, const ui_rec_actions_t *a)
{
    *r = (ui_recording_t){ 0 };
    r->root = box(scr, 0, 0, 320, 240);
    lv_obj_set_style_bg_color(r->root, RC_BG, 0);
    lv_obj_set_style_bg_opa(r->root, LV_OPA_COVER, 0);
    /* Clickable so a tap that misses the keys stops here, as the old
     * full-screen overlay did - nothing underneath can be reached. */
    lv_obj_add_flag(r->root, LV_OBJ_FLAG_CLICKABLE);

    static const char *const glyphs[4] = { LV_SYMBOL_WIFI, LV_SYMBOL_DRIVE, LV_SYMBOL_AUDIO, LV_SYMBOL_UPLOAD };
    for (int i = 0; i < 4; i++) {
        r->seg[i] = box(r->root, 0, i * 60, 44, 58);
        lv_obj_set_style_bg_opa(r->seg[i], LV_OPA_COVER, 0);
        r->seg_tick[i] = box(r->seg[i], 0, 0, 3, 58);
        lv_obj_set_style_bg_opa(r->seg_tick[i], LV_OPA_COVER, 0);
        r->seg_glyph[i] = text(r->seg[i], glyphs[i], &lv_font_montserrat_20, RC_TEXT);
        lv_obj_align(r->seg_glyph[i], LV_ALIGN_TOP_MID, 1, 9);
        r->seg_word[i] = text(r->seg[i], "", &lv_font_montserrat_12, RC_TEXT);
        lv_obj_align(r->seg_word[i], LV_ALIGN_TOP_MID, 1, 36);
    }
    lv_obj_t *edge = box(r->root, 44, 0, 2, 240);
    lv_obj_set_style_bg_color(edge, lv_color_hex(0x2A3338), 0);
    lv_obj_set_style_bg_opa(edge, LV_OPA_COVER, 0);

    r->command = text(r->root, "", &lv_font_montserrat_32, RC_TEXT);
    lv_obj_set_pos(r->command, X0, 2);
    r->dest = text(r->root, "", &lv_font_montserrat_14, RC_DIM);
    r->status = text(r->root, "", &lv_font_montserrat_14, RC_TEXT);
    lv_obj_align(r->status, LV_ALIGN_TOP_RIGHT, -6, 14);

    r->timer_row = box(r->root, X0, 44, W, 58);
    r->dot = box(r->timer_row, 0, 20, 16, 16);
    lv_obj_set_style_radius(r->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(r->dot, RC_FAIL, 0);
    lv_obj_set_style_bg_opa(r->dot, LV_OPA_COVER, 0);
    r->timer = text(r->timer_row, "", &lv_font_montserrat_48, RC_TEXT);
    lv_obj_set_pos(r->timer, 22, 0);
    r->aside = text(r->timer_row, "", &lv_font_montserrat_20, RC_DIM);
    lv_obj_align(r->aside, LV_ALIGN_TOP_RIGHT, 0, 6);
    r->aside2 = text(r->timer_row, "", &lv_font_montserrat_14, RC_DIM);
    lv_obj_align(r->aside2, LV_ALIGN_TOP_RIGHT, 0, 32);

    r->bar = lv_bar_create(r->root);
    lv_obj_remove_style_all(r->bar);
    lv_obj_set_pos(r->bar, X0, 108);
    lv_obj_set_size(r->bar, W, 10);
    lv_bar_set_range(r->bar, 0, 1000);
    lv_obj_set_style_bg_color(r->bar, RC_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(r->bar, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r->bar, RC_FAIL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(r->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(r->bar, 5, LV_PART_INDICATOR);

    r->stall = box(r->root, X0, 102, W, 24);
    lv_obj_set_style_bg_color(r->stall, RC_WARN, 0);
    lv_obj_set_style_bg_opa(r->stall, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r->stall, 4, 0);
    lv_obj_t *st = text(r->stall, "WAITING ON BACKEND", &lv_font_montserrat_14, RC_INK);
    lv_obj_center(st);

    r->card = box(r->root, X0, 44, W, 82);
    lv_obj_set_style_radius(r->card, 8, 0);
    lv_obj_set_style_border_width(r->card, 3, 0);
    r->card_word = text(r->card, "", &lv_font_montserrat_32, RC_TEXT);
    lv_obj_align(r->card_word, LV_ALIGN_TOP_LEFT, 12, 6);
    r->card_detail = text(r->card, "", &lv_font_montserrat_14, RC_TEXT);
    lv_label_set_long_mode(r->card_detail, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(r->card_detail, W - 24);
    lv_obj_align(r->card_detail, LV_ALIGN_TOP_LEFT, 12, 44);

    r->sel = box(r->root, X0, 128, W, 56);
    r->sel_caption = text(r->sel, "", &lv_font_montserrat_12, RC_DIM);
    lv_obj_set_pos(r->sel_caption, 0, 0);
    lv_obj_t *prev = key(r->sel, 0, 12, 48, 44, LV_SYMBOL_LEFT, RC_PANEL, RC_TEXT, a ? a->sel_prev : NULL);
    (void)prev;
    lv_obj_t *next = key(r->sel, W - 48, 12, 48, 44, LV_SYMBOL_RIGHT, RC_PANEL, RC_TEXT, a ? a->sel_next : NULL);
    (void)next;
    r->sel_label = text(r->sel, "", &lv_font_montserrat_20, RC_TEXT);
    lv_label_set_long_mode(r->sel_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(r->sel_label, W - 112, 24);
    lv_obj_set_style_text_align(r->sel_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(r->sel_label, 56, 22);

    /* CANCEL keeps the left spot even when SEND is absent: the destructive
     * key must not move under the thumb depending on the command. */
    r->cancel = key(r->root, X0, 188, 128, 48, LV_SYMBOL_CLOSE " CANCEL", RC_FAIL, RC_INK, a ? a->cancel : NULL);
    r->send = key(r->root, X0 + W - 128, 188, 128, 48, "SEND " LV_SYMBOL_RIGHT, RC_OK, RC_INK, a ? a->send : NULL);

    lv_obj_add_flag(r->root, LV_OBJ_FLAG_HIDDEN);
}

static void seg_set(ui_recording_t *r, int i, seg_kind_t k, const char *word)
{
    static const uint32_t colors[] = { 0x2ECC71, 0x4FC3F7, 0x78909C, 0xFFA726, 0xEF5350 };
    lv_color_t c = lv_color_hex(colors[k]);
    bool solid = k == SEG_WARN || k == SEG_FAIL;
    /* A problem is a solid block (shape and colour); healthy or unknown is a
     * dark segment with a coloured tick and glyph. */
    lv_obj_set_style_bg_color(r->seg[i], solid ? c : RC_INK, 0);
    lv_obj_set_style_bg_color(r->seg_tick[i], c, 0);
    lv_obj_set_style_text_color(r->seg_glyph[i], solid ? RC_INK : c, 0);
    lv_obj_set_style_text_color(r->seg_word[i], solid ? RC_INK : RC_TEXT, 0);
    lv_label_set_text(r->seg_word[i], word);
}

static bool result_is_bad(const char *s)
{
    /* status_deck_ui.c's rule for the same strings */
    return strstr(s, "FAILED") || strstr(s, "CANCELLED") || strstr(s, "NOT ") || strstr(s, "NO ");
}

static void fmt_mmss(uint32_t ms, char *b, size_t n)
{
    uint32_t s = ms / 1000u;
    snprintf(b, n, "%02u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
}

static void card(ui_recording_t *r, const char *word, const char *detail, lv_color_t c, bool solid)
{
    lv_obj_set_style_bg_color(r->card, solid ? c : RC_BG, 0);
    lv_obj_set_style_bg_opa(r->card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(r->card, c, 0);
    lv_obj_set_style_text_color(r->card_word, solid ? RC_INK : c, 0);
    lv_obj_set_style_text_color(r->card_detail, solid ? RC_INK : RC_TEXT, 0);
    lv_label_set_text(r->card_word, word);
    lv_obj_set_style_text_font(r->card_word, &lv_font_montserrat_32, 0);
    lv_obj_update_layout(r->card_word);
    if (lv_obj_get_width(r->card_word) > W - 24) {
        lv_obj_set_style_text_font(r->card_word, &lv_font_montserrat_20, 0);
    }
    lv_label_set_text(r->card_detail, detail ? detail : "");
}

void ui_recording_hide(ui_recording_t *r)
{
    lv_obj_add_flag(r->root, LV_OBJ_FLAG_HIDDEN);
}

void ui_recording_apply(ui_recording_t *r, const ui_rec_view_t *v)
{
    show(r->root, true);
    lv_obj_move_foreground(r->root);

    const char *cmd = v->command ? v->command : "";
    bool note = strcmp(cmd, "NOTE") == 0, go = strcmp(cmd, "GO") == 0;
    bool bounded = v->target_ms > 0 && v->target_ms <= UI_REC_AUTO_STOP_MAX_MS;
    bool capturing = v->phase == UI_REC_ARMING || v->phase == UI_REC_RECORDING;
    bool behind = v->phase == UI_REC_RECORDING && v->uploader_behind;
    bool no_endpoint = v->phase == UI_REC_FAILED && v->fail_reason && strcmp(v->fail_reason, "ENDPOINT NOT SET") == 0;
    bool net_up = v->wifi == UI_DIAG_WIFI_ONLINE;

    lv_label_set_text(r->command, cmd);
    lv_label_set_text(r->dest, note ? "to project" : go ? "GitHub issue" : "quick note");
    lv_obj_update_layout(r->command);
    lv_obj_set_pos(r->dest, X0 + lv_obj_get_width(r->command) + 12, 16);

    /* ---- rail -------------------------------------------------------- */
    seg_set(r, 0, net_up ? SEG_OK : v->wifi == UI_DIAG_WIFI_JOINING ? SEG_INFO : SEG_FAIL,
            net_up ? "OK" : v->wifi == UI_DIAG_WIFI_JOINING ? "JOIN" : v->wifi == UI_DIAG_WIFI_RETRY ? "LOST" : "OFF");
    if (!net_up) {
        seg_set(r, 1, SEG_NONE, "--");
    } else if (no_endpoint) {
        seg_set(r, 1, SEG_NONE, "OFF");
    } else if (v->phase == UI_REC_FAILED) {
        seg_set(r, 1, SEG_FAIL, "ERR");
    } else if (behind) {
        seg_set(r, 1, SEG_WARN, "SLOW");
    } else if (v->api == UI_DIAG_API_DOWN) {
        seg_set(r, 1, SEG_FAIL, "DOWN");
    } else {
        seg_set(r, 1, v->api == UI_DIAG_API_OK ? SEG_OK : SEG_NONE, v->api == UI_DIAG_API_OK ? "OK" : "--");
    }
    if (v->phase == UI_REC_ARMING) {
        seg_set(r, 2, SEG_INFO, "ARM");
    } else if (behind) {
        seg_set(r, 2, SEG_WARN, "HELD");
    } else if (v->phase == UI_REC_RECORDING) {
        seg_set(r, 2, SEG_OK, "LIVE");
    } else {
        seg_set(r, 2, SEG_NONE, "OFF");
    }
    switch (v->phase) {
    case UI_REC_RECORDING: seg_set(r, 3, behind ? SEG_WARN : SEG_OK, behind ? "WAIT" : "OK"); break;
    case UI_REC_UPLOADING: seg_set(r, 3, SEG_INFO, "SEND"); break;
    case UI_REC_READY:     seg_set(r, 3, SEG_OK, "DONE"); break;
    case UI_REC_FAILED:    seg_set(r, 3, no_endpoint ? SEG_NONE : SEG_FAIL, no_endpoint ? "OFF" : "ERR"); break;
    case UI_REC_RESULT: {
        bool bad = v->result && result_is_bad(v->result);
        seg_set(r, 3, bad ? SEG_NONE : SEG_OK, bad ? "--" : "DONE");
        break;
    }
    default:               seg_set(r, 3, SEG_NONE, "--"); break;
    }

    /* ---- main ---------------------------------------------------------- */
    bool over = !capturing && v->phase != UI_REC_STARTING;
    show(r->timer_row, !over);
    show(r->card, over);
    show(r->stall, behind);
    show(r->bar, v->phase == UI_REC_RECORDING && bounded && !behind);

    char t[12];
    lv_color_t status_c = RC_DIM;
    const char *status = "";
    if (!over) {
        fmt_mmss(v->phase == UI_REC_RECORDING ? v->elapsed_ms : 0, t, sizeof(t));
        lv_label_set_text(r->timer, v->phase == UI_REC_STARTING ? "--:--" : t);
        lv_obj_set_style_text_color(r->timer, v->phase != UI_REC_RECORDING ? RC_DIM : behind ? RC_WARN : RC_TEXT, 0);
        /* The dot means the mic is live: not while arming, not while the
         * loop is stalled waiting for a buffer (the timer is frozen then). */
        show(r->dot, v->phase == UI_REC_RECORDING && !behind);
        if (bounded) {
            lv_label_set_text_fmt(r->aside, "of %lus", (unsigned long)(v->target_ms / 1000u));
            lv_label_set_text(r->aside2, "ends by itself");
        } else {
            fmt_mmss(v->target_ms, t, sizeof(t));
            lv_label_set_text_fmt(r->aside, "cap %s", t);
            lv_label_set_text(r->aside2, "SEND to finish");
        }
        if (v->phase == UI_REC_RECORDING && bounded) {
            int32_t pm = (int32_t)((uint64_t)v->elapsed_ms * 1000u / v->target_ms);
            lv_bar_set_value(r->bar, pm > 1000 ? 1000 : pm, LV_ANIM_OFF);
        }
        status = v->phase == UI_REC_STARTING ? "STARTING" : v->phase == UI_REC_ARMING ? "ARMING"
               : behind ? "PAUSED" : "LIVE MIC";
        status_c = v->phase == UI_REC_RECORDING ? (behind ? RC_WARN : RC_FAIL) : RC_DIM;
    } else {
        char word[32], detail[64];
        uint32_t s = v->elapsed_ms / 1000u;
        switch (v->phase) {
        case UI_REC_UPLOADING:
            snprintf(detail, sizeof(detail), "%02u:%02u recorded - finishing the upload", (unsigned)(s / 60u),
                     (unsigned)(s % 60u));
            card(r, "UPLOADING", detail, RC_INFO, false);
            status = "UPLOADING";
            status_c = RC_INFO;
            break;
        case UI_REC_READY:
            card(r, note ? "NOTE SENT" : "SENT", "the backend accepted the upload", RC_OK, true);
            status = "DONE";
            status_c = RC_OK;
            break;
        case UI_REC_CANCELLED:
            card(r, "CANCELLED", "discarded - nothing was sent", RC_NONE, false);
            status = "CANCELLED";
            break;
        case UI_REC_FAILED:
            if (no_endpoint) {
                card(r, "NOT SENT", "recorded, but no endpoint is set for it", RC_NONE, false);
                status = "NO ENDPOINT";
            } else {
                card(r, "UPLOAD FAILED", v->fail_reason ? v->fail_reason : "", RC_FAIL, false);
                status = "FAILED";
                status_c = RC_FAIL;
            }
            break;
        case UI_REC_RESULT:
        default: {
            const char *res = v->result ? v->result : "";
            const char *colon = strstr(res, ": ");
            bool bad = result_is_bad(res);
            /* "FAILED: UPLOAD TIMEOUT" -> word + detail; "ISSUE #13" stays whole */
            snprintf(word, sizeof(word), "%.*s", colon ? (int)(colon - res) : (int)sizeof(word) - 1, res);
            card(r, word, colon ? colon + 2 : (bad ? "" : "the backend accepted it"), bad ? RC_WARN : RC_OK, !bad);
            status = bad ? "NOT SENT" : "DONE";
            status_c = bad ? RC_WARN : RC_OK;
            break;
        }
        }
    }
    /* Once it is over, the card says what happened; the corner word would
     * only repeat it and crowd the destination. */
    show(r->status, !over);
    lv_label_set_text(r->status, status);
    lv_obj_set_style_text_color(r->status, status_c, 0);

    bool sel = (note || go) && v->selector && (capturing || v->phase == UI_REC_STARTING);
    show(r->sel, sel);
    if (sel) {
        lv_label_set_text(r->sel_caption, note ? "NOTE TO PROJECT" : "GO FILES TO (GITHUB)");
        lv_label_set_text(r->sel_label, v->selector);
    }
    show(r->cancel, capturing);
    show(r->send, capturing && !bounded);
}
