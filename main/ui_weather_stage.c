#include "ui_weather_stage.h"

/* A pass always owns its snapshot; one pending slot coalesces newer data. */
bool ui_weather_stage_offer(ui_weather_stage_t *s, const weather_status_t *snapshot)
{
    if (!s || !snapshot || !snapshot->valid) return false;
    if (s->running) { s->pending = *snapshot; s->pending_valid = true; return false; }
    s->active = *snapshot; s->running = true; s->pending_valid = false; s->step = 0;
    return true;
}

bool ui_weather_stage_current(const ui_weather_stage_t *s, unsigned *card, weather_stage_field_t *field)
{
    if (!s || !s->running || !card || !field || s->step >= 12) return false;
    *card = s->step / 3; *field = (weather_stage_field_t)(s->step % 3);
    return true;
}

/* Return true if a coalesced newer pass starts; false for a continuing or
 * finished pass. Inspect running/current to distinguish those outcomes. */
bool ui_weather_stage_advance(ui_weather_stage_t *s)
{
    if (!s || !s->running) return false;
    if (++s->step < 12) return false;
    if (s->pending_valid) {
        s->active = s->pending; s->pending_valid = false; s->step = 0;
        return true;
    }
    s->running = false; s->step = 0; return false;
}

bool ui_weather_stage_cancel(ui_weather_stage_t *s, weather_status_t *latest)
{
    if (!s || !latest || !s->running) return false;
    *latest = s->pending_valid ? s->pending : s->active;
    s->running = false; s->pending_valid = false; s->step = 0;
    return true;
}
