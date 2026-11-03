#pragma once
#include "lvgl.h"

lv_obj_t *ui_tour_selector_create(lv_obj_t *screen, lv_event_cb_t tour_a,
                                  lv_event_cb_t tour_b, lv_event_cb_t back,
                                  lv_event_cb_t deleted);
