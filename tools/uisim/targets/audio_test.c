#include "ws_sim.h"
#include "ui_audio_test.h"
#include <assert.h>
#include <stdio.h>
static ui_audio_test_view_t view;
static void action(ui_audio_action_t command, void *ctx)
{
    (void)ctx;
    if(command==UI_AUDIO_BACK) { ui_audio_test_close(); puts("AUDIO TEST CLOSED"); return; }
    if(command==UI_AUDIO_WIND) view.wind=!view.wind;
    if(command==UI_AUDIO_VOICE) view.voice=true;
    ui_audio_test_update(&view);
}
void ws_render(lv_obj_t *screen)
{
    ui_audio_test_open(screen,action,NULL);
    view=(ui_audio_test_view_t){
        .message=ws_fx_str("message","Choose a scene"),
        .running=ws_fx_bool("running",true),
        .wind=ws_fx_bool("wind",false), .voice=ws_fx_bool("voice",false),
        .next_voice=(unsigned)ws_fx_int("next_voice",1),
    };
    ui_audio_test_update(&view);
}
