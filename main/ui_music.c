/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Music remote - see ui_music.h.
 * @details Built once on open; ui_music_apply() only changes texts and key
 *          states, so nothing is deleted from inside a key's event except the
 *          whole sheet, and that goes through lv_async_call.
 *
 *          Layout (chosen over a rows layout - see the music review README):
 *
 *    y   2.. 50   "< HOME" key, link tag, ink rule at 54
 *    x   8..228   title or playback state (20), state + artist (14), status
 *                 (14), then |<<  PLAY  >>| as three 100 px tall keys
 *    x 242..312   volume column: [+] on top, the number and WHICH volume it
 *                 is, [-] at the bottom, behind a hairline
 */

#include "ui_music.h"

#include <stdio.h>
#include <string.h>
#include "ui_paper.h"
#include "ui_theme.h"

#define TEXT_MAX 96

/* Allocated on open (LVGL allocator -> PSRAM), freed on close: an
 * experiment costs no internal RAM while it is not on screen. */
typedef struct {
    lv_obj_t *sheet;
    ui_music_command_fn command;
    ui_music_exit_fn exit;
    void *ctx;
    ui_music_view_t view;
    char title[TEXT_MAX];
    char artist[TEXT_MAX];
    bool exit_queued;

    lv_obj_t *tag;          /* SIMULATED / CONNECTED / NOT CONNECTED / CONNECTING */
    lv_obj_t *line1;        /* title, or the playback state when there is none */
    lv_obj_t *line2;        /* state + artist, or what is being controlled */
    lv_obj_t *status;       /* pending / last result / why keys are off */
    lv_obj_t *keys[UI_MUSIC_CMD_COUNT];
    lv_obj_t *vol_value;
    lv_obj_t *vol_kind;
} music_t;
static music_t *mu;

const char *ui_music_cmd_name(ui_music_cmd_t cmd)
{
    switch (cmd) {
    case UI_MUSIC_CMD_PREV: return "Previous";
    case UI_MUSIC_CMD_PLAY_PAUSE: return "Play/pause";
    case UI_MUSIC_CMD_NEXT: return "Next";
    case UI_MUSIC_CMD_VOL_DOWN: return "Volume down";
    case UI_MUSIC_CMD_VOL_UP: return "Volume up";
    default: return "Command";
    }
}

/* ---- taps ---------------------------------------------------------------- */

static bool link_usable(void)
{
    return mu->view.link == UI_MUSIC_LINK_SIMULATED || mu->view.link == UI_MUSIC_LINK_CONNECTED;
}

/* Every refusal lives here, not only in the faded look: a tap on a faded key
 * is already ignored by LVGL, and a tap while a command is out is dropped so
 * one finger cannot queue up five "Next"s. */
static bool cmd_allowed(ui_music_cmd_t cmd)
{
    if (!link_usable() || mu->view.pending != UI_MUSIC_CMD_NONE) {
        return false;
    }
    if (cmd == UI_MUSIC_CMD_VOL_DOWN && mu->view.volume == 0) {
        return false;
    }
    if (cmd == UI_MUSIC_CMD_VOL_UP && mu->view.volume >= 100) {
        return false;
    }
    return true;
}

static void key_cb(lv_event_t *e)
{
    ui_music_cmd_t cmd = (ui_music_cmd_t)(intptr_t)lv_event_get_user_data(e);
    if (cmd_allowed(cmd) && mu->command) {
        mu->command(cmd, mu->ctx);
    }
}

static void fire_exit(void *unused)
{
    (void)unused;
    if (!mu) {
        return;
    }
    mu->exit_queued = false;
    if (mu->sheet && mu->exit) {
        mu->exit(mu->ctx);
    }
}

static void exit_cb(lv_event_t *e)
{
    (void)e;
    if (!mu->exit_queued) {
        mu->exit_queued = true;
        lv_async_call(fire_exit, NULL);   /* the app deletes this key */
    }
}

/* ---- building ------------------------------------------------------------ */

static lv_obj_t *cmd_key(ui_music_cmd_t cmd, const char *glyph, bool filled, int x, int y, int w, int h)
{
    lv_obj_t *k = ui_paper_key(mu->sheet, glyph, &lv_font_montserrat_32, filled, key_cb,
                               (void *)(intptr_t)cmd);
    lv_obj_set_pos(k, x, y);
    lv_obj_set_size(k, w, h);
    mu->keys[cmd] = k;
    return k;
}

static lv_obj_t *text_at(const char *s, const lv_font_t *font, lv_color_t c, int x, int y, int w)
{
    lv_obj_t *l = ui_paper_text(mu->sheet, s, font, c);
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        /* One line, cut with dots: DOTS only cuts when the height is fixed. */
        lv_obj_set_size(l, w, lv_font_get_line_height(font));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    }
    return l;
}

/* Header: the way out top-left, the link tag
 * top-right, an ink rule. Nothing else is in this band, so a thumb going
 * for the transport keys cannot land on "< HOME". */
static void build_header(void)
{
    lv_obj_t *home = ui_paper_key(mu->sheet, LV_SYMBOL_LEFT " HOME", &lv_font_montserrat_20, false, exit_cb, NULL);
    lv_obj_set_pos(home, 6, 2);
    lv_obj_set_size(home, 104, 48);

    mu->tag = ui_paper_text(mu->sheet, "", &lv_font_montserrat_14, PAPER_INK);
    lv_obj_set_style_border_width(mu->tag, 2, 0);
    lv_obj_set_style_radius(mu->tag, 4, 0);
    lv_obj_set_style_pad_hor(mu->tag, 8, 0);
    lv_obj_set_style_pad_ver(mu->tag, 4, 0);
    lv_obj_set_style_bg_opa(mu->tag, LV_OPA_COVER, 0);

    ui_paper_rule(mu->sheet, 8, 54, 304, PAPER_INK);
}

/* Transport on the left as three tall keys (the most-used controls get the
 * most glass), volume as a column on the right like a fader: + on top, -
 * at the bottom, so volume and transport are never stacked a few px apart. */
static void build_body(void)
{
    mu->line1 = text_at("", &lv_font_montserrat_20, PAPER_INK, 12, 62, 212);
    mu->line2 = text_at("", &lv_font_montserrat_14, PAPER_GREY, 12, 88, 212);
    mu->status = text_at("", &lv_font_montserrat_14, PAPER_GREY, 12, 110, 212);

    cmd_key(UI_MUSIC_CMD_PREV, LV_SYMBOL_PREV, false, 8, 136, 60, 100);
    cmd_key(UI_MUSIC_CMD_PLAY_PAUSE, LV_SYMBOL_PLAY, true, 76, 136, 84, 100);
    cmd_key(UI_MUSIC_CMD_NEXT, LV_SYMBOL_NEXT, false, 168, 136, 60, 100);

    /* hairline between the two halves */
    lv_obj_t *v = lv_obj_create(mu->sheet);
    lv_obj_remove_style_all(v);
    lv_obj_set_pos(v, 234, 62);
    lv_obj_set_size(v, 1, 174);
    lv_obj_set_style_bg_color(v, PAPER_HAIRLINE, 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);

    cmd_key(UI_MUSIC_CMD_VOL_UP, LV_SYMBOL_PLUS, false, 242, 62, 70, 52);
    cmd_key(UI_MUSIC_CMD_VOL_DOWN, LV_SYMBOL_MINUS, false, 242, 184, 70, 52);
    mu->vol_value = text_at("", &lv_font_montserrat_20, PAPER_INK, 238, 120, 78);
    lv_obj_set_style_text_align(mu->vol_value, LV_TEXT_ALIGN_CENTER, 0);
    mu->vol_kind = ui_paper_text(mu->sheet, "", &lv_font_montserrat_14, PAPER_GREY);
    lv_obj_set_pos(mu->vol_kind, 238, 144);
    lv_obj_set_width(mu->vol_kind, 78);
    lv_label_set_long_mode(mu->vol_kind, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(mu->vol_kind, LV_TEXT_ALIGN_CENTER, 0);
}

/* ---- showing a state ----------------------------------------------------- */

static void show_tag(void)
{
    const char *t;
    lv_color_t c = PAPER_INK;
    lv_color_t bg = PAPER_BG;
    lv_color_t fg = PAPER_INK;
    switch (mu->view.link) {
    case UI_MUSIC_LINK_SIMULATED: t = "SIMULATED"; break;
    case UI_MUSIC_LINK_CONNECTED: t = "CONNECTED"; bg = PAPER_INK; fg = PAPER_BG; break;
    case UI_MUSIC_LINK_DISCONNECTED: t = LV_SYMBOL_WARNING " NOT CONNECTED"; c = PAPER_RUST; fg = PAPER_RUST; break;
    default: t = "CONNECTING"; c = PAPER_GREY; fg = PAPER_GREY; break;
    }
    lv_label_set_text(mu->tag, t);
    lv_obj_set_style_border_color(mu->tag, c, 0);
    lv_obj_set_style_bg_color(mu->tag, bg, 0);
    lv_obj_set_style_text_color(mu->tag, fg, 0);
    lv_obj_update_layout(mu->tag);
    lv_obj_set_pos(mu->tag, 312 - lv_obj_get_width(mu->tag), 14);
}

static const char *play_word(void)
{
    switch (mu->view.play) {
    case UI_MUSIC_PLAY_PLAYING: return "Playing";
    case UI_MUSIC_PLAY_PAUSED: return "Paused";
    default: return "Playback not known";
    }
}

static void show_track(void)
{
    char buf[TEXT_MAX + 32];   /* state word + bullet + artist */
    bool has_title = mu->title[0] != '\0';
    bool has_artist = mu->artist[0] != '\0';
    if (has_title) {
        lv_label_set_text(mu->line1, mu->title);
        lv_obj_set_style_text_color(mu->line1, PAPER_INK, 0);
        snprintf(buf, sizeof(buf), "%s%s%s", play_word(), has_artist ? "  \xE2\x80\xA2  " : "", mu->artist);
        lv_label_set_text(mu->line2, buf);
    } else {
        /* No track text is a normal state, not an error: the headline is the
         * playback state and line 2 names what the keys control. */
        lv_label_set_text(mu->line1, play_word());
        lv_obj_set_style_text_color(mu->line1, mu->view.play == UI_MUSIC_PLAY_UNKNOWN ? PAPER_GREY : PAPER_INK, 0);
        lv_label_set_text(mu->line2, has_artist ? mu->artist : "Spotify on the laptop");
    }
}

static void show_status(void)
{
    char buf[96];
    lv_color_t c = PAPER_GREY;
    bool sim = mu->view.link == UI_MUSIC_LINK_SIMULATED;

    if (mu->view.link == UI_MUSIC_LINK_DISCONNECTED) {
        snprintf(buf, sizeof(buf), "Laptop not reachable");
        c = PAPER_RUST;
    } else if (mu->view.link == UI_MUSIC_LINK_UNKNOWN) {
        snprintf(buf, sizeof(buf), "Looking for the laptop...");
    } else if (mu->view.pending != UI_MUSIC_CMD_NONE) {
        if (sim) {
            snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH " %s (simulated)...", ui_music_cmd_name(mu->view.pending));
        } else {
            snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH " Sending %s...", ui_music_cmd_name(mu->view.pending));
        }
        c = PAPER_INK;
    } else if (mu->view.last_result == UI_MUSIC_RESULT_SENT) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s %s", ui_music_cmd_name(mu->view.last_cmd),
                 sim ? "(simulated)" : "sent");
    } else if (mu->view.last_result == UI_MUSIC_RESULT_FAILED) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s failed", ui_music_cmd_name(mu->view.last_cmd));
        c = PAPER_RUST;
    } else if (mu->view.last_result == UI_MUSIC_RESULT_TIMED_OUT) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s: no answer", ui_music_cmd_name(mu->view.last_cmd));
        c = PAPER_RUST;
    } else if (sim) {
        snprintf(buf, sizeof(buf), "Simulated: nothing is sent");
    } else {
        snprintf(buf, sizeof(buf), "Ready");
    }
    lv_label_set_text(mu->status, buf);
    lv_obj_set_style_text_color(mu->status, c, 0);
}

static void show_volume(void)
{
    static const char *const kinds[] = { "Volume", "Laptop volume", "Spotify volume" };
    int k = mu->view.volume_kind;
    lv_label_set_text(mu->vol_kind, kinds[(k >= 0 && k <= 2) ? k : 0]);
    if (mu->view.volume == UI_MUSIC_VOLUME_NONE) {
        lv_label_set_text(mu->vol_value, "?");   /* "not known" does not fit the column */
        lv_obj_set_style_text_color(mu->vol_value, PAPER_GREY, 0);
    } else {
        lv_label_set_text_fmt(mu->vol_value, "%d%%", mu->view.volume);
        lv_obj_set_style_text_color(mu->vol_value, PAPER_INK, 0);
    }
}

static void show_keys(void)
{
    /* The key shows what a tap will do: pause while playing, play while
     * paused, both when nobody knows. */
    lv_obj_t *pp = lv_obj_get_child(mu->keys[UI_MUSIC_CMD_PLAY_PAUSE], 0);
    lv_label_set_text(pp, mu->view.play == UI_MUSIC_PLAY_PLAYING  ? LV_SYMBOL_PAUSE
                          : mu->view.play == UI_MUSIC_PLAY_PAUSED ? LV_SYMBOL_PLAY
                                                                 : LV_SYMBOL_PLAY LV_SYMBOL_PAUSE);

    for (int i = 0; i < UI_MUSIC_CMD_COUNT; i++) {
        bool on = link_usable();
        if (i == UI_MUSIC_CMD_VOL_DOWN && mu->view.volume == 0) {
            on = false;
        }
        if (i == UI_MUSIC_CMD_VOL_UP && mu->view.volume >= 100) {
            on = false;
        }
        /* The key whose command is out stays pressed-looking until the
         * answer; the others stay as they are (taps are dropped meanwhile). */
        bool pending = mu->view.pending == (ui_music_cmd_t)i;
        ui_paper_key_set_filled(mu->keys[i], i == UI_MUSIC_CMD_PLAY_PAUSE && !pending);
        if (pending) {
            lv_obj_set_style_bg_color(mu->keys[i], PAPER_PRESSED, 0);
        }
        ui_paper_key_set_enabled(mu->keys[i], on);
    }
}

void ui_music_apply(const ui_music_view_t *view)
{
    if (!mu || !mu->sheet || !view) {
        return;
    }
    mu->view = *view;
    snprintf(mu->title, sizeof(mu->title), "%s", view->title ? view->title : "");
    snprintf(mu->artist, sizeof(mu->artist), "%s", view->artist ? view->artist : "");
    mu->view.title = mu->title;
    mu->view.artist = mu->artist;
    show_tag();
    show_track();
    show_status();
    show_volume();
    show_keys();
}

lv_obj_t *ui_music_open(lv_obj_t *scr, const ui_music_view_t *view, ui_music_command_fn command,
                        ui_music_exit_fn exit, void *ctx)
{
    ui_music_close();
    mu = lv_malloc_zeroed(sizeof(*mu));
    if (mu == NULL) {
        return NULL;
    }
    mu->command = command;
    mu->exit = exit;
    mu->ctx = ctx;
    mu->sheet = ui_paper_sheet(scr);
    build_header();
    build_body();
    ui_music_view_t unknown = { .pending = UI_MUSIC_CMD_NONE, .last_cmd = UI_MUSIC_CMD_NONE,
                                .volume = UI_MUSIC_VOLUME_NONE };
    ui_music_apply(view ? view : &unknown);
    return mu->sheet;
}

void ui_music_close(void)
{
    if (mu) {
        lv_async_call_cancel(fire_exit, NULL);
        if (mu->sheet) {
            lv_obj_delete(mu->sheet);
        }
        lv_free(mu);
        mu = NULL;
    }
}

bool ui_music_is_open(void)
{
    return mu != NULL && mu->sheet != NULL;
}
