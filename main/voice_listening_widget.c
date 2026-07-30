/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief VoiceListeningWidget: rotating ring + glisten highlight + static
 *        inner ring + halo + navy disc + mic icon.
 * @details Five concentric native-primitive layers, back to front (creation
 *          order = z-order, all siblings under one container, all centered
 *          on the same point):
 *            1. outer ring (lv_arc, rotating) - thick blue circle
 *            2. inner ring (lv_arc, static)   - thin light-blue circle
 *            3. halo (lv_obj, static)         - subtle pale-blue flat disc
 *            4. disc (lv_obj, static)         - dark navy filled disc,
 *                                                slightly smaller than the
 *                                                halo so the halo's edge
 *                                                peeks out around it
 *            5. mic icon (lv_obj x3)          - white capsule+stand+base,
 *                                                parented to the disc
 *          The halo is a second flat circle behind the disc, not
 *          lv_obj_set_style_shadow_*, and its opacity is a fixed value, not
 *          animated - "depth" here comes from one static color layer
 *          peeking around another, not a blur or a gradient.
 *          One lv_arc does double duty as both the thick blue outer ring
 *          (its background arc, spanning the full 360deg) and the
 *          traveling highlight (its indicator arc, a short bright
 *          cyan-toward-white segment) - lv_arc_draw() adds the same
 *          `rotation` value to both the background and indicator angles
 *          (see lv_arc.c), so animating just that one int32 property turns
 *          the whole ring and moves the highlight with it in a single step.
 *          The highlight's color is a single flat color mixed toward white
 *          (lv_color_lighten), not a two-stop gradient - LVGL's arc has no
 *          per-pixel gradient-along-a-stroke primitive, and a real gradient
 *          was explicitly ruled out; this is the closest flat-color
 *          approximation of "cyan-to-white" that stays within that limit.
 *          That single-property animation is also what keeps this cheap:
 *          one lv_anim ticking one int32, no timers of our own, no polling,
 *          nothing scaling, fading, or otherwise animated on any of the
 *          other four (static) layers - just a redraw of a handful of small
 *          shapes per animation frame.
 *          The mic icon is three plain white lv_obj rectangles (capsule +
 *          stand + base) rather than a font glyph or bitmap - LVGL's
 *          bundled symbol font (lv_symbol_def.h) has no microphone glyph,
 *          and native primitives avoid pulling in a new font/image asset
 *          for a shape this simple.
 */

#include "voice_listening_widget.h"

/* Mission 19: grown from 130 to 148 (all inner layers scaled with it, same
 * ~1.14x factor and the same proportional gaps between layers) so the
 * bottom edge reaches close to the nav bar - see the alignment call in
 * status_deck_ui() for the exact top/bottom math. */
#define RING_DIAM 148
#define RING_WIDTH 14
#define GLINT_SPAN_DEG 40 /* how much of the outer ring the highlight covers */

#define INNER_RING_DIAM 114
#define INNER_RING_WIDTH 2

#define HALO_DIAM 102
#define DISC_DIAM 86

/* ~7s per rotation - inside the requested 6-8s range. Linear path (constant
 * angular speed, no ease-in/out) is what keeps this reading as "calm" and
 * "futuristic" rather than flashy - an eased path would visibly speed up
 * and slow down once per lap. */
#define ROTATION_PERIOD_MS 7000

/* Capsule + stand + base, all parented directly to the disc and centered on
 * it - so they track the disc's own box with no separate alignment math.
 * Scaled with the rest of Mission 19's resize; the disc (86px) still has
 * enough radius to hold them (the icon's own extent is ~39px from center,
 * disc radius is 43px). */
static void build_mic_icon(lv_obj_t *disc)
{
    lv_obj_t *head = lv_obj_create(disc);
    lv_obj_set_size(head, 32, 46);
    lv_obj_set_style_radius(head, 16, 0);
    lv_obj_set_style_bg_color(head, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(head, LV_ALIGN_CENTER, 0, -16);

    lv_obj_t *stand = lv_obj_create(disc);
    lv_obj_set_size(stand, 6, 19);
    lv_obj_set_style_radius(stand, 3, 0);
    lv_obj_set_style_bg_color(stand, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(stand, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(stand, 0, 0);
    lv_obj_clear_flag(stand, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(stand, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(stand, LV_ALIGN_CENTER, 0, 16);

    lv_obj_t *base = lv_obj_create(disc);
    lv_obj_set_size(base, 34, 6);
    lv_obj_set_style_radius(base, 3, 0);
    lv_obj_set_style_bg_color(base, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(base, 0, 0);
    lv_obj_clear_flag(base, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(base, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(base, LV_ALIGN_CENTER, 0, 27);
}

/* An lv_arc, not a plain circle + overlay: its background/indicator split
 * is exactly "thick static ring" + "small arc that can be rotated as one
 * number" with no manual angle math of our own, and lv_arc_set_rotation is
 * a native LVGL primitive - see the file comment. */
static lv_obj_t *build_outer_ring(lv_obj_t *parent)
{
    lv_obj_t *ring = lv_arc_create(parent);
    lv_obj_set_size(ring, RING_DIAM, RING_DIAM);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);

    /* Background arc = the ring itself: a full circle, thick and blue. */
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_obj_set_style_arc_width(ring, RING_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(ring, false, LV_PART_MAIN);

    /* Indicator arc = the highlight: a short, bright cyan-toward-white
     * segment at the same radius as the background (zero indicator
     * padding - see get_indicator_max_pad() in lv_arc.c). Rounded caps
     * give it a soft "glint" look instead of a hard-edged block. */
    lv_arc_set_angles(ring, 0, GLINT_SPAN_DEG);
    lv_obj_set_style_pad_all(ring, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring, RING_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, lv_color_lighten(lv_palette_main(LV_PALETTE_CYAN), 160), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);

    /* lv_arc always draws a knob rect (lv_arc_draw() in lv_arc.c has no
     * "disabled" branch for it) - opa 0 is what actually hides it, not
     * just clearing CLICKABLE above. */
    lv_obj_set_style_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);

    return ring;
}

/* Thin, stationary, light-blue - no rotation applied to this one, and no
 * indicator/knob drawn on it at all (opa 0 for both parts) since it never
 * needs to show a highlight or be interactive. */
static lv_obj_t *build_inner_ring(lv_obj_t *parent)
{
    lv_obj_t *ring = lv_arc_create(parent);
    lv_obj_set_size(ring, INNER_RING_DIAM, INNER_RING_DIAM);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);

    lv_arc_set_bg_angles(ring, 0, 360);
    lv_obj_set_style_arc_width(ring, INNER_RING_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(ring, false, LV_PART_MAIN);

    lv_obj_set_style_opa(ring, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);

    return ring;
}

/* Halo (subtle, pale blue, fixed low opacity - not animated) sits directly
 * behind the disc and is a few px wider, so only its rim shows; disc (dark
 * navy, full opacity) on top of it, with the mic icon on top of that. Both
 * are lv_obj circles (radius = half of size), not lv_arc - full filled
 * discs, not strokes. */
static void build_disc(lv_obj_t *parent)
{
    lv_obj_t *halo = lv_obj_create(parent);
    lv_obj_set_size(halo, HALO_DIAM, HALO_DIAM);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(halo, lv_palette_lighten(LV_PALETTE_BLUE, 4), 0);
    lv_obj_set_style_bg_opa(halo, LV_OPA_40, 0);
    lv_obj_set_style_border_width(halo, 0, 0);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(halo, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *disc = lv_obj_create(parent);
    lv_obj_set_size(disc, DISC_DIAM, DISC_DIAM);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_palette_darken(LV_PALETTE_BLUE, 4), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, 0, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(disc, LV_ALIGN_CENTER, 0, 0);

    build_mic_icon(disc);
}

/* lv_anim_exec_xcb_t is `void(*)(void *, int32_t)` - lv_arc_set_rotation's
 * signature already matches it exactly, so no wrapper function is needed
 * (the same cast LVGL's own arc examples use). */
static void start_rotation(lv_obj_t *ring)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_rotation);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_duration(&a, ROTATION_PERIOD_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

lv_obj_t *voice_listening_widget_create(lv_obj_t *parent)
{
    /* Container is just the outer ring's own bounding box - callers
     * position/size this exactly like they would the ring itself. */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, RING_DIAM, RING_DIAM);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE);

    /* Back to front: outer rotating ring, static inner ring, halo+disc+icon
     * on top - see the file comment for the full layer list. */
    lv_obj_t *outer_ring = build_outer_ring(cont);
    build_inner_ring(cont);
    build_disc(cont);
    start_rotation(outer_ring);

    return cont;
}
