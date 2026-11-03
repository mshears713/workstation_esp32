#include "ui_newton.h"
#include "newton_art.h"
static newton_scene_t scene;
static uint32_t last_tick, elapsed;
static bool regrown;
static void tick(lv_timer_t *timer)
{
    (void)timer;uint32_t now=lv_tick_get(),delta=now-last_tick;last_tick=now;elapsed+=delta;
    newton_scene_tick(&scene,delta);
    if(elapsed>=3400 && !regrown) { (void)newton_scene_drop(&scene);regrown=true; }
    newton_scene_audio_state(&scene,true,scene.gust_left_ms>0,false);
    ui_newton_update(&scene,"Simulated audio state; no speaker");
}
void ws_render(lv_obj_t *screen)
{
    newton_scene_open(&scene,NULL);newton_scene_audio_state(&scene,true,true,false);
    ui_newton_open(screen,newton_art_get(),NULL,NULL);
    (void)newton_scene_drop(&scene);last_tick=lv_tick_get();elapsed=0;regrown=false;
    ui_newton_update(&scene,"Simulated audio state; no speaker");lv_timer_create(tick,50,NULL);
}
