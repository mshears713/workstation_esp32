#pragma once
#include <stdbool.h>
#include "lincoln_speeches.h"
#include <stdint.h>

/* Pure scene protocol. Native art supplies durations; the audio worker supplies
 * start/completion/stop acknowledgements. No clip duration drives reading. */
typedef enum {
    LINCOLN_IDLE, LINCOLN_HAT_OFF, LINCOLN_LOOK, LINCOLN_PAPER_OUT,
    LINCOLN_WAIT_AUDIO, LINCOLN_READING, LINCOLN_RETURN, LINCOLN_STOPPING
} lincoln_phase_t;
typedef struct { uint32_t hat_ms, look_ms, paper_ms, return_ms; } lincoln_timing_t;
typedef struct {
    bool open, ready, request_voice, request_stop, request_paper, failed;
    bool cabin_request;
    lincoln_phase_t phase;
    lincoln_phase_t cancel_phase;
    lincoln_timing_t timing;
    uint32_t elapsed_ms, request_id, cabin_left_ms;
    uint32_t cancel_elapsed_ms;
    unsigned next_clip, clip, cabin_next, cabin_active;
    uint8_t return_frames[40], return_count;
    uint16_t return_ms[40];
} lincoln_scene_t;

lincoln_timing_t lincoln_scene_native_timing(void);
unsigned lincoln_scene_actor_frame(const lincoln_scene_t *s);
unsigned lincoln_scene_cabin_frame(const lincoln_scene_t *s);

bool lincoln_scene_open(lincoln_scene_t *s, const lincoln_timing_t *timing);
void lincoln_scene_close(lincoln_scene_t *s);
void lincoln_scene_ready(lincoln_scene_t *s, bool ready);
void lincoln_scene_tick(lincoln_scene_t *s, uint32_t ms);
void lincoln_scene_tap_person(lincoln_scene_t *s);
bool lincoln_scene_take_voice(lincoln_scene_t *s, uint32_t *id, unsigned *clip);
bool lincoln_scene_take_stop(lincoln_scene_t *s, uint32_t *id);
bool lincoln_scene_take_paper(lincoln_scene_t *s);
void lincoln_scene_voice_started(lincoln_scene_t *s, uint32_t id, bool success);
void lincoln_scene_voice_finished(lincoln_scene_t *s, uint32_t id, bool success);
void lincoln_scene_voice_stopped(lincoln_scene_t *s, uint32_t id);
bool lincoln_scene_tap_cabin(lincoln_scene_t *s);
bool lincoln_scene_take_cabin(lincoln_scene_t *s, unsigned *effect);
