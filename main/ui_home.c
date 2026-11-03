/* Orlando HOME. Art is native Pixelorama; this file only composes the UI. */
#include "ui_home.h"
#include "orlando_home_pixels.h"
#include <string.h>

void ui_home_build(ui_home_t *h, lv_obj_t *page, lv_event_cb_t talk_cb, lv_event_cb_t menu_cb)
{
    memset(h, 0, sizeof(*h));
    lv_obj_set_style_bg_color(page, lv_color_hex(0x182F40), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_t *scene = lv_image_create(page);
    lv_image_set_src(scene, &orlando_home_image);
    lv_obj_set_pos(scene, 0, 0);
    lv_obj_remove_flag(scene, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Artifacts & Instruments");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFF0CE), 0);
    lv_obj_set_pos(title, 8, 163);

    lv_obj_t *welcome = lv_label_create(page);
    lv_label_set_text(welcome, "Welcome, traveler.\nEgypt, Greece &\nmedieval Europe await.");
    lv_obj_set_style_text_font(welcome, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(welcome, lv_color_hex(0xE7E7DB), 0);
    lv_obj_set_pos(welcome, 8, 189);
    lv_obj_set_width(welcome, 234);

    /* Keep the caller's real Menu navigation callback. 64x48 finger target. */
    lv_obj_t *menu = lv_button_create(page);
    lv_obj_remove_style_all(menu);
    lv_obj_set_pos(menu, 248, 186);
    lv_obj_set_size(menu, 64, 48);
    lv_obj_set_style_bg_color(menu, lv_color_hex(0xE6CAA0), 0);
    lv_obj_set_style_bg_opa(menu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(menu, 8, 0);
    if (menu_cb) lv_obj_add_event_cb(menu, menu_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(menu);
    lv_label_set_text(label, "Menu");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x182F40), 0);
    lv_obj_center(label);

    /* The mouse retains the existing TALK entry point without a new task. */
    if (talk_cb) {
        lv_obj_t *talk = lv_obj_create(page);
        lv_obj_remove_style_all(talk);
        lv_obj_set_pos(talk, 132, 78);
        lv_obj_set_size(talk, 56, 76);
        lv_obj_remove_flag(talk, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(talk, talk_cb, LV_EVENT_CLICKED, NULL);
    }
}

/* Compatibility seams for existing subsystem callers. HOME has no weather
 * objects or caretaker choreography; background networking is unchanged. */
void ui_home_apply_weather(ui_home_t *h, const ui_home_weather_t *w) {(void)h;(void)w;}
void ui_home_reveal_weather_field(ui_home_t *h,const weather_status_t *w,unsigned c,weather_stage_field_t f) {(void)h;(void)w;(void)c;(void)f;}
bool ui_home_weather_stage_offer(const weather_status_t *w) {(void)w;return false;}
bool ui_home_weather_stage_current(unsigned *c,weather_stage_field_t *f) {(void)c;(void)f;return false;}
const weather_status_t *ui_home_weather_stage_snapshot(void) {return NULL;}
bool ui_home_weather_stage_reveal(void) {return false;}
bool ui_home_weather_stage_running(void) {return false;}
void ui_home_weather_stage_cancel(void) {}
lv_obj_t *ui_home_weather_field_object(unsigned c,weather_stage_field_t f) {(void)c;(void)f;return NULL;}
void ui_home_caretaker_end(void) {}
void ui_home_caretaker_frame(int s,unsigned f) {(void)s;(void)f;}
void ui_home_set_link(ui_home_t *h,voice_widget_link_t link) {(void)h;(void)link;}
