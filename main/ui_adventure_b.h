#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
/* A self-contained, visual-only overlay. Caller owns page selection on exit. */
void ui_adventure_b_open(lv_obj_t *screen, void (*exit_cb)(void *), void *ctx);
void ui_adventure_b_close(void);
bool ui_adventure_b_is_open(void);
/* Host/device diagnostic hooks: no network, recording or global page state. */
void ui_adventure_b_seek(unsigned beat, uint32_t elapsed_ms);
void ui_adventure_b_next(void);
void ui_adventure_b_skip(void);
unsigned ui_adventure_b_beat(void);
unsigned ui_adventure_b_owned_timers(void);
