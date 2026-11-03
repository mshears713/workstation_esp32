#pragma once
#include "lvgl.h"
void ui_mouse_open(lv_obj_t *screen, void (*exit_cb)(void *), void *ctx);
void ui_mouse_close(void);

void ui_mouse_home_create(lv_obj_t *page);
void ui_mouse_home_set_active(bool active);

void ui_mouse_home_set_refresh_cb(lv_event_cb_t cb);
void ui_mouse_home_refresh_result(bool success);
void ui_mouse_home_refresh_notice(const char *message);
