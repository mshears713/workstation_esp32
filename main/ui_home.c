/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ui_home.h"

#include <stdio.h>
#include "ui_theme.h"
#include "weather_icons.h"

/* 1.3.0 layout, 320x240, from the approved mockup (home130-v2):
 *
 *    x 6..200   the weather column: NOW panel (y 6..106), +2 h row,
 *               +6 h row, tomorrow row - four blocks, nothing else
 *    x 206..316 the microphone (104 px, top), its link caption, MENU
 *
 * Mission 16-19 made HOME one big microphone. It is still here and still the
 * TALK button, just a quarter of the screen: the stopped ring + red mic that
 * says "backend down" must still catch the eye across the room, which is why
 * it keeps its colour and motion and gains a caption rather than shrinking
 * further. No clock - Mike has one above the device. */
#define COL_X 6
#define COL_W 194
#define MIC_DIAM 104
#define RIGHT_X 210          /* left edge of the right column */
#define RIGHT_W 106

#define DEG "\xC2\xB0"

static lv_obj_t *block(lv_obj_t *parent, int y, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, COL_X, y);
    lv_obj_set_size(o, COL_W, h);
    lv_obj_set_style_bg_color(o, UI_PANEL, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 10, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *icon(lv_obj_t *parent)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);   /* until there is weather */
    return img;
}

static void set_icon(lv_obj_t *img, const char *key, bool large)
{
    const lv_image_dsc_t *src = key ? weather_icon(key, large) : NULL;
    if (src) {
        lv_image_set_src(img, src);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_home_build(ui_home_t *h, lv_obj_t *page, lv_event_cb_t talk_cb, lv_event_cb_t menu_cb)
{
    /* ---- NOW: icon, big temperature, condition; location and rain below */
    lv_obj_t *now = block(page, 6, 100);
    h->now_icon = icon(now);
    lv_obj_set_pos(h->now_icon, 6, 6);
    h->now_temp = text(now, &lv_font_montserrat_48, UI_TEXT);
    lv_obj_set_pos(h->now_temp, 62, 0);
    h->now_text = text(now, &lv_font_montserrat_14, UI_TEXT_DIM);
    lv_obj_set_pos(h->now_text, 64, 52);
    h->now_where = text(now, &lv_font_montserrat_14, UI_TEXT_DIM);
    lv_obj_align(h->now_where, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    h->now_rain = text(now, &lv_font_montserrat_14, UI_RAIN);
    lv_obj_align(h->now_rain, LV_ALIGN_BOTTOM_RIGHT, -8, -6);

    /* ---- +2 h, +6 h, tomorrow: label, icon, temperature, rain % --------- */
    static const int row_y[UI_HOME_SLOTS + 1] = { 112, 156, 200 };
    for (int i = 0; i < UI_HOME_SLOTS + 1; i++) {
        lv_obj_t *row = block(page, row_y[i], i < UI_HOME_SLOTS ? 40 : 36);
        h->row[i].label = text(row, &lv_font_montserrat_14, UI_TEXT_DIM);
        lv_obj_align(h->row[i].label, LV_ALIGN_LEFT_MID, 8, 0);
        h->row[i].icon = icon(row);
        lv_obj_align(h->row[i].icon, LV_ALIGN_LEFT_MID, 52, 0);
        h->row[i].temp = text(row, &lv_font_montserrat_20, UI_TEXT);
        lv_obj_align(h->row[i].temp, LV_ALIGN_LEFT_MID, 88, 0);
        h->row[i].rain = text(row, &lv_font_montserrat_14, UI_RAIN);
        lv_obj_align(h->row[i].rain, LV_ALIGN_RIGHT_MID, -8, 0);
    }
    /* Tomorrow's low, smaller and silver beside the high, so "88° 73°"
     * leaves room for the rain chance at the right edge. */
    h->tmrw_lo = text(lv_obj_get_parent(h->row[UI_HOME_SLOTS].temp), &lv_font_montserrat_14, UI_TEXT_DIM);

    /* ---- Microphone: link status, and the TALK button ------------------- */
    h->mic = voice_listening_widget_create_sized(page, MIC_DIAM);
    lv_obj_set_pos(h->mic, RIGHT_X + (RIGHT_W - MIC_DIAM) / 2, 6);
    lv_obj_add_flag(h->mic, LV_OBJ_FLAG_CLICKABLE);
    if (talk_cb) {
        lv_obj_add_event_cb(h->mic, talk_cb, LV_EVENT_CLICKED, NULL);
    }
    h->link_label = text(page, &lv_font_montserrat_14, UI_TEXT_DIM);
    lv_obj_set_width(h->link_label, RIGHT_W);
    lv_obj_set_style_text_align(h->link_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(h->link_label, RIGHT_X, 116);

    /* ---- MENU: every other page, one tap away --------------------------- */
    lv_obj_t *menu = lv_button_create(page);
    lv_obj_set_pos(menu, RIGHT_X, 174);
    lv_obj_set_size(menu, RIGHT_W, 60);
    lv_obj_set_style_bg_color(menu, UI_ACCENT, 0);
    lv_obj_set_style_radius(menu, 10, 0);
    if (menu_cb) {
        lv_obj_add_event_cb(menu, menu_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *ml = lv_label_create(menu);
    lv_label_set_text(ml, "MENU");
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ml, UI_TEXT, 0);
    lv_obj_center(ml);

    const ui_home_weather_t loading = { .valid = false, .status = "Loading weather" };
    ui_home_apply_weather(h, &loading);
}

void ui_home_apply_weather(ui_home_t *h, const ui_home_weather_t *w)
{
    if (h->now_temp == NULL) {
        return;   /* not built */
    }
    char buf[24];

    if (!w->valid) {
        lv_label_set_text(h->now_temp, "--" DEG);
        lv_label_set_text(h->now_text, w->status ? w->status : "");
        lv_label_set_text(h->now_where, w->location ? w->location : "");
        lv_label_set_text(h->now_rain, "");
        set_icon(h->now_icon, NULL, true);
        for (int i = 0; i < UI_HOME_SLOTS + 1; i++) {
            lv_label_set_text(h->row[i].label, i < UI_HOME_SLOTS ? "" : "Tmrw");
            lv_label_set_text(h->row[i].temp, "--");
            lv_label_set_text(h->row[i].rain, "");
            set_icon(h->row[i].icon, NULL, false);
        }
        lv_label_set_text(h->tmrw_lo, "");
        return;
    }

    snprintf(buf, sizeof(buf), "%d" DEG, w->now_temp);
    lv_label_set_text(h->now_temp, buf);
    lv_label_set_text(h->now_text, w->now_text ? w->now_text : "");
    set_icon(h->now_icon, w->now_icon, true);
    snprintf(buf, sizeof(buf), "Rain %d%%", w->now_rain);
    lv_label_set_text(h->now_rain, buf);

    /* Old data says so, in the place the location normally sits: the numbers
     * still beat a blank column, but not without a timestamp. */
    if (w->stale_as_of) {
        snprintf(buf, sizeof(buf), "as of %s", w->stale_as_of);
        lv_label_set_text(h->now_where, buf);
        lv_obj_set_style_text_color(h->now_where, UI_WARN, 0);
    } else {
        lv_label_set_text(h->now_where, w->location ? w->location : "");
        lv_obj_set_style_text_color(h->now_where, UI_TEXT_DIM, 0);
    }

    for (int i = 0; i < UI_HOME_SLOTS; i++) {
        lv_label_set_text(h->row[i].label, w->slot[i].label ? w->slot[i].label : "");
        set_icon(h->row[i].icon, w->slot[i].icon, false);
        snprintf(buf, sizeof(buf), "%d" DEG, w->slot[i].temp);
        lv_label_set_text(h->row[i].temp, buf);
        snprintf(buf, sizeof(buf), "%d%%", w->slot[i].rain);
        lv_label_set_text(h->row[i].rain, buf);
    }

    lv_label_set_text(h->row[UI_HOME_SLOTS].label, "Tmrw");
    set_icon(h->row[UI_HOME_SLOTS].icon, w->tmrw_icon, false);
    snprintf(buf, sizeof(buf), "%d" DEG, w->tmrw_hi);
    lv_label_set_text(h->row[UI_HOME_SLOTS].temp, buf);
    snprintf(buf, sizeof(buf), "%d" DEG, w->tmrw_lo);
    lv_label_set_text(h->tmrw_lo, buf);
    lv_obj_align_to(h->tmrw_lo, h->row[UI_HOME_SLOTS].temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -2);
    snprintf(buf, sizeof(buf), "%d%%", w->tmrw_rain);
    lv_label_set_text(h->row[UI_HOME_SLOTS].rain, buf);

    /* "101° -12° 100%" does not fit in 194 px. Not an Orlando problem, but the
     * location is config: if the low would touch the rain chance, drop the
     * low rather than draw one number over another. Runs every ten minutes,
     * so the forced layout pass costs nothing. */
    lv_obj_remove_flag(h->tmrw_lo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(h->tmrw_lo);
    lv_area_t lo, rain;
    lv_obj_get_coords(h->tmrw_lo, &lo);
    lv_obj_get_coords(h->row[UI_HOME_SLOTS].rain, &rain);
    if (lo.x2 + 4 > rain.x1) {
        lv_obj_add_flag(h->tmrw_lo, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_home_set_link(ui_home_t *h, voice_widget_link_t link)
{
    if (h->mic == NULL) {
        return;
    }
    voice_listening_widget_set_link(h->mic, link);

    const char *caption;
    lv_color_t color = UI_TEXT_DIM;
    switch (link) {
    case VOICE_WIDGET_LINK_OK:
        caption = "LINK OK";
        break;
    case VOICE_WIDGET_LINK_DOWN:
        caption = "NO BACKEND";
        color = UI_ERR;
        break;
    case VOICE_WIDGET_LINK_NO_NET:
        caption = "NO WI-FI";
        color = UI_WARN;
        break;
    case VOICE_WIDGET_LINK_UNKNOWN:
    default:
        caption = "CONNECTING";
        break;
    }
    /* Called every 200 ms; only touch the label when something changed, so
     * an unchanged caption costs no redraw. */
    if (lv_strcmp(lv_label_get_text(h->link_label), caption) != 0) {
        lv_label_set_text(h->link_label, caption);
        lv_obj_set_style_text_color(h->link_label, color, 0);
    }
}
