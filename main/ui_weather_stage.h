#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "weather_client.h"

/* Fixed, owned snapshots. UI caller alone owns this state; no LVGL pointers. */
typedef enum { WEATHER_STAGE_ICON, WEATHER_STAGE_TEMPERATURE, WEATHER_STAGE_RAIN } weather_stage_field_t;
typedef struct {
    weather_status_t active, pending;
    bool running, pending_valid;
    uint8_t step;
} ui_weather_stage_t;

bool ui_weather_stage_offer(ui_weather_stage_t *s, const weather_status_t *snapshot);
bool ui_weather_stage_current(const ui_weather_stage_t *s, unsigned *card, weather_stage_field_t *field);
bool ui_weather_stage_advance(ui_weather_stage_t *s);
bool ui_weather_stage_cancel(ui_weather_stage_t *s, weather_status_t *latest);
