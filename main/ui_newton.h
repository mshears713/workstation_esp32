#pragma once
#include "lvgl.h"
#include "newton_scene.h"

/* Art owner supplies native exports; these coordinates are explicit integration
 * anchors, not a second illustration. Images stay at native 1:1 display scale. */
typedef struct { const lv_image_dsc_t *image; int16_t x, y; } newton_tile_t;
typedef struct { const lv_image_dsc_t *const *images; unsigned count; unsigned frame_ms; } newton_frames_t;
typedef struct {
    const newton_tile_t *background; unsigned background_count;
    newton_frames_t tree, fruit, person[8], caution;
    int16_t tree_x, ground_y, person_foot_x;
    lv_point_t tree_origin, fruit_origin, person_foot_anchor;
    bool native_verified;
} ui_newton_art_t;
typedef enum { UI_NEWTON_BACK, UI_NEWTON_WIND, UI_NEWTON_SPEAK, UI_NEWTON_TREE } ui_newton_action_t;
typedef void (*ui_newton_cb_t)(ui_newton_action_t action, void *ctx);
bool ui_newton_art_valid(const ui_newton_art_t *art);
void ui_newton_open(lv_obj_t *screen, const ui_newton_art_t *art, ui_newton_cb_t cb, void *ctx);
void ui_newton_update(const newton_scene_t *scene, const char *audio_message);
void ui_newton_close(void);
