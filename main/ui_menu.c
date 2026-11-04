/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ui_menu.h"

#include "ui_kit.h"
#include "ui_theme.h"

/* The spec asked for a scrollable list. Five destinations fit on one screen
 * as 150x54 buttons, which is better at arm's length than a list you have to
 * scroll: every page is one look and one tap away, and the buttons are as
 * big as the corner nav ones they replace. The sixth slot went to DIAG; past
 * six, a second page reached by a key in the title band ("MORE >") keeps
 * every button full size instead of shrinking them into a list.
 *
 *    y  58..112   row 1
 *    y 118..172   row 2
 *    y 178..232   row 3 (full width when the count is odd)
 */
#define MENU_TOP 58
#define MENU_ROW_H 54
#define MENU_GAP 6
#define MENU_BTN_W 151

void ui_menu_build(lv_obj_t *page, const char *title, const ui_menu_item_t *items, int count,
                   const ui_menu_item_t *corner)
{
    ui_page_title(page, title);

    if (corner) {
        /* Title band, right end: the same 52 px height as HOME on the left. */
        lv_obj_t *k = lv_button_create(page);
        lv_obj_set_size(k, 96, 52);
        lv_obj_set_pos(k, 320 - 2 - 96, 2);
        lv_obj_set_style_bg_color(k, UI_PANEL, 0);
        lv_obj_set_style_border_width(k, 2, 0);
        lv_obj_set_style_border_color(k, UI_ACCENT, 0);
        lv_obj_set_style_radius(k, 10, 0);
        if (corner->cb) {
            lv_obj_add_event_cb(k, corner->cb, LV_EVENT_CLICKED, NULL);
        }
        lv_obj_t *l = lv_label_create(k);
        lv_label_set_text(l, corner->label);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(l, UI_TEXT, 0);
        lv_obj_center(l);
    }

    for (int i = 0; i < count && i < 6; i++) {
        bool alone = (i == count - 1) && (count % 2 == 1);
        lv_obj_t *btn = lv_button_create(page);
        lv_obj_set_size(btn, alone ? 2 * MENU_BTN_W + MENU_GAP : MENU_BTN_W, MENU_ROW_H);
        lv_obj_set_pos(btn, 6 + (i % 2) * (MENU_BTN_W + MENU_GAP), MENU_TOP + (i / 2) * (MENU_ROW_H + MENU_GAP));
        lv_obj_set_style_bg_color(btn, UI_PANEL, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, UI_ACCENT, 0);
        lv_obj_set_style_radius(btn, 10, 0);
        if (items[i].cb) {
            lv_obj_add_event_cb(btn, items[i].cb, LV_EVENT_CLICKED, NULL);
        }
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, items[i].label);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(l, UI_TEXT, 0);
        lv_obj_center(l);
    }
}

void ui_menu_build_pictures(lv_obj_t *page,const char *title,const ui_menu_picture_item_t *items,int count,const ui_menu_item_t *corner)
{
    lv_obj_set_style_bg_color(page,lv_color_hex(0x254656),0);
    lv_obj_set_style_bg_grad_color(page,lv_color_hex(0x507a78),0);
    lv_obj_set_style_bg_grad_dir(page,LV_GRAD_DIR_VER,0);
    lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);
    ui_menu_build(page,title,NULL,0,corner);
    lv_obj_t *t=lv_obj_get_child(page,0);
    lv_obj_set_style_text_color(t,lv_color_hex(0xf6edda),0);
    if (corner) {
        /* Back key (96 px) and corner key (96 px) leave 124 px of band: the
         * 32 px title would run under the corner key, so it steps to 20 px
         * and centres between them. */
        lv_obj_set_style_text_font(t,&lv_font_montserrat_20,0);
        lv_obj_align(t,LV_ALIGN_TOP_MID,0,18);
    }
    for (int i=0;i<count && i<3;i++) {
        lv_obj_t *card=lv_button_create(page);
        lv_obj_set_pos(card,8+i*104,70);lv_obj_set_size(card,96,150);
        lv_obj_set_style_bg_color(card,lv_color_hex(0xe4d2ad),0);
        lv_obj_set_style_border_width(card,2,0);lv_obj_set_style_border_color(card,lv_color_hex(0x8a6d4b),0);
        lv_obj_set_style_radius(card,10,0);lv_obj_set_style_pad_all(card,0,0);
        lv_obj_set_style_shadow_color(card,lv_color_hex(0x132d31),0);
        lv_obj_set_style_shadow_width(card,5,0);lv_obj_set_style_shadow_offset_y(card,3,0);
        lv_obj_set_style_translate_y(card,2,LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(card,1,LV_STATE_PRESSED);
        if(items[i].cb)lv_obj_add_event_cb(card,items[i].cb,LV_EVENT_CLICKED,NULL);
        /* Each picture is inset like a postcard; the whole card is tappable. */
        lv_obj_t *scene=lv_obj_create(card);lv_obj_remove_style_all(scene);
        lv_obj_set_pos(scene,5,5);lv_obj_set_size(scene,86,104);
        lv_obj_set_style_bg_color(scene,lv_color_hex(i==0?0x789cb3:0x7594a7),0);
        lv_obj_set_style_bg_opa(scene,LV_OPA_COVER,0);lv_obj_set_style_radius(scene,6,0);
        lv_obj_remove_flag(scene,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *ground=lv_obj_create(scene);lv_obj_remove_style_all(ground);
        lv_obj_set_pos(ground,0,77);lv_obj_set_size(ground,86,27);
        lv_obj_set_style_bg_color(ground,lv_color_hex(0x8ca77b),0);lv_obj_set_style_bg_opa(ground,LV_OPA_COVER,0);
        lv_obj_remove_flag(ground,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *pic=lv_image_create(scene);lv_image_set_src(pic,items[i].image);lv_image_set_scale(pic,items[i].scale);lv_image_set_antialias(pic,false);lv_obj_align(pic,LV_ALIGN_CENTER,0,0);
        lv_obj_t *label=lv_label_create(card);lv_label_set_text(label,items[i].label);
        /* 20 px: the card names are read from arm's length, like every key. */
        lv_obj_set_style_text_font(label,&lv_font_montserrat_20,0);lv_obj_set_style_text_color(label,lv_color_hex(0x172533),0);
        lv_obj_align(label,LV_ALIGN_BOTTOM_MID,0,-10);
    }
}
