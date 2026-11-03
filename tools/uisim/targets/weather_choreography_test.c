#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_home.h"
#include "ui_kit.h"
#include "ui_mouse.h"

static int menu_hits, mic_hits, house_hits;
static void menu_cb(lv_event_t *e) { (void)e; menu_hits++; }
static void mic_cb(lv_event_t *e) { (void)e; mic_hits++; }
static void house_cb(lv_event_t *e) { (void)e; house_hits++; }
static void advance(unsigned n) { while(n--) { lv_tick_inc(167); lv_timer_handler(); } }
void ws_render(lv_obj_t *scr)
{
    static ui_home_t home;
    lv_obj_t *page=ui_page_create(scr);
    ui_mouse_home_set_refresh_cb(house_cb);
    ui_home_build(&home,page,mic_cb,menu_cb);
    lv_obj_update_layout(scr);
    lv_point_t p={295,150}; lv_obj_t *target=lv_indev_search_obj(scr,&p);
    assert(target); lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); assert(house_hits==1);
    p=(lv_point_t){260,200}; target=lv_indev_search_obj(scr,&p);
    assert(target); lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); assert(menu_hits==1);
    p=(lv_point_t){260,55}; target=lv_indev_search_obj(scr,&p);
    assert(target); lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); assert(mic_hits==1);
    ui_home_weather_t initial={.valid=true,.location="Before",.now_temp=11,.now_icon="sun",.now_text="Clear",.now_rain=1,
        .slot={{.label="1 PM",.temp=12,.icon="sun",.rain=2},{.label="2 PM",.temp=13,.icon="sun",.rain=3}},
        .tmrw_hi=14,.tmrw_lo=4,.tmrw_icon="sun",.tmrw_rain=4};
    ui_home_apply_weather(&home,&initial);
    weather_status_t a={.valid=true,.seq=1,.location="After",.now_temp=71,.now_icon="rain",.now_text="Rain",.now_rain=81,
        .slot={{.label="3 PM",.temp=72,.icon="cloud",.rain=82},{.label="7 PM",.temp=73,.icon="storm",.rain=83}},
        .tmrw_hi=74,.tmrw_lo=64,.tmrw_icon="moon",.tmrw_rain=84};
    weather_status_t b=a; b.seq=2; b.now_temp=91; b.tmrw_rain=99;
    assert(ui_home_weather_stage_offer(&a));
    ui_mouse_home_refresh_result(true);
    advance(20);
    p=(lv_point_t){260,200}; target=lv_indev_search_obj(scr,&p);
    assert(target); lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); assert(menu_hits==2);
    assert(strstr(lv_label_get_text(home.now_temp),"11"));
    assert(!ui_home_weather_stage_offer(&b));
    unsigned transitions=0, previous=0;
    for(unsigned i=0;i<4000 && ui_home_weather_stage_running();i++) {
        advance(1);
        unsigned card; weather_stage_field_t field;
        if (ui_home_weather_stage_current(&card,&field)) {
            const weather_status_t *active=ui_home_weather_stage_snapshot();
            unsigned index=(active->seq==2?12:0)+card*3+(unsigned)field;
            assert(index>=previous && index<=previous+1);
            if(index!=previous) { transitions++; previous=index; }
        }
    }
    assert(!ui_home_weather_stage_running());
    assert(transitions==23);
    assert(strstr(lv_label_get_text(home.now_temp),"91"));
    assert(strstr(lv_label_get_text(home.row[2].rain),"99"));
    assert(ui_home_weather_stage_offer(&a));
    ui_mouse_home_refresh_result(true);
    assert(!ui_home_weather_stage_offer(&b));
    advance(3);
    ui_mouse_home_set_active(false);
    assert(!ui_home_weather_stage_running());
    assert(strstr(lv_label_get_text(home.now_temp),"91"));
    assert(strstr(lv_label_get_text(home.row[2].rain),"99"));
    ui_mouse_home_set_active(true);
    lv_obj_delete(page);
    puts("CHOREOGRAPHY PASS: two snapshots, 24 ordered reveals, cancellation applies latest");
}
