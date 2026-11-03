#include "ui_lincoln.h"
#include "lincoln_art.h"
#include <assert.h>
#include <stdio.h>
static lincoln_scene_t scene;
static uint32_t last_tick,clock_ms,voice_id,voice_until;
static bool voice_active,cabin2,cabin3,stopped;
static lv_obj_t *root;
#ifndef LINCOLN_SIM_SCENARIO
#define LINCOLN_SIM_SCENARIO 0
#endif

static void action(ui_lincoln_event_t event,void *ctx)
{
    (void)ctx;
    if(event==UI_LINCOLN_PERSON)lincoln_scene_tap_person(&scene);
    if(event==UI_LINCOLN_CABIN)(void)lincoln_scene_tap_cabin(&scene);
    if(event==UI_LINCOLN_BACK){lincoln_scene_close(&scene);ui_lincoln_close();}
}
static void tick(lv_timer_t *timer)
{
    (void)timer;uint32_t now=lv_tick_get(),delta=now-last_tick;last_tick=now;clock_ms+=delta;
    lincoln_scene_tick(&scene,delta);
    if(LINCOLN_SIM_SCENARIO==1&&clock_ms>=6500&&!stopped){lincoln_scene_tap_person(&scene);stopped=true;}
    if(LINCOLN_SIM_SCENARIO==2&&clock_ms>=350&&!stopped){lincoln_scene_tap_person(&scene);stopped=true;}
    if(clock_ms>=3500&&!cabin2){(void)lincoln_scene_tap_cabin(&scene);cabin2=true;}
    if(clock_ms>=7000&&!cabin3){(void)lincoln_scene_tap_cabin(&scene);cabin3=true;}
    uint32_t id;unsigned clip,effect;
    if(lincoln_scene_take_stop(&scene,&id)){voice_active=false;lincoln_scene_voice_stopped(&scene,id);}
    if(lincoln_scene_take_voice(&scene,&id,&clip)){
        static const uint32_t durations[]={25920,25920};
        voice_id=id;voice_until=clock_ms+durations[clip];voice_active=true;
        lincoln_scene_voice_started(&scene,id,true);
    }
    (void)lincoln_scene_take_paper(&scene);(void)lincoln_scene_take_cabin(&scene,&effect);
    if(voice_active&&clock_ms>=voice_until){voice_active=false;lincoln_scene_voice_finished(&scene,voice_id,true);}
    ui_lincoln_update(&scene,NULL);lv_obj_update_layout(root);
}
void ws_render(lv_obj_t *screen)
{
    root=screen;lincoln_timing_t timing=lincoln_scene_native_timing();assert(lincoln_scene_open(&scene,&timing));lincoln_scene_ready(&scene,true);
    ui_lincoln_open(screen,lincoln_art_get(),action,NULL);lincoln_scene_tap_person(&scene);
    (void)lincoln_scene_tap_cabin(&scene);last_tick=lv_tick_get();clock_ms=0;
    ui_lincoln_update(&scene,NULL);lv_timer_create(tick,20,NULL);
}
