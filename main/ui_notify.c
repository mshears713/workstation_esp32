/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief NOTIFY card stack and playback overlay - see ui_notify.h.
 * @details One card in front, the rest peeking out behind it (two ghost
 *          edges at most), position dots, edge keys to move. Cards swap
 *          instantly - no slide over the whole screen.
 *
 *          The cards, in order:
 *            1. the queue's front: the oldest pending message - its text if
 *               loaded, PLAY to hear it (or "No messages waiting");
 *            2. "+N more waiting" when the queue holds more - a count, not
 *               content: the backend only describes the front one;
 *            3. earlier outcomes this boot, newest first.
 *
 *    y   0.. 56   shell's HOME, MESSAGES title
 *    y  58..186   the card; the ones behind show as two edges under it
 *    y 196..238   <   PLAY (front card) or "2 / 5"   >
 */

#include "ui_notify.h"

#include <stdio.h>
#include "ui_kit.h"
#include "ui_theme.h"

#define NC_CARD   lv_color_hex(0x1C1F24)
#define NC_GHOST1 lv_color_hex(0x2A2E35)
#define NC_GHOST2 lv_color_hex(0x202328)
#define NC_EDGE   lv_color_hex(0x4A515B)

#define CX 12
#define CY 58
#define CW 288
#define CH 128

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 10, 0);
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

static lv_obj_t *key(lv_obj_t *parent, int x, int y, int w, int h, const char *label, bool primary,
                     lv_event_cb_t cb, void *ud)
{
    lv_obj_t *k = lv_button_create(parent);
    lv_obj_remove_style_all(k);
    lv_obj_set_pos(k, x, y);
    lv_obj_set_size(k, w, h);
    lv_obj_set_style_radius(k, 8, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(k, primary ? UI_ACCENT : NC_CARD, 0);
    lv_obj_set_style_border_color(k, UI_ACCENT, 0);
    lv_obj_set_style_border_width(k, primary ? 0 : 2, 0);
    lv_obj_set_style_bg_color(k, lv_color_hex(0x0D3C7A), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(k, NC_EDGE, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(k, primary ? NC_GHOST1 : NC_CARD, LV_STATE_DISABLED);
    if (cb) {
        lv_obj_add_event_cb(k, cb, LV_EVENT_CLICKED, ud);
    }
    lv_obj_t *l = text(k, label, &lv_font_montserrat_20, UI_TEXT);
    lv_obj_set_style_text_color(l, lv_color_hex(0x6B7280), LV_STATE_DISABLED);
    lv_obj_center(l);
    return k;
}

static void enable(lv_obj_t *k, bool on)
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

static void show(lv_obj_t *o, bool on)
{
    if (on) {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void fmt_ago(uint32_t s, char *b, size_t n)
{
    if (s < 60u) {
        snprintf(b, n, "just now");
    } else if (s < 3600u) {
        snprintf(b, n, "%u min ago", (unsigned)(s / 60u));
    } else if (s < 86400u) {
        snprintf(b, n, "%u h ago", (unsigned)(s / 3600u));
    } else {
        snprintf(b, n, "%u d ago", (unsigned)(s / 86400u));
    }
}

static int more_cards(const ui_notify_view_t *v)
{
    return v->pending > 1 ? 1 : 0;
}

static void draw(ui_notify_page_t *p)
{
    const ui_notify_view_t *v = &p->view;
    p->total = 1 + more_cards(v) + v->done_count;
    if (p->index >= p->total) {
        p->index = p->total - 1;
    }
    if (p->index < 0) {
        p->index = 0;
    }
    int behind = p->total - 1 - p->index;
    show(p->ghost1, behind >= 1);
    show(p->ghost2, behind >= 2);

    char kicker[48] = "", headline[48] = "", meta[48] = "", ago[20];
    const char *body = "";
    lv_color_t kc = UI_TEXT_DIM, hc = UI_TEXT;
    bool play = false;
    bool link_ok = v->link == UI_NOTIFY_LINK_OK;
    lv_obj_set_style_border_width(p->card, 0, 0);

    if (p->index == 0) {
        if (v->link == UI_NOTIFY_LINK_UNKNOWN) {
            snprintf(kicker, sizeof(kicker), "MESSAGES");
            snprintf(headline, sizeof(headline), "Checking...");
            body = "Asking CLAWBOX for waiting messages.";
        } else if (v->pending <= 0) {
            snprintf(kicker, sizeof(kicker), "QUEUE");
            snprintf(headline, sizeof(headline), "No messages waiting");
            body = v->done_count ? "Earlier ones from this boot are behind this card."
                                 : "Nothing played since the device started.";
        } else {
            snprintf(kicker, sizeof(kicker), "WAITING  1 of %d", v->pending);
            snprintf(headline, sizeof(headline), "New message");
            hc = UI_WARN;
            if (v->front_text) {
                body = v->front_text;
            } else if (v->front_text_failed) {
                body = "Its text could not be loaded. PLAY to hear it.";
            } else if (!link_ok) {
                body = "Its text has not been loaded.";   /* nothing is loading while offline */
            } else {
                body = "Loading its text...";
            }
            if (v->front_age_known) {
                fmt_ago(v->front_age_s, ago, sizeof(ago));
                snprintf(meta, sizeof(meta), "sent %s", ago);
            }
            play = true;
        }
        if (v->link == UI_NOTIFY_LINK_DOWN || v->link == UI_NOTIFY_LINK_NO_NET) {
            /* The count is the last one CLAWBOX gave: say so, and do not
             * offer PLAY, which needs CLAWBOX to fetch the audio. */
            snprintf(meta, sizeof(meta), LV_SYMBOL_WARNING " %s: may be old",
                     v->link == UI_NOTIFY_LINK_DOWN ? "CLAWBOX not answering" : "No network");
            kc = UI_WARN;
        }
    } else if (p->index == 1 && more_cards(v)) {
        snprintf(kicker, sizeof(kicker), "WAITING  2-%d of %d", v->pending, v->pending);
        snprintf(headline, sizeof(headline), "+%d more waiting", v->pending - 1);
        body = "They play in order, oldest first. Each one's text shows here once it reaches the front.";
    } else {
        const ui_notify_done_t *d = &v->done[p->index - 1 - more_cards(v)];
        fmt_ago(v->now_s > d->uptime_s ? v->now_s - d->uptime_s : 0, ago, sizeof(ago));
        snprintf(kicker, sizeof(kicker), "EARLIER  %s", ago);
        switch (d->outcome) {
        case UI_NOTIFY_DELIVERED:
            snprintf(headline, sizeof(headline), LV_SYMBOL_OK " Delivered");
            hc = UI_OK;
            snprintf(meta, sizeof(meta), "played and acknowledged");
            break;
        case UI_NOTIFY_ACK_FAILED:
            snprintf(headline, sizeof(headline), "Played, not acknowledged");
            hc = UI_WARN;
            snprintf(meta, sizeof(meta), "CLAWBOX did not take the ack: it will come back");
            break;
        case UI_NOTIFY_STOPPED:
            snprintf(headline, sizeof(headline), "Stopped");
            hc = UI_WARN;
            snprintf(meta, sizeof(meta), "not acknowledged: still in the queue");
            break;
        case UI_NOTIFY_FETCH_FAILED:
        default:
            snprintf(headline, sizeof(headline), "Could not fetch");
            hc = UI_ERR;
            snprintf(meta, sizeof(meta), "%s", d->reason ? d->reason : "not played: still in the queue");
            break;
        }
        body = d->text ? d->text : "(its text was not loaded)";
    }

    lv_label_set_text(p->kicker, kicker);
    lv_obj_set_style_text_color(p->kicker, UI_TEXT_DIM, 0);
    lv_label_set_text(p->headline, headline);
    lv_obj_set_style_text_color(p->headline, hc, 0);
    lv_label_set_text(p->body, body);
    lv_label_set_text(p->meta, meta);
    lv_obj_set_style_text_color(p->meta, kc, 0);
    show(p->play, play);
    enable(p->play, play && link_ok);

    enable(p->prev, p->index > 0);
    enable(p->next, p->index < p->total - 1);
    /* Where you are, in the spot PLAY takes on the front card. */
    lv_label_set_text_fmt(p->dots, "%d / %d", p->index + 1, p->total);
    show(p->dots, !play);
    lv_obj_align(p->dots, LV_ALIGN_TOP_MID, 0, 206);
}

static void prev_cb(lv_event_t *e)
{
    ui_notify_page_t *p = lv_event_get_user_data(e);
    p->index--;
    draw(p);
}

static void next_cb(lv_event_t *e)
{
    ui_notify_page_t *p = lv_event_get_user_data(e);
    p->index++;
    draw(p);
}

void ui_notify_build(ui_notify_page_t *p, lv_obj_t *page, const ui_notify_actions_t *a)
{
    *p = (ui_notify_page_t){ .page = page };
    ui_page_title(page, "MESSAGES");

    /* Cards behind: inset and lower, so their edges read as a stack. */
    p->ghost2 = box(page, CX + 16, CY + 8, CW - 32, CH, NC_GHOST2);
    p->ghost1 = box(page, CX + 8, CY + 4, CW - 16, CH, NC_GHOST1);
    lv_obj_set_style_border_color(p->ghost1, NC_EDGE, 0);
    lv_obj_set_style_border_width(p->ghost1, 1, 0);
    lv_obj_set_style_border_color(p->ghost2, NC_EDGE, 0);
    lv_obj_set_style_border_width(p->ghost2, 1, 0);
    p->card = box(page, CX, CY, CW, CH, NC_CARD);

    p->kicker = text(p->card, "", &lv_font_montserrat_12, UI_TEXT_DIM);
    lv_obj_set_pos(p->kicker, 12, 5);
    p->headline = text(p->card, "", &lv_font_montserrat_20, UI_TEXT);
    lv_obj_set_pos(p->headline, 12, 20);
    p->body = text(p->card, "", &lv_font_montserrat_14, UI_TEXT);
    lv_label_set_long_mode(p->body, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(p->body, CW - 24, 64);   /* four lines (~170 characters); longer ends in dots */
    lv_obj_set_pos(p->body, 12, 44);
    p->meta = text(p->card, "", &lv_font_montserrat_12, UI_TEXT_DIM);
    lv_label_set_long_mode(p->meta, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(p->meta, CW - 24, 15);
    lv_obj_set_pos(p->meta, 12, 110);

    p->play = key(page, 88, 196, 144, 42, LV_SYMBOL_PLAY " PLAY", true, a ? a->play : NULL, NULL);

    p->prev = key(page, 12, 196, 64, 42, LV_SYMBOL_LEFT, false, prev_cb, p);
    p->next = key(page, 244, 196, 64, 42, LV_SYMBOL_RIGHT, false, next_cb, p);
    p->dots = text(page, "", &lv_font_montserrat_20, UI_TEXT_DIM);
    lv_obj_align(p->dots, LV_ALIGN_TOP_MID, 0, 204);
}

void ui_notify_apply(ui_notify_page_t *p, const ui_notify_view_t *v)
{
    /* A new front message brings the stack back to it. */
    bool new_front = v->pending > 0 && (p->view.pending != v->pending || p->view.front_text != v->front_text);
    p->view = *v;
    if (new_front) {
        p->index = 0;
    }
    draw(p);
}

/* ---- playback overlay ---------------------------------------------------- */

void ui_notify_overlay_build(ui_notify_overlay_t *o, lv_obj_t *scr, lv_event_cb_t stop_cb)
{
    *o = (ui_notify_overlay_t){ 0 };
    o->root = box(scr, 0, 0, 320, 240, UI_BG);
    lv_obj_set_style_radius(o->root, 0, 0);
    lv_obj_add_flag(o->root, LV_OBJ_FLAG_CLICKABLE);   /* nothing underneath is reachable */

    lv_obj_t *card = box(o->root, 12, 10, 296, 112, NC_CARD);
    o->kicker = text(card, "MESSAGE", &lv_font_montserrat_12, UI_TEXT_DIM);
    lv_obj_set_pos(o->kicker, 12, 8);
    o->text = text(card, "", &lv_font_montserrat_20, UI_TEXT);
    lv_label_set_long_mode(o->text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(o->text, 272, 72);
    lv_obj_set_pos(o->text, 12, 26);

    /* FETCH -> PLAY -> ACK: playing and acknowledging are different facts. */
    static const char *const names[3] = { "FETCH", "PLAY", "ACK" };
    for (int i = 0; i < 3; i++) {
        o->step[i] = box(o->root, 12 + i * 100, 130, 96, 30, NC_CARD);
        lv_obj_set_style_radius(o->step[i], 6, 0);
        o->step_word[i] = text(o->step[i], names[i], &lv_font_montserrat_14, UI_TEXT_DIM);
        lv_obj_center(o->step_word[i]);
    }
    o->result = text(o->root, "", &lv_font_montserrat_14, UI_TEXT);
    lv_label_set_long_mode(o->result, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_long_mode(o->result, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(o->result, 296);
    lv_obj_set_pos(o->result, 12, 166);

    o->stop = lv_button_create(o->root);
    lv_obj_remove_style_all(o->stop);
    lv_obj_set_size(o->stop, 200, 48);
    lv_obj_set_pos(o->stop, 60, 192);
    lv_obj_set_style_radius(o->stop, 8, 0);
    lv_obj_set_style_bg_color(o->stop, UI_ERR, 0);
    lv_obj_set_style_bg_opa(o->stop, LV_OPA_COVER, 0);
    if (stop_cb) {
        lv_obj_add_event_cb(o->stop, stop_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *sl = text(o->stop, LV_SYMBOL_STOP " STOP", &lv_font_montserrat_20, UI_TEXT);
    lv_obj_center(sl);
    lv_obj_add_flag(o->root, LV_OBJ_FLAG_HIDDEN);
}

static void step(ui_notify_overlay_t *o, int i, int state)   /* 0 to come, 1 now, 2 done, 3 failed */
{
    static const uint32_t bg[4] = { 0x1C1F24, 0x1565C0, 0x1B5E20, 0x7F1D1D };
    lv_obj_set_style_bg_color(o->step[i], lv_color_hex(bg[state]), 0);
    lv_obj_set_style_text_color(o->step_word[i], state == 0 ? UI_TEXT_DIM : UI_TEXT, 0);
    static const char *const names[3] = { "FETCH", "PLAY", "ACK" };
    if (state == 2) {
        lv_label_set_text_fmt(o->step_word[i], LV_SYMBOL_OK " %s", names[i]);
    } else if (state == 3) {
        lv_label_set_text_fmt(o->step_word[i], LV_SYMBOL_CLOSE " %s", names[i]);
    } else {
        lv_label_set_text(o->step_word[i], names[i]);
    }
}

void ui_notify_overlay_apply(ui_notify_overlay_t *o, const ui_notify_pb_view_t *v)
{
    show(o->root, true);
    lv_obj_move_foreground(o->root);
    lv_label_set_text(o->text, v->text ? v->text : "(text not loaded - listen)");
    lv_obj_set_style_text_color(o->text, v->text ? UI_TEXT : UI_TEXT_DIM, 0);

    const char *res = "";
    lv_color_t rc = UI_TEXT_DIM;
    int s[3] = { 0, 0, 0 };
    switch (v->phase) {
    case UI_NOTIFY_PB_FETCHING: s[0] = 1; res = "Fetching the audio from CLAWBOX..."; break;
    case UI_NOTIFY_PB_PLAYING:  s[0] = 2; s[1] = 1; res = "Playing on the speaker"; break;
    case UI_NOTIFY_PB_ACKING:   s[0] = 2; s[1] = 2; s[2] = 1; res = "Telling CLAWBOX it was heard..."; break;
    case UI_NOTIFY_PB_DONE:
    default:
        switch (v->outcome) {
        case UI_NOTIFY_DELIVERED:
            s[0] = s[1] = s[2] = 2;
            res = "Delivered: played and acknowledged";
            rc = UI_OK;
            break;
        case UI_NOTIFY_ACK_FAILED:
            s[0] = s[1] = 2;
            s[2] = 3;
            res = "Played, but the ack failed: it will come back";
            rc = UI_WARN;
            break;
        case UI_NOTIFY_STOPPED:
            s[0] = 2;
            s[1] = 3;
            res = "Stopped: not acknowledged, still queued";
            rc = UI_WARN;
            break;
        case UI_NOTIFY_FETCH_FAILED:
        default:
            s[0] = 3;
            res = v->reason ? v->reason : "Could not fetch the audio";
            rc = UI_ERR;
            break;
        }
        break;
    }
    for (int i = 0; i < 3; i++) {
        step(o, i, s[i]);
    }
    lv_label_set_text(o->result, res);
    lv_obj_set_style_text_color(o->result, rc, 0);
    /* STOP means something only while the speaker is playing. */
    show(o->stop, v->phase == UI_NOTIFY_PB_PLAYING);
    if (v->remaining > 0 && v->phase == UI_NOTIFY_PB_DONE) {
        lv_label_set_text_fmt(o->kicker, "MESSAGE  -  %d more waiting", v->remaining);
    } else {
        lv_label_set_text(o->kicker, "MESSAGE");
    }
}

void ui_notify_overlay_hide(ui_notify_overlay_t *o)
{
    show(o->root, false);
}
