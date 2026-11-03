/* Render target: the Friendly Companion eyes (main/ui_companion.c), opened
 * the way status_deck_ui.c would open it. Fixture state picks the look, the
 * mood and an optional reaction; `preview` snaps to a pose for stills, and
 * without it the real timers run, so --frames shows the actual motion and
 * --tap / --input drive the real touch handlers.
 * The exit callback closes the face and leaves a marker, so a test can see
 * it fired. `show_state` adds a small state line for the tap tests (the eyes
 * are drawn, not labels, so the widget tree cannot see them otherwise). */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ws_sim.h"
#include "ui_companion.h"

static lv_obj_t *screen;
static lv_obj_t *state_label;
static ui_companion_config_t cfg;
static ui_companion_reaction_t later_react;
static ui_companion_mood_t later_mood;

static void fake_exit(void *ctx)
{
    (void)ctx;
    ui_companion_close();
    lv_obj_t *l = lv_label_create(screen);
    lv_label_set_text(l, "COMPANION EXITED");
    lv_obj_center(l);
}

static ui_companion_mood_t mood_from(const char *key, ui_companion_mood_t def)
{
    for (int i = 0; i < UI_COMPANION_MOOD_COUNT; i++) {
        if (ws_fx_is(key, ui_companion_mood_name((ui_companion_mood_t)i))) {
            return (ui_companion_mood_t)i;
        }
    }
    return def;
}

static ui_companion_reaction_t react_from(const char *key)
{
    for (int i = 0; i < UI_COMPANION_REACT_COUNT; i++) {
        if (ws_fx_is(key, ui_companion_reaction_name((ui_companion_reaction_t)i))) {
            return (ui_companion_reaction_t)i;
        }
    }
    return UI_COMPANION_REACT_NONE;
}

static void react_later_cb(lv_timer_t *t)
{
    (void)t;
    ui_companion_react(later_react);
}

static void mood_later_cb(lv_timer_t *t)
{
    (void)t;
    ui_companion_set_mood(later_mood);
    const char *say2 = ws_fx_str("say2", "");
    if (say2[0]) {
        ui_companion_say(say2);
    }
}

static void state_cb(lv_timer_t *t)
{
    (void)t;
    char buf[64];
    ui_companion_describe(buf, sizeof(buf));
    lv_label_set_text(state_label, buf);
    lv_obj_move_foreground(state_label);
}

/* measure=true: print the face's own redraw stats to the render log
 * (stdout) once a second, so the cost of a sequence is a counted number. */
static void report_cb(lv_timer_t *t)
{
    (void)t;
    ui_companion_stats_t m;
    ui_companion_get_stats(&m);
    printf("measure t=%lu ticks=%lu with_redraw=%lu avg_px=%lu max_px=%lu\n", (unsigned long)lv_tick_get(),
           (unsigned long)m.ticks, (unsigned long)m.ticks_with_redraw,
           (unsigned long)(m.ticks_with_redraw ? m.px_total / m.ticks_with_redraw : 0), (unsigned long)m.px_max);
    fflush(stdout);
}

void ws_render(lv_obj_t *scr)
{
    screen = scr;
    ui_companion_style_t style = UI_COMPANION_STYLE_ROBO;
    if (ws_fx_is("style", "pill")) {
        style = UI_COMPANION_STYLE_PILL;
    } else if (ws_fx_is("style", "ember")) {
        style = UI_COMPANION_STYLE_EMBER;
    }
    ui_companion_mood_t mood = mood_from("mood", UI_COMPANION_MOOD_NEUTRAL);
    cfg = (ui_companion_config_t){
        .style = style,
        .mood = mood,
        .seed = (uint32_t)ws_fx_int("seed", 7),
        .still = ws_fx_bool("still", false),
        .doze_s = (uint32_t)ws_fx_int("doze_s", 0),
        .exit = fake_exit,
    };
    ui_companion_open(scr, &cfg);

    ui_companion_reaction_t react = react_from("react");
    if (ws_fx_bool("preview", false)) {
        ui_companion_preview(mood, react, (uint32_t)ws_fx_int("elapsed_ms", 0));
    } else if (react != UI_COMPANION_REACT_NONE) {
        int at = ws_fx_int("react_at_ms", 0);
        if (at <= 0) {
            ui_companion_react(react);
        } else {
            later_react = react;
            lv_timer_t *t = lv_timer_create(react_later_cb, (uint32_t)at, NULL);
            lv_timer_set_repeat_count(t, 1);
        }
    }
    int look_x = ws_fx_int("look_x", -1), look_y = ws_fx_int("look_y", -1);
    if (look_x >= 0 && look_y >= 0) {
        ui_companion_look_at(look_x, look_y);
    }
    int mood2_at = ws_fx_int("mood2_at_ms", 0);
    if (mood2_at > 0) {
        later_mood = mood_from("mood2", UI_COMPANION_MOOD_NEUTRAL);
        lv_timer_t *t = lv_timer_create(mood_later_cb, (uint32_t)mood2_at, NULL);
        lv_timer_set_repeat_count(t, 1);
    }
    ui_companion_say(ws_fx_str("say", ""));

    if (ws_fx_bool("measure", false)) {
        lv_timer_create(report_cb, 1000, NULL);
    }
    if (ws_fx_bool("show_state", false)) {
        state_label = lv_label_create(scr);
        lv_obj_set_style_text_font(state_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(state_label, lv_color_hex(0x808890), 0);
        lv_obj_align(state_label, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
        state_cb(NULL);
        lv_timer_create(state_cb, 20, NULL);
    }
}
