#pragma once
#include "lvgl.h"
/* Ordinary LVGL UI widgets; not authored artwork. Borrowed lifetime: parent owns
 * all objects. No timers, audio or input handlers. Safe to destroy with parent. */
typedef enum { STORY_SPEECH, STORY_THOUGHT, STORY_SHOUT, STORY_CAPTION } story_dialogue_style_t;
typedef struct { lv_obj_t *panel, *text, *tail1, *tail2; } story_dialogue_t;
story_dialogue_t ui_story_dialogue_create(lv_obj_t *parent,
    story_dialogue_style_t style, const char *text, int x, int y, int width, int height);
void ui_story_dialogue_set_visible(story_dialogue_t *dialogue, bool visible);
