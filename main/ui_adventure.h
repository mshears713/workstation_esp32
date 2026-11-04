#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* Tour A: never calls audio, recording, networking or OTA itself. The app
 * narrates through the scene callback. */
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
/* Called after every scene change (including the first HOME scene on open).
 * Survives open/close; pass NULL to clear. */
void ui_adventure_set_scene_cb(void (*cb)(adventure_scene_t scene, void *ctx), void *ctx);
unsigned ui_adventure_timer_count(void);

/* Reuse the original accepted art without copying or redrawing its pixels. */
typedef enum { TOUR_ASSET_HOME, TOUR_ASSET_HOUSE, TOUR_ASSET_MOUSE, TOUR_ASSET_VAN } tour_original_asset_t;
const lv_image_dsc_t *ui_adventure_original_asset(tour_original_asset_t asset, unsigned frame);
