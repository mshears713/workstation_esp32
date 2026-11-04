/* Artifacts & Instruments HOME: the real ui_home.c with app callbacks counted.
 * Fixtures: default (the reviewed still), castle at a chosen time (advance-ms),
 * welcome (radar card), van, test (lifecycle + timing assertions). */
#include "ws_sim.h"
#include "ui_home.h"
#include "ui_kit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern int headless_display_write_rgb(const char *path);
static const char *capture_dir; static uint32_t capture_start;
static void capture(lv_timer_t *t){(void)t;char path[512];unsigned ms=lv_tick_get()-capture_start;lv_obj_update_layout(lv_screen_active());lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);snprintf(path,sizeof(path),"%s/frame-%05u.rgb",capture_dir,ms);assert(headless_display_write_rgb(path)==0);}
static lv_obj_t *screen,*page; static ui_home_t home;
static unsigned menu_hits,intro_hits,tour_hits,phase,violations,checks;
static uint32_t t0;
static void menu(lv_event_t *e){(void)e;menu_hits++;}
static void intro(lv_event_t *e){(void)e;intro_hits++;}
static void tour(lv_event_t *e){(void)e;tour_hits++;}
static void tap(int x,int y){lv_obj_update_layout(screen);lv_point_t p={x,y};lv_obj_t *o=lv_indev_search_obj(screen,&p);assert(o);lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);}
static bool has_text(lv_obj_t *o,const char *t){if(lv_obj_has_class(o,&lv_label_class)&&strstr(lv_label_get_text(o),t))return true;for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)if(has_text(lv_obj_get_child(o,i),t))return true;return false;}

/* Every 50 ms for the whole test: the princess is never out unless the bridge is down. */
static void invariant(lv_timer_t *t){(void)t;ui_home_state_t s=ui_home_state();checks++;if(s.princess_visible&&!s.drawbridge_down)violations++;}

static void step(lv_timer_t *timer){
 ui_home_state_t s=ui_home_state(); uint32_t now=lv_tick_get()-t0;
 switch(phase){
 case 0: /* still */
  assert(!s.intro_open&&!s.princess_visible&&s.bridge_frame==0&&s.van_x==196&&s.mouse_x==134&&s.timers==1);
  tap(160,45); t0=lv_tick_get(); phase++; lv_timer_set_period(timer,300); break;
 case 1: /* 300 ms: lowering, nobody out yet */
  assert(s.castle_running&&s.bridge_frame==1&&!s.princess_visible); phase++; lv_timer_set_period(timer,550); break;
 case 2: /* 850 ms: down, princess stepping out on it */
  assert(s.drawbridge_down&&s.princess_visible&&s.princess_y>=84); phase++; lv_timer_set_period(timer,900); break;
 case 3: /* 1750 ms: out on the bridge, near the bank */
  assert(s.drawbridge_down&&s.princess_visible&&s.princess_y>=106); phase++; lv_timer_set_period(timer,1950); break;
 case 4: /* 3700 ms: back inside, bridge rising */
  assert(!s.princess_visible&&s.bridge_frame<3); phase++; lv_timer_set_period(timer,700); break;
 case 5: /* 4400 ms: sequence over, raised again */
  assert(!s.castle_running&&s.bridge_frame==0&&!s.princess_visible);
  tap(50,170); assert(menu_hits==1);                                  /* house = main menu */
  tap(155,190); assert(ui_home_state().intro_open&&intro_hits==1);   /* mouse = guide (+ spoken guide request) */
  tap(160,200); assert(!ui_home_state().intro_open);
  ui_home_show_welcome(); assert(ui_home_state().intro_open&&has_text(page,"Welcome to Artifacts & Instruments!"));
  ui_home_show_welcome(); /* second call while open: no-op, no stacked card */
  tap(160,200); assert(!ui_home_state().intro_open);
  tap(250,175); assert(ui_home_state().van_running&&tour_hits==0);    /* van */
  ui_home_show_welcome(); assert(!ui_home_state().intro_open);       /* never over a moving van */
  phase++; lv_timer_set_period(timer,1000); break;
 case 6: assert(s.van_running&&s.van_door_open&&s.mouse_x>134&&s.van_x==196); phase++; lv_timer_set_period(timer,1400); break;
 case 7: assert(s.van_running&&!s.van_door_open&&s.van_x>196); phase++; lv_timer_set_period(timer,1000); break;
 default:
  assert(tour_hits==1); /* van gone -> Tour A at Egypt (app callback) */
  ui_home_set_active(false); lv_obj_update_layout(page); assert(ui_home_state().van_x==196&&ui_home_state().mouse_x==134&&ui_home_state().bridge_frame==0);
  ui_home_set_active(true);
  assert(violations==0&&checks>150);
  printf("ARTIFACTS_HOME_PASS: drawbridge down before princess (%u checks, 0 violations), bridge back up, house menu, mouse guide+audio request, radar welcome card (not over van), van to Tour A, reset\n",checks);
  lv_obj_delete(page); assert(ui_home_state().timers==0); lv_timer_delete(timer); (void)now;
 }
}

void ws_render(lv_obj_t *scr){
 screen=scr; capture_dir=ws_fx_str("capture_dir",""); capture_start=lv_tick_get();
 if(capture_dir[0])lv_timer_create(capture,50,NULL);
 page=ui_page_create(scr); ui_home_build(&home,page,NULL,menu); ui_home_set_actions(ws_noop_event_cb,tour,intro);
 const char *a=ws_fx_str("action","");
 if(!strcmp(a,"castle"))tap(160,45);
 else if(!strcmp(a,"van"))tap(250,175);
 else if(!strcmp(a,"guide"))tap(155,190);
 else if(!strcmp(a,"welcome"))ui_home_show_welcome();
 if(ws_fx_bool("test",false)){lv_timer_create(invariant,50,NULL);t0=lv_tick_get();lv_timer_create(step,200,NULL);}
}
