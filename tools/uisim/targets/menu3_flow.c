#include "ws_sim.h"
#include "ui_menu.h"
#include "ui_kit.h"
#include "ui_adventure.h"
#include "ui_weather_page.h"
#include "weather_icons.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_obj_t *scr,*menu,*more,*pictures,*weather,*back;static ui_home_t w;static int active,hits;
static void show(int n){active=n;lv_obj_t *pages[]={menu,more,pictures,weather};for(int i=0;i<4;i++){if(i==n)lv_obj_remove_flag(pages[i],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(pages[i],LV_OBJ_FLAG_HIDDEN);}if(n==3)lv_obj_add_flag(back,LV_OBJ_FLAG_HIDDEN);else{lv_obj_remove_flag(back,LV_OBJ_FLAG_HIDDEN);lv_label_set_text(lv_obj_get_child(back,0),n==1?LV_SYMBOL_LEFT " MENU":n==2?LV_SYMBOL_LEFT " MORE":LV_SYMBOL_LEFT " HOME");}}
static void next(lv_event_t *e){(void)e;show(active==0?1:2);}
static void previous(lv_event_t *e){(void)e;show(active==3?2:active==2?1:0);}
static void forecast(lv_event_t *e){(void)e;hits++;show(3);}
static void tap(int x,int y){lv_obj_update_layout(scr);lv_point_t p={x,y};lv_obj_t *o=lv_indev_search_obj(scr,&p);assert(o);lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);}
void ws_render(lv_obj_t *screen){
 scr=screen;menu=ui_page_create(scr);more=ui_page_create(scr);pictures=ui_page_create(scr);weather=ui_page_create(scr);
 const ui_menu_item_t a[]={ {"SENSORS",ws_noop_event_cb} };const ui_menu_item_t corner={"NEXT",next};
 ui_menu_build(menu,"MENU",a,1,&corner);ui_menu_build(more,"MORE",a,1,&corner);
 const ui_menu_picture_item_t cards[]={{"Weather",weather_icon("partly",true),320,forecast},{"Mouse",ui_adventure_original_asset(TOUR_ASSET_MOUSE,0),768,ws_noop_event_cb},{"Adventure",ui_adventure_original_asset(TOUR_ASSET_VAN,0),320,ws_noop_event_cb}};
 ui_menu_build_pictures(pictures,"EXPLORE",cards,3,NULL);ui_weather_page_build(&w,weather,ws_noop_event_cb,previous);back=ui_back_button_create(scr,previous);show(0);
 ui_home_weather_t data={.valid=true,.location="Orlando",.now_temp=84,.now_icon="partly",.now_text="Partly cloudy",.now_rain=10,.slot={{"3 PM",87,"storm",60},{"7 PM",79,"partly",20}},.tmrw_hi=88,.tmrw_lo=73,.tmrw_icon="sun",.tmrw_rain=20};
 ui_weather_page_apply_weather(&w,&data);
 if(ws_fx_bool("exercise",false)){
  for(int i=0;i<3;i++){
   tap(270,26);assert(active==1);tap(270,26);assert(active==2);
   tap(45,120);assert(active==3);assert(strstr(lv_label_get_text(w.now_temp),"84"));
   data.now_temp=75;ui_weather_page_apply_weather(&w,&data);assert(strstr(lv_label_get_text(w.now_temp),"75"));
   data.stale_as_of="9:15";ui_weather_page_apply_weather(&w,&data);assert(strstr(lv_label_get_text(w.now_where),"9:15"));
   data.stale_as_of=NULL;data.valid=false;data.status="No weather";ui_weather_page_apply_weather(&w,&data);assert(!strcmp(lv_label_get_text(w.now_text),"No weather"));
   data.valid=true;
   data.now_temp=84;ui_weather_page_apply_weather(&w,&data);
   tap(260,205);assert(active==2);tap(45,25);assert(active==1);tap(45,25);assert(active==0);
  }
  assert(hits==3);puts("MENU3_FLOW_PASS: builder navigation, card hit, forecast update, Weather back and repeat");
 }
 if(ws_fx_bool("show_weather",false)){show(3);}
 else if(ws_fx_bool("show_more",false)){show(2);if(ws_fx_bool("pressed",false))lv_obj_add_state(lv_obj_get_child(pictures,1),LV_STATE_PRESSED);}
}
