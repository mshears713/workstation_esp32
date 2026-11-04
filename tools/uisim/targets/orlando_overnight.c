#include "ws_sim.h"
#include "ui_home.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern int headless_display_write_rgb(const char *path);
static const char *capture_dir;static uint32_t capture_start;
static void capture(lv_timer_t *t){(void)t;char path[512];unsigned ms=lv_tick_get()-capture_start;lv_obj_update_layout(lv_screen_active());lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);snprintf(path,sizeof(path),"%s/frame-%05u.rgb",capture_dir,ms);assert(headless_display_write_rgb(path)==0);}
static lv_obj_t *screen,*page;static ui_home_t home;static unsigned menu_hits,intro_hits,tour_hits,phase;static bool show_menu;
static void tap(int x,int y){lv_obj_update_layout(screen);lv_point_t p={x,y};lv_obj_t *o=lv_indev_search_obj(screen,&p);assert(o);lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);}
static void menu(lv_event_t *e){(void)e;menu_hits++;if(show_menu){lv_obj_add_flag(page,LV_OBJ_FLAG_HIDDEN);lv_obj_t *m=ui_page_create(screen);const ui_menu_item_t items[]={{"SENSORS",ws_noop_event_cb},{"LOG",ws_noop_event_cb},{"PROJECTS",ws_noop_event_cb},{"FIRMWARE",ws_noop_event_cb},{"SETTINGS",ws_noop_event_cb},{"DIAG",ws_noop_event_cb}};const ui_menu_item_t corner={"MORE >",ws_noop_event_cb};ui_menu_build(m,"MENU",items,6,&corner);}}
static void intro(lv_event_t *e){(void)e;intro_hits++;}
static void tour(lv_event_t *e){(void)e;tour_hits++;}
static bool has_label(lv_obj_t *obj,const char *text){if(lv_obj_has_class(obj,&lv_label_class)&&strcmp(lv_label_get_text(obj),text)==0)return true;for(uint32_t i=0;i<lv_obj_get_child_count(obj);i++)if(has_label(lv_obj_get_child(obj,i),text))return true;return false;}
static unsigned idle_phase;
static void idle_test(lv_timer_t *t){ui_home_state_t st=ui_home_state();assert(!st.intro_open&&intro_hits==0);if(idle_phase==0){assert(!st.castle_guest_visible);idle_phase++;lv_timer_set_period(t,1600);}else if(idle_phase==1){assert(st.castle_running&&st.castle_guest_visible&&!st.princess_visible);idle_phase++;lv_timer_set_period(t,2600);}else{assert(!st.castle_guest_visible&&!st.princess_visible);puts("IDLE_PEEK_PASS: no auto intro; right-window peek at 7s; no automatic princess; clears");lv_timer_delete(t);}}
static void exercise(lv_timer_t *timer){ui_home_state_t st=ui_home_state();
 if(phase==0){assert(!st.intro_open&&intro_hits==0&&!has_label(page,"Menu"));assert(st.wheel_angle>0&&st.car_x>12&&st.castle_running);for(int i=0;i<3;i++){tap(270,85);tap(160,45);tap(55,90);}assert(ui_home_state().timers==1);phase++;lv_timer_set_period(timer,3600);}
 else if(phase==1){assert(!st.wheel_running&&!st.launch_running&&!st.castle_running);tap(50,160);assert(menu_hits==1);tap(148,190);assert(ui_home_state().intro_open&&intro_hits==0&&has_label(page,"Hello traveler, welcome to\nMaker Faire 2026!"));tap(160,200);assert(!ui_home_state().intro_open);tap(260,175);assert(ui_home_state().van_running&&tour_hits==0);phase++;lv_timer_set_period(timer,1000);}
 else if(phase==2){assert(st.van_running&&st.van_door_open&&st.mouse_x>126&&st.van_x==211);phase++;lv_timer_set_period(timer,1400);}
 else if(phase==3){assert(st.van_running&&!st.van_door_open&&st.van_x>211);phase++;lv_timer_set_period(timer,1000);}
 else {assert(!st.van_running&&st.mouse_x>126&&st.van_x>320&&tour_hits==0);ui_home_set_active(false);lv_obj_update_layout(page);assert(!ui_home_state().active&&ui_home_state().mouse_x==126&&ui_home_state().van_x==211);ui_home_set_active(true);lv_obj_delete(page);assert(ui_home_state().timers==0);puts("ORLANDO_OVERNIGHT_PASS: tap-only text intro, house main menu, landmark repeat, walk-board-drive-depart and page reset, no fake tour, timer cleanup");lv_timer_delete(timer);}
}
void ws_render(lv_obj_t *scr){screen=scr;capture_dir=ws_fx_str("capture_dir","");capture_start=lv_tick_get();if(capture_dir[0])lv_timer_create(capture,50,NULL);
 page=ui_page_create(scr);ui_home_build(&home,page,ws_noop_event_cb,menu);ui_home_set_actions(ws_noop_event_cb,tour,intro);
 const char *a=ws_fx_str("action","");show_menu=!strcmp(a,"home");
 if(!strcmp(a,"wheel"))tap(270,85);else if(!strcmp(a,"castle"))tap(160,45);else if(!strcmp(a,"coaster"))tap(55,90);else if(!strcmp(a,"guide"))tap(148,190);else if(!strcmp(a,"van"))tap(260,175);else if(!strcmp(a,"home"))tap(50,160);else if(!strcmp(a,"all")){tap(270,85);tap(160,45);tap(55,90);}
 if(ws_fx_bool("test",false)){tap(270,85);tap(160,45);tap(55,90);lv_timer_create(exercise,500,NULL);}
 if(ws_fx_bool("idle_test",false))lv_timer_create(idle_test,6000,NULL);
}
