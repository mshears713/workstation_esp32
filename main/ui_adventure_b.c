#include "ui_adventure_b.h"
#include "adventure_story.h"
#include "adventure_assets.h"
#include "voice_listening_widget.h"
#include <stddef.h>
#include <stdio.h>

/* Only this owned 100-ms timer moves the story. No voice-control, backend,
 * survey or application action handlers are linked into this module. */
static lv_obj_t *root,*canvas,*title,*panel,*caption,*tail,*progress,*mic,*next_label,*skip_label;
static lv_timer_t *timer;
static b_story_state_t state;
static uint32_t last_tick;
static void (*tour_exit_callback)(void *);
static void *exit_ctx;
static const b_image_t *mouse_frames[]={&b_mouse0,&b_mouse1,&b_mouse2,&b_mouse3,&b_mouse4,&b_mouse5};
static const b_image_t *climb_frames[]={&b_climb0,&b_climb1,&b_climb2,&b_climb3};
static const b_image_t *smoke_frames[]={&b_smoke0,&b_smoke1,&b_smoke2,&b_smoke3};
static const b_image_t *whirl_frames[]={&b_whirl0,&b_whirl1,&b_whirl2,&b_whirl3};
static int lerp(int a,int b,uint32_t t,uint32_t end){if(t>end)t=end;return a+(int)(((int64_t)b-a)*t/end);}

/* Exact nearest-neighbour run drawing: palettes and indexed pixels stay in
 * flash, without allocating a full decoded 320x240 backdrop per frame. */
static void paint(lv_layer_t *layer,const b_image_t *im,int x,int y,int scale,int clipx)
{
 lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;d.radius=0;
 if(im->runs){
  for(int row=0;row<im->h;row++){
   int col=0;
   for(unsigned p=im->rows[row];p<im->rows[row+1];p+=2){
    int count=im->runs[p];uint32_t c=im->pal[im->runs[p+1]];
    if(c>>24){lv_area_t a={x+col*scale,y+row*scale,x+(col+count)*scale-1,y+(row+1)*scale-1};
     if(a.x2>clipx)a.x2=clipx;
     if(a.x1<=a.x2){d.bg_color=lv_color_hex(c&0xffffff);d.bg_opa=c>>24;lv_draw_rect(layer,&d,&a);}
    }col+=count;
   }
  }return;
 }
 for(int row=0;row<im->h;row++)for(int col=0;col<im->w;){
  uint8_t ix=im->px[row*im->w+col];int end=col+1;
  while(end<im->w && im->px[row*im->w+end]==ix)end++;
  uint32_t c=im->pal[ix];
  if(c>>24){
   lv_area_t a={x+col*scale,y+row*scale,x+end*scale-1,y+(row+1)*scale-1};
   if(a.x2>clipx)a.x2=clipx;
   if(a.x1<=a.x2){d.bg_color=lv_color_hex(c&0xffffff);d.bg_opa=c>>24;lv_draw_rect(layer,&d,&a);}
  }col=end;
 }
}
static void draw(lv_event_t *e)
{
 lv_layer_t *l=lv_event_get_layer(e);const b_beat_t *b=&b_story[state.beat];
 lv_area_t area;lv_obj_get_coords(lv_event_get_target(e),&area);int ox=area.x1,oy=area.y1;
 const b_image_t *bg=b->place==B_EGYPT?&b_egypt:b->place==B_GREECE?&b_greece:b->place==B_FRANCE?&b_france:&b_home;
 paint(l,bg,ox,oy-((b->place!=B_HOME && b->place!=B_PORTAL && b->action!=B_DRIVE)?8:0),2,32767);
 uint32_t t=state.elapsed;unsigned f=(t/167)%6,q=(t/167)%4;int mx=76,my=160;
 bool destination=b->place==B_EGYPT||b->place==B_GREECE||b->place==B_FRANCE;
 if(b->place==B_HOME){
  paint(l,&b_house,ox+16,oy+96,2,32767);
  paint(l,smoke_frames[q],ox+29,oy+78,1,32767);
 }
 switch(b->action){
 case B_EMERGE:
  /* House draws first; reveal through the doorway, not through the facade. */
  mx=t<6000?48:lerp(48,112,t-6000,6000);my=lerp(132,160,t,6000);
  if(t>900){
   /* Front-door emergence: upper body is masked to the doorway until it
    * reaches the path. This is display clipping, not changed source art. */
   if(t<6000){
    const b_image_t *m=mouse_frames[f];lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
    for(int yy=0;yy<m->h;yy++)for(int xx=0;xx<m->w;xx++){
     uint32_t c=m->pal[m->px[yy*m->w+xx]];if(!(c>>24))continue;
     int x=ox+mx+xx*2,y=oy+my+yy*2;
     if(y<oy+168 && (x<ox+46||x>ox+66))continue;
     lv_area_t a={x,y,x+1,y+1};d.bg_color=lv_color_hex(c&0xffffff);d.bg_opa=LV_OPA_COVER;lv_draw_rect(l,&d,&a);
    }
   }else paint(l,mouse_frames[f],ox+mx,oy+my,2,32767);
  }
  break;
 case B_INTRO: paint(l,mouse_frames[f],ox+112,oy+160,2,32767);break;
 case B_SING:
  paint(l,mouse_frames[f],ox+(b->place==B_HOME?112:79),oy+(destination?120:158)-(q==1?2:0),2,32767);
  paint(l,&b_notes,ox+(b->place==B_HOME?176:116),oy+116-(int)(q*3),1,32767);break;
 case B_BOARD:
  paint(l,&b_van,ox+(t<9500?174:lerp(174,250,t-9500,2500)),oy+114,2,32767);
  mx=lerp(112,240,t,9500);if(t<9500)paint(l,mouse_frames[f],ox+mx,oy+160,2,ox+222);break;
 case B_DRIVE:{
  int vx=lerp(-128,321,t,b->duration?b->duration:7000);
  if(state.beat==20)vx=lerp(321,150,t,6000);
  paint(l,&b_van,ox+vx,oy+114+(q&1),2,32767);
  if(state.beat==20 && t>6500)paint(l,mouse_frames[f],ox+lerp(165,96,t-6500,1500),oy+160,2,32767);
  break;}
 case B_WARP:
  paint(l,&b_portal,ox+224,oy+105,1,32767);
  if(t>6000)paint(l,whirl_frames[q],ox+232,oy+118,1,32767);
  paint(l,&b_van,ox+lerp(-128,280,t,b->duration>600?b->duration-600:6000),oy+114,2,ox+261);break;
 case B_FRIEND:
  paint(l,mouse_frames[f],ox+91,oy+120,2,32767);paint(l,&b_mummy,ox+194,oy+64+(q==1),2,32767);break;
 case B_CLIMB:{
  int ladder_x=b->place==B_GREECE?75:251;
  paint(l,&b_ladder,ox+ladder_x,oy+75,1,32767);
  int yy=t<5000?lerp(120,76,t,5000):lerp(76,120,t-5000,5000);
  paint(l,climb_frames[q],ox+ladder_x,oy+yy,1,32767);break;}
 case B_LOOK:
  paint(l,mouse_frames[f],ox+83,oy+120,2,32767);
  if(b->place==B_FRANCE)paint(l,&b_notes,ox+127,oy+110,1,32767);
  break;
 case B_END:paint(l,mouse_frames[f],ox+96,oy+160,2,32767);break;
 case B_STOP:paint(l,mouse_frames[f],ox+79,oy+120,2,32767);break;
 }
}
static void restyle(void)
{
 if(!root)return;
 const b_beat_t *b=&b_story[state.beat];
 lv_label_set_text(title,b->title);lv_label_set_text(caption,b->text);
 bool destination=b->place==B_EGYPT||b->place==B_GREECE||b->place==B_FRANCE;
 bool scenic=b->action==B_DRIVE||b->action==B_WARP;
 if(scenic){lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(tail,LV_OBJ_FLAG_HIDDEN);}
 else lv_obj_remove_flag(panel,LV_OBJ_FLAG_HIDDEN);
 lv_obj_set_pos(panel,8,destination?153:23);lv_obj_set_size(panel,304,destination?39:57);
 lv_obj_set_style_pad_all(panel,destination?1:5,0);
 lv_obj_set_style_text_font(caption,destination?&lv_font_montserrat_14:&lv_font_montserrat_20,0);
 lv_obj_set_pos(tail,43,destination?147:78);
 lv_obj_set_pos(progress,8,scenic?176:1);
 lv_obj_add_flag(progress,LV_OBJ_FLAG_HIDDEN); /* Provenance belongs in the gallery, not the story. */
 lv_obj_center(caption);
 uint32_t fill=0x172638,ink=0xfff5dc,border=0xe8bd70;
 int radius=6,border_w=1;
 if(b->treatment==B_SPEECH){fill=0xfff2d8;ink=0x172638;radius=12;border_w=2;}
 if(b->treatment==B_THOUGHT){fill=0xe3f3e8;ink=0x213f41;radius=19;border=0x557e74;border_w=2;}
 if(b->treatment==B_SHOUT){fill=0xf5be65;ink=0x263547;radius=2;border_w=3;}
 lv_obj_set_style_bg_color(panel,lv_color_hex(fill),0);lv_obj_set_style_radius(panel,radius,0);
 lv_obj_set_style_border_color(panel,lv_color_hex(border),0);lv_obj_set_style_border_width(panel,border_w,0);
 lv_obj_set_style_text_color(caption,lv_color_hex(ink),0);
 lv_obj_set_style_bg_color(tail,lv_color_hex(fill),0);
 if(scenic||b->treatment==B_CAPTION)lv_obj_add_flag(tail,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(tail,LV_OBJ_FLAG_HIDDEN);
 lv_obj_set_style_radius(tail,b->treatment==B_THOUGHT?6:0,0);
 lv_label_set_text(next_label,b->action==B_END?"REPLAY":"NEXT >");
 lv_label_set_text(skip_label,state.beat<5?"SKIP INTRO":"SKIP");
 if(b->place==B_HOME && (b->action==B_INTRO||b->action==B_SING))lv_obj_remove_flag(mic,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(mic,LV_OBJ_FLAG_HIDDEN);
 char s[30];snprintf(s,sizeof s,"%02u / %02u  VISUAL ONLY",state.beat+1,B_BEAT_COUNT);lv_label_set_text(progress,s);
 lv_obj_invalidate(canvas);
}
static void tick(lv_timer_t *t)
{
 (void)t;if(!root || !state.active)return;
 uint32_t now=lv_tick_get(),delta=now-last_tick;last_tick=now;unsigned old=state.beat;
 b_story_advance(&state,delta);
 if(old!=state.beat)restyle();else lv_obj_invalidate(canvas);
}
static void destroyed(lv_event_t *e)
{
 if(lv_event_get_target(e)!=root)return;
 if(timer){lv_timer_delete(timer);timer=NULL;}b_story_close(&state);
 root=canvas=title=panel=caption=tail=progress=mic=next_label=skip_label=NULL;tour_exit_callback=NULL;exit_ctx=NULL;
}
void ui_adventure_b_close(void)
{
 b_story_close(&state);if(timer){lv_timer_delete(timer);timer=NULL;}
 if(root){lv_obj_t *r=root;lv_obj_delete(r);}
 tour_exit_callback=NULL;exit_ctx=NULL;
}
static void leave(lv_event_t *e){(void)e;void(*cb)(void*)=tour_exit_callback;void *ctx=exit_ctx;ui_adventure_b_close();if(cb)cb(ctx);}
void ui_adventure_b_next(void){if(!root)return;if(state.beat==B_BEAT_COUNT-1)b_story_start(&state);else b_story_next(&state);last_tick=lv_tick_get();restyle();}
void ui_adventure_b_skip(void){if(!root)return;b_story_skip(&state);last_tick=lv_tick_get();restyle();}
static void next(lv_event_t *e){(void)e;ui_adventure_b_next();}
static void skip(lv_event_t *e){(void)e;ui_adventure_b_skip();}
static lv_obj_t *button(int x,int w,const char *text,lv_event_cb_t cb)
{
 lv_obj_t *b=lv_button_create(root);lv_obj_set_pos(b,x,192);lv_obj_set_size(b,w,48);
 lv_obj_set_style_radius(b,0,0);lv_obj_set_style_bg_color(b,lv_color_hex(0x172638),0);
 lv_obj_set_style_border_width(b,1,0);lv_obj_set_style_border_color(b,lv_color_hex(0x5a7979),0);
 lv_obj_set_style_pad_all(b,0,0);lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,NULL);
 lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);lv_obj_set_style_text_color(l,lv_color_hex(0xfff2d8),0);lv_obj_center(l);return l;
}
void ui_adventure_b_open(lv_obj_t *screen,void(*exit_cb)(void *),void *ctx)
{
 ui_adventure_b_close();tour_exit_callback=exit_cb;exit_ctx=ctx;b_story_start(&state);
 root=lv_obj_create(screen);lv_obj_remove_style_all(root);lv_obj_set_pos(root,0,0);lv_obj_set_size(root,320,240);
 lv_obj_set_style_bg_color(root,lv_color_hex(0x172638),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
 lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(root,destroyed,LV_EVENT_DELETE,NULL);
 canvas=lv_obj_create(root);lv_obj_remove_style_all(canvas);lv_obj_set_size(canvas,320,192);lv_obj_set_pos(canvas,0,0);
 lv_obj_remove_flag(canvas,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(canvas,draw,LV_EVENT_DRAW_MAIN,NULL);
 mic=voice_listening_widget_create_sized(canvas,38);lv_obj_set_pos(mic,147,151);voice_listening_widget_set_link(mic,VOICE_WIDGET_LINK_OK);
 for(unsigned i=0;i<lv_obj_get_child_count(mic);i++)lv_anim_delete(lv_obj_get_child(mic,i),NULL);
 lv_obj_remove_flag(mic,LV_OBJ_FLAG_CLICKABLE);
 title=lv_label_create(root);lv_obj_set_pos(title,8,3);lv_obj_set_width(title,304);lv_obj_set_style_text_font(title,&lv_font_montserrat_14,0);lv_obj_set_style_text_color(title,lv_color_hex(0x172638),0);
 panel=lv_obj_create(root);lv_obj_remove_style_all(panel);lv_obj_set_pos(panel,8,23);lv_obj_set_size(panel,304,57);lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);lv_obj_set_style_pad_all(panel,5,0);lv_obj_remove_flag(panel,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 caption=lv_label_create(panel);lv_obj_set_width(caption,290);lv_obj_set_style_text_font(caption,&lv_font_montserrat_20,0);lv_obj_set_style_text_line_space(caption,3,0);lv_label_set_long_mode(caption,LV_LABEL_LONG_WRAP);lv_obj_center(caption);
 tail=lv_obj_create(root);lv_obj_remove_style_all(tail);lv_obj_set_pos(tail,43,78);lv_obj_set_size(tail,9,7);lv_obj_set_style_bg_opa(tail,LV_OPA_COVER,0);lv_obj_remove_flag(tail,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 progress=lv_label_create(root);lv_obj_set_pos(progress,8,183);lv_obj_set_style_bg_color(progress,lv_color_hex(0x172638),0);lv_obj_set_style_bg_opa(progress,LV_OPA_COVER,0);lv_obj_set_style_text_color(progress,lv_color_hex(0xfff2d8),0);lv_obj_set_style_text_font(progress,&lv_font_montserrat_12,0);
 button(0,76,"EXIT",leave);skip_label=button(76,132,"SKIP INTRO",skip);next_label=button(208,112,"NEXT >",next);
#if B_ART_PLACEHOLDER
 lv_obj_t *warn=lv_label_create(root);lv_label_set_text(warn,"SCENERY PLACEHOLDER");lv_obj_set_pos(warn,8,88);lv_obj_set_style_text_color(warn,lv_color_white(),0);
#endif
 last_tick=lv_tick_get();timer=lv_timer_create(tick,100,NULL);restyle();
}
bool ui_adventure_b_is_open(void){return root!=NULL && state.active;}
void ui_adventure_b_seek(unsigned beat,uint32_t elapsed_ms){if(!root)return;state.beat=beat<B_BEAT_COUNT?beat:B_BEAT_COUNT-1;state.elapsed=0;b_story_advance(&state,elapsed_ms);last_tick=lv_tick_get();restyle();}
unsigned ui_adventure_b_beat(void){return state.beat;}
unsigned ui_adventure_b_owned_timers(void){return timer?1:0;}
