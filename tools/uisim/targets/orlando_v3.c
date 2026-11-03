#include "ws_sim.h"
#include "ui_home.h"
#include "ui_adventure.h"
#include "ui_menu.h"
#include "ui_kit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern int headless_display_write_rgb(const char *path);
static const char *capture_dir;
static uint32_t capture_start;
static void capture(lv_timer_t *timer){
 (void)timer;char path[512];unsigned ms=lv_tick_get()-capture_start;
 lv_obj_update_layout(lv_screen_active());lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);
 snprintf(path,sizeof(path),"%s/frame-%05u.rgb",capture_dir,ms);
 assert(headless_display_write_rgb(path)==0);
}
static lv_obj_t *screen,*page,*menu_page,*back;
static ui_home_t home;
static unsigned home_hits,tour_hits,intro_hits,menu_hits,phase;
static void tap(int x,int y) {
 lv_obj_update_layout(screen);lv_point_t p={x,y};lv_obj_t *o=lv_indev_search_obj(screen,&p);assert(o);
 lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);
}
static int timers(void){int n=0;for(lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t))n++;return n;}
static void returned(void *ctx){(void)ctx;lv_obj_remove_flag(page,LV_OBJ_FLAG_HIDDEN);ui_home_set_active(true);}
static void go_home(lv_event_t *e){(void)e;home_hits++;}
static void go_tour(lv_event_t *e){(void)e;if(ui_adventure_is_open())return;tour_hits++;ui_home_set_active(false);ui_adventure_open(screen,returned,NULL);}
static void intro(lv_event_t *e){(void)e;intro_hits++;ui_home_intro_audio_result(false);}
static void menu_back(lv_event_t *e){(void)e;lv_obj_delete(menu_page);menu_page=NULL;if(back)lv_obj_delete(back);back=NULL;returned(NULL);}
static void menu(lv_event_t *e){
 (void)e;menu_hits++;ui_home_set_active(false);lv_obj_add_flag(page,LV_OBJ_FLAG_HIDDEN);
 menu_page=ui_page_create(screen);
 const ui_menu_item_t items[]={{"SENSORS",ws_noop_event_cb},{"LOG",ws_noop_event_cb},{"PROJECTS",ws_noop_event_cb},{"FIRMWARE",ws_noop_event_cb},{"SETTINGS",ws_noop_event_cb},{"DIAG",ws_noop_event_cb}};
 const ui_menu_item_t corner={"MORE >",ws_noop_event_cb};ui_menu_build(menu_page,"MENU",items,6,&corner);
 back=ui_back_button_create(screen,menu_back);
}
static int initial_timers;
static void exercise(lv_timer_t *t){
 ui_home_state_t st=ui_home_state();
 if(phase==0){
  assert(st.wheel_angle>0 && st.car_x>8 && st.peek_x<120);
  /* Repeated landmark taps reuse one owned timer and restart bounded motion. */
  for(int i=0;i<4;i++){tap(270,70);tap(160,60);tap(55,85);}
  assert(ui_home_state().timers==1);assert(timers()==initial_timers);
  phase++;lv_timer_set_period(t,3600);
 }else if(phase==1){
  assert(!st.wheel_running&&!st.castle_running&&!st.launch_running);
  tap(280,217);assert(ui_home_state().menu_open);tap(40,20);assert(!ui_home_state().menu_open);
  tap(280,217);tap(240,120);assert(menu_hits==1&&!ui_home_state().active);
  lv_obj_send_event(back,LV_EVENT_CLICKED,NULL);assert(ui_home_state().active);
  tap(260,175);assert(tour_hits==1&&ui_adventure_is_open()&&!ui_home_state().active);
  tap(270,215);assert(!ui_adventure_is_open()&&ui_home_state().active);
  tap(50,155);assert(home_hits==1);
  /* Dismiss/reopen, page through, and block repeated underlying mouse events. */
  for(int i=0;i<3;i++){
   tap(148,190);assert(ui_home_state().intro_open);
   tap(148,140); /* modal background must not invoke the underlying mouse */
   if(ui_home_state().intro_open){tap(70,200);assert(ui_home_state().intro_page==1);tap(230,200);}
   assert(!ui_home_state().intro_open);
  }
  assert(intro_hits==3);
  ui_home_set_active(false);assert(!ui_home_state().active);
  phase++;lv_timer_set_period(t,500);
 }else{
  assert(!st.active&&!st.wheel_running&&!st.castle_running&&!st.launch_running);
  ui_home_set_active(true);assert(ui_home_state().active);
  lv_obj_delete(page);page=NULL;assert(ui_home_state().timers==0);
  page=ui_page_create(screen);ui_home_build(&home,page,ws_noop_event_cb,menu);ui_home_set_actions(go_home,go_tour,intro);
  puts("ORLANDO_V3_PASS: landmarks restart/finish, menu collapse/outside dismiss/navigation/return, tour exit return, intro repeated open/next/close, house reset, timer cleanup");
  lv_timer_delete(t);
 }
}
void ws_render(lv_obj_t *scr){
 screen=scr;int before=timers();
 capture_dir=ws_fx_str("capture_dir","");capture_start=lv_tick_get();
 if(capture_dir[0])lv_timer_create(capture,50,NULL);
 if(ws_fx_bool("test",false))for(int i=0;i<3;i++){
  page=ui_page_create(scr);ui_home_build(&home,page,ws_noop_event_cb,menu);lv_obj_delete(page);assert(timers()==before);
 }
 page=ui_page_create(scr);ui_home_build(&home,page,ws_noop_event_cb,menu);ui_home_set_actions(go_home,go_tour,intro);
 const char *a=ws_fx_str("action","");
 if(!strcmp(a,"wheel"))tap(270,70);
 else if(!strcmp(a,"castle"))tap(160,60);
 else if(!strcmp(a,"coaster"))tap(55,85);
 else if(!strcmp(a,"guide2")){tap(148,190);tap(70,200);}
 else if(!strcmp(a,"guide"))tap(148,190);
 else if(!strcmp(a,"menu"))tap(280,217);
 else if(!strcmp(a,"tour"))tap(260,175);
 else if(!strcmp(a,"all")){tap(270,70);tap(160,60);tap(55,85);}
 if(ws_fx_bool("test",false)){
  tap(270,70);tap(160,60);tap(55,85);
  lv_timer_create(exercise,500,NULL);initial_timers=timers();
 }
}
