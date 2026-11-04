/*
 * Artifacts & Instruments: when to welcome a visitor.
 *
 * Plain C, no ESP-IDF or LVGL, so the host test (tools/hosttest) runs the
 * same code the device does. The input is the AT581X's yes/no output, NOT a
 * distance: the SENSOR dock radar is a 5.8 GHz Doppler presence detector
 * (radar_presence.c). "How close" is set in the radar's own sensitivity
 * register; this module only decides "long enough" and "is this a new person".
 *
 * Doppler radar answers "something is moving", so a visitor who stands still
 * reading the screen can drop out for a moment. Short gaps (gap_ms) are
 * therefore forgiven while waiting for the dwell, and a welcome is only re-armed
 * after a long continuous quiet (rearm_ms) - not after one missed sample.
 *
 *   IDLE --detect--> CANDIDATE --detected for dwell_ms--> WELCOME (once) --> LATCHED
 *   CANDIDATE --quiet > gap_ms--> IDLE
 *   LATCHED --quiet >= rearm_ms--> IDLE (re-armed)
 *   any state --engaged (a touch)--> LATCHED: someone already using it needs no welcome
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PRESENCE_IDLE, PRESENCE_CANDIDATE, PRESENCE_LATCHED } presence_state_t;

typedef struct {
    uint32_t dwell_ms;   /* continuous-ish presence before the welcome (default 6000) */
    uint32_t gap_ms;     /* dropout forgiven while waiting for the dwell (default 2500) */
    uint32_t rearm_ms;   /* continuous quiet before a new visitor can be welcomed (default 30000) */
} presence_params_t;

typedef struct {
    presence_params_t p;
    presence_state_t state;
    uint32_t since_ms;      /* CANDIDATE: when presence started */
    uint32_t last_seen_ms;  /* last sample with detection (or the engage time) */
    uint32_t welcomes;      /* total welcomes fired - for DIAGNOSTICS and tests */
    uint32_t rearms;        /* total re-arms */
} presence_logic_t;

#define PRESENCE_DEFAULT_DWELL_MS 6000u
#define PRESENCE_DEFAULT_GAP_MS 2500u
#define PRESENCE_DEFAULT_REARM_MS 30000u

/* Zero fields take the defaults above. */
void presence_init(presence_logic_t *l, presence_params_t p);

/* One radar sample. Returns true exactly once per visitor: the moment to welcome.
 * `can_welcome` false (HOME not showing, tour or dialog open) holds a ready
 * visitor in CANDIDATE instead of firing; the welcome fires once it is true again
 * if they are still there. */
bool presence_sample(presence_logic_t *l, bool detected, bool can_welcome, uint32_t now_ms);

/* A touch: the visitor has already found the screen. */
void presence_engaged(presence_logic_t *l, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
