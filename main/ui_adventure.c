/* Architecture Adventure A — native Pixelorama artwork.
 * Independent simulator module. No microphone, audio, network or OTA calls:
 * narration is the app's job, told which scene is showing via the scene
 * callback (ui_adventure_set_scene_cb).
 * Background panels are exported natively and assembled at exact 1:1 scale.
 */
#include "ui_adventure.h"
#include "ui_story_dialogue.h"
#include "voice_listening_widget.h"
#include "mouse_pixels.h"
#include "house_pixels.h"
#include "tour_climb_pixels.h"
#include "tour_ladder_pixels.h"
#if __has_include("tour_pixels.h")
#include "tour_pixels.h"
#else
#define TOUR_NATIVE_ASSETS 0
#endif
#include <stdint.h>
#include <string.h>

static struct {
 lv_obj_t *root,*stage,*mouse,*dialogue,*text,*next_label,*van,*portal,*mummy,*climber,*tail1,*tail2;
 lv_timer_t *timer;
 void (*exit_cb)(void *); void *ctx;
 adventure_scene_t scene,portal_target;
 uint32_t started,last_touch;
 unsigned ticks;
 int intro_line;
 bool touch_seen;
} a;
static lv_image_dsc_t mouse_frames[6];
/* Outside `a` on purpose: ui_adventure_open() clears `a`. */
static void (*scene_cb)(adventure_scene_t, void *);
static void *scene_cb_ctx;
void ui_adventure_set_scene_cb(void (*cb)(adventure_scene_t scene, void *ctx), void *ctx) { scene_cb=cb;scene_cb_ctx=ctx; }
static const char *titles[ADVENTURE_SCENE_COUNT]={
 "A LITTLE ADVENTURE","MEET YOUR GUIDE","ALL ABOARD","THROUGH TIME",
 "EGYPT / THE PYRAMIDS","EGYPT / BUILDING IN STONE","A FRIENDLY SURPRISE",
 "GREECE / THE ACROPOLIS","GREECE / COUNT THE COLUMNS",
 "FRANCE / GOTHIC ARCHES","FRANCE / WINDOWS OF LIGHT",
 "A TINY ENCORE","ONE LAST LOOK","HOME AGAIN"
};
static const char *lines[ADVENTURE_SCENE_COUNT]={
 "A small mouse.\nA very big world.",
 "Hello! I live here.\nCome explore with me!",
 "Hop into my little van.\nOur adventure starts now!",
 "Through a make-believe portal.\nWhere will we land?",
 "Limestone blocks form\nmost of these pyramids.",
 "Much of the stone came\nfrom the Giza plateau.",
 "A friendly mummy!\nA make-believe meeting.",
 "The Parthenon's outer\ncolumns are Doric.",
 "Eight columns at each end;\nseventeen along each side.",
 "Gothic buildings feature\npointed stone arches.",
 "Chartres is famous for\nits stained-glass windows.",
 "La-la-la! A tiny concert.\nJust pictures, no sound.",
 "Up the ladder for one\nlast look at those windows!",
 "Three places. So much to see!\nShall we go again?"
};
static const char *intro_lines[]={
 "Hello! I live in this little house.\nI'm your curious mouse guide.",
 "I like big buildings, tiny details,\nand a little make-believe.",
 "Today: pyramids, columns,\nand windows full of colour.",
 "We'll drive through a time portal.\nThat's the pretend part!",
 "Each stop has two little facts.\nNo long history lesson here.",
 "Ready? Tap Next to set off.\nSkip takes us straight to Egypt."
};
static lv_obj_t *box(lv_obj_t *p,int x,int y,int w,int h,uint32_t c) {
 lv_obj_t *o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(c),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
static lv_obj_t *label(lv_obj_t *p,const char *s,int x,int y,int w,uint32_t c,const lv_font_t *font) {
 lv_obj_t *o=lv_label_create(p);lv_label_set_text(o,s);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
 lv_obj_set_style_text_color(o,lv_color_hex(c),0);lv_obj_set_style_text_font(o,font,0);
 lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);return o;
}
static lv_obj_t *sprite(lv_obj_t *p,const lv_image_dsc_t *src,int x,int y,int scale) {
 if(!src || !src->data)return NULL;
 lv_obj_t *o=lv_image_create(p);lv_image_set_src(o,src);lv_image_set_pivot(o,0,0);
 lv_image_set_scale(o,scale);lv_image_set_antialias(o,false);lv_obj_set_pos(o,x,y);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static void visible(lv_obj_t *o,bool yes) { if(o) {if(yes)lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);} }
static void root_deleted(lv_event_t *e) {
 (void)e;if(a.timer){lv_timer_delete(a.timer);a.timer=NULL;}
 a.root=NULL;a.stage=NULL;a.mouse=NULL;a.dialogue=NULL;a.text=NULL;a.next_label=NULL;
 a.van=NULL;a.portal=NULL;a.mummy=NULL;a.climber=NULL;a.tail1=NULL;a.tail2=NULL;
}
static void show_scene(void);
static void pose(uint32_t elapsed);
void ui_adventure_close(void) {
 if(a.timer){lv_timer_delete(a.timer);a.timer=NULL;}
 if(a.root)lv_obj_delete(a.root);
 a.root=NULL;a.stage=NULL;a.mouse=NULL;a.dialogue=NULL;a.text=NULL;a.next_label=NULL;
 a.van=NULL;a.portal=NULL;a.mummy=NULL;a.climber=NULL;a.tail1=NULL;a.tail2=NULL;
}
void ui_adventure_seek(adventure_scene_t scene) {
 if(!a.root || scene<0 || scene>=ADVENTURE_SCENE_COUNT)return;
 a.scene=scene;a.started=lv_tick_get();a.ticks=0;a.intro_line=-1;show_scene();pose(0);
 if(scene_cb)scene_cb(scene,scene_cb_ctx);
}
void ui_adventure_next(void) {
 if(!a.root)return;
 if(a.scene==ADVENTURE_PORTAL){ui_adventure_seek(a.portal_target);return;}
 if(a.scene==ADVENTURE_DEPART || a.scene==ADVENTURE_MUMMY || a.scene==ADVENTURE_GREECE_COLUMNS || a.scene==ADVENTURE_CLIMB){
  a.portal_target=a.scene==ADVENTURE_DEPART?ADVENTURE_EGYPT:a.scene==ADVENTURE_MUMMY?ADVENTURE_GREECE:a.scene==ADVENTURE_GREECE_COLUMNS?ADVENTURE_GOTHIC:ADVENTURE_END;
  ui_adventure_seek(ADVENTURE_PORTAL);return;
 }
 ui_adventure_seek(a.scene==ADVENTURE_END?ADVENTURE_HOME:(adventure_scene_t)(a.scene+1));
}
void ui_adventure_skip(void) {
 if(!a.root)return;
 if(a.scene==ADVENTURE_PORTAL){ui_adventure_seek(a.portal_target);return;}
 adventure_scene_t n=ADVENTURE_END;
 if(a.scene<ADVENTURE_EGYPT)n=ADVENTURE_EGYPT;
 else if(a.scene<ADVENTURE_GREECE)n=ADVENTURE_GREECE;
 else if(a.scene<ADVENTURE_GOTHIC)n=ADVENTURE_GOTHIC;
 else if(a.scene<ADVENTURE_SING)n=ADVENTURE_SING;
 ui_adventure_seek(n);
}
static void control_cb(lv_event_t *e) {
 intptr_t action=(intptr_t)lv_event_get_user_data(e);uint32_t now=lv_tick_get();
 /* Exit is an immediate escape, never swallowed by navigation debounce. */
 if(action!=2 && a.touch_seen && now-a.last_touch<180)return;
 a.last_touch=now;a.touch_seen=true;
 if(action==0)ui_adventure_next();else if(action==1)ui_adventure_skip();else {
  void (*cb)(void *)=a.exit_cb;void *ctx=a.ctx;ui_adventure_close();if(cb)cb(ctx);
 }
}
static int lerp(int x,int y,uint32_t ms,uint32_t duration) {return x+(y-x)*(int)(ms<duration?ms:duration)/(int)duration;}
static bool world_arrival(void) {return a.scene==ADVENTURE_EGYPT || a.scene==ADVENTURE_GREECE || a.scene==ADVENTURE_GOTHIC;}
static void pose(uint32_t elapsed) {
 if(!a.mouse)return;
 unsigned frame=(elapsed/150)%6;lv_image_set_src(a.mouse,&mouse_frames[frame]);
 int x=280,y=154;bool show=true;
 if(a.scene==ADVENTURE_HOME){y=101;show=elapsed>=600;x=lerp(48,172,elapsed>600?elapsed-600:0,2400);}
 else if(a.scene==ADVENTURE_INTRO){x=174;y=101-(elapsed/300)%2*2;int n=(int)(elapsed/10000);if(n>5)n=5;if(n!=a.intro_line){a.intro_line=n;lv_label_set_text(a.text,intro_lines[n]);}}
 else if(a.scene==ADVENTURE_DEPART){y=101;x=lerp(174,216,elapsed,1200);show=elapsed<1400;if(a.van)lv_obj_set_x(a.van,lerp(190,320,elapsed>1600?elapsed-1600:0,2400));}
 else if(a.scene==ADVENTURE_PORTAL){show=false;if(a.van)lv_obj_set_x(a.van,lerp(25,253,elapsed,1800));}
 else if(world_arrival()){show=elapsed>=4500;x=280;if(a.van){lv_obj_set_x(a.van,lerp(-64,242,elapsed,4300));lv_obj_set_y(a.van,lerp(154,126,elapsed>2800?elapsed-2800:0,1500));}}
 else if(a.scene==ADVENTURE_MUMMY){x=242;y=154;}
 else if(a.scene==ADVENTURE_SING){x=280;y=154-(elapsed/220)%3*2;}
 else if(a.scene==ADVENTURE_CLIMB){show=elapsed<1000;x=lerp(280,235,elapsed,1000);y=154;if(a.climber){visible(a.climber,elapsed>=1000);lv_image_set_src(a.climber,climb_frames[(elapsed/200)%4]);lv_obj_set_y(a.climber,lerp(154,47,elapsed>1000?elapsed-1000:0,6000));}}
 else if(a.scene==ADVENTURE_END){x=90;y=102;}
 lv_obj_set_pos(a.mouse,x,y);visible(a.mouse,show);
 bool text_on=!(a.scene==ADVENTURE_DEPART || a.scene==ADVENTURE_PORTAL || (world_arrival() && elapsed<4500) || (a.scene==ADVENTURE_HOME && elapsed<1400) || (a.scene==ADVENTURE_CLIMB && elapsed<1000));
 visible(a.dialogue,text_on);visible(a.tail1,text_on);visible(a.tail2,text_on);
#if TOUR_NATIVE_ASSETS
 if(a.van){lv_image_set_src(a.van,tour_van_frames[(elapsed/180)%2]);if(a.scene==ADVENTURE_PORTAL)lv_obj_set_style_opa(a.van,elapsed<1700?255:elapsed>=2000?0:255-(elapsed-1700)*255/300,0);}
 if(a.portal)lv_image_set_src(a.portal,tour_portal_frames[(elapsed/180)%4]);
 if(a.mummy)lv_image_set_src(a.mummy,tour_mummy_frames[(elapsed/500)%2]);
#endif
}
static void tick(lv_timer_t *t) {
 (void)t;if(!a.root)return;a.ticks++;uint32_t elapsed=lv_tick_get()-a.started;pose(elapsed);
 /* Entire journey can play itself; touch navigation always interrupts. */
 uint32_t duration=a.scene==ADVENTURE_HOME?3200:a.scene==ADVENTURE_INTRO?60000:
  a.scene==ADVENTURE_DEPART?4000:a.scene==ADVENTURE_PORTAL?2200:
  a.scene==ADVENTURE_END?0:a.scene==ADVENTURE_MUMMY?6500:
  a.scene==ADVENTURE_CLIMB?8500:a.scene==ADVENTURE_SING?6000:world_arrival()?12000:9000;
 if(duration && elapsed>=duration)ui_adventure_next();
}
static void dialogue(void) {
 story_dialogue_style_t style=STORY_SPEECH;
 if(a.scene==ADVENTURE_EGYPT || a.scene==ADVENTURE_EGYPT_STONE)style=STORY_THOUGHT;
 if(a.scene==ADVENTURE_MUMMY)style=STORY_SHOUT;
 if(a.scene==ADVENTURE_GOTHIC || a.scene==ADVENTURE_GOTHIC_GLASS || a.scene==ADVENTURE_CLIMB)style=STORY_CAPTION;
 bool world=(a.scene>=ADVENTURE_EGYPT && a.scene<=ADVENTURE_GOTHIC_GLASS)||a.scene==ADVENTURE_SING || a.scene==ADVENTURE_CLIMB;
 story_dialogue_t d=ui_story_dialogue_create(a.stage,style,lines[a.scene],8,135,world?220:304,53);
 a.dialogue=d.panel;a.text=d.text;a.tail1=d.tail1;a.tail2=d.tail2;
}
static void show_scene(void) {
 lv_obj_clean(a.stage);a.mouse=NULL;a.van=NULL;a.portal=NULL;a.mummy=NULL;a.climber=NULL;a.tail1=NULL;a.tail2=NULL;
#if TOUR_NATIVE_ASSETS
 const lv_image_dsc_t *bg=&tour_bg_home;
 if(a.scene>=ADVENTURE_EGYPT && a.scene<=ADVENTURE_MUMMY)bg=&tour_bg_egypt;
 if(a.scene>=ADVENTURE_GREECE && a.scene<=ADVENTURE_GREECE_COLUMNS)bg=&tour_bg_greece;
 if((a.scene>=ADVENTURE_GOTHIC && a.scene<=ADVENTURE_GOTHIC_GLASS)||a.scene==ADVENTURE_CLIMB||a.scene==ADVENTURE_SING)bg=&tour_bg_gothic;
 if(a.scene==ADVENTURE_PORTAL){if(a.portal_target==ADVENTURE_GREECE)bg=&tour_bg_egypt;else if(a.portal_target==ADVENTURE_GOTHIC)bg=&tour_bg_greece;else if(a.portal_target==ADVENTURE_END)bg=&tour_bg_gothic;}
 if(bg->data)sprite(a.stage,bg,0,0,256);else label(a.stage,"NATIVE BACKGROUND PENDING",10,90,300,0xE0D9C7,&lv_font_montserrat_14);
#else
 label(a.stage,"NATIVE BACKGROUND PENDING",10,90,300,0xE0D9C7,&lv_font_montserrat_14);
#endif
 lv_obj_t *band=box(a.stage,0,0,320,25,0x172330);lv_obj_set_style_bg_opa(band,220,0);
 label(band,titles[a.scene],9,5,305,0xFFE8B7,&lv_font_montserrat_14);
 bool home=a.scene<=ADVENTURE_DEPART || a.scene==ADVENTURE_END || (a.scene==ADVENTURE_PORTAL && a.portal_target==ADVENTURE_EGYPT);
 if(home){
  /* Existing 40px native house deliberately shown at 1.5x nearest-neighbour. */
  sprite(a.stage,&house_unoccupied,17,73,384);
  lv_obj_t *mic=voice_listening_widget_create_sized(a.stage,48);lv_obj_set_pos(mic,232,77);lv_obj_remove_flag(mic,LV_OBJ_FLAG_CLICKABLE);
 }
 a.mouse=sprite(a.stage,&mouse_frames[0],94,102,512);
#if TOUR_NATIVE_ASSETS
 if(a.scene==ADVENTURE_DEPART || a.scene==ADVENTURE_PORTAL || (a.scene>=ADVENTURE_EGYPT && a.scene<=ADVENTURE_GOTHIC_GLASS))a.van=sprite(a.stage,&tour_van_0,242,126,256);
 if(a.scene==ADVENTURE_DEPART && a.van)lv_obj_set_pos(a.van,190,102);
 if(a.scene==ADVENTURE_PORTAL && a.van)lv_obj_set_pos(a.van,25,a.portal_target==ADVENTURE_EGYPT?102:126);
 if(a.scene==ADVENTURE_PORTAL)a.portal=sprite(a.stage,&tour_portal_0,263,a.portal_target==ADVENTURE_EGYPT?70:94,256);
 if(a.scene==ADVENTURE_MUMMY){a.mummy=sprite(a.stage,&tour_mummy_0,284,140,256);visible(a.van,false);}
#endif
 if(a.scene==ADVENTURE_CLIMB){
  lv_obj_t *clip=box(a.stage,237,47,18,139,0xFFFFFF);lv_obj_set_style_bg_opa(clip,LV_OPA_TRANSP,0);
  sprite(clip,&caretaker_ladder,0,0,256);a.climber=sprite(a.stage,climb_frames[0],235,154,256);
 }
 if(a.scene==ADVENTURE_SING){lv_obj_t *mic=voice_listening_widget_create_sized(a.stage,48);lv_obj_set_pos(mic,242,120);lv_obj_remove_flag(mic,LV_OBJ_FLAG_CLICKABLE);lv_obj_t *song=box(a.stage,250,45,52,31,0xF3E9CF);lv_obj_set_style_radius(song,10,0);label(song,"LA!",7,4,40,0x25303D,&lv_font_montserrat_20);}
 dialogue();if(a.next_label)lv_label_set_text(a.next_label,a.scene==ADVENTURE_END?"Replay":"Next");
}
void ui_adventure_open(lv_obj_t *screen,void (*exit_cb)(void *),void *ctx) {
 ui_adventure_close();memset(&a,0,sizeof(a));a.exit_cb=exit_cb;a.ctx=ctx;a.portal_target=ADVENTURE_EGYPT;
 for(unsigned i=0;i<6;i++)mouse_frames[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=16,.h=16,.stride=64},.data_size=sizeof(mouse_pixels[i]),.data=(const uint8_t *)mouse_pixels[i]};
 a.root=box(screen,0,0,320,240,0x101820);lv_obj_add_event_cb(a.root,root_deleted,LV_EVENT_DELETE,NULL);
 a.stage=box(a.root,0,0,320,192,0x182532);
 static const char *names[]={"Next","Skip","Exit"};
 for(int i=0;i<3;i++){
  lv_obj_t *b=lv_button_create(a.root);lv_obj_set_pos(b,4+i*106,192);lv_obj_set_size(b,100,48);
  lv_obj_set_style_radius(b,8,0);lv_obj_set_style_bg_color(b,lv_color_hex(0xE5C58F),0);lv_obj_set_style_text_color(b,lv_color_hex(0x172533),0);
  lv_obj_add_event_cb(b,control_cb,LV_EVENT_CLICKED,(void *)(intptr_t)i);
  lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,names[i]);lv_obj_set_style_text_font(l,&lv_font_montserrat_20,0);lv_obj_center(l);if(i==0)a.next_label=l;
 }
 ui_adventure_seek(ADVENTURE_HOME);a.timer=lv_timer_create(tick,50,NULL);
}
adventure_scene_t ui_adventure_scene(void){return a.scene;}
bool ui_adventure_is_open(void){return a.root!=NULL;}
unsigned ui_adventure_timer_count(void){return a.timer?1:0;}

const lv_image_dsc_t *ui_adventure_original_asset(tour_original_asset_t asset, unsigned frame) {
 if(asset==TOUR_ASSET_HOME)return &tour_bg_home;
 if(asset==TOUR_ASSET_HOUSE)return &house_unoccupied;
 if(asset==TOUR_ASSET_VAN)return tour_van_frames[frame%2];
 unsigned i=frame%6;
 mouse_frames[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=16,.h=16,.stride=64},.data_size=sizeof(mouse_pixels[i]),.data=(const uint8_t *)mouse_pixels[i]};
 return &mouse_frames[i];
}
