#include "ws_sim.h"
#include "ui_menu.h"
#include "ui_kit.h"
#include "ui_adventure.h"
#include "ui_weather_page.h"
#include "weather_icons.h"
#include "backend_health.h"
#include "ui_mouse.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Only app page presentation and backend are substitutes. Callback bodies below
 * are extracted directly from the actual status_deck_ui.c by the generator. */
typedef enum {APP_PAGE_HOME,APP_PAGE_MENU,APP_PAGE_MORE,APP_PAGE_MORE3,APP_PAGE_WEATHER} app_page_t;
static app_page_t current_app_page;static bool weather_force_update;static unsigned refresh_hits;
static lv_obj_t *scr,*pages[5],*back,*page_home;static ui_home_t weather_ui,home_ui;static weather_status_t data;
static backend_health_state_t fake_health=BACKEND_HEALTH_OK;
void weather_client_get(weather_status_t *out){*out=data;refresh_hits++;}
void backend_health_get_status(backend_health_status_t *out){memset(out,0,sizeof(*out));out->state=fake_health;}
void ui_mouse_home_refresh_result(bool ok){(void)ok;}
bool ui_home_weather_stage_running(void){return false;}
bool ui_home_weather_stage_offer(const weather_status_t *w){(void)w;return false;}
void ui_home_weather_stage_cancel(void){}
void ui_home_apply_weather(ui_home_t *h,const ui_home_weather_t *v){(void)h;(void)v;}
#define WEATHER_STALE_S (30 * 60)
static void set_active_page(app_page_t n){current_app_page=n;for(int i=0;i<5;i++){if(i==n)lv_obj_remove_flag(pages[i],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(pages[i],LV_OBJ_FLAG_HIDDEN);}if(n==APP_PAGE_WEATHER)lv_obj_add_flag(back,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(back,LV_OBJ_FLAG_HIDDEN);}
static void render_weather(void);
/* EXTRACTED_APP_CALLBACKS */
/* EXTRACTED_APP_WEATHER_MAPPER */
static void tap(int x,int y){lv_obj_update_layout(scr);lv_point_t p={x,y};lv_obj_t *o=lv_indev_search_obj(scr,&p);assert(o);lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);}
void ws_render(lv_obj_t *screen){
 scr=screen;for(int i=0;i<5;i++)pages[i]=ui_page_create(scr);page_home=pages[APP_PAGE_HOME];
 lv_obj_t *home_menu=lv_button_create(pages[APP_PAGE_HOME]);lv_obj_set_pos(home_menu,110,70);lv_obj_set_size(home_menu,100,60);lv_obj_add_event_cb(home_menu,menu_button_cb,LV_EVENT_CLICKED,NULL);
 const ui_menu_item_t dummy[]={{"SENSORS",ws_noop_event_cb}};
 const ui_menu_item_t first={"MORE",more_nav_button_cb},second={"NEXT",more3_nav_button_cb};
 ui_menu_build(pages[APP_PAGE_MENU],"MENU",dummy,1,&first);ui_menu_build(pages[APP_PAGE_MORE],"MORE",dummy,1,&second);
 const ui_menu_picture_item_t cards[]={{"Weather",weather_icon("partly",true),320,weather_nav_button_cb},{"Mouse",ui_adventure_original_asset(TOUR_ASSET_MOUSE,0),768,ws_noop_event_cb},{"Adventure",ui_adventure_original_asset(TOUR_ASSET_VAN,0),320,ws_noop_event_cb}};
 ui_menu_build_pictures(pages[APP_PAGE_MORE3],"EXPLORE",cards,3,NULL);ui_weather_page_build(&weather_ui,pages[APP_PAGE_WEATHER],ws_noop_event_cb,more3_nav_button_cb);back=ui_back_button_create(scr,back_nav_cb);
 data=(weather_status_t){.valid=true,.seq=1,.location="Orlando",.now_temp=84,.now_icon="partly",.now_text="Partly cloudy",.now_rain=10,.slot={{"3 PM",87,"storm",60},{"7 PM",79,"partly",20}},.tmrw_hi=88,.tmrw_lo=73,.tmrw_icon="sun",.tmrw_rain=20};
 for(int i=0;i<3;i++){
  set_active_page(APP_PAGE_HOME);tap(150,90);assert(current_app_page==APP_PAGE_MENU);tap(270,26);assert(current_app_page==APP_PAGE_MORE);tap(270,26);assert(current_app_page==APP_PAGE_MORE3);tap(45,120);assert(current_app_page==APP_PAGE_WEATHER&&!weather_force_update);assert(strstr(lv_label_get_text(weather_ui.now_temp),"84"));
  data.now_temp=75;data.seq++;render_weather();assert(strstr(lv_label_get_text(weather_ui.now_temp),"75"));
  data.age_s=1801;strcpy(data.as_of,"9:15");render_weather();assert(strstr(lv_label_get_text(weather_ui.now_where),"9:15"));
  data.valid=false;fake_health=BACKEND_HEALTH_DOWN;render_weather();assert(!strcmp(lv_label_get_text(weather_ui.now_text),"No weather"));
  data.valid=true;data.age_s=0;data.now_temp=84;data.seq++;
  tap(260,205);assert(current_app_page==APP_PAGE_MORE3);tap(45,25);assert(current_app_page==APP_PAGE_MORE);tap(45,25);assert(current_app_page==APP_PAGE_MENU);
 }
 assert(refresh_hits>=12);puts("MENU3_APP_CALLBACKS_PASS: extracted app callbacks and weather mapper, repeated navigation, fresh/stale/unavailable updates; page container and backend substituted");
}
