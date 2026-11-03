#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_home.h"
#include "ui_kit.h"
#include "ui_mouse.h"
static int menu_hits, mic_hits, house_hits, saved_x, phase;
static void house_cb(lv_event_t *e) { (void)e; house_hits++; ui_mouse_home_refresh_notice("UPDATING"); }
static lv_obj_t *page, *mouse;
static ui_home_t home;
static void menu_cb(lv_event_t *e) { (void)e; menu_hits++; }
static void mic_cb(lv_event_t *e) { (void)e; mic_hits++; }
static int timers(void) { int n=0; for(lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t))n++; return n; }
static void exercise(lv_timer_t *t)
{
    if (phase==0) {
        assert(lv_obj_get_x(mouse)>210);
        saved_x=lv_obj_get_x(mouse);
        lv_obj_add_flag(page,LV_OBJ_FLAG_HIDDEN);
        ui_mouse_home_set_active(false);
        phase++;
    } else if (phase==1) {
        assert(lv_obj_get_x(mouse)==saved_x);
        lv_obj_remove_flag(page,LV_OBJ_FLAG_HIDDEN);
        ui_mouse_home_set_active(true);
        phase++;
    } else if (phase==2) {
        assert(lv_obj_get_x(mouse)!=saved_x);
        ui_mouse_home_refresh_result(false);
        assert(strcmp(lv_label_get_text(lv_obj_get_child(page,-1)), "NO UPDATE")==0);
        ui_mouse_home_refresh_result(true);
        assert(strcmp(lv_label_get_text(lv_obj_get_child(page,-1)), "UPDATED")==0);
        phase++;
    } else {
        assert(lv_obj_get_x(mouse)<210);
        printf("HOME MOUSE PASS: motion, hidden pause, resume, touch hit-testing, weather update, timer cleanup\n");
        lv_timer_delete(t);
    }
}
void ws_render(lv_obj_t *scr)
{
    /* Repeated destruction must not retain the mouse timer. */
    ui_mouse_home_set_refresh_cb(house_cb);
    int baseline=timers();
    for(int i=0;i<3;i++) {
        page=ui_page_create(scr);
        ui_home_build(&home,page,mic_cb,menu_cb);
        lv_obj_delete(page);
        assert(timers()==baseline);
    }
    page=ui_page_create(scr);
    ui_home_build(&home,page,mic_cb,menu_cb);
    mouse=lv_obj_get_child(page,-2);
    assert(!lv_obj_has_flag(mouse,LV_OBJ_FLAG_CLICKABLE));
    lv_obj_update_layout(scr);
    lv_point_t pt={220,150};
    assert(lv_indev_search_obj(scr,&pt)!=mouse);
    pt=(lv_point_t){295,150};
    lv_obj_t *house=lv_indev_search_obj(scr,&pt); assert(house==lv_obj_get_child(page,-3));
    lv_obj_send_event(house,LV_EVENT_CLICKED,NULL); assert(house_hits==1); assert(strcmp(lv_label_get_text(lv_obj_get_child(page,-1)), "UPDATING")==0);
    /* The foreground mouse must not steal a house tap even when it overlaps. */
    lv_obj_set_x(mouse,262); lv_obj_update_layout(scr);
    pt=(lv_point_t){285,145};
    assert(lv_indev_search_obj(scr,&pt)==house);
    lv_obj_send_event(house,LV_EVENT_CLICKED,NULL); assert(house_hits==2);
    lv_obj_set_x(mouse,210); lv_obj_update_layout(scr);
    pt=(lv_point_t){260,200};
    lv_obj_t *target=lv_indev_search_obj(scr,&pt); assert(target);
    lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); assert(menu_hits==1);
    pt=(lv_point_t){260,55};
    target=lv_indev_search_obj(scr,&pt); assert(target);
    lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); assert(mic_hits==1);
    const ui_home_weather_t w={.valid=true,.location="Orlando",.now_temp=81,.now_text="Live weather",.slot={{.label="4 PM"},{.label="8 PM"}}};
    ui_home_apply_weather(&home,&w);
    assert(strstr(lv_label_get_text(home.now_temp),"81"));
    ui_home_set_link(&home,VOICE_WIDGET_LINK_OK);
    lv_timer_create(exercise,500,NULL);
}
