/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Event-feedback survey - see ui_survey.h.
 * @details One sheet, one body. Each step is built into the body when it is
 *          reached and thrown away when it is left, so only the screen on
 *          view exists. Step changes requested from a key's own click are
 *          deferred with lv_async_call: rebuilding the body deletes that key,
 *          and LVGL must not delete an object inside its own event.
 *
 *    y   0.. 40   header: where you are (left), SAMPLE tag (right), ink rule
 *    y  48.. 96   the question, 20 px, up to two lines
 *    y 100..180   the answer control
 *    y 184..232   Back (outlined) and Next (filled; disabled until answered)
 */

#include "ui_survey.h"

#include <stdio.h>
#include <string.h>
#include "ui_paper.h"
#include "ui_theme.h"

/* Placeholders until Mike writes the real ones (and decides where answers
 * go). Chosen to exercise the three controls: a 1-5 rating, a 0-10 slider
 * and a three-way choice. */
const ui_survey_question_t ui_survey_sample_questions[UI_SURVEY_QUESTIONS] = {
    { "How useful was the demo you just saw?", "Useful", UI_SURVEY_RATING, 1, 5,
      "Not useful", "Very useful", { 0 } },
    { "How likely are you to tell a friend about it?", "Tell a friend", UI_SURVEY_SLIDER, 0, 10,
      "Not likely", "Very likely", { 0 } },
    { "What should we build next?", "Next build", UI_SURVEY_CHOICE, 0, 2, NULL, NULL,
      { "Longer\nbattery", "Phone\napp", "More\nsensors" } },
};

#define HOLD_TO_EXIT_MS 3000
#define X0 16
#define W 288

static struct {
    const ui_survey_config_t *cfg;
    lv_obj_t *sheet;
    lv_obj_t *body;
    lv_timer_t *tick;
    ui_survey_step_t step;
    ui_survey_step_t pending;      /* applied by apply_pending_step() */
    int answer[UI_SURVEY_QUESTIONS];
    bool submitted;                /* this visitor's answers are stored; no second submit */
    bool save_failed;
    uint32_t response_no;
    uint32_t thanks_left_s;
    uint32_t hold_start;
    bool exit_fired;
    /* the current step's live objects (children of body) */
    lv_obj_t *next_key;
    lv_obj_t *tiles[5];
    int tile_count;
    lv_obj_t *value_label;
    lv_obj_t *slider;
    lv_obj_t *minus_key, *plus_key;
    lv_obj_t *countdown;
} sv;

static void build_step(void);

/* ---- small helpers -------------------------------------------------- */

static const ui_survey_question_t *question(int i)
{
    return &sv.cfg->questions[i];
}

static int current_q(void)
{
    return (int)sv.step - (int)UI_SURVEY_STEP_Q1;   /* 0..2 on a question step */
}

static void set_enabled(lv_obj_t *key, bool on)
{
    ui_paper_key_set_enabled(key, on);
}

/* Answer as words for the review screen: "4 of 5", "7 of 10", "Phone app". */
static void answer_text(int qi, char *buf, size_t len)
{
    const ui_survey_question_t *q = question(qi);
    int a = sv.answer[qi];
    if (a == UI_SURVEY_UNANSWERED) {
        snprintf(buf, len, "not answered");
    } else if (q->kind == UI_SURVEY_CHOICE) {
        snprintf(buf, len, "%s", q->choices[a] ? q->choices[a] : "?");
        for (char *p = buf; *p; p++) {
            if (*p == '\n') {
                *p = ' ';
            }
        }
    } else {
        snprintf(buf, len, "%d of %d", a, q->max);
    }
}

static void go_to(ui_survey_step_t step);

static void apply_pending_step(void *unused)
{
    (void)unused;
    if (sv.sheet == NULL) {
        return;   /* closed before this ran */
    }
    sv.step = sv.pending;
    build_step();
}

/* From a click: defer, the clicked key is about to be deleted. */
static void go_to(ui_survey_step_t step)
{
    sv.pending = step;
    lv_async_call(apply_pending_step, NULL);
}

static void reset_visitor(void)
{
    for (int i = 0; i < UI_SURVEY_QUESTIONS; i++) {
        sv.answer[i] = UI_SURVEY_UNANSWERED;
    }
    sv.submitted = false;
    sv.save_failed = false;
}

/* ---- header ----------------------------------------------------------- */

static void header(const char *kicker, lv_color_t kicker_color, int progress)
{
    lv_obj_t *k = ui_paper_text(sv.body, kicker, &lv_font_montserrat_14, kicker_color);
    lv_obj_set_pos(k, X0, 12);

    /* Three dots after "Question N of 3": done and current in ink, to come
     * as hairline rings. */
    if (progress > 0) {
        lv_obj_update_layout(k);
        int x = X0 + lv_obj_get_width(k) + 12;
        for (int i = 0; i < UI_SURVEY_QUESTIONS; i++) {
            lv_obj_t *d = lv_obj_create(sv.body);
            lv_obj_remove_style_all(d);
            lv_obj_set_size(d, 10, 10);
            lv_obj_set_pos(d, x + i * 16, 15);
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(d, 2, 0);
            lv_obj_set_style_border_color(d, i < progress ? PAPER_INK : PAPER_HAIRLINE, 0);
            lv_obj_set_style_bg_color(d, PAPER_INK, 0);
            lv_obj_set_style_bg_opa(d, i < progress ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    /* Says, on every screen, that these are not the real questions. */
    lv_obj_t *tag = ui_paper_text(sv.body, "SAMPLE", &lv_font_montserrat_14, PAPER_GREY);
    lv_obj_set_style_border_width(tag, 1, 0);
    lv_obj_set_style_border_color(tag, PAPER_GREY, 0);
    lv_obj_set_style_radius(tag, 4, 0);
    lv_obj_set_style_pad_hor(tag, 6, 0);
    lv_obj_set_style_pad_ver(tag, 2, 0);
    lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, -12, 9);

    ui_paper_rule(sv.body, X0, 40, W, PAPER_INK);
}

/* Back / Next along the bottom. Next starts disabled until answered. */
static void nav_row(const char *next_text, lv_event_cb_t back_cb, lv_event_cb_t next_cb, bool next_ok)
{
    lv_obj_t *back = ui_paper_key(sv.body, LV_SYMBOL_LEFT " Back", &lv_font_montserrat_20, false, back_cb, NULL);
    lv_obj_set_size(back, 120, 48);
    lv_obj_set_pos(back, 12, 184);

    sv.next_key = ui_paper_key(sv.body, next_text, &lv_font_montserrat_20, true, next_cb, NULL);
    lv_obj_set_size(sv.next_key, 168, 48);
    lv_obj_set_pos(sv.next_key, 140, 184);
    set_enabled(sv.next_key, next_ok);
}

/* ---- welcome ---------------------------------------------------------- */

static void start_cb(lv_event_t *e)
{
    (void)e;
    go_to(UI_SURVEY_STEP_Q1);
}

static void fire_exit(void *unused)
{
    (void)unused;
    if (sv.sheet && sv.cfg->exit) {
        sv.cfg->exit(sv.cfg->ctx);
    }
}

/* The organiser's way out: hold the top bar. Not a button - a visitor
 * tapping around must not leave the kiosk by accident. */
static void hold_zone_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        sv.hold_start = lv_tick_get();
        sv.exit_fired = false;
    } else if (code == LV_EVENT_PRESSING && !sv.exit_fired &&
               lv_tick_elaps(sv.hold_start) >= HOLD_TO_EXIT_MS) {
        sv.exit_fired = true;
        lv_async_call(fire_exit, NULL);   /* the app deletes this zone */
    }
}

static void build_welcome(void)
{
    header("Event feedback", PAPER_GREY, 0);

    lv_obj_t *zone = lv_obj_create(sv.body);
    lv_obj_remove_style_all(zone);
    lv_obj_set_size(zone, 320, 40);
    lv_obj_set_pos(zone, 0, 0);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(zone, hold_zone_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(zone, hold_zone_cb, LV_EVENT_PRESSING, NULL);

    lv_obj_t *h = ui_paper_text(sv.body, "Got 30 seconds?", &lv_font_montserrat_32, PAPER_INK);
    lv_obj_set_pos(h, X0, 54);

    lv_obj_t *sub = ui_paper_text(sv.body, "Three quick questions about the demo.",
                                  &lv_font_montserrat_20, PAPER_GREY);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(sub, W);
    lv_obj_set_pos(sub, X0, 98);

    lv_obj_t *start = ui_paper_key(sv.body, "Start " LV_SYMBOL_RIGHT, &lv_font_montserrat_20, true, start_cb, NULL);
    lv_obj_set_size(start, W, 52);
    lv_obj_set_pos(start, X0, 160);

    lv_obj_t *hint = ui_paper_text(sv.body, "Organiser: hold the top bar 3 s to leave", &lv_font_montserrat_12,
                                   PAPER_GREY);
    lv_obj_set_pos(hint, X0, 219);
}

/* ---- question screens -------------------------------------------------- */

static void back_cb(lv_event_t *e)
{
    (void)e;
    go_to((ui_survey_step_t)(sv.step - 1));   /* answers stay where they are */
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    if (sv.answer[current_q()] != UI_SURVEY_UNANSWERED) {
        go_to((ui_survey_step_t)(sv.step + 1));
    }
}

static void restyle_tiles(void)
{
    int a = sv.answer[current_q()];
    const ui_survey_question_t *q = question(current_q());
    for (int i = 0; i < sv.tile_count; i++) {
        int value = q->kind == UI_SURVEY_RATING ? q->min + i : i;
        ui_paper_key_set_filled(sv.tiles[i], value == a);
    }
}

static void tile_cb(lv_event_t *e)
{
    sv.answer[current_q()] = (int)(intptr_t)lv_event_get_user_data(e);
    restyle_tiles();
    set_enabled(sv.next_key, true);
}

static void build_rating(const ui_survey_question_t *q)
{
    sv.tile_count = q->max - q->min + 1;
    if (sv.tile_count > 5) {
        sv.tile_count = 5;
    }
    for (int i = 0; i < sv.tile_count; i++) {
        char num[8];
        snprintf(num, sizeof(num), "%d", q->min + i);
        sv.tiles[i] = ui_paper_key(sv.body, num, &lv_font_montserrat_32, false, tile_cb,
                                   (void *)(intptr_t)(q->min + i));
        lv_obj_set_size(sv.tiles[i], 52, 56);
        lv_obj_set_pos(sv.tiles[i], X0 + i * 59, 102);
    }
    lv_obj_t *lo = ui_paper_text(sv.body, q->low_word, &lv_font_montserrat_14, PAPER_GREY);
    lv_obj_set_pos(lo, X0, 162);
    lv_obj_t *hi = ui_paper_text(sv.body, q->high_word, &lv_font_montserrat_14, PAPER_GREY);
    lv_obj_align(hi, LV_ALIGN_TOP_RIGHT, -X0, 162);
    restyle_tiles();
}

/* Tiles sit under the question, wherever it ends - a one-line question
 * would otherwise leave a band of empty paper above them. */
static void build_choice(const ui_survey_question_t *q, int prompt_bottom)
{
    int y = prompt_bottom + 14 < 102 ? prompt_bottom + 14 : 102;
    sv.tile_count = 3;
    for (int i = 0; i < 3; i++) {
        sv.tiles[i] = ui_paper_key(sv.body, q->choices[i], &lv_font_montserrat_20, false, tile_cb,
                                   (void *)(intptr_t)i);
        lv_obj_set_size(sv.tiles[i], 92, 76);
        lv_obj_set_pos(sv.tiles[i], X0 + i * 98, y);
        lv_obj_set_style_text_align(lv_obj_get_child(sv.tiles[i], 0), LV_TEXT_ALIGN_CENTER, 0);
    }
    restyle_tiles();
}

/* The slider shows nothing chosen until the visitor touches it or a step
 * key: a knob parked in the middle would be an answer nobody gave. */
static void slider_show(void)
{
    const ui_survey_question_t *q = question(current_q());
    int a = sv.answer[current_q()];
    bool set = a != UI_SURVEY_UNANSWERED;
    if (set) {
        lv_label_set_text_fmt(sv.value_label, "%d", a);
        lv_slider_set_value(sv.slider, a, LV_ANIM_OFF);
    } else {
        lv_label_set_text(sv.value_label, "?");
    }
    lv_obj_set_style_text_color(sv.value_label, set ? PAPER_INK : PAPER_GREY, 0);
    lv_obj_set_style_bg_opa(sv.slider, set ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sv.slider, set ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(sv.slider, set ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_KNOB);
    /* A step key that cannot step says so by fading, rather than doing nothing. */
    set_enabled(sv.minus_key, !set || a > q->min);
    set_enabled(sv.plus_key, !set || a < q->max);
    set_enabled(sv.next_key, set);
}

static void step_cb(lv_event_t *e)
{
    const ui_survey_question_t *q = question(current_q());
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int *a = &sv.answer[current_q()];
    if (*a == UI_SURVEY_UNANSWERED) {
        *a = (q->min + q->max + 1) / 2;   /* first press lands mid-scale, visibly */
    } else {
        *a += dir;
        *a = *a < q->min ? q->min : *a > q->max ? q->max : *a;
    }
    slider_show();
}

static void slider_cb(lv_event_t *e)
{
    sv.answer[current_q()] = lv_slider_get_value(lv_event_get_target(e));
    slider_show();
}

static void build_slider(const ui_survey_question_t *q)
{
    lv_obj_t *lo = ui_paper_text(sv.body, q->low_word, &lv_font_montserrat_14, PAPER_GREY);
    lv_obj_set_pos(lo, X0, 112);
    lv_obj_t *hi = ui_paper_text(sv.body, q->high_word, &lv_font_montserrat_14, PAPER_GREY);
    lv_obj_align(hi, LV_ALIGN_TOP_RIGHT, -X0, 112);

    sv.value_label = ui_paper_text(sv.body, "?", &lv_font_montserrat_32, PAPER_INK);
    lv_obj_align(sv.value_label, LV_ALIGN_TOP_MID, 0, 96);

    sv.minus_key = ui_paper_key(sv.body, LV_SYMBOL_MINUS, &lv_font_montserrat_20, false, step_cb, (void *)-1);
    lv_obj_set_size(sv.minus_key, 52, 48);
    lv_obj_set_pos(sv.minus_key, X0, 134);
    sv.plus_key = ui_paper_key(sv.body, LV_SYMBOL_PLUS, &lv_font_montserrat_20, false, step_cb, (void *)1);
    lv_obj_set_size(sv.plus_key, 52, 48);
    lv_obj_set_pos(sv.plus_key, X0 + W - 52, 134);

    sv.slider = lv_slider_create(sv.body);
    lv_obj_remove_style_all(sv.slider);
    lv_slider_set_range(sv.slider, q->min, q->max);
    lv_obj_set_size(sv.slider, 156, 10);
    lv_obj_set_pos(sv.slider, 82, 153);
    /* A finger-sized grab area around a thin line. */
    lv_obj_set_ext_click_area(sv.slider, 18);
    lv_obj_set_style_bg_color(sv.slider, PAPER_HAIRLINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sv.slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sv.slider, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sv.slider, PAPER_INK, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sv.slider, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sv.slider, PAPER_BG, LV_PART_KNOB);
    lv_obj_set_style_border_color(sv.slider, PAPER_INK, LV_PART_KNOB);
    lv_obj_set_style_border_width(sv.slider, 3, LV_PART_KNOB);
    lv_obj_set_style_radius(sv.slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sv.slider, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(sv.slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void build_question(void)
{
    int qi = current_q();
    const ui_survey_question_t *q = question(qi);
    char kicker[24];
    snprintf(kicker, sizeof(kicker), "Question %d of %d", qi + 1, UI_SURVEY_QUESTIONS);
    header(kicker, PAPER_GREY, qi + 1);

    lv_obj_t *p = ui_paper_text(sv.body, q->prompt, &lv_font_montserrat_20, PAPER_INK);
    lv_label_set_long_mode(p, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(p, W);
    lv_obj_set_pos(p, X0, 48);
    lv_obj_update_layout(p);
    /* The band holds two lines at 20 px (~50 characters). A longer question
     * drops to 14 px rather than running into the answer control. */
    if (lv_obj_get_height(p) > 50) {
        lv_obj_set_style_text_font(p, &lv_font_montserrat_14, 0);
        lv_obj_update_layout(p);
    }

    bool answered = sv.answer[qi] != UI_SURVEY_UNANSWERED;
    nav_row(qi == UI_SURVEY_QUESTIONS - 1 ? "Review " LV_SYMBOL_RIGHT : "Next " LV_SYMBOL_RIGHT,
            back_cb, next_cb, answered);
    switch (q->kind) {
    case UI_SURVEY_RATING: build_rating(q); break;
    case UI_SURVEY_SLIDER: build_slider(q); slider_show(); break;
    case UI_SURVEY_CHOICE: build_choice(q, 48 + lv_obj_get_height(p)); break;
    }
}

/* ---- review / submit --------------------------------------------------- */

static void edit_cb(lv_event_t *e)
{
    go_to((ui_survey_step_t)(UI_SURVEY_STEP_Q1 + (int)(intptr_t)lv_event_get_user_data(e)));
}

static void submit_cb(lv_event_t *e)
{
    (void)e;
    if (sv.submitted) {
        return;   /* already stored for this visitor */
    }
    for (int i = 0; i < UI_SURVEY_QUESTIONS; i++) {
        if (sv.answer[i] == UI_SURVEY_UNANSWERED) {
            go_to((ui_survey_step_t)(UI_SURVEY_STEP_Q1 + i));
            return;
        }
    }
    /* Disabled before the callback, so a second tap during it lands on a
     * dead key; on success the step changes and the key is gone. */
    set_enabled(sv.next_key, false);
    uint32_t no = 0;
    if (sv.cfg->submit && sv.cfg->submit(sv.answer, &no, sv.cfg->ctx)) {
        sv.submitted = true;
        sv.save_failed = false;
        sv.response_no = no;
        go_to(UI_SURVEY_STEP_THANKS);
    } else {
        sv.save_failed = true;
        go_to(UI_SURVEY_STEP_REVIEW);   /* redraw with the notice and a live key */
    }
}

static void build_review(void)
{
    if (sv.save_failed) {
        header(LV_SYMBOL_WARNING " Not saved. Try again.", PAPER_RUST, 0);
    } else {
        header("Check your answers", PAPER_GREY, 0);
    }

    for (int i = 0; i < UI_SURVEY_QUESTIONS; i++) {
        /* The whole row is the way back to that question. */
        lv_obj_t *row = ui_paper_key(sv.body, "", &lv_font_montserrat_14, false, edit_cb, (void *)(intptr_t)i);
        lv_obj_set_size(row, W, 42);
        lv_obj_set_pos(row, X0, 48 + i * 45);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, PAPER_HAIRLINE, 0);

        lv_obj_t *name = lv_obj_get_child(row, 0);
        lv_label_set_text(name, question(i)->short_label);
        lv_obj_set_style_text_color(name, PAPER_GREY, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 0);

        char buf[40];
        answer_text(i, buf, sizeof(buf));
        lv_obj_t *val = ui_paper_text(row, buf, &lv_font_montserrat_20,
                                      sv.answer[i] == UI_SURVEY_UNANSWERED ? PAPER_RUST : PAPER_INK);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -32, 0);
        lv_obj_t *pen = ui_paper_text(row, LV_SYMBOL_EDIT, &lv_font_montserrat_14, PAPER_GREY);
        lv_obj_align(pen, LV_ALIGN_RIGHT_MID, -10, 0);
    }
    nav_row(LV_SYMBOL_OK " Submit", back_cb, submit_cb, true);
}

/* ---- thank you --------------------------------------------------------- */

static void next_visitor_cb(lv_event_t *e)
{
    (void)e;
    reset_visitor();
    go_to(UI_SURVEY_STEP_WELCOME);
}

static void build_thanks(void)
{
    header("All done", PAPER_GREY, 0);

    lv_obj_t *t = ui_paper_text(sv.body, "Thanks!", &lv_font_montserrat_32, PAPER_INK);
    lv_obj_set_pos(t, X0, 50);

    char note[128];
    snprintf(note, sizeof(note), "%s\nResponse #%lu.", sv.cfg->saved_note ? sv.cfg->saved_note : "",
             (unsigned long)sv.response_no);
    lv_obj_t *n = ui_paper_text(sv.body, note, &lv_font_montserrat_14, PAPER_GREY);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(n, 160);
    lv_obj_set_pos(n, X0, 92);

    sv.countdown = ui_paper_text(sv.body, "", &lv_font_montserrat_14, PAPER_GREY);
    lv_label_set_text_fmt(sv.countdown, "Resets in %lu s", (unsigned long)sv.thanks_left_s);
    lv_obj_set_pos(sv.countdown, X0, 160);

    /* The right column is the code. Size matters more than anything else
     * here: LVGL scales whole modules, and at this panel's 0.15 mm/px a
     * short URL gets 3-4 px modules (a ~17 mm code a phone reads from a
     * hand's length); a long one drops to 2 px and gets hard to scan. Keep
     * the real URL short. */
#if LV_USE_QRCODE
    lv_obj_t *qr = lv_qrcode_create(sv.body);
    lv_qrcode_set_size(qr, 124);
    lv_qrcode_set_dark_color(qr, PAPER_INK);
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_set_quiet_zone(qr, true);
    lv_qrcode_update(qr, sv.cfg->qr_url, (uint32_t)strlen(sv.cfg->qr_url));
    lv_obj_set_pos(qr, 186, 46);
#endif
    lv_obj_t *cap = ui_paper_text(sv.body, sv.cfg->qr_caption, &lv_font_montserrat_12, PAPER_GREY);
    lv_label_set_long_mode(cap, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(cap, 144);
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(cap, 176, 174);

    lv_obj_t *nv = ui_paper_key(sv.body, "Next visitor " LV_SYMBOL_RIGHT, &lv_font_montserrat_20, true,
                                next_visitor_cb, NULL);
    lv_obj_set_size(nv, 164, 48);
    lv_obj_set_pos(nv, 12, 184);
}

/* ---- steps and the 1 s tick ------------------------------------------- */

static void build_step(void)
{
    lv_obj_clean(sv.body);
    sv.next_key = NULL;
    sv.tile_count = 0;
    sv.countdown = NULL;
    if (sv.step == UI_SURVEY_STEP_THANKS) {
        sv.thanks_left_s = sv.cfg->thanks_reset_s;
    }
    switch (sv.step) {
    case UI_SURVEY_STEP_WELCOME: build_welcome(); break;
    case UI_SURVEY_STEP_Q1:
    case UI_SURVEY_STEP_Q2:
    case UI_SURVEY_STEP_Q3:      build_question(); break;
    case UI_SURVEY_STEP_REVIEW:  build_review(); break;
    case UI_SURVEY_STEP_THANKS:  build_thanks(); break;
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (sv.step == UI_SURVEY_STEP_THANKS) {
        if (sv.thanks_left_s > 0) {
            sv.thanks_left_s--;
        }
        if (sv.thanks_left_s == 0) {
            reset_visitor();
            sv.step = UI_SURVEY_STEP_WELCOME;
            build_step();
        } else if (sv.countdown) {
            lv_label_set_text_fmt(sv.countdown, "Resets in %lu s", (unsigned long)sv.thanks_left_s);
        }
    } else if (sv.step != UI_SURVEY_STEP_WELCOME && sv.cfg->idle_reset_s > 0 &&
               lv_display_get_inactive_time(NULL) > sv.cfg->idle_reset_s * 1000U) {
        /* Someone walked away mid-survey: the next person starts clean. */
        reset_visitor();
        sv.step = UI_SURVEY_STEP_WELCOME;
        build_step();
    }
}

lv_obj_t *ui_survey_open(lv_obj_t *scr, const ui_survey_config_t *cfg)
{
    ui_survey_close();
    memset(&sv, 0, sizeof(sv));
    sv.cfg = cfg;
    reset_visitor();
    sv.sheet = ui_paper_sheet(scr);
    sv.body = lv_obj_create(sv.sheet);
    lv_obj_remove_style_all(sv.body);
    lv_obj_set_size(sv.body, 320, 240);
    lv_obj_clear_flag(sv.body, LV_OBJ_FLAG_SCROLLABLE);
    sv.step = UI_SURVEY_STEP_WELCOME;
    build_step();
    sv.tick = lv_timer_create(tick_cb, 1000, NULL);
    return sv.sheet;
}

void ui_survey_close(void)
{
    if (sv.tick) {
        lv_timer_delete(sv.tick);
        sv.tick = NULL;
    }
    if (sv.sheet) {
        lv_obj_delete(sv.sheet);
        sv.sheet = NULL;
    }
}

bool ui_survey_is_open(void)
{
    return sv.sheet != NULL;
}

void ui_survey_preview(ui_survey_step_t step, const int *answers, bool save_failed, uint32_t response_no)
{
    if (!sv.sheet) {
        return;
    }
    for (int i = 0; i < UI_SURVEY_QUESTIONS; i++) {
        sv.answer[i] = answers ? answers[i] : UI_SURVEY_UNANSWERED;
    }
    sv.save_failed = save_failed;
    sv.submitted = step == UI_SURVEY_STEP_THANKS;
    sv.response_no = response_no;
    sv.step = step;
    build_step();
}
