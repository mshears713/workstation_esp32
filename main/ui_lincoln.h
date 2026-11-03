#pragma once
#include "lvgl.h"
#include "lincoln_scene.h"
typedef struct {
    const lv_image_dsc_t *const *backdrop;
    const lv_image_dsc_t *const *actor;
    const lv_image_dsc_t *const *cabin;
    bool native_persistence_verified;
    bool artist_accepted;
} ui_lincoln_art_t;
typedef enum { UI_LINCOLN_BACK, UI_LINCOLN_CABIN, UI_LINCOLN_PERSON } ui_lincoln_event_t;
typedef void (*ui_lincoln_cb_t)(ui_lincoln_event_t event,void *ctx);
bool ui_lincoln_art_valid(const ui_lincoln_art_t *art);
void ui_lincoln_open(lv_obj_t *screen,const ui_lincoln_art_t *art,ui_lincoln_cb_t cb,void *ctx);
void ui_lincoln_update(const lincoln_scene_t *state,const char *message);
void ui_lincoln_close(void);
/* Pure frame selection, shared by actual LVGL and timing acceptance tests. */
unsigned ui_lincoln_actor_frame(const lincoln_scene_t *state);
unsigned ui_lincoln_cabin_frame(const lincoln_scene_t *state);
