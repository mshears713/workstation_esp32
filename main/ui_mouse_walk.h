#pragma once
#include <stdbool.h>

/* Standalone Mouse-page motion only. HOME has a separate state machine. */
typedef struct {
    int x, direction, frame, dwell, station;
    bool paused;
} ui_mouse_walk_t;

static inline void ui_mouse_walk_reset(ui_mouse_walk_t *s)
{
    *s = (ui_mouse_walk_t){.x=8, .direction=1, .station=-1};
}

static inline void ui_mouse_walk_step(ui_mouse_walk_t *s)
{
    static const int stops[3] = {32, 136, 240};
    if (s->paused) return;
    if (s->dwell) {
        if (--s->dwell == 0) s->station = -1;
        return;
    }
    int next = s->x + s->direction * 6;
    for (int i=0; i<3; ++i) {
        if ((s->direction > 0 && s->x < stops[i] && next >= stops[i]) ||
            (s->direction < 0 && s->x > stops[i] && next <= stops[i])) {
            next = stops[i];
            s->station = i;
            s->dwell = 5; /* 835ms; background pause freezes this too. */
            break;
        }
    }
    s->frame = (s->frame + 1) % 6;
    s->x = next;
    if (s->x >= 264) { s->x=264; s->direction=-1; }
    if (s->x <= 8) { s->x=8; s->direction=1; }
}
