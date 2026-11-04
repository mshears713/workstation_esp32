#include "ws_sim.h"
#include "ui_home.h"
#include "ui_kit.h"
#include "ui_adventure.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_obj_t *screen,*page;static ui_home_t home;static unsigned tour_hits;
static void returned(void *ctx){(void)ctx;ui_home_set_active(true);}
static void tour(lv_event_t *e){(void)e;tour_hits++;ui_home_set_active(false);ui_adventure_open(screen,returned,NULL);ui_adventure_seek(ADVENTURE_EGYPT);}
static void tap(int x,int y){lv_obj_update_layout(screen);lv_point_t p={x,y};lv_obj_t *o=lv_indev_search_obj(screen,&p);assert(o);lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);}
static bool has(lv_obj_t *o,const char *text){if(lv_obj_has_class(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text))return true;for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)if(has(lv_obj_get_child(o,i),text))return true;return false;}
static void after_drive(lv_timer_t *t){assert(tour_hits==1&&ui_adventure_is_open()&&ui_adventure_scene()==ADVENTURE_EGYPT);puts("ARTIFACTS_FLOW_PASS: continuous dwell, one trigger, invalid samples do not rearm, measured absence rearm, van to real Tour A Egypt");lv_timer_delete(t);}
void ws_render(lv_obj_t *scr){screen=scr;page=ui_page_create(scr);ui_home_build(&home,page,ws_noop_event_cb,ws_noop_event_cb);ui_home_set_actions(ws_noop_event_cb,tour,ws_noop_event_cb);
 const char *a=ws_fx_str("action","");
 if(!strcmp(a,"egypt")){tour(NULL);return;}
 if(!strcmp(a,"presence")){ui_home_presence_configure(150,8000,2000);ui_home_presence_sample(true,120,0);ui_home_presence_sample(true,120,8000);return;}
 if(!strcmp(a,"test")){
  ui_home_presence_configure(150,8000,2000);ui_home_presence_sample(true,120,0);ui_home_presence_sample(true,120,7999);assert(!ui_home_state().intro_open);
  ui_home_presence_sample(true,120,8000);assert(ui_home_state().intro_open&&has(page,"Welcome to Artifacts & Instruments!\nTouch the screen to explore.\nTap the mouse for tour guidance."));tap(160,200);assert(!ui_home_state().intro_open);
  ui_home_presence_sample(true,120,18000);assert(!ui_home_state().intro_open);ui_home_presence_sample(false,0,20000);ui_home_presence_sample(false,0,24000);ui_home_presence_sample(true,120,25000);ui_home_presence_sample(true,120,34000);assert(!ui_home_state().intro_open);
  ui_home_presence_sample(true,250,35000);ui_home_presence_sample(true,250,37000);ui_home_presence_sample(true,120,38000);ui_home_presence_sample(true,120,46000);assert(ui_home_state().intro_open);tap(160,200);
  tap(260,175);assert(ui_home_state().van_running);lv_timer_create(after_drive,3400,NULL);
 }
}
