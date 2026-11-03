#pragma once
#include "lvgl.h"
typedef enum { UI_AUDIO_BACK, UI_AUDIO_WIND, UI_AUDIO_VOICE, UI_AUDIO_GARDEN, UI_AUDIO_LINCOLN } ui_audio_action_t;
typedef void (*ui_audio_action_cb_t)(ui_audio_action_t action, void *ctx);
typedef struct {
    const char *message;
    bool running, wind, voice;
    unsigned next_voice;
} ui_audio_test_view_t;
void ui_audio_test_open(lv_obj_t *screen, ui_audio_action_cb_t cb, void *ctx);
void ui_audio_test_update(const ui_audio_test_view_t *view);
void ui_audio_test_close(void);
