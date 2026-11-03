#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* Visual-only tour: never calls voice_control, recording, networking or OTA. */
typedef enum {
 ADVENTURE_HOME, ADVENTURE_INTRO, ADVENTURE_DEPART, ADVENTURE_PORTAL,
 ADVENTURE_EGYPT, ADVENTURE_EGYPT_STONE, ADVENTURE_MUMMY,
 ADVENTURE_GREECE, ADVENTURE_GREECE_COLUMNS,
 ADVENTURE_GOTHIC, ADVENTURE_GOTHIC_GLASS,
 ADVENTURE_SING, ADVENTURE_CLIMB, ADVENTURE_END, ADVENTURE_SCENE_COUNT
} adventure_scene_t;
void ui_adventure_open(lv_obj_t *screen, void (*exit_cb)(void *), void *ctx);
void ui_adventure_close(void);
void ui_adventure_next(void);
void ui_adventure_skip(void);
void ui_adventure_seek(adventure_scene_t scene);
adventure_scene_t ui_adventure_scene(void);
bool ui_adventure_is_open(void);
unsigned ui_adventure_timer_count(void);
