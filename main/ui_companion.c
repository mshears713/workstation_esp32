/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Friendly Companion eyes - see ui_companion.h.
 * @details Provenance: the behaviour vocabulary (rounded-rect eyes, eyelids
 *          drawn in the background colour - triangles for tired/angry, a
 *          rounded rect rising from below for happy - autoblink, idle gaze,
 *          curious outer eye, laugh = vertical shake, confused = horizontal
 *          shake) is modelled on FluxGarage RoboEyes 1.1.2, commit
 *          b42f8e596535234932be3514ac7a813d4ced0046 (GPL-3.0, read, not
 *          copied). This file is written from scratch for LVGL: time-based
 *          easing instead of per-frame halving, scripted reactions with
 *          smooth curves instead of alternate-frame flicker, and touch.
 *
 *          How it runs:
 *            tick_cb (lv_timer, 30 ms while anything moves, 100 ms when
 *            still) -> target pose from mood + reaction + gaze -> ease the
 *            current pose toward it -> integer draw state per eye -> if a
 *            draw state changed, invalidate (old box U new box) of that eye
 *            only -> draw_cb paints both eyes from the stored draw state.
 *
 *          No canvases, images, opacity layers, shadows or transforms.
 *          Everything is solid fills; the only translucency is glyph/dot
 *          opacity, which is a per-pixel blend, not a layer.
 */

#include "ui_companion.h"

#include <stdio.h>
#include <string.h>

#define FACE_W 320
#define FACE_H 240
#define FACE_CX 160
#define PERIOD_ANIM_MS 30
#define PERIOD_IDLE_MS 100
#define PERIOD_ZZZ_MS 66
#define KEY_SHOW_MS 4000
#define CORNER_W 76           /* top-left zone that reveals the HOME key */
#define CORNER_H 68
#define TICKLE_TAPS 4
#define TICKLE_WINDOW_MS 2500
#define FOLLOW_SLOP_PX 12     /* finger moved further than this: a drag, not a tap/hold */
#define MIN_EYE_H 5           /* a closed eye is a line this thick */

/* ---- looks ------------------------------------------------------------- */

typedef struct {
    const char *name;
    uint32_t bg, eye, accent, blush, key_bg, text;
    int16_t eye_w, eye_h, radius, gap, cy;   /* at rest, px */
    int16_t gaze_x, gaze_y;                  /* how far the eyes travel, px */
} style_t;

static const style_t styles[UI_COMPANION_STYLE_COUNT] = {
    /* RoboEyes' own look, scaled up: square-ish, generous radius. Navy
     * and sky come from gallery concept 08 (Friendly Companion). */
    [UI_COMPANION_STYLE_ROBO] = { "robo", 0x0E1628, 0x46D9FF, 0x8CC8FF, 0x9C4A7E, 0x1F2942, 0xEEF1F8,
                                  84, 84, 22, 40, 108, 46, 26 },
    /* Tall pills, closer to a cartoon character than a robot. */
    [UI_COMPANION_STYLE_PILL] = { "pill", 0x000000, 0xF2F5FA, 0xF2F5FA, 0x8E3555, 0x1C1F24, 0xF2F5FA,
                                  54, 104, 27, 50, 108, 52, 22 },
    /* Wide, low, warm: a lamp with a mood. */
    [UI_COMPANION_STYLE_EMBER] = { "ember", 0x1A1109, 0xFFB547, 0xFFD38A, 0xA5482C, 0x33241A, 0xFFE3BC,
                                   100, 62, 31, 34, 108, 40, 30 },
};

static const char *const mood_names[UI_COMPANION_MOOD_COUNT] = {
    "neutral", "happy", "sleepy", "thinking", "listening", "worried", "asleep",
};
static const char *const react_names[UI_COMPANION_REACT_COUNT] = {
    "none", "blink", "boop", "happy", "laugh", "confused", "surprised", "wink", "yawn",
};

/* ---- state ------------------------------------------------------------- */

/* Eased pose values (index into cur[]/tgt[]). L = left eye, R = right. */
enum {
    P_GX, P_GY,                 /* gaze offset, px */
    P_SWL, P_SWR, P_SHL, P_SHR, /* eye width/height scale */
    P_FLATL, P_FLATR,           /* flat upper lid, fraction of eye height */
    P_OUTL, P_OUTR,             /* outer-corner droop (sad, sleepy) */
    P_INL, P_INR,               /* inner-corner lid (squint, cross) */
    P_LOWL, P_LOWR,             /* lower lid rising (smile) */
    P_BLUSH,                    /* 0..1 */
    P_SHUT,                     /* 0..1 eyes closed (falling asleep), eased slowly */
    P_COUNT
};

typedef struct {
    int16_t x1, y1, x2, y2, r;      /* eye rect */
    int16_t flat, outer, inner;     /* lid depths, px (0 = none) */
    int16_t low_top;                /* top of the smile cover, or INT16_MAX */
    int16_t bx1, by1, bx2, by2;     /* blush rect (bx2 < bx1 = none) */
} eye_draw_t;

enum { GLYPH_NONE = 0, GLYPH_QUESTION, GLYPH_ZZZ, GLYPH_DOTS };

typedef struct {
    uint8_t kind;
    uint8_t opa[3];
    int16_t x[3], y[3];
} glyph_draw_t;

/* Allocated with lv_malloc on open (PSRAM on the device) and freed on close,
 * so the face holds no internal RAM while it is not on screen. */
typedef struct {
    const ui_companion_config_t *cfg;
    const style_t *s;
    lv_obj_t *face;
    lv_obj_t *say;
    lv_obj_t *key;
    lv_timer_t *timer;
    uint32_t rng;
    uint32_t last_tick;

    ui_companion_mood_t mood;
    ui_companion_mood_t mood_before_sleep;
    ui_companion_reaction_t react;
    uint32_t react_start;

    float cur[P_COUNT], tgt[P_COUNT];
    float look_x, look_y;           /* where the eyes want to point, px */
    uint32_t look_hold_until;       /* idle gaze leaves look_x/y alone until then */
    uint32_t next_gaze;
    float gaze_tau;

    /* blink: per eye, start time and how long it stays shut */
    bool blinking[2];
    uint32_t blink_start[2];
    uint32_t blink_hold[2];
    uint32_t next_blink;
    bool double_blink;

    /* touch */
    lv_point_t press;
    bool dragging, woke_on_press, press_in_corner;
    uint32_t last_touch;
    uint32_t tickle_first;
    int tickle_count;
    uint32_t key_hide_at;

    /* additive, not eased */
    float shake_x, bounce_y, pulse, squash;

    eye_draw_t ed[2];
    glyph_draw_t gd;
    lv_area_t glyph_box;
    bool drawn;
    ui_companion_stats_t stats;
    uint32_t tick_px;
} companion_t;

static companion_t *st;

/* ---- small helpers ------------------------------------------------------ */

static uint32_t rnd(void)
{
    /* xorshift32: a fixed seed gives the same idle life every run */
    uint32_t x = st->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    st->rng = x;
    return x;
}

static int32_t rnd_range(int32_t lo, int32_t hi)
{
    return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1));
}

static float rnd_unit(void)   /* -1 .. 1 */
{
    return (float)rnd_range(-1000, 1000) / 1000.0f;
}

static float sin_deg(float deg)
{
    int32_t d = (int32_t)deg % 360;
    if (d < 0) {
        d += 360;
    }
    return (float)lv_trigo_sin((int16_t)d) / (float)LV_TRIGO_SIN_MAX;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float absf(float v)
{
    return v < 0 ? -v : v;
}

static int16_t rnd16(float v)
{
    return (int16_t)(v < 0 ? v - 0.5f : v + 0.5f);
}

static uint32_t now_ms(void)
{
    return lv_tick_get();
}

static void wake_timer(void)
{
    if (st->timer) {
        lv_timer_set_period(st->timer, PERIOD_ANIM_MS);
        lv_timer_ready(st->timer);
    }
}

/* ---- blinking ------------------------------------------------------------ */

#define BLINK_CLOSE_MS 70
#define BLINK_OPEN_MS 120

static void blink_eye(int i, uint32_t hold_ms)
{
    st->blinking[i] = true;
    st->blink_start[i] = now_ms();
    st->blink_hold[i] = hold_ms;
}

static void blink_both(uint32_t hold_ms)
{
    blink_eye(0, hold_ms);
    blink_eye(1, hold_ms);
}

/* 0 = open, 1 = shut. Closes with ease-in, opens fast then settles. */
static float blink_amount(int i, uint32_t now)
{
    if (!st->blinking[i]) {
        return 0;
    }
    uint32_t t = now - st->blink_start[i];
    uint32_t hold = st->blink_hold[i];
    if (t < BLINK_CLOSE_MS) {
        float u = (float)t / BLINK_CLOSE_MS;
        return u * u;
    }
    if (t < BLINK_CLOSE_MS + hold) {
        return 1;
    }
    t -= BLINK_CLOSE_MS + hold;
    if (t < BLINK_OPEN_MS) {
        float u = 1.0f - (float)t / BLINK_OPEN_MS;
        return u * u;
    }
    st->blinking[i] = false;
    return 0;
}

/* ---- idle life ------------------------------------------------------------ */

static void schedule_blink(uint32_t now)
{
    int32_t lo = 2400, hi = 5600;
    if (st->mood == UI_COMPANION_MOOD_SLEEPY) {
        lo = 3500;
        hi = 7000;
    } else if (st->mood == UI_COMPANION_MOOD_LISTENING) {
        lo = 3000;
        hi = 6000;
    }
    st->next_blink = now + (uint32_t)rnd_range(lo, hi);
}

static void schedule_gaze(uint32_t now)
{
    int32_t lo = 1400, hi = 4200;
    switch (st->mood) {
    case UI_COMPANION_MOOD_SLEEPY:    lo = 2500; hi = 6000; break;
    case UI_COMPANION_MOOD_WORRIED:   lo = 900;  hi = 2400; break;
    case UI_COMPANION_MOOD_LISTENING: lo = 2000; hi = 5000; break;
    case UI_COMPANION_MOOD_THINKING:  lo = 1800; hi = 4000; break;
    default: break;
    }
    st->next_gaze = now + (uint32_t)rnd_range(lo, hi);
}

static void pick_gaze(void)
{
    float ax = st->s->gaze_x, ay = st->s->gaze_y;
    switch (st->mood) {
    case UI_COMPANION_MOOD_THINKING:
        st->look_x = ax * (0.55f + 0.15f * rnd_unit());
        st->look_y = ay * (-0.75f + 0.12f * rnd_unit());
        break;
    case UI_COMPANION_MOOD_LISTENING:
        st->look_x = ax * 0.12f * rnd_unit();
        st->look_y = ay * 0.12f * rnd_unit();
        break;
    case UI_COMPANION_MOOD_WORRIED:
        st->look_x = ax * 0.9f * rnd_unit();
        st->look_y = ay * (0.25f + 0.25f * rnd_unit());
        break;
    case UI_COMPANION_MOOD_ASLEEP:
        st->look_x = 0;
        st->look_y = 0;
        break;
    default:
        if (rnd() % 100 < 35) {
            st->look_x = 0;   /* back to looking at you */
            st->look_y = 0;
        } else {
            st->look_x = ax * 0.9f * rnd_unit();
            st->look_y = ay * 0.8f * rnd_unit();
        }
        break;
    }
    /* People often blink with a big eye movement. */
    if (rnd() % 100 < 20 && !st->blinking[0]) {
        blink_both(30);
    }
}

/* ---- mood + reaction -> target pose ---------------------------------------- */

static void set_both(int left, float v)
{
    st->tgt[left] = v;
    st->tgt[left + 1] = v;
}

static void base_pose(void)
{
    st->tgt[P_GX] = st->look_x;
    st->tgt[P_GY] = st->look_y;
    set_both(P_SWL, 1);
    set_both(P_SHL, 1);
    set_both(P_FLATL, 0);
    set_both(P_OUTL, 0);
    set_both(P_INL, 0);
    set_both(P_LOWL, 0);
    st->tgt[P_BLUSH] = 0;
    st->tgt[P_SHUT] = 0;
}

static void mood_pose(uint32_t now)
{
    switch (st->mood) {
    case UI_COMPANION_MOOD_HAPPY:
        set_both(P_LOWL, 0.62f);
        st->tgt[P_BLUSH] = 0.8f;
        break;
    case UI_COMPANION_MOOD_SLEEPY:
        set_both(P_FLATL, 0.42f);
        set_both(P_OUTL, 0.22f);
        set_both(P_SWL, 1.04f);
        st->tgt[P_GY] += 6;
        break;
    case UI_COMPANION_MOOD_THINKING:
        /* "hmm": both eyes narrowed from below, looking up and aside.
         * Symmetric on purpose - lopsided eyes belong to CONFUSED. */
        set_both(P_LOWL, 0.34f);
        set_both(P_SHL, 0.94f);
        break;
    case UI_COMPANION_MOOD_LISTENING:
        set_both(P_SWL, 1.08f);
        set_both(P_SHL, 1.08f);
        st->pulse = 1.0f + 0.035f * sin_deg((float)(now % 1250) * 360.0f / 1250.0f);
        break;
    case UI_COMPANION_MOOD_WORRIED:
        set_both(P_OUTL, 0.42f);
        set_both(P_SWL, 0.92f);
        set_both(P_SHL, 0.92f);
        st->tgt[P_GY] += 5;
        break;
    case UI_COMPANION_MOOD_ASLEEP:
        st->tgt[P_GY] = 12;
        st->tgt[P_GX] = 0;
        set_both(P_SWL, 1.06f);
        set_both(P_FLATL, 0.42f);        /* keep the sleepy droop while closing */
        set_both(P_OUTL, 0.22f);
        st->tgt[P_SHUT] = 1;
        break;
    default:
        break;
    }
}

/* Reactions replace the mood's lids for their duration (the gaze stays). */
static uint32_t react_length(ui_companion_reaction_t r)
{
    switch (r) {
    case UI_COMPANION_REACT_BOOP:      return 1500;
    case UI_COMPANION_REACT_HAPPY:     return 2400;
    case UI_COMPANION_REACT_LAUGH:     return 1700;
    case UI_COMPANION_REACT_CONFUSED:  return 2400;
    case UI_COMPANION_REACT_SURPRISED: return 1100;
    case UI_COMPANION_REACT_WINK:      return 900;
    case UI_COMPANION_REACT_YAWN:      return 2600;
    default:                           return 0;
    }
}

static bool react_pose(uint32_t now, float *tau_shape, float *tau_lid)
{
    uint32_t rt = now - st->react_start;
    if (st->react == UI_COMPANION_REACT_NONE || rt >= react_length(st->react)) {
        if (st->react != UI_COMPANION_REACT_NONE) {
            st->react = UI_COMPANION_REACT_NONE;
            schedule_gaze(now);
        }
        return false;
    }
    float ay = st->s->gaze_y, ax = st->s->gaze_x;
    float gy = st->tgt[P_GY], gx = st->tgt[P_GX];
    base_pose();
    st->tgt[P_GX] = gx;
    st->tgt[P_GY] = gy;

    switch (st->react) {
    case UI_COMPANION_REACT_BOOP:
        if (rt < 100) {            /* flinch: squash */
            set_both(P_SHL, 0.70f);
            set_both(P_SWL, 1.12f);
            *tau_shape = 30;
        } else if (rt < 280) {     /* pop back up, a bit too far */
            set_both(P_SHL, 1.12f);
            set_both(P_SWL, 0.96f);
            *tau_shape = 45;
        } else {                   /* then a smile */
            set_both(P_LOWL, 0.8f);
            st->tgt[P_BLUSH] = 1;
        }
        break;
    case UI_COMPANION_REACT_HAPPY:
        set_both(P_LOWL, 0.82f);
        st->tgt[P_BLUSH] = 1;
        if (rt < 420) {
            st->bounce_y = -9.0f * absf(sin_deg((float)rt * 180.0f / 420.0f));
        }
        break;
    case UI_COMPANION_REACT_LAUGH: {
        set_both(P_LOWL, 0.95f);
        set_both(P_SWL, 1.06f);
        st->tgt[P_BLUSH] = 1;
        st->tgt[P_GX] = 0;
        st->tgt[P_GY] = -ay * 0.25f;
        float fade = 1.0f - (float)rt / 1700.0f;
        /* 3.5 hops a second, like chuckles */
        float hop = absf(sin_deg((float)rt * 180.0f * 3.5f / 1000.0f));
        st->bounce_y = -16.0f * fade * hop;
        st->squash = 1.0f - 0.14f * fade * (1.0f - hop);   /* flatten on each landing */
        *tau_lid = 50;
        break;
    }
    case UI_COMPANION_REACT_CONFUSED:
        if (rt < 650) {            /* shake the head */
            st->shake_x = 13.0f * sin_deg((float)rt * 360.0f * 5.0f / 1000.0f) * (1.0f - (float)rt / 650.0f);
            st->tgt[P_GX] = 0;
            st->tgt[P_GY] = 0;
        } else {                   /* then one eye up, one squinting, and a "?" */
            st->tgt[P_SHL] = 0.78f;
            st->tgt[P_INL] = 0.35f;
            st->tgt[P_SHR] = 1.12f;
            st->tgt[P_GX] = -ax * 0.45f;
            st->tgt[P_GY] = -ay * 0.5f;
        }
        break;
    case UI_COMPANION_REACT_SURPRISED:
        set_both(P_SWL, 1.2f);
        set_both(P_SHL, 1.22f);
        st->tgt[P_GX] = 0;
        st->tgt[P_GY] = -ay * 0.2f;
        *tau_shape = 35;
        *tau_lid = 35;
        break;
    case UI_COMPANION_REACT_WINK:
        st->tgt[P_LOWL] = 0.55f;
        st->tgt[P_BLUSH] = 1;
        break;
    case UI_COMPANION_REACT_YAWN:
        set_both(P_FLATL, 0.55f);
        set_both(P_OUTL, 0.2f);
        set_both(P_SWL, 1.06f);
        st->tgt[P_GY] = ay * 0.4f;
        *tau_lid = 260;
        break;
    default:
        break;
    }
    return true;
}

/* Curiosity (RoboEyes idea): looking sideways, the outer eye grows. */
static void curiosity(void)
{
    float k = clampf(st->tgt[P_GX] / st->s->gaze_x, -1, 1);
    if (k > 0) {
        st->tgt[P_SHR] *= 1.0f + 0.12f * k;
        st->tgt[P_SHL] *= 1.0f - 0.05f * k;
    } else {
        st->tgt[P_SHL] *= 1.0f - 0.12f * k;
        st->tgt[P_SHR] *= 1.0f + 0.05f * k;
    }
}

/* ---- geometry ------------------------------------------------------------- */

static void compute_draw(uint32_t now, eye_draw_t out[2], glyph_draw_t *g)
{
    const style_t *s = st->s;
    float k = clampf(st->cur[P_GX] / s->gaze_x, -1, 1);
    float gap = s->gap * (1.0f - 0.12f * absf(k));   /* a turned head brings the eyes closer */
    bool asleep = st->mood == UI_COMPANION_MOOD_ASLEEP && st->react == UI_COMPANION_REACT_NONE;

    for (int i = 0; i < 2; i++) {
        eye_draw_t *e = &out[i];
        float sw = st->cur[P_SWL + i], sh = st->cur[P_SHL + i];
        float w = s->eye_w * sw * st->pulse;
        float h = s->eye_h * sh * st->pulse * st->squash;
        float shut = blink_amount(i, now);
        if (st->cur[P_SHUT] > shut) {
            shut = st->cur[P_SHUT];
        }
        float hd = h * (1.0f - shut);
        if (hd < MIN_EYE_H) {
            hd = MIN_EYE_H;
        }
        float side = i == 0 ? -1.0f : 1.0f;
        float cx = FACE_CX + side * (gap / 2 + s->eye_w / 2.0f) + st->cur[P_GX] + st->shake_x;
        float cy = s->cy + st->cur[P_GY] + st->bounce_y;
        e->x1 = rnd16(cx - w / 2);
        e->x2 = rnd16(cx + w / 2) - 1;
        e->y1 = rnd16(cy - hd / 2);
        e->y2 = rnd16(cy + hd / 2) - 1;
        float r = s->radius * (sw < sh ? sw : sh);
        if (r > hd / 2) {
            r = hd / 2;
        }
        if (r > w / 2) {
            r = w / 2;
        }
        e->r = rnd16(r);
        /* Lids only on an open eye; a closed eye is just the line. */
        bool lids = hd >= 10;
        e->flat = lids ? rnd16(st->cur[P_FLATL + i] * hd) : 0;
        e->outer = lids ? rnd16(st->cur[P_OUTL + i] * hd) : 0;
        e->inner = lids ? rnd16(st->cur[P_INL + i] * hd) : 0;
        float low = st->cur[P_LOWL + i];
        e->low_top = (lids && low > 0.02f) ? rnd16(cy + hd / 2 - low * 0.7f * hd) : INT16_MAX;
        float b = st->cur[P_BLUSH];
        if (b > 0.05f) {
            /* under the eye, nudged outward */
            float bw = s->eye_w * 0.52f * b, bh = 9.0f * b;
            float bcx = cx + side * s->eye_w * 0.14f;
            float bcy = s->cy + st->cur[P_GY] * 0.5f + s->eye_h / 2.0f + 13;
            e->bx1 = rnd16(bcx - bw / 2);
            e->bx2 = rnd16(bcx + bw / 2);
            e->by1 = rnd16(bcy - bh / 2);
            e->by2 = rnd16(bcy + bh / 2);
        } else {
            e->bx1 = 0;
            e->bx2 = -1;
            e->by1 = 0;
            e->by2 = -1;
        }
    }

    memset(g, 0, sizeof(*g));
    const eye_draw_t *R = &out[1];
    if (st->react == UI_COMPANION_REACT_CONFUSED && now - st->react_start >= 650) {
        uint32_t t = now - st->react_start - 650;
        g->kind = GLYPH_QUESTION;
        g->opa[0] = (uint8_t)(t >= 150 ? 255 : t * 255 / 150);
        g->x[0] = R->x2 + 8;
        g->y[0] = R->y1 - 30 - (int16_t)(t < 150 ? (150 - t) / 15 : 0);
    } else if (asleep && st->cur[P_SHUT] > 0.8f) {
        g->kind = GLYPH_ZZZ;
        uint32_t base = now - st->react_start;   /* react_start doubles as sleep start */
        for (int k2 = 0; k2 < 3; k2++) {
            uint32_t p = (base + (uint32_t)k2 * 800) % 2400;
            float u = (float)p / 2400.0f;
            g->x[k2] = (int16_t)(FACE_CX + s->gap / 2 + s->eye_w - 6 + (int16_t)(u * 22) + k2 * 4);
            g->y[k2] = (int16_t)(s->cy + 4 - (int16_t)(u * 58));
            float o = u < 0.2f ? u / 0.2f : (1.0f - u) / 0.8f;
            g->opa[k2] = (uint8_t)(o * 255);
        }
    } else if (st->mood == UI_COMPANION_MOOD_THINKING && st->react == UI_COMPANION_REACT_NONE) {
        g->kind = GLYPH_DOTS;
        int active = (int)((now / 380) % 3);
        for (int k2 = 0; k2 < 3; k2++) {
            g->x[k2] = (int16_t)(FACE_CX - 18 + k2 * 18);
            g->y[k2] = (int16_t)(s->cy + s->eye_h / 2 + 30);
            g->opa[k2] = k2 == active ? 255 : 80;
        }
    }
}

static void eye_box(const eye_draw_t *e, lv_area_t *a)
{
    a->x1 = e->x1 - 4;
    a->y1 = e->y1 - 4;
    a->x2 = e->x2 + 4;
    a->y2 = e->y2 + 4;
    if (e->bx2 >= e->bx1) {
        if (e->bx1 - 2 < a->x1) a->x1 = e->bx1 - 2;
        if (e->bx2 + 2 > a->x2) a->x2 = e->bx2 + 2;
        if (e->by2 + 2 > a->y2) a->y2 = e->by2 + 2;
    }
}

static void glyph_box(const glyph_draw_t *g, lv_area_t *a)
{
    a->x1 = INT16_MAX;
    a->y1 = INT16_MAX;
    a->x2 = INT16_MIN;
    a->y2 = INT16_MIN;
    int n = g->kind == GLYPH_QUESTION ? 1 : (g->kind == GLYPH_NONE ? 0 : 3);
    int bw = g->kind == GLYPH_QUESTION ? 30 : (g->kind == GLYPH_DOTS ? 12 : 22);
    int bh = g->kind == GLYPH_QUESTION ? 40 : (g->kind == GLYPH_DOTS ? 12 : 26);
    for (int i = 0; i < n; i++) {
        if (g->x[i] - 2 < a->x1) a->x1 = g->x[i] - 2;
        if (g->y[i] - 2 < a->y1) a->y1 = g->y[i] - 2;
        if (g->x[i] + bw > a->x2) a->x2 = g->x[i] + bw;
        if (g->y[i] + bh > a->y2) a->y2 = g->y[i] + bh;
    }
}

static void invalidate_union(const lv_area_t *a, const lv_area_t *b)
{
    lv_area_t u;
    bool va = a->x2 >= a->x1, vb = b->x2 >= b->x1;
    if (va && vb) {
        /* lv_area_join is private API in LVGL 9.5; this is all it does */
        u.x1 = LV_MIN(a->x1, b->x1);
        u.y1 = LV_MIN(a->y1, b->y1);
        u.x2 = LV_MAX(a->x2, b->x2);
        u.y2 = LV_MAX(a->y2, b->y2);
    } else if (va) {
        u = *a;
    } else if (vb) {
        u = *b;
    } else {
        return;
    }
    st->tick_px += (uint32_t)lv_area_get_size(&u);
    lv_obj_invalidate_area(st->face, &u);
}

static void refresh(uint32_t now)
{
    eye_draw_t ne[2];
    glyph_draw_t ng;
    compute_draw(now, ne, &ng);
    for (int i = 0; i < 2; i++) {
        if (!st->drawn || memcmp(&ne[i], &st->ed[i], sizeof(ne[i])) != 0) {
            lv_area_t a, b;
            eye_box(&st->ed[i], &a);
            eye_box(&ne[i], &b);
            if (!st->drawn) {
                a = b;
            }
            invalidate_union(&a, &b);
            st->ed[i] = ne[i];
        }
    }
    if (memcmp(&ng, &st->gd, sizeof(ng)) != 0) {
        lv_area_t nb;
        glyph_box(&ng, &nb);
        invalidate_union(&st->glyph_box, &nb);
        st->gd = ng;
        st->glyph_box = nb;
    }
    st->drawn = true;
}

/* ---- drawing ---------------------------------------------------------------- */

static void fill_rect(lv_layer_t *layer, int x1, int y1, int x2, int y2, int r, lv_color_t c, lv_opa_t opa)
{
    if (x2 < x1 || y2 < y1) {
        return;
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = opa;
    d.radius = r;
    lv_area_t a = { x1, y1, x2, y2 };
    lv_draw_rect(layer, &d, &a);
}

static void fill_tri(lv_layer_t *layer, int ax, int ay, int bx, int by, int cx, int cy, lv_color_t c)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = c;
    d.opa = LV_OPA_COVER;
    d.p[0].x = ax;
    d.p[0].y = ay;
    d.p[1].x = bx;
    d.p[1].y = by;
    d.p[2].x = cx;
    d.p[2].y = cy;
    lv_draw_triangle(layer, &d);
}

static void draw_eye(lv_layer_t *layer, const eye_draw_t *e, bool left, lv_color_t eye, lv_color_t bg)
{
    fill_rect(layer, e->x1, e->y1, e->x2, e->y2, e->r, eye, LV_OPA_COVER);
    int x1 = e->x1 - 2, x2 = e->x2 + 2;
    int top = e->y1 - 2;
    if (e->flat > 0) {
        fill_rect(layer, x1, top, x2, e->y1 + e->flat, 0, bg, LV_OPA_COVER);
        top = e->y1 + e->flat;
    }
    /* Outer corner = the left eye's left side, the right eye's right side. */
    if (e->outer > 0) {
        if (left) {
            fill_tri(layer, x1, top, x2, top, x1, top + e->outer, bg);
        } else {
            fill_tri(layer, x1, top, x2, top, x2, top + e->outer, bg);
        }
    }
    if (e->inner > 0) {
        if (left) {
            fill_tri(layer, x1, top, x2, top, x2, top + e->inner, bg);
        } else {
            fill_tri(layer, x1, top, x2, top, x1, top + e->inner, bg);
        }
    }
    if (e->low_top != INT16_MAX) {
        /* A disc a little wider than the eye rising from below leaves a
         * smiling crescent: thick in the middle, tapering to the corners. */
        int w = e->x2 - e->x1 + 1;
        int cw = w * 13 / 10;
        int cx = (e->x1 + e->x2) / 2;
        fill_rect(layer, cx - cw / 2, e->low_top, cx + cw / 2, e->low_top + cw, LV_RADIUS_CIRCLE, bg,
                  LV_OPA_COVER);
    }
}

static void draw_cb(lv_event_t *ev)
{
    lv_layer_t *layer = lv_event_get_layer(ev);
    lv_color_t bg = lv_color_hex(st->s->bg);
    lv_color_t eye = lv_color_hex(st->s->eye);
    for (int i = 0; i < 2; i++) {
        draw_eye(layer, &st->ed[i], i == 0, eye, bg);
    }
    for (int i = 0; i < 2; i++) {
        const eye_draw_t *e = &st->ed[i];
        if (e->bx2 >= e->bx1) {
            fill_rect(layer, e->bx1, e->by1, e->bx2, e->by2, (e->by2 - e->by1) / 2 + 1,
                      lv_color_hex(st->s->blush), LV_OPA_COVER);
        }
    }
    const glyph_draw_t *g = &st->gd;
    lv_color_t accent = lv_color_hex(st->s->accent);
    if (g->kind == GLYPH_DOTS) {
        for (int i = 0; i < 3; i++) {
            fill_rect(layer, g->x[i], g->y[i], g->x[i] + 9, g->y[i] + 9, 5, accent, g->opa[i]);
        }
    } else if (g->kind != GLYPH_NONE) {
        static const char *const zs[3] = { "z", "z", "Z" };
        int n = g->kind == GLYPH_QUESTION ? 1 : 3;
        for (int i = 0; i < n; i++) {
            if (g->opa[i] == 0) {
                continue;
            }
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.color = accent;
            d.opa = g->opa[i];
            d.font = g->kind == GLYPH_QUESTION ? &lv_font_montserrat_32 : &lv_font_montserrat_20;
            d.text = g->kind == GLYPH_QUESTION ? "?" : zs[i];
            lv_area_t a = { g->x[i], g->y[i], g->x[i] + 40, g->y[i] + 40 };
            lv_draw_label(layer, &d, &a);
        }
    }
}

/* ---- the tick ------------------------------------------------------------- */

static void ease(int i, float tau, float dt)
{
    /* first-order lag: frame-rate independent enough, no libm */
    st->cur[i] += (st->tgt[i] - st->cur[i]) * (dt / (tau + dt));
}

static void hide_key(void)
{
    if (st->key) {
        lv_obj_delete(st->key);
        st->key = NULL;
    }
}

static void doze_check(uint32_t now)
{
    const ui_companion_config_t *c = st->cfg;
    if (c->still || c->doze_s == 0 || st->mood == UI_COMPANION_MOOD_ASLEEP ||
        st->react != UI_COMPANION_REACT_NONE || st->key) {
        return;
    }
    if (now - st->last_touch >= c->doze_s * 1000U) {
        st->mood_before_sleep = st->mood;
        st->mood = UI_COMPANION_MOOD_ASLEEP;
        st->react_start = now;
    }
}

static void step(uint32_t now, float dt, bool snap)
{
    st->shake_x = 0;
    st->bounce_y = 0;
    st->pulse = 1;
    st->squash = 1;

    if (st->key && (int32_t)(now - st->key_hide_at) >= 0) {
        hide_key();
        st->look_hold_until = 0;
    }
    doze_check(now);

    /* idle life */
    bool idle_ok = !st->cfg->still && st->mood != UI_COMPANION_MOOD_ASLEEP && !st->dragging;
    if (idle_ok && st->react == UI_COMPANION_REACT_NONE && (int32_t)(now - st->next_blink) >= 0) {
        uint32_t hold = st->mood == UI_COMPANION_MOOD_SLEEPY ? 260 : 30;
        blink_both(hold);
        if (rnd() % 100 < 18) {
            st->double_blink = true;
        }
        schedule_blink(now);
    }
    if (st->double_blink && !st->blinking[0] && !st->blinking[1]) {
        st->double_blink = false;
        blink_both(30);
    }
    if (idle_ok && st->react == UI_COMPANION_REACT_NONE && (int32_t)(now - st->look_hold_until) >= 0 &&
        (int32_t)(now - st->next_gaze) >= 0) {
        pick_gaze();
        schedule_gaze(now);
    }

    float tau_shape = 60, tau_lid = 85;
    base_pose();
    mood_pose(now);
    if (react_pose(now, &tau_shape, &tau_lid)) {
        if (st->react == UI_COMPANION_REACT_YAWN && now - st->react_start >= 1900 && !st->blinking[0] &&
            now - st->react_start < 1950) {
            blink_both(300);
        }
    }
    curiosity();

    float gaze_tau = st->dragging ? 45 : (st->mood == UI_COMPANION_MOOD_SLEEPY ? 160 : st->gaze_tau);
    if (st->mood == UI_COMPANION_MOOD_ASLEEP) {
        tau_lid = 300;
        tau_shape = 300;
        gaze_tau = 400;
    }
    for (int i = 0; i < P_COUNT; i++) {
        if (snap) {
            st->cur[i] = st->tgt[i];
            continue;
        }
        float tau = i <= P_GY ? gaze_tau : (i <= P_SHR ? tau_shape : (i == P_BLUSH ? 140 : tau_lid));
        ease(i, tau, dt);
    }
}

static bool moving(void)
{
    if (st->react != UI_COMPANION_REACT_NONE || st->blinking[0] || st->blinking[1] || st->double_blink ||
        st->dragging || st->mood == UI_COMPANION_MOOD_LISTENING) {
        return true;
    }
    for (int i = 0; i < P_COUNT; i++) {
        float lim = i <= P_GY ? 0.3f : 0.004f;
        if (absf(st->tgt[i] - st->cur[i]) > lim) {
            return true;
        }
    }
    return false;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = now_ms();
    float dt = (float)(now - st->last_tick);
    st->last_tick = now;
    if (dt < 1) {
        dt = 1;
    }
    if (dt > 100) {
        dt = 100;
    }
    st->tick_px = 0;
    step(now, dt, false);
    refresh(now);
    st->stats.ticks++;
    if (st->tick_px) {
        st->stats.ticks_with_redraw++;
        st->stats.px_total += st->tick_px;
        if (st->tick_px > st->stats.px_max) {
            st->stats.px_max = st->tick_px;
        }
    }
    uint32_t period = moving() ? PERIOD_ANIM_MS
                               : (st->mood == UI_COMPANION_MOOD_ASLEEP ? PERIOD_ZZZ_MS : PERIOD_IDLE_MS);
    lv_timer_set_period(st->timer, period);
}

/* ---- touch ------------------------------------------------------------------- */

static void fire_exit(void *unused)
{
    (void)unused;
    if (st && st->face && st->cfg->exit) {
        st->cfg->exit(st->cfg->ctx);
    }
}

static void key_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(fire_exit, NULL);   /* the app deletes this key */
}

static void show_key(uint32_t now)
{
    if (!st->key) {
        st->key = lv_button_create(st->face);
        lv_obj_remove_style_all(st->key);
        lv_obj_set_size(st->key, 104, 48);
        lv_obj_set_pos(st->key, 8, 8);
        lv_obj_set_style_radius(st->key, 24, 0);
        lv_obj_set_style_bg_opa(st->key, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(st->key, lv_color_hex(st->s->key_bg), 0);
        lv_obj_set_style_border_width(st->key, 2, 0);
        lv_obj_set_style_border_color(st->key, lv_color_hex(st->s->accent), 0);
        lv_obj_set_style_bg_color(st->key, lv_color_hex(st->s->accent), LV_STATE_PRESSED);
        lv_obj_add_event_cb(st->key, key_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *l = lv_label_create(st->key);
        lv_label_set_text(l, LV_SYMBOL_LEFT " HOME");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(st->s->text), 0);
        lv_obj_center(l);
    }
    st->key_hide_at = now + KEY_SHOW_MS;
    /* glance toward it - sideways only, so the eye never slides under the key */
    st->look_x = -st->s->gaze_x * 0.6f;
    st->look_y = 0;
    st->look_hold_until = st->key_hide_at;
}

static void start_react(ui_companion_reaction_t r, uint32_t now)
{
    if (st->mood == UI_COMPANION_MOOD_ASLEEP && r != UI_COMPANION_REACT_BLINK) {
        st->mood = st->mood_before_sleep;
    }
    if (r == UI_COMPANION_REACT_BLINK) {
        blink_both(30);
    } else {
        st->react = r;
        st->react_start = now;
        if (r == UI_COMPANION_REACT_WINK) {
            blink_eye(1, 330);
        }
        if (r == UI_COMPANION_REACT_SURPRISED) {
            st->blinking[0] = st->blinking[1] = false;
        }
    }
    wake_timer();
}

static void tap(lv_point_t p, uint32_t now)
{
    if (now - st->tickle_first > TICKLE_WINDOW_MS) {
        st->tickle_first = now;
        st->tickle_count = 0;
    }
    if (++st->tickle_count >= TICKLE_TAPS) {
        st->tickle_count = 0;
        start_react(UI_COMPANION_REACT_CONFUSED, now);
        return;
    }
    /* look at the finger */
    float ax = st->s->gaze_x, ay = st->s->gaze_y;
    st->look_x = clampf((float)(p.x - FACE_CX) / 130.0f, -1, 1) * ax;
    st->look_y = clampf((float)(p.y - st->s->cy) / 100.0f, -1, 1) * ay;
    st->look_hold_until = now + 1500;
    start_react(UI_COMPANION_REACT_BOOP, now);
}

static void face_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = { 0, 0 };
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    uint32_t now = now_ms();

    switch (code) {
    case LV_EVENT_PRESSED:
        st->press = p;
        st->dragging = false;
        st->press_in_corner = p.x < CORNER_W && p.y < CORNER_H;
        st->woke_on_press = st->mood == UI_COMPANION_MOOD_ASLEEP;
        st->last_touch = now;
        if (st->woke_on_press) {
            start_react(UI_COMPANION_REACT_SURPRISED, now);
        } else if (!st->press_in_corner) {
            tap(p, now);   /* react on touch, not on release: it feels alive */
        }
        break;
    case LV_EVENT_PRESSING: {
        int dx = p.x - st->press.x, dy = p.y - st->press.y;
        if (!st->dragging && !st->press_in_corner && dx * dx + dy * dy > FOLLOW_SLOP_PX * FOLLOW_SLOP_PX) {
            st->dragging = true;
            wake_timer();
        }
        if (st->dragging) {
            st->look_x = clampf((float)(p.x - FACE_CX) / 130.0f, -1, 1) * st->s->gaze_x;
            st->look_y = clampf((float)(p.y - st->s->cy) / 100.0f, -1, 1) * st->s->gaze_y;
            st->look_hold_until = now + 800;
            st->last_touch = now;
        }
        break;
    }
    case LV_EVENT_RELEASED:
        st->last_touch = now;
        if (st->dragging) {
            st->dragging = false;
            st->look_hold_until = now + 800;
            st->press_in_corner = false;
            st->woke_on_press = true;   /* swallow the click that follows a drag */
        }
        break;
    case LV_EVENT_LONG_PRESSED:
        if (!st->dragging && !st->woke_on_press && !st->press_in_corner) {
            start_react(UI_COMPANION_REACT_LAUGH, now);
        }
        break;
    case LV_EVENT_SHORT_CLICKED:
        if (st->woke_on_press || st->dragging) {
            break;
        }
        if (st->press_in_corner) {
            show_key(now);
            wake_timer();
        }
        break;
    default:
        break;
    }
}

/* ---- public ------------------------------------------------------------------ */

lv_obj_t *ui_companion_open(lv_obj_t *scr, const ui_companion_config_t *cfg)
{
    ui_companion_close();
    st = lv_malloc_zeroed(sizeof(*st));
    if (!st) {
        return NULL;
    }
    st->cfg = cfg;
    st->s = &styles[cfg->style < UI_COMPANION_STYLE_COUNT ? cfg->style : 0];
    st->rng = cfg->seed ? cfg->seed : (lv_tick_get() * 2654435761u) | 1u;
    st->mood = cfg->mood < UI_COMPANION_MOOD_COUNT ? cfg->mood : UI_COMPANION_MOOD_NEUTRAL;
    st->mood_before_sleep = st->mood == UI_COMPANION_MOOD_ASLEEP ? UI_COMPANION_MOOD_NEUTRAL : st->mood;
    st->gaze_tau = 55;
    st->pulse = 1;
    st->squash = 1;
    st->glyph_box = (lv_area_t){ 0, 0, -1, -1 };

    uint32_t now = now_ms();
    st->last_tick = now;
    st->last_touch = now;
    st->react_start = now;
    st->tickle_first = now;

    st->face = lv_obj_create(scr);
    lv_obj_remove_style_all(st->face);
    lv_obj_set_size(st->face, FACE_W, FACE_H);
    lv_obj_set_pos(st->face, 0, 0);
    lv_obj_set_style_bg_opa(st->face, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(st->face, lv_color_hex(st->s->bg), 0);
    lv_obj_clear_flag(st->face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(st->face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(st->face, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(st->face, face_cb, LV_EVENT_ALL, NULL);

    st->say = lv_label_create(st->face);
    lv_obj_set_style_text_font(st->say, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(st->say, lv_color_hex(st->s->text), 0);
    lv_obj_set_style_text_align(st->say, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(st->say, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(st->say, 288, 24);   /* fixed height: one line, "..." if longer */
    lv_obj_set_pos(st->say, 16, 202);
    lv_label_set_text(st->say, "");
    lv_obj_add_flag(st->say, LV_OBJ_FLAG_HIDDEN);

    /* Start from the resting pose of the mood, eyes shut unless still. */
    step(now, 1, true);
    if (!cfg->still) {
        blink_both(280);   /* waking up: start shut, then open */
        st->blink_start[0] = st->blink_start[1] = now - BLINK_CLOSE_MS;
        schedule_blink(now + 600);
        st->next_gaze = now + 1200;
    }
    refresh(now);
    st->timer = lv_timer_create(tick_cb, PERIOD_ANIM_MS, NULL);
    return st->face;
}

void ui_companion_close(void)
{
    lv_async_call_cancel(fire_exit, NULL);
    if (st && st->timer) {
        lv_timer_delete(st->timer);
        st->timer = NULL;
    }
    if (st && st->face) {
        lv_obj_delete(st->face);   /* the say line and the key go with it */
        st->face = NULL;
    }
    if (st) {
        lv_free(st);
        st = NULL;
    }
}

bool ui_companion_is_open(void)
{
    return st && st->face;
}

void ui_companion_set_mood(ui_companion_mood_t mood)
{
    if (!st || !st->face || mood >= UI_COMPANION_MOOD_COUNT || mood == st->mood) {
        return;
    }
    if (mood == UI_COMPANION_MOOD_ASLEEP) {
        st->mood_before_sleep = st->mood;
        st->react = UI_COMPANION_REACT_NONE;
    }
    uint32_t now = now_ms();
    st->mood = mood;
    st->react_start = now;   /* for ASLEEP: when the z's start */
    st->look_hold_until = now;
    pick_gaze();            /* aim for the new mood straight away */
    schedule_gaze(now);
    wake_timer();
}

ui_companion_mood_t ui_companion_get_mood(void)
{
    return st ? st->mood : UI_COMPANION_MOOD_NEUTRAL;
}

void ui_companion_react(ui_companion_reaction_t reaction)
{
    if (!st || !st->face || reaction == UI_COMPANION_REACT_NONE || reaction >= UI_COMPANION_REACT_COUNT) {
        return;
    }
    start_react(reaction, now_ms());
}

void ui_companion_look_at(int32_t x, int32_t y)
{
    if (!st || !st->face) {
        return;
    }
    st->look_x = clampf((float)(x - FACE_CX) / 130.0f, -1, 1) * st->s->gaze_x;
    st->look_y = clampf((float)(y - st->s->cy) / 100.0f, -1, 1) * st->s->gaze_y;
    st->look_hold_until = now_ms() + 1500;
    wake_timer();
}

void ui_companion_say(const char *text)
{
    if (!st || !st->say) {
        return;
    }
    if (text && text[0]) {
        lv_label_set_text(st->say, text);
        lv_obj_remove_flag(st->say, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(st->say, "");
        lv_obj_add_flag(st->say, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_companion_preview(ui_companion_mood_t mood, ui_companion_reaction_t reaction, uint32_t elapsed_ms)
{
    if (!st || !st->face) {
        return;
    }
    uint32_t now = now_ms();
    st->mood = mood < UI_COMPANION_MOOD_COUNT ? mood : UI_COMPANION_MOOD_NEUTRAL;
    st->react = reaction < UI_COMPANION_REACT_COUNT ? reaction : UI_COMPANION_REACT_NONE;
    if (st->react == UI_COMPANION_REACT_BLINK) {
        st->react = UI_COMPANION_REACT_NONE;
    }
    st->react_start = now - elapsed_ms;
    st->blinking[0] = st->blinking[1] = false;
    if (st->react == UI_COMPANION_REACT_WINK && elapsed_ms < BLINK_CLOSE_MS + 330 + BLINK_OPEN_MS) {
        st->blinking[1] = true;
        st->blink_start[1] = now - elapsed_ms;
        st->blink_hold[1] = 330;
    }
    if (st->mood == UI_COMPANION_MOOD_THINKING) {
        st->look_x = st->s->gaze_x * 0.55f;
        st->look_y = -st->s->gaze_y * 0.75f;
    }
    step(now, 1, true);
    refresh(now);
    wake_timer();
}

void ui_companion_get_stats(ui_companion_stats_t *out)
{
    if (st) {
        *out = st->stats;
    } else {
        memset(out, 0, sizeof(*out));
    }
}

void ui_companion_describe(char *buf, size_t len)
{
    if (!st || !st->face) {
        snprintf(buf, len, "closed");
        return;
    }
    snprintf(buf, len, "mood=%s react=%s key=%s", mood_names[st->mood], react_names[st->react],
             st->key ? "shown" : "hidden");
}

const char *ui_companion_mood_name(ui_companion_mood_t mood)
{
    return mood < UI_COMPANION_MOOD_COUNT ? mood_names[mood] : "?";
}

const char *ui_companion_reaction_name(ui_companion_reaction_t reaction)
{
    return reaction < UI_COMPANION_REACT_COUNT ? react_names[reaction] : "?";
}
