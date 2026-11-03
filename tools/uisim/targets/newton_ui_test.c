#include "ui_newton.h"
#include "newton_art.h"
#include <assert.h>
#include <stdio.h>
static newton_scene_t scene;
static unsigned hits[4];
static lv_point_t point;
static lv_indev_state_t pressed_state;
static void action(ui_newton_action_t command,void *ctx)
{
    (void)ctx;hits[command]++;
    if(command==UI_NEWTON_BACK){newton_scene_close(&scene);ui_newton_close();return;}
    if(command==UI_NEWTON_TREE)(void)newton_scene_drop(&scene);
    if(command==UI_NEWTON_WIND)scene.wind=!scene.wind;
    if(command==UI_NEWTON_SPEAK)newton_scene_tap_person(&scene);
    ui_newton_update(&scene,"fixture");
}
static void read_pointer(lv_indev_t *dev,lv_indev_data_t *data)
{
    (void)dev;data->point=point;data->state=pressed_state;
}
static void tap(lv_indev_t *dev,int x,int y)
{
    point=(lv_point_t){x,y};pressed_state=LV_INDEV_STATE_PRESSED;
    lv_tick_inc(40);lv_indev_read(dev);
    pressed_state=LV_INDEV_STATE_RELEASED;lv_tick_inc(40);lv_indev_read(dev);
}
static int timers(void)
{
    int n=0;for(lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t))n++;return n;
}
void ws_render(lv_obj_t *screen)
{
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input,lv_display_get_default());lv_indev_set_read_cb(input,read_pointer);
    lv_indev_set_mode(input,LV_INDEV_MODE_EVENT);
    unsigned base=lv_obj_get_child_count(screen);int base_timers=timers();
    for(int i=0;i<500;i++) {
        newton_scene_open(&scene,NULL);ui_newton_open(screen,newton_art_get(),action,NULL);
        ui_newton_update(&scene,"SD unavailable");newton_scene_close(&scene);ui_newton_close();
        assert(lv_obj_get_child_count(screen)==base && timers()==base_timers);
    }
    assert(ui_newton_art_valid(newton_art_get()));
    newton_scene_open(&scene,NULL);ui_newton_open(screen,newton_art_get(),action,NULL);
    ui_newton_update(&scene,"SD unavailable");lv_obj_update_layout(screen);
    tap(input,145,160);tap(input,270,216);tap(input,160,216);
    assert(hits[UI_NEWTON_SPEAK]==0 && hits[UI_NEWTON_WIND]==0);
    tap(input,265,110);assert(scene.falling==0); /* visual scene remains useful offline */
    newton_scene_audio_state(&scene,true,false,false);ui_newton_update(&scene,"ready");
    lv_obj_update_layout(screen);tap(input,145,160);assert(scene.queued_voice);
    assert(newton_scene_take_voice_request(&scene));newton_scene_voice_ack(&scene,true,1);
    for(int i=0;i<50;i++)tap(input,270,216);
    assert(scene.queued_voice && !newton_scene_take_voice_request(&scene));
    tap(input,160,216);assert(scene.wind);
    newton_scene_tick(&scene,750);ui_newton_update(&scene,"ready");
    tap(input,50,216);assert(hits[UI_NEWTON_BACK]==1 && !scene.open);
    assert(lv_obj_get_child_count(screen)==base && timers()==base_timers);
    lv_indev_delete(input);
    puts("NEWTON UI PASS: actual pointer taps, offline controls, tree, person, one voice queue, wind during voice, Back, 500 lifecycles");
}
