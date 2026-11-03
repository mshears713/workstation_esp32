#include "ws_sim.h"
#include "ui_newton.h"
#include "newton_art.h"
void ws_render(lv_obj_t *screen)
{
    newton_scene_t state;newton_scene_open(&state,NULL);
    newton_scene_audio_state(&state,ws_fx_bool("ready",true),ws_fx_bool("wind",false),false);
    int clip=ws_fx_int("clip",0);
    if(clip) {
        newton_scene_tap_person(&state);(void)newton_scene_take_voice_request(&state);
        newton_scene_voice_ack(&state,true,(unsigned)clip);
    }
    if(ws_fx_bool("drop",false))(void)newton_scene_drop(&state);
    if(ws_fx_bool("grow",false)) {
        (void)newton_scene_drop(&state);newton_scene_tick(&state,3000);
        (void)newton_scene_drop(&state);
    }
    if(ws_fx_bool("queue",false))newton_scene_tap_person(&state);
    newton_scene_tick(&state,(uint32_t)ws_fx_int("elapsed_ms",0));
    ui_newton_open(screen,ws_fx_bool("fixture_art",false)?NULL:newton_art_get(),NULL,NULL);
    ui_newton_update(&state,"SD unavailable - insert card, retry");
}
