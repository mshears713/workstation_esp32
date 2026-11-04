#include "presence_logic.h"

void presence_init(presence_logic_t *l, presence_params_t p)
{
    if (!p.dwell_ms) p.dwell_ms = PRESENCE_DEFAULT_DWELL_MS;
    if (!p.gap_ms) p.gap_ms = PRESENCE_DEFAULT_GAP_MS;
    if (!p.rearm_ms) p.rearm_ms = PRESENCE_DEFAULT_REARM_MS;
    *l = (presence_logic_t){ .p = p, .state = PRESENCE_IDLE };
}

bool presence_sample(presence_logic_t *l, bool detected, bool can_welcome, uint32_t now_ms)
{
    if (detected) l->last_seen_ms = now_ms;
    /* uint32_t subtraction: correct across the 49-day lv_tick wrap. */
    uint32_t quiet = now_ms - l->last_seen_ms;
    switch (l->state) {
    case PRESENCE_IDLE:
        if (detected) {
            l->state = PRESENCE_CANDIDATE;
            l->since_ms = now_ms;
        }
        return false;
    case PRESENCE_CANDIDATE:
        if (!detected && quiet > l->p.gap_ms) {
            l->state = PRESENCE_IDLE;   /* walked past */
            return false;
        }
        if (can_welcome && now_ms - l->since_ms >= l->p.dwell_ms) {
            l->state = PRESENCE_LATCHED;
            l->welcomes++;
            return true;
        }
        return false;
    case PRESENCE_LATCHED:
    default:
        if (!detected && quiet >= l->p.rearm_ms) {
            l->state = PRESENCE_IDLE;
            l->rearms++;
        }
        return false;
    }
}

void presence_engaged(presence_logic_t *l, uint32_t now_ms)
{
    l->state = PRESENCE_LATCHED;
    l->last_seen_ms = now_ms;
}
