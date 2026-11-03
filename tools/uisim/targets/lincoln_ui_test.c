#include "ui_lincoln.h"
#include "lincoln_art.h"
#include <assert.h>
#include <stdio.h>
static lincoln_scene_t scene;
static lv_point_t point;
static lv_indev_state_t press;
static unsigned hits[3];
static const lincoln_timing_t timing={2720,1460,300,3060};
static void action(ui_lincoln_event_t e,void *ctx)
{
    (void)ctx;hits[e]++;
    if(e==UI_LINCOLN_PERSON)lincoln_scene_tap_person(&scene);
    if(e==UI_LINCOLN_CABIN)(void)lincoln_scene_tap_cabin(&scene);
    if(e==UI_LINCOLN_BACK){lincoln_scene_close(&scene);ui_lincoln_close();}
    ui_lincoln_update(&scene,NULL);
}
static void read_pointer(lv_indev_t *dev,lv_indev_data_t *data)
{(void)dev;data->point=point;data->state=press;}
static void tap(lv_indev_t *dev,int x,int y)
{
    point=(lv_point_t){x,y};press=LV_INDEV_STATE_PRESSED;lv_tick_inc(40);lv_indev_read(dev);
    press=LV_INDEV_STATE_RELEASED;lv_tick_inc(40);lv_indev_read(dev);
}
static unsigned timers(void)
{unsigned n=0;for(lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t))n++;return n;}
void ws_render(lv_obj_t *screen)
{
    assert(ui_lincoln_art_valid(lincoln_art_get()));assert(!lincoln_art_get()->artist_accepted);
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input,lv_display_get_default());lv_indev_set_read_cb(input,read_pointer);lv_indev_set_mode(input,LV_INDEV_MODE_EVENT);
    unsigned base=lv_obj_get_child_count(screen),base_timers=timers();
    for(unsigned i=0;i<500;i++){
        assert(lincoln_scene_open(&scene,&timing));ui_lincoln_open(screen,lincoln_art_get(),action,NULL);
        ui_lincoln_update(&scene,"SD unavailable");lincoln_scene_close(&scene);ui_lincoln_close();
        assert(lv_obj_get_child_count(screen)==base&&timers()==base_timers);
    }
    assert(lincoln_scene_open(&scene,&timing));ui_lincoln_open(screen,lincoln_art_get(),action,NULL);
    ui_lincoln_update(&scene,"SD unavailable");lv_obj_update_layout(screen);
    tap(input,220,150);assert(hits[UI_LINCOLN_PERSON]==0);
    lincoln_scene_ready(&scene,true);ui_lincoln_update(&scene,NULL);lv_obj_update_layout(screen);
    tap(input,220,150);assert(scene.phase==LINCOLN_HAT_OFF);
    lincoln_scene_tick(&scene,200);unsigned before=ui_lincoln_actor_frame(&scene);
    tap(input,270,216);assert(scene.phase==LINCOLN_STOPPING&&ui_lincoln_actor_frame(&scene)==before);
    uint32_t id;assert(lincoln_scene_take_stop(&scene,&id));lincoln_scene_voice_stopped(&scene,id);
    lincoln_scene_tick(&scene,10000);assert(scene.phase==LINCOLN_IDLE);
    tap(input,75,140);assert(scene.cabin_left_ms==2800&&ui_lincoln_cabin_frame(&scene)==0);
    lincoln_scene_tick(&scene,2180);assert(ui_lincoln_cabin_frame(&scene)==11);
    lincoln_scene_tick(&scene,620);assert(ui_lincoln_cabin_frame(&scene)==0);
    scene.phase=LINCOLN_READING;
    for(unsigned ms=0;ms<30000;ms+=20){scene.elapsed_ms=ms;assert(ui_lincoln_actor_frame(&scene)>=21&&ui_lincoln_actor_frame(&scene)<42);ui_lincoln_update(&scene,NULL);}
    tap(input,50,216);assert(hits[UI_LINCOLN_BACK]==1&&!scene.open);
    assert(lv_obj_get_child_count(screen)==base&&timers()==base_timers);lv_indev_delete(input);
    puts("LINCOLN DRAFT UI PASS: native descriptors, actual pointer taps, stop pose, door returns idle, two selected reading loops, 500 object/timer cleanup cycles");
}
