#include "ui_audio_test.h"
#include <assert.h>
#include <stdio.h>
static ui_audio_test_view_t view;
static unsigned hits[5];
static lv_point_t point;
static lv_indev_state_t pressed_state;
static void action(ui_audio_action_t command,void *ctx)
{
    (void)ctx; hits[command]++;
    if(command==UI_AUDIO_BACK){ui_audio_test_close();return;}
    if(command==UI_AUDIO_WIND)view.wind=!view.wind;
    if(command==UI_AUDIO_VOICE)view.voice=true;
    ui_audio_test_update(&view);
}
static void read_pointer(lv_indev_t *dev,lv_indev_data_t *data)
{
    (void)dev;data->point=point;data->state=pressed_state;
}
static void tap(lv_indev_t *dev,int x,int y)
{
    point=(lv_point_t){x,y};pressed_state=LV_INDEV_STATE_PRESSED;
    lv_tick_inc(40);lv_indev_read(dev);
    pressed_state=LV_INDEV_STATE_RELEASED;
    lv_tick_inc(40);lv_indev_read(dev);
}
static int timers(void)
{
    int n=0;for(lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t))n++;return n;
}
void ws_render(lv_obj_t *screen)
{
    lv_indev_t *input=lv_indev_create();
    lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input,lv_display_get_default());
    lv_indev_set_read_cb(input,read_pointer);
    lv_indev_set_mode(input,LV_INDEV_MODE_EVENT);
    unsigned base_children=lv_obj_get_child_count(screen);
    int base_timers=timers();
    for(int i=0;i<200;i++){
        ui_audio_test_open(screen,action,NULL);ui_audio_test_close();
        assert(lv_obj_get_child_count(screen)==base_children);
        assert(timers()==base_timers);
    }
    ui_audio_test_open(screen,action,NULL);
    view=(ui_audio_test_view_t){.message="Choose a scene",.next_voice=1};
    ui_audio_test_update(&view);lv_obj_update_layout(screen);
    tap(input,80,118);tap(input,230,118);
    assert(hits[UI_AUDIO_WIND]==0 && hits[UI_AUDIO_VOICE]==0);
    assert(hits[UI_AUDIO_GARDEN]==1 && hits[UI_AUDIO_LINCOLN]==1);
    tap(input,80,206);assert(hits[UI_AUDIO_BACK]==1);
    assert(lv_obj_get_child_count(screen)==base_children);
    lv_indev_delete(input);
    puts("AUDIO SELECTOR PASS: actual LVGL Newton/Lincoln/Back taps, no Wind or numbered-voice buttons, 200 open/close cycles");
}
