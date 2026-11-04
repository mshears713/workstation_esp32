/* Interactive Orlando scene. Original Tour A pixels are shared, never redrawn.
 * New landmarks are native Pixelorama exports. No ESP subsystem dependencies. */
#include "ui_home.h"
#include "ui_adventure.h"
#include "orlando_landmarks.h"
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "orlando_v3.h"
#include "orlando_v4.h"
#include "orlando_v5_background.h"
#include "orlando_v5_mouse.h"
#include "orlando_v5_crest.h"

enum { ACT_HOME=1,ACT_TOUR,ACT_INTRO,ACT_WHEEL,ACT_CASTLE,ACT_COASTER,
       ACT_MENU,ACT_ALL,ACT_TALK,ACT_DISMISS,ACT_NEXT,ACT_CLOSE };
static struct {
 lv_obj_t *root,*wheel,*car,*peek,*princess,*mouse,*sign,*shade,*tray,*menu_label;
 lv_obj_t *dialog,*dialog_text,*audio_hint,*guide_nav,*cabins[12],*cues[3];
 lv_timer_t *timer;
 lv_event_cb_t home_cb,tour_cb,intro_cb,menu_cb,talk_cb;
 uint32_t elapsed,wheel_start,castle_start,car_start,last_tick,intro_dismissed;
 int wheel_base,wheel_angle;
 unsigned intro_page;
 bool active,spinning,peeking,launching,menu_open;
} s;
static void action(lv_event_t *e);
static void wheel_position(int angle) {
 lv_image_set_rotation(s.wheel,angle);
 for(int i=0;i<12;i++) {
  float a=(angle/10.0f+i*30)*0.0174532925f;
  lv_obj_set_pos(s.cabins[i],266+(int)lroundf(26*cosf(a)),89+(int)lroundf(26*sinf(a)));
 }
}
static void visible(lv_obj_t *o,bool show) {
 if(!o)return;
 if(show)lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *flat(lv_obj_t *p,int x,int y,int w,int h,uint32_t c,lv_opa_t opa) {
 lv_obj_t *o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(c),0);lv_obj_set_style_bg_opa(o,opa,0);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
static lv_obj_t *label(lv_obj_t *p,const char *t,int x,int y,int w,const lv_font_t *f,uint32_t c) {
 lv_obj_t *o=lv_label_create(p);lv_label_set_text(o,t);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
 lv_obj_set_style_text_font(o,f,0);lv_obj_set_style_text_color(o,lv_color_hex(c),0);return o;
}
static lv_obj_t *sprite(const lv_image_dsc_t *src,int x,int y,int scale) {
 lv_obj_t *o=lv_image_create(s.root);lv_image_set_src(o,src);lv_image_set_pivot(o,0,0);
 lv_image_set_scale(o,scale);lv_image_set_antialias(o,false);lv_obj_set_pos(o,x,y);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static lv_obj_t *hit(int x,int y,int w,int h,int id) {
 lv_obj_t *o=flat(s.root,x,y,w,h,0,LV_OPA_TRANSP);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(o,action,LV_EVENT_CLICKED,(void *)(intptr_t)id);return o;
}
static lv_obj_t *button(lv_obj_t *p,const char *text,int x,int y,int w,int h,int id) {
 lv_obj_t *b=flat(p,x,y,w,h,0xE5C58F,LV_OPA_COVER);lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_style_radius(b,7,0);lv_obj_add_event_cb(b,action,LV_EVENT_CLICKED,(void *)(intptr_t)id);
 lv_obj_t *t=label(b,text,0,0,w,&lv_font_montserrat_14,0x172533);
 lv_obj_set_style_text_align(t,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(t);return b;
}
static void close_menu(void) {
 s.menu_open=false;visible(s.shade,false);visible(s.tray,false);
 if(s.menu_label)lv_label_set_text(s.menu_label,"Menu");
}
static void close_intro(void) {
 if(s.dialog)lv_obj_delete(s.dialog);
 s.dialog=NULL;s.dialog_text=NULL;s.audio_hint=NULL;s.guide_nav=NULL;s.intro_dismissed=s.elapsed;
}
static const char *guide[]={
 "I'm your curious mouse guide!\nHome and Menu open controls.\nMore adventures are coming.\nTap me whenever you like.",
 "Tap the Eye to give it a spin.\nThe castle has two shy guests.\nTap the coaster to launch a car.\nTap me again whenever you like."
};
static void open_intro(lv_event_t *e) {
 if(s.dialog)return;
 close_menu();visible(s.sign,false);s.intro_page=0;
 s.dialog=flat(s.root,0,0,320,240,0x142331,180);lv_obj_add_flag(s.dialog,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_t *paper=flat(s.dialog,10,27,300,204,0xF3E9CF,LV_OPA_COVER);lv_obj_set_style_radius(paper,10,0);
 lv_obj_t *portrait=lv_image_create(paper);lv_image_set_src(portrait,ui_adventure_original_asset(TOUR_ASSET_MOUSE,0));
 lv_image_set_pivot(portrait,0,0);lv_image_set_scale(portrait,512);lv_image_set_antialias(portrait,false);lv_obj_set_pos(portrait,12,8);
 label(paper,"Hello, traveler!",54,14,234,&lv_font_montserrat_20,0x25303D);
 s.dialog_text=label(paper,guide[0],12,58,276,&lv_font_montserrat_14,0x25303D);
 /* Routine audio availability is not part of the welcome copy. */
 s.guide_nav=button(paper,"Next",12,151,128,42,ACT_NEXT);button(paper,"Close guide",158,151,130,42,ACT_CLOSE);
 if(s.intro_cb)s.intro_cb(e);
}
void ui_home_intro_audio_result(bool accepted) {
 (void)accepted; /* Written guide remains complete whether voice accepts or is busy. */
}
static void reset_scene(void) {
 s.spinning=s.peeking=s.launching=false;s.wheel_angle=0;
 if(s.wheel)wheel_position(0);
 for(int i=0;i<3;i++)visible(s.cues[i],false);
 visible(s.car,false);visible(s.peek,false);visible(s.princess,false);
 close_intro();close_menu();visible(s.sign,false);s.intro_dismissed=s.elapsed;
}
static void action(lv_event_t *e) {
 int id=(int)(intptr_t)lv_event_get_user_data(e);
 if(!s.active)return;
 if(s.dialog && id!=ACT_NEXT && id!=ACT_CLOSE)return;
 switch(id) {
 case ACT_HOME:
  s.menu_open=true;visible(s.shade,true);visible(s.tray,true);lv_label_set_text(s.menu_label,"Close");break;
 case ACT_TOUR:break; /* Next scene intentionally deferred. */
 case ACT_INTRO:open_intro(e);break;
 case ACT_WHEEL:close_menu();s.wheel_start=s.elapsed;s.wheel_base=s.wheel_angle;s.spinning=true;break;
 case ACT_CASTLE:close_menu();s.castle_start=s.elapsed;s.peeking=true;break;
 case ACT_COASTER:close_menu();s.car_start=s.elapsed;s.launching=true;break;
 case ACT_MENU:
  s.menu_open=!s.menu_open;visible(s.shade,s.menu_open);visible(s.tray,s.menu_open);
  lv_label_set_text(s.menu_label,s.menu_open?"Close":"Menu");break;
 case ACT_ALL:close_menu();if(s.menu_cb)s.menu_cb(e);break;
 case ACT_TALK:close_menu();if(s.talk_cb)s.talk_cb(e);break;
 case ACT_DISMISS:close_menu();break;
 case ACT_NEXT:s.intro_page=(s.intro_page+1)%2;lv_label_set_text(s.dialog_text,guide[s.intro_page]);lv_label_set_text(lv_obj_get_child(s.guide_nav,0),s.intro_page?"Back":"Next");break;
 case ACT_CLOSE:close_intro();break;
 }
}
static void tick(lv_timer_t *t) {
 (void)t;uint32_t now=lv_tick_get(),dt=now-s.last_tick;s.last_tick=now;
 if(!s.active || !s.root || lv_obj_has_flag(s.root,LV_OBJ_FLAG_HIDDEN))return;
 s.elapsed+=dt;
 /* Native-resolution alternative; original six-pose source remains intact. */
 lv_image_set_src(s.mouse,orlando_mouse_frames[(s.elapsed/250)%6]);
 if(s.spinning) {
  uint32_t d=s.elapsed-s.wheel_start;
  if(d>=3200){d=3200;s.spinning=false;}
  s.wheel_angle=(s.wheel_base+(int)(d*3600/3200))%3600;
  wheel_position(s.wheel_angle);
 }
 if(s.peeking) {
  uint32_t d=s.elapsed-s.castle_start;
  int amount=d<600?(int)d*20/600:d<2000?20:d<2600?(int)(2600-d)*20/600:0;
  lv_obj_set_x(s.peek,131-amount*3/4);visible(s.peek,d<2600);
  visible(s.princess,d>=400 && d<2200);
  if(d>=2600)s.peeking=false;
 }
 if(s.launching) {
  uint32_t d=s.elapsed-s.car_start;
  int distance=(int)(d*d/8000); if(distance>90)distance=90;
  if(d<850){lv_image_set_rotation(s.car,3210);lv_obj_set_pos(s.car,12+distance*3/4,119-distance*65*3/(81*4));}
  else {int t=(d-850)>350?350:(int)(d-850);lv_image_set_rotation(s.car,t<120?0:300);lv_obj_set_pos(s.car,79+t*30/350,65+t*15/350);}
  visible(s.car,d<1200);
  if(d>=1200)s.launching=false;
 }
 bool sign=!s.dialog&&!s.menu_open && s.elapsed-s.intro_dismissed>=6000 && (s.elapsed%14000)>=9000;
 visible(s.sign,sign);

 /* Brief hint strokes, no extra permanent buttons; suppressed during overlays/motion. */
 unsigned cue=s.elapsed%18000;
 for(int i=0;i<3;i++)visible(s.cues[i],!s.dialog&&!s.menu_open&&!s.spinning&&!s.peeking&&!s.launching && cue>=4000u+i*900u && cue<4400u+i*900u);
}
static void deleted(lv_event_t *e) {
 (void)e;if(s.timer)lv_timer_delete(s.timer);memset(&s,0,sizeof(s));
}
void ui_home_set_active(bool active) {
 if(!s.root)return;
 s.active=active;s.last_tick=lv_tick_get();
 if(!active){reset_scene();if(s.timer)lv_timer_pause(s.timer);}
 else if(s.timer)lv_timer_resume(s.timer);
}
void ui_home_set_actions(lv_event_cb_t home_cb,lv_event_cb_t tour_cb,lv_event_cb_t intro_cb) {
 s.home_cb=home_cb;s.tour_cb=tour_cb;s.intro_cb=intro_cb;
}
ui_home_state_t ui_home_state(void) {
 return (ui_home_state_t){.active=s.active,.menu_open=s.menu_open,.intro_open=s.dialog!=NULL,
 .sign_visible=s.sign&&!lv_obj_has_flag(s.sign,LV_OBJ_FLAG_HIDDEN),.wheel_running=s.spinning,
 .castle_running=s.peeking,.launch_running=s.launching,.timers=s.timer?1u:0u,.intro_page=s.intro_page,
 .wheel_angle=s.wheel_angle,.car_x=s.car?lv_obj_get_x(s.car):0,.peek_x=s.peek?lv_obj_get_x(s.peek):0};
}
void ui_home_build(ui_home_t *h,lv_obj_t *page,lv_event_cb_t talk_cb,lv_event_cb_t menu_cb) {
 memset(h,0,sizeof(*h));
 /* One app HOME instance. Delete the old owned page before constructing another. */
 if(s.timer){lv_timer_delete(s.timer);s.timer=NULL;}
 memset(&s,0,sizeof(s));s.root=page;s.active=true;s.last_tick=lv_tick_get();s.menu_cb=menu_cb;s.talk_cb=talk_cb;
 lv_obj_add_event_cb(page,deleted,LV_EVENT_DELETE,NULL);
 lv_obj_set_style_bg_color(page,lv_color_hex(0x91AD7A),0);lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);
 sprite(&orlando_v5_background,0,0,256);
 sprite(&hulk_launch,6,64,192);
 sprite(&orlando_crest,77,65,256);
 sprite(&eye_base,247,91,192);
 s.wheel=sprite(&eye_rim,239,61,192);lv_image_set_pivot(s.wheel,40,40);
 for(int i=0;i<12;i++){s.cabins[i]=sprite(&eye_cabin,0,0,192);}
 wheel_position(0);
 s.peek=sprite(&castle_silhouette,131,81,192);visible(s.peek,false);
 sprite(&center_castle,125,40,192);
 s.princess=sprite(&window_princess_v3,154,87,192);visible(s.princess,false);
 s.car=sprite(&coaster_car,12,119,120);lv_image_set_pivot(s.car,14,8);visible(s.car,false);
 sprite(ui_adventure_original_asset(TOUR_ASSET_HOUSE,0),8,124,448);
 sprite(ui_adventure_original_asset(TOUR_ASSET_VAN,0),211,150,384);
 s.mouse=sprite(orlando_mouse_frames[0],126,170,256);
 /* Small captions sit within the original objects' hit regions. */
 lv_obj_t *home=label(page,"Home",17,208,66,&lv_font_montserrat_14,0x172533);
 lv_obj_set_style_text_align(home,LV_TEXT_ALIGN_CENTER,0);
 hit(6,132,84,92,ACT_HOME);
 hit(119,160,62,68,ACT_INTRO);hit(235,55,75,75,ACT_WHEEL);
 hit(120,35,82,100,ACT_CASTLE);hit(0,60,90,75,ACT_COASTER);
 /* A held sign is a separate overlay, preserving the original mouse pixels. */
 s.sign=flat(page,115,139,70,70,0,LV_OPA_TRANSP);
 /* Handle meets the mouse paw; board is raised away from bottom controls. */
 flat(s.sign,40,22,3,41,0x866E50,LV_OPA_COVER);
 flat(s.sign,35,61,8,4,0x866E50,LV_OPA_COVER);
 lv_obj_t *board=flat(s.sign,0,0,70,24,0xF3E9CF,LV_OPA_COVER);lv_obj_set_style_border_width(board,1,0);
 lv_obj_set_style_border_color(board,lv_color_hex(0x866E50),0);
 label(board,"Guide",10,3,58,&lv_font_montserrat_14,0x25303D);
 lv_obj_add_flag(s.sign,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(s.sign,action,LV_EVENT_CLICKED,(void *)(intptr_t)ACT_INTRO);visible(s.sign,false);
 const int cx[]={56,160,271},cy[]={42,25,66};
 for(int i=0;i<3;i++) {
  s.cues[i]=flat(page,cx[i]-7,cy[i]-7,14,14,0,LV_OPA_TRANSP);
  lv_obj_set_style_border_width(s.cues[i],2,0);lv_obj_set_style_border_color(s.cues[i],lv_color_hex(0xF3D59A),0);lv_obj_set_style_radius(s.cues[i],7,0);visible(s.cues[i],false);
 }
 s.shade=hit(0,0,320,240,ACT_DISMISS);visible(s.shade,false);
 s.tray=flat(page,190,101,122,90,0x172533,LV_OPA_COVER);lv_obj_set_style_radius(s.tray,8,0);
 button(s.tray,"Guide",4,4,114,40,ACT_INTRO);button(s.tray,"Main menu",4,47,114,40,ACT_ALL);visible(s.tray,false);
 lv_obj_t *menu=flat(page,248,194,64,40,0,LV_OPA_TRANSP);
 lv_obj_add_flag(menu,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(menu,action,LV_EVENT_CLICKED,(void *)(intptr_t)ACT_MENU);
 lv_obj_t *face=flat(menu,0,8,64,32,0xE5C58F,LV_OPA_COVER);lv_obj_set_style_radius(face,7,0);
 s.menu_label=label(face,"Menu",0,0,64,&lv_font_montserrat_14,0x172533);
 lv_obj_set_style_text_align(s.menu_label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(s.menu_label);
 s.timer=lv_timer_create(tick,50,NULL);
}

/* Compatibility only: the scene has no weather UI or weather choreography. */
void ui_home_apply_weather(ui_home_t *h,const ui_home_weather_t *w){(void)h;(void)w;}
void ui_home_reveal_weather_field(ui_home_t *h,const weather_status_t *w,unsigned c,weather_stage_field_t f){(void)h;(void)w;(void)c;(void)f;}
bool ui_home_weather_stage_offer(const weather_status_t *w){(void)w;return false;}
bool ui_home_weather_stage_current(unsigned *c,weather_stage_field_t *f){(void)c;(void)f;return false;}
const weather_status_t *ui_home_weather_stage_snapshot(void){return NULL;}
bool ui_home_weather_stage_reveal(void){return false;}
bool ui_home_weather_stage_running(void){return false;}
void ui_home_weather_stage_cancel(void){}
lv_obj_t *ui_home_weather_field_object(unsigned c,weather_stage_field_t f){(void)c;(void)f;return NULL;}
void ui_home_caretaker_end(void){}
void ui_home_caretaker_frame(int slot,unsigned frame){(void)slot;(void)frame;}
void ui_home_set_link(ui_home_t *h,voice_widget_link_t link){(void)h;(void)link;}
