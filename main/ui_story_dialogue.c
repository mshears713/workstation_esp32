#include "ui_story_dialogue.h"
static lv_obj_t *shape(lv_obj_t *p,int x,int y,int w,int h,uint32_t color) {
 lv_obj_t *o=lv_obj_create(p);lv_obj_remove_style_all(o);
 lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
story_dialogue_t ui_story_dialogue_create(lv_obj_t *parent,story_dialogue_style_t style,const char *text,int x,int y,int width,int height) {
 story_dialogue_t d={0};uint32_t paper=0xF3E9CF,ink=0x25303D;int radius=10,border=1;
 if(style==STORY_THOUGHT)radius=20;
 if(style==STORY_SHOUT){radius=2;border=3;paper=0xFFE3A1;}
 if(style==STORY_CAPTION){radius=2;paper=0x202B45;ink=0xFFF0CB;}
 if(style==STORY_THOUGHT){d.tail1=shape(parent,x+width+4,y+18,8,8,paper);d.tail2=shape(parent,x+width+20,y+25,5,5,paper);lv_obj_set_style_radius(d.tail1,LV_RADIUS_CIRCLE,0);lv_obj_set_style_radius(d.tail2,LV_RADIUS_CIRCLE,0);}
 if(style==STORY_SPEECH && width<280){d.tail1=shape(parent,x+width-6,y+19,12,12,paper);lv_obj_set_style_transform_rotation(d.tail1,450,0);}
 d.panel=shape(parent,x,y,width,height,paper);lv_obj_set_style_radius(d.panel,radius,0);
 lv_obj_set_style_border_width(d.panel,border,0);lv_obj_set_style_border_color(d.panel,lv_color_hex(0xD2AD73),0);
 d.text=lv_label_create(d.panel);lv_label_set_text(d.text,text);lv_obj_set_pos(d.text,10,8);lv_obj_set_width(d.text,width-20);
 lv_obj_set_style_text_color(d.text,lv_color_hex(ink),0);lv_obj_set_style_text_font(d.text,&lv_font_montserrat_14,0);lv_label_set_long_mode(d.text,LV_LABEL_LONG_WRAP);
 return d;
}
void ui_story_dialogue_set_visible(story_dialogue_t *d,bool visible) {
 lv_obj_t *objects[]={d->panel,d->tail1,d->tail2};
 for(unsigned i=0;i<3;i++)if(objects[i]){if(visible)lv_obj_remove_flag(objects[i],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(objects[i],LV_OBJ_FLAG_HIDDEN);}
}
