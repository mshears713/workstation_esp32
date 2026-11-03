#include "ui_lincoln.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t *page,*actor,*cabin,*label,*person_hit,*cabin_hit,*speak_btn;
static const ui_lincoln_art_t *assets;
static ui_lincoln_cb_t callback;
static void *context;
static unsigned last_actor=UINT32_MAX,last_cabin=UINT32_MAX;
static char last_label[128];
unsigned ui_lincoln_actor_frame(const lincoln_scene_t *s) { return lincoln_scene_actor_frame(s); }
unsigned ui_lincoln_cabin_frame(const lincoln_scene_t *s) { return lincoln_scene_cabin_frame(s); }
static bool valid_group(const lv_image_dsc_t *const *images,unsigned count,unsigned w,unsigned h)
{
    if(!images)return false;
    for(unsigned i=0;i<count;i++)if(!images[i]||!images[i]->data||!images[i]->data_size||
        images[i]->header.w!=w||images[i]->header.h!=h)return false;
    return true;
}
bool ui_lincoln_art_valid(const ui_lincoln_art_t *a)
{
    return a&&a->native_persistence_verified&&valid_group(a->backdrop,8,80,96)&&
        valid_group(a->actor,54,64,96)&&valid_group(a->cabin,16,112,112);
}
static void inert(lv_obj_t *o)
{ lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE); }
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h)
{
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
static void event(lv_event_t *e)
{ if(callback)callback((ui_lincoln_event_t)(uintptr_t)lv_event_get_user_data(e),context); }
static lv_obj_t *hit(lv_obj_t *parent,int x,int y,int w,int h,ui_lincoln_event_t kind)
{ lv_obj_t *o=box(parent,x,y,w,h);lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,(void *)(uintptr_t)kind);return o; }
static lv_obj_t *button(const char *text,int x,ui_lincoln_event_t kind)
{
    lv_obj_t *o=hit(page,x,192,104,48,kind);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x302c36),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_t *l=lv_label_create(o);lv_label_set_text(l,text);lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(l,lv_color_white(),0);inert(l);lv_obj_center(l);return o;
}
void ui_lincoln_open(lv_obj_t *screen,const ui_lincoln_art_t *art,ui_lincoln_cb_t cb,void *ctx)
{
    if(page)return;
    assets=art;callback=cb;context=ctx;last_actor=last_cabin=UINT32_MAX;last_label[0]=0;
    page=box(screen,0,0,320,240);lv_obj_add_flag(page,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(page,lv_color_hex(0x14121b),0);lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);
    lv_obj_t *stage=box(page,0,0,320,192);inert(stage);
    if(ui_lincoln_art_valid(art)) {
        for(unsigned i=0;i<8;i++){
            lv_obj_t *o=lv_image_create(stage);lv_image_set_src(o,art->backdrop[i]);
            lv_obj_set_pos(o,(i%4)*80,(i/4)*96);inert(o);
        }
        cabin=lv_image_create(stage);lv_obj_set_pos(cabin,18,66);inert(cabin);
        actor=lv_image_create(stage);lv_obj_set_pos(actor,188,92);inert(actor);
    }
    cabin_hit=hit(stage,18,66,112,112,UI_LINCOLN_CABIN);
    person_hit=hit(stage,188,92,64,96,UI_LINCOLN_PERSON);
    label=lv_label_create(stage);lv_obj_set_pos(label,4,2);lv_obj_set_size(label,312,32);inert(label);
    lv_obj_set_style_text_font(label,&lv_font_montserrat_12,0);lv_obj_set_style_text_color(label,lv_color_white(),0);
    lv_obj_set_style_bg_color(label,lv_color_hex(0x211d30),0);lv_obj_set_style_bg_opa(label,220,0);
    button("BACK",0,UI_LINCOLN_BACK);button("CABIN",108,UI_LINCOLN_CABIN);
    speak_btn=button("READ",216,UI_LINCOLN_PERSON);
}
void ui_lincoln_update(const lincoln_scene_t *s,const char *message)
{
    if(!page||!s)return;
    bool valid=ui_lincoln_art_valid(assets);
    unsigned actor_frame=ui_lincoln_actor_frame(s),cabin_frame=ui_lincoln_cabin_frame(s);
    if(valid){
        if(actor_frame!=last_actor){lv_image_set_src(actor,assets->actor[actor_frame]);last_actor=actor_frame;}
        if(cabin_frame!=last_cabin){lv_image_set_src(cabin,assets->cabin[cabin_frame]);last_cabin=cabin_frame;}
    }
    lv_obj_set_state(person_hit,LV_STATE_DISABLED,!s->ready);
    lv_obj_set_state(cabin_hit,LV_STATE_DISABLED,!s->ready);
    lv_obj_set_state(speak_btn,LV_STATE_DISABLED,!s->ready);
    lv_label_set_text(lv_obj_get_child(speak_btn,0),s->phase==LINCOLN_IDLE?"READ":s->phase==LINCOLN_STOPPING?"STOPPING":"STOP");
    const char *phase_names[]={"Tap Lincoln to read","Removing hat","Looking inside","Unfolding paper","Waiting for audio","Reading - tap to stop","Putting paper away","Stopping audio"};
    char text[128];snprintf(text,sizeof(text),"LINCOLN - v2 review\n%s",!valid?"Missing native art":
        s->failed?"Audio unavailable - retry":message?message:phase_names[s->phase]);
    if(strcmp(text,last_label)){strcpy(last_label,text);lv_label_set_text(label,text);}
}
void ui_lincoln_close(void)
{
    if(page)lv_obj_delete(page);
    page=actor=cabin=label=person_hit=cabin_hit=speak_btn=NULL;assets=NULL;callback=NULL;context=NULL;
}
