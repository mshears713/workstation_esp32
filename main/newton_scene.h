#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Pure state: no LVGL, file handles, clocks, allocations or audio ownership. */
#define NEWTON_FRUITS 1
#define NEWTON_REGROW_MS 20000u
#define NEWTON_GROW_ANIM_MS 840u
#define NEWTON_WALK_DISTANCE 71
typedef enum {
    NEWTON_IDLE, NEWTON_LOOK_UP, NEWTON_THINK, NEWTON_WALK_OUT,
    NEWTON_WALK_BACK, NEWTON_PRISM, NEWTON_INSPECT, NEWTON_BONK
} newton_action_t;
typedef enum { NEWTON_ATTACHED, NEWTON_FALLING, NEWTON_ON_GROUND, NEWTON_REGROWING } newton_fruit_phase_t;
typedef struct {
    uint32_t version;
    uint32_t regrow_left_ms[NEWTON_FRUITS];
} newton_saved_t;
typedef struct {
    bool open, ready, wind, speaking, queued_voice, voice_pending;
    bool dirty, request_failed, storage_failed;
    unsigned clip;
    int falling; /* -1 or a valid fruit index; at most one head-bonk at a time */
    int person_offset, approach_start, walk_start;
    uint32_t approach_ms;
    uint32_t action_ms, fall_ms, pending_ms, wind_ms, gust_left_ms;
    newton_action_t action;
    newton_saved_t saved;
} newton_scene_t;

void newton_scene_open(newton_scene_t *s, const newton_saved_t *saved);
void newton_scene_close(newton_scene_t *s);
void newton_scene_tick(newton_scene_t *s, uint32_t elapsed_ms);
void newton_scene_audio_state(newton_scene_t *s, bool ready, bool wind, bool speaking);
void newton_scene_tap_person(newton_scene_t *s);
bool newton_scene_take_voice_request(newton_scene_t *s);
/* Called only on actual worker acknowledgement, never on a touch alone. */
void newton_scene_voice_ack(newton_scene_t *s, bool accepted, unsigned clip);
bool newton_scene_drop(newton_scene_t *s);
newton_fruit_phase_t newton_scene_fruit_phase(const newton_scene_t *s, unsigned fruit);
void newton_scene_save(const newton_scene_t *s, newton_saved_t *out);
const char *newton_scene_action_name(newton_action_t action);
