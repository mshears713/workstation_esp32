#include "ui_audio_test.h"
#include "ui_theme.h"
static lv_obj_t *page,*message;
static ui_audio_action_cb_t callback;
static void *callback_ctx;
static void pressed(lv_event_t *e)
{if(callback)callback((ui_audio_action_t)(uintptr_t)lv_event_get_user_data(e),callback_ctx);}
static void button(const char *title,int x,int y,int w,int h,ui_audio_action_t action)
{
    lv_obj_t *b=lv_button_create(page);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
    lv_obj_add_event_cb(b,pressed,LV_EVENT_CLICKED,(void *)(uintptr_t)action);
    lv_obj_t *label=lv_label_create(b);lv_label_set_text(label,title);
    lv_obj_set_style_text_font(label,&lv_font_montserrat_20,0);lv_obj_center(label);
}
void ui_audio_test_open(lv_obj_t *screen,ui_audio_action_cb_t cb,void *ctx)
{
    if(page)return;
    callback=cb;callback_ctx=ctx;
    page=lv_obj_create(screen);lv_obj_remove_style_all(page);lv_obj_set_size(page,320,240);
    lv_obj_set_style_bg_color(page,UI_BG,0);lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);
    lv_obj_remove_flag(page,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(page,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label=lv_label_create(page);lv_label_set_text(label,"AUDIO TEST");
    lv_obj_set_style_text_font(label,&lv_font_montserrat_20,0);lv_obj_align(label,LV_ALIGN_TOP_MID,0,12);
    message=lv_label_create(page);lv_label_set_text(message,"Choose a scene");
    lv_obj_set_style_text_font(message,&lv_font_montserrat_14,0);lv_obj_set_style_text_align(message,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_width(message,296);lv_obj_set_pos(message,12,46);
    button("NEWTON",12,78,142,76,UI_AUDIO_GARDEN);button("LINCOLN",166,78,142,76,UI_AUDIO_LINCOLN);
    label=lv_label_create(page);lv_label_set_text(label,"Local SD music, effects and speech");
    lv_obj_set_style_text_font(label,&lv_font_montserrat_12,0);lv_obj_align(label,LV_ALIGN_TOP_MID,0,163);
    button("BACK",12,184,296,48,UI_AUDIO_BACK);
}
void ui_audio_test_update(const ui_audio_test_view_t *view)
{if(page&&view)lv_label_set_text(message,view->message?view->message:"Choose a scene");}
void ui_audio_test_close(void)
{if(page)lv_obj_delete(page);page=message=NULL;callback=NULL;callback_ctx=NULL;}
