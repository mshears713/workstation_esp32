/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Friendly Companion (experiment): a full-screen pair of expressive
 *        eyes that blink, look around, change mood and react to touch.
 *        Pure LVGL - the same face runs in the simulator.
 * @details Built when opened, deleted when closed (timer included), so it
 *          costs nothing while it is not on screen. Both eyes are drawn by
 *          one object's DRAW_MAIN handler with rounded rectangles plus
 *          background-coloured triangles/rectangles as eyelids, and only the
 *          area around each eye is invalidated when it changes.
 *
 *          Behaviour is modelled on FluxGarage RoboEyes (GPL-3.0, commit
 *          b42f8e596535234932be3514ac7a813d4ced0046, library 1.1.2): rounded
 *          eye shapes, eyelid moods, autoblink, idle gaze, "curious" outer
 *          eye, laugh and confused shakes. No RoboEyes code was copied; this
 *          is an independent LVGL implementation (time-based easing, scripted
 *          reactions, touch handling).
 *
 *          Touch, handled inside:
 *            tap the face           -> BOOP: flinch, look at the finger, smile
 *            4 quick taps           -> CONFUSED ("too much!")
 *            hold still ~0.4 s      -> LAUGH
 *            drag                   -> the eyes follow the finger
 *            tap while asleep       -> wakes up (SURPRISED)
 *            tap the top-left corner-> a "< HOME" key appears for 4 s; tapping
 *                                      it calls cfg->exit. A single stray tap
 *                                      can never leave.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Eye treatments explored for the experiment (see the README for why). */
typedef enum {
    UI_COMPANION_STYLE_ROBO = 0,   /* cyan rounded squares on navy (RoboEyes-like) */
    UI_COMPANION_STYLE_PILL,       /* tall white pills on black */
    UI_COMPANION_STYLE_EMBER,      /* wide warm-amber eyes on dark brown */
    UI_COMPANION_STYLE_COUNT
} ui_companion_style_t;

/** Lasting moods: what the face looks like at rest. */
typedef enum {
    UI_COMPANION_MOOD_NEUTRAL = 0,
    UI_COMPANION_MOOD_HAPPY,       /* smiling lower lids, blush */
    UI_COMPANION_MOOD_SLEEPY,      /* heavy lids, slow blinks, looks down */
    UI_COMPANION_MOOD_THINKING,    /* looks up and aside, one eye squints, "..." */
    UI_COMPANION_MOOD_LISTENING,   /* wide, centred, gently breathing */
    UI_COMPANION_MOOD_WORRIED,     /* drooping outer lids, smaller, glances about */
    UI_COMPANION_MOOD_ASLEEP,      /* closed, floating z's; a tap wakes it */
    UI_COMPANION_MOOD_COUNT
} ui_companion_mood_t;

/** One-shot reactions: play over the mood, then return to it. */
typedef enum {
    UI_COMPANION_REACT_NONE = 0,
    UI_COMPANION_REACT_BLINK,
    UI_COMPANION_REACT_BOOP,       /* flinch then smile (a tap) */
    UI_COMPANION_REACT_HAPPY,      /* a little hop and a smile, ~2.4 s */
    UI_COMPANION_REACT_LAUGH,      /* crescent eyes bouncing, ~1.7 s */
    UI_COMPANION_REACT_CONFUSED,   /* head-shake, then lopsided eyes and "?", ~2.4 s */
    UI_COMPANION_REACT_SURPRISED,  /* wide eyes, ~1.1 s */
    UI_COMPANION_REACT_WINK,
    UI_COMPANION_REACT_YAWN,       /* lids sag, slow blink, ~2.6 s */
    UI_COMPANION_REACT_COUNT
} ui_companion_reaction_t;

typedef void (*ui_companion_exit_fn)(void *ctx);

typedef struct {
    ui_companion_style_t style;
    ui_companion_mood_t mood;      /* starting mood */
    uint32_t seed;                 /* idle randomness; 0 = from the tick. Fixed = reproducible renders */
    bool still;                    /* no autoblink, idle gaze or dozing (previews and tests) */
    uint32_t doze_s;               /* no touch this long -> falls ASLEEP (0 = never) */
    ui_companion_exit_fn exit;     /* the "< HOME" key; the app then calls ui_companion_close() */
    void *ctx;
} ui_companion_config_t;

/** Opens the face full-screen on `scr`. One at a time; `cfg` must outlive it.
 *  Unless cfg->still, the eyes start closed and open (waking up). */
lv_obj_t *ui_companion_open(lv_obj_t *scr, const ui_companion_config_t *cfg);
/** Deletes everything the face built (timer and pending exit included).
 *  Safe if not open. Do not call from inside one of the face's own events;
 *  the exit callback is already deferred, so closing from it is fine. */
void ui_companion_close(void);
bool ui_companion_is_open(void);

/** Status hook for the app (e.g. backend down -> WORRIED, night -> SLEEPY).
 *  Setting a mood while ASLEEP wakes the face into that mood. */
void ui_companion_set_mood(ui_companion_mood_t mood);
ui_companion_mood_t ui_companion_get_mood(void);
/** Plays a reaction now (replacing one in progress). Wakes an ASLEEP face. */
void ui_companion_react(ui_companion_reaction_t reaction);
/** Glance at a screen point (e.g. toward a notification) for ~1.5 s. */
void ui_companion_look_at(int32_t x, int32_t y);
/** One line under the eyes, 20 px (NULL or "" hides it). Status should be a
 *  word as well as a mood: "Backend is down." with WORRIED, not the face alone. */
void ui_companion_say(const char *text);

/** For previews and tests: jump straight to `mood` with `reaction` already
 *  `elapsed_ms` into its script, eased values snapped to their targets. */
void ui_companion_preview(ui_companion_mood_t mood, ui_companion_reaction_t reaction, uint32_t elapsed_ms);
/** What the face has asked LVGL to redraw since it opened: timer ticks, how
 *  many of them invalidated anything, and the invalidated pixels (sum and
 *  worst tick). For judging cost on the device as well as in the simulator. */
typedef struct {
    uint32_t ticks;
    uint32_t ticks_with_redraw;
    uint64_t px_total;
    uint32_t px_max;
} ui_companion_stats_t;
void ui_companion_get_stats(ui_companion_stats_t *out);

/** "mood=happy react=boop key=shown" - for tests that cannot see pixels. */
void ui_companion_describe(char *buf, size_t len);

const char *ui_companion_mood_name(ui_companion_mood_t mood);
const char *ui_companion_reaction_name(ui_companion_reaction_t reaction);

#ifdef __cplusplus
}
#endif
