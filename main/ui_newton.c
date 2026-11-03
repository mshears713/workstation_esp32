#include "ui_newton.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t *page, *stage, *tree, *person, *fruit[NEWTON_FRUITS];
static lv_obj_t *person_hit, *tree_hit, *status_label, *wind_btn, *speak_btn;
static const ui_newton_art_t *art;
static ui_newton_cb_t callback;
static void *callback_ctx;
static bool fixtures;
static char last_status[160];
static const char *caption(newton_action_t action)
{
    static const char *captions[]={"Tap Newton or the apple tree", "Looking to the sky", "A moment to think",
        "Taking a stroll", "Back to the garden", "Light and colour", "Let's investigate", "Oh! An apple!"};
    return (unsigned)action<8?captions[action]:captions[0];
}
static bool image_valid(const lv_image_dsc_t *im)
{
    return im && im->header.w && im->header.h && im->header.w <= 320 &&
            im->header.h <= 240 && im->data && im->data_size;
}
static bool frames_valid(newton_frames_t f)
{
    if (!f.images || !f.count || f.count > 64 || f.frame_ms < 40 || f.frame_ms > 2000) return false;
    for (unsigned i=0;i<f.count;i++) {
        if (!image_valid(f.images[i])) return false;
        if (f.images[i]->header.w != f.images[0]->header.w || f.images[i]->header.h != f.images[0]->header.h) return false;
    }
    return true;
}
bool ui_newton_art_valid(const ui_newton_art_t *a)
{
    if (!a || !a->native_verified || !a->background || !a->background_count ||
            a->background_count > 16 || !frames_valid(a->tree) || !frames_valid(a->fruit) ||
            a->tree.count != 25 || a->fruit.count != 25 || !frames_valid(a->caution) ||
            a->ground_y > 192 || a->ground_y < 120 || a->tree_x < 0 ||
            a->tree_x + a->tree.images[0]->header.w > 320) return false;
    for(unsigned i=0;i<8;i++) {
        if (!frames_valid(a->person[i]) || a->person[i].images[0]->header.h > a->ground_y ||
                a->person[i].images[0]->header.w > 80) return false;
        int half=(int)a->person[i].images[0]->header.w/2;
        if(a->person_foot_x-half<0 || a->person_foot_x+NEWTON_WALK_DISTANCE+half>320) return false;
    }
    for(unsigned i=0;i<a->background_count;i++) {
        newton_tile_t tile=a->background[i];
        if(!image_valid(tile.image) || tile.x<0 || tile.y < -24 ||
                tile.x+tile.image->header.w>320 || tile.y+tile.image->header.h>240) return false;
    }
    if(a->tree_origin.x<0 || a->tree_origin.y<0 ||
            a->tree_origin.x+a->tree.images[0]->header.w>320 ||
            a->tree_origin.y+a->tree.images[0]->header.h>192) return false;
    return true;
}
static void event(lv_event_t *e)
{
    if(callback) callback((ui_newton_action_t)(uintptr_t)lv_event_get_user_data(e),callback_ctx);
}
static void inert(lv_obj_t *o)
{
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
}
static lv_obj_t *plain(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color)
{
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);inert(o);return o;
}
static lv_obj_t *hit(lv_obj_t *parent,int x,int y,int w,int h,ui_newton_action_t action)
{
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,(void *)(uintptr_t)action);return o;
}
static lv_obj_t *button(const char *title,int x,ui_newton_action_t action)
{
    lv_obj_t *o=hit(page,x,192,104,48,action);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x213344),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_t *l=lv_label_create(o);lv_label_set_text(l,title);
    lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(l,lv_color_white(),0);lv_obj_center(l);inert(l);return o;
}
static const lv_image_dsc_t *frame(newton_frames_t f,uint32_t ms)
{
    return f.images[(ms/f.frame_ms)%f.count];
}
void ui_newton_open(lv_obj_t *screen,const ui_newton_art_t *a,ui_newton_cb_t cb,void *ctx)
{
    if(page)return;
    callback=cb;callback_ctx=ctx;art=a;fixtures=!ui_newton_art_valid(a);
    last_status[0]=0;
    page=plain(screen,0,0,320,240,0x101820);
    /* Prevent presses outside hit regions reaching the underlying HOME/menu. */
    lv_obj_add_flag(page,LV_OBJ_FLAG_CLICKABLE);
    stage=plain(page,0,0,320,192,0x243746);
    if(fixtures) {
        tree=plain(stage,24,40,112,136,0x446677);
        person=plain(stage,156,112,48,64,0x998855);
        lv_obj_t *l=lv_label_create(tree);lv_label_set_text(l,"TREE\nFIXTURE");lv_obj_center(l);
        l=lv_label_create(person);lv_label_set_text(l,"POSE");lv_obj_center(l);
        for(unsigned i=0;i<NEWTON_FRUITS;i++)fruit[i]=plain(stage,0,0,10,10,0xFFCC55);
    } else {
        for(unsigned i=0;i<a->background_count;i++) {
            lv_obj_t *im=lv_image_create(stage);lv_image_set_src(im,a->background[i].image);
            lv_obj_set_pos(im,a->background[i].x,a->background[i].y);inert(im);
        }
        tree=lv_image_create(stage);person=lv_image_create(stage);inert(tree);inert(person);
        for(unsigned i=0;i<NEWTON_FRUITS;i++) {
            fruit[i]=lv_image_create(stage);lv_image_set_src(fruit[i],a->fruit.images[0]);inert(fruit[i]);
        }
    }
    tree_hit=hit(stage,12,30,130,150,UI_NEWTON_TREE);
    person_hit=hit(stage,156,112,48,64,UI_NEWTON_SPEAK);
    status_label=lv_label_create(stage);lv_obj_set_pos(status_label,4,2);lv_obj_set_size(status_label,312,32);
    lv_obj_set_style_text_font(status_label,&lv_font_montserrat_12,0);
    lv_obj_set_style_text_color(status_label,lv_color_white(),0);inert(status_label);
    lv_obj_set_style_bg_color(status_label,lv_color_hex(0x172B30),0);
    lv_obj_set_style_bg_opa(status_label,220,0);
    button("BACK",0,UI_NEWTON_BACK);wind_btn=button("WIND",108,UI_NEWTON_WIND);
    speak_btn=button("SPEAK",216,UI_NEWTON_SPEAK);
}
void ui_newton_update(const newton_scene_t *s,const char *message)
{
    if(!page || !s)return;
    int ground=fixtures?176:art->ground_y;
    int px=(fixtures?180:art->person_foot_x)+s->person_offset;
    int pw=48,ph=64;
    bool approaching=s->falling>=0 && s->fall_ms<s->approach_ms;
    uint32_t after=s->fall_ms>=s->approach_ms?s->fall_ms-s->approach_ms:0;
    bool bonk=s->falling>=0 && !approaching && after>=595 && after<995;
    int foot_y=fixtures?64:art->person_foot_anchor.y;
    newton_action_t pose=bonk?NEWTON_BONK:approaching?NEWTON_WALK_OUT:s->action;
    if(!fixtures) {
        newton_frames_t f=art->person[pose];uint32_t clock=s->action_ms;
        if(approaching)clock=s->fall_ms;
        else if(bonk)clock=after-595;
        else if(pose==NEWTON_WALK_BACK && s->action_ms>=1600) { f=art->caution;clock-=1600; }
        else if(pose==NEWTON_WALK_OUT && s->action_ms>=1600)f=art->person[NEWTON_IDLE];
        const lv_image_dsc_t *im=frame(f,clock);
        lv_image_set_src(person,im);pw=im->header.w;ph=im->header.h;
        unsigned tree_index=s->wind?1+(s->wind_ms/180)%5:0;
        if(s->falling>=0 && !approaching && after>=680)tree_index=21+(after-680)/150%4;
        im=art->tree.images[tree_index];lv_image_set_src(tree,im);
        lv_obj_set_pos(tree,art->tree_origin.x,art->tree_origin.y);
        lv_obj_set_pos(tree_hit,art->tree_origin.x,art->tree_origin.y);
        lv_obj_set_size(tree_hit,im->header.w,im->header.h);
    } else lv_obj_set_x(tree,24+(s->wind?(int)(s->wind_ms/250%3)-1:0));
    lv_obj_set_pos(person,px-(fixtures?pw/2:art->person_foot_anchor.x),ground-foot_y);
    lv_obj_set_pos(person_hit,px-(pw<48?48:pw)/2,ground-foot_y);
    lv_obj_set_size(person_hit,pw<48?48:pw,ph<48?48:ph);
    for(unsigned i=0;i<NEWTON_FRUITS;i++) {
        newton_fruit_phase_t phase=newton_scene_fruit_phase(s,i);
        unsigned index=0;
        if(phase==NEWTON_FALLING)index=6+(after/85>7?7:after/85);
        else if(phase==NEWTON_ON_GROUND)index=14;
        else if(phase==NEWTON_REGROWING)index=15+(NEWTON_GROW_ANIM_MS-s->saved.regrow_left_ms[i])/140;
        if(!fixtures) {
            lv_image_set_src(fruit[i],art->fruit.images[index]);
            lv_obj_set_pos(fruit[i],art->fruit_origin.x,art->fruit_origin.y);
        } else {
            lv_obj_set_pos(fruit[i],phase==NEWTON_FALLING?px-5:50,phase==NEWTON_FALLING?ground-ph-10:65);
            lv_obj_set_style_opa(fruit[i],phase==NEWTON_ON_GROUND?0:255,0);
        }
    }
    lv_obj_move_foreground(person); /* outside actor never behind tree/backing */
    if(s->falling>=0)lv_obj_move_foreground(fruit[s->falling]);
    lv_obj_move_foreground(status_label);
    lv_obj_set_state(wind_btn,LV_STATE_DISABLED,!s->ready);
    lv_obj_set_state(speak_btn,LV_STATE_DISABLED,!s->ready);
    lv_obj_set_state(person_hit,LV_STATE_DISABLED,!s->ready);
    lv_label_set_text(lv_obj_get_child(wind_btn,0),s->wind?"WIND ON":"WIND");
    lv_label_set_text(lv_obj_get_child(speak_btn,0),s->queued_voice?"QUEUED":s->speaking?"SPEAKING":"SPEAK");
    char status[160];
    if(!s->ready)snprintf(status,sizeof(status),"%s%s",fixtures?"TEST FIXTURES\n":"",message?message:"Audio unavailable");
    else {
        bool audio_error=message && (strstr(message,"missing") || strstr(message,"failed") || strstr(message,"unreadable"));
        snprintf(status,sizeof(status),"%s%s%s",fixtures?"TEST FIXTURES - NOT FINAL ART\n":"Newton's garden - playful legend\n",
            s->storage_failed?"Fruit memory unavailable":s->request_failed?"Voice unavailable":audio_error?message:caption(bonk?NEWTON_BONK:approaching?NEWTON_WALK_OUT:s->action),s->queued_voice?" / one queued":"");
    }
    if(strcmp(last_status,status)) { strcpy(last_status,status);lv_label_set_text(status_label,status); }
}
void ui_newton_close(void)
{
    if(page)lv_obj_delete(page);
    page=stage=tree=person=person_hit=tree_hit=status_label=wind_btn=speak_btn=NULL;
    for(unsigned i=0;i<NEWTON_FRUITS;i++)fruit[i]=NULL;
    art=NULL;callback=NULL;callback_ctx=NULL;
}
