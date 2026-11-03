#include "newton_scene.h"
#include <string.h>

static uint32_t add_sat(uint32_t a, uint32_t b)
{
    return UINT32_MAX - a < b ? UINT32_MAX : a + b;
}
void newton_scene_open(newton_scene_t *s, const newton_saved_t *saved)
{
    memset(s, 0, sizeof(*s));
    s->open = true; s->falling = -1; s->saved.version = 1;
    if (saved && saved->version == 1) {
        for (unsigned i = 0; i < NEWTON_FRUITS; ++i)
            s->saved.regrow_left_ms[i] = saved->regrow_left_ms[i] <= NEWTON_REGROW_MS ?
                    saved->regrow_left_ms[i] : NEWTON_REGROW_MS;
    }
}
void newton_scene_close(newton_scene_t *s)
{
    s->open = s->ready = s->wind = s->speaking = false;
    s->queued_voice = s->voice_pending = false;
    s->falling = -1; s->action = NEWTON_IDLE;
}
void newton_scene_audio_state(newton_scene_t *s, bool ready, bool wind, bool speaking)
{
    if (!s->open) return;
    s->ready = ready; s->wind = ready && wind;
    if (s->speaking && !speaking) {
        /* Complete movement even if the UI missed an intermediate poll. */
        if (s->falling<0 && s->action == NEWTON_WALK_OUT) s->person_offset = NEWTON_WALK_DISTANCE;
        if (s->falling<0 && s->action == NEWTON_WALK_BACK) s->person_offset = 0;
        s->action = NEWTON_IDLE; s->action_ms = 0;
    }
    s->speaking = ready && speaking;
    if (!ready) {
        s->queued_voice = s->voice_pending = false;
        s->action = NEWTON_IDLE;
    }
}
void newton_scene_tap_person(newton_scene_t *s)
{
    if (s->open && s->ready) s->queued_voice = true; /* exactly one waiting tap */
}
bool newton_scene_take_voice_request(newton_scene_t *s)
{
    if (!s->open || !s->ready || s->speaking || s->voice_pending || !s->queued_voice)
        return false;
    s->queued_voice = false; s->voice_pending = true;
    s->pending_ms = 0; s->request_failed = false;
    return true;
}
void newton_scene_voice_ack(newton_scene_t *s, bool accepted, unsigned clip)
{
    if (!s->open || !s->voice_pending) return;
    s->voice_pending = false;
    if (!accepted || clip < 1 || clip > 6) {
        s->request_failed = true; s->queued_voice = false; return;
    }
    static const newton_action_t actions[] = {
        NEWTON_LOOK_UP, NEWTON_THINK, NEWTON_WALK_OUT,
        NEWTON_WALK_BACK, NEWTON_PRISM, NEWTON_INSPECT
    };
    s->speaking = true; s->clip = clip; s->action = actions[clip - 1];
    s->action_ms = 0;s->walk_start=s->person_offset;
}
bool newton_scene_drop(newton_scene_t *s)
{
    if (!s->open || s->falling >= 0) return false;
    /* Native pack has one persistent apple. A second tree tap regrows it. */
    if(s->saved.regrow_left_ms[0] > NEWTON_GROW_ANIM_MS) {
        s->saved.regrow_left_ms[0]=NEWTON_GROW_ANIM_MS;s->dirty=true;
        s->gust_left_ms=1500;return true;
    }
    for (unsigned i = 0; i < NEWTON_FRUITS; ++i) {
        if (s->saved.regrow_left_ms[i] == 0) {
            s->saved.regrow_left_ms[i] = NEWTON_REGROW_MS;
            s->falling = (int)i; s->fall_ms = 0; s->dirty = true;
            s->approach_start=s->person_offset;
            s->approach_ms=(uint32_t)(NEWTON_WALK_DISTANCE-s->person_offset)*1600/NEWTON_WALK_DISTANCE;
            s->gust_left_ms = 6000;
            return true;
        }
    }
    return false;
}
void newton_scene_tick(newton_scene_t *s, uint32_t elapsed_ms)
{
    if (!s->open) return;
    s->action_ms = add_sat(s->action_ms, elapsed_ms);
    s->wind_ms = (s->wind_ms + elapsed_ms % 900u) % 900u;
    s->gust_left_ms = s->gust_left_ms > elapsed_ms ? s->gust_left_ms - elapsed_ms : 0;
    if (s->voice_pending) {
        s->pending_ms = add_sat(s->pending_ms, elapsed_ms);
        if (s->pending_ms >= 2000) {
            s->voice_pending = s->queued_voice = false; s->request_failed = true;
        }
    }
    for (unsigned i = 0; i < NEWTON_FRUITS; ++i) {
        uint32_t left = s->saved.regrow_left_ms[i];
        if (left && left <= NEWTON_GROW_ANIM_MS) {
            s->saved.regrow_left_ms[i] = left > elapsed_ms ? left - elapsed_ms : 0;
            if (!s->saved.regrow_left_ms[i]) s->dirty = true;
        }
    }
    if (s->falling >= 0) {
        s->fall_ms = add_sat(s->fall_ms, elapsed_ms);
        if(s->fall_ms < s->approach_ms) {
            s->person_offset=s->approach_start+(NEWTON_WALK_DISTANCE-s->approach_start)*(int)s->fall_ms/(int)s->approach_ms;
        } else s->person_offset=NEWTON_WALK_DISTANCE;
        if (s->fall_ms >= s->approach_ms+1080) s->falling = -1;
    }
    else if (s->action == NEWTON_WALK_OUT)
        s->person_offset = s->action_ms < 1600 ? s->walk_start+(int)(s->action_ms*(NEWTON_WALK_DISTANCE-s->walk_start)/1600) : NEWTON_WALK_DISTANCE;
    else if (s->action == NEWTON_WALK_BACK)
        s->person_offset = s->action_ms < 1600 ? s->walk_start-(int)(s->action_ms*s->walk_start/1600) : 0;
}
newton_fruit_phase_t newton_scene_fruit_phase(const newton_scene_t *s, unsigned fruit)
{
    if (fruit >= NEWTON_FRUITS) return NEWTON_ON_GROUND;
    if (s->falling == (int)fruit) {
        if(s->fall_ms < s->approach_ms)return NEWTON_ATTACHED;
        return s->fall_ms-s->approach_ms<680 ? NEWTON_FALLING : NEWTON_ON_GROUND;
    }
    uint32_t left = s->saved.regrow_left_ms[fruit];
    return !left ? NEWTON_ATTACHED : left <= NEWTON_GROW_ANIM_MS ? NEWTON_REGROWING : NEWTON_ON_GROUND;
}
void newton_scene_save(const newton_scene_t *s, newton_saved_t *out) { *out = s->saved; }
const char *newton_scene_action_name(newton_action_t action)
{
    static const char *names[] = {"idle", "look_up", "think", "walk_out", "walk_back",
                                  "demonstrate_prism", "inspect_then_delight", "bonk"};
    return (unsigned)action < sizeof(names) / sizeof(names[0]) ? names[action] : "idle";
}
