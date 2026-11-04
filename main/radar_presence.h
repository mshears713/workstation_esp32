/*
 * SENSOR dock radar: MoreSense MS58-3909S68U4 (Airtouch AT581X, 5.8 GHz
 * Doppler). Identified from Espressif's own factory firmware for this dock
 * (esp-box components/bsp: BSP_RADAR_OUT_IO = GPIO21, AT581X at I2C 0x28 on
 * the dock bus GPIO40/41, the same bus the AHT30 uses) and the dock spec
 * sheet ("human presence detection, around 2 meters").
 *
 * It reports YES/NO, not a distance. The "reach" setting is the chip's
 * unitless detection threshold (levels 1..10 = 623..983, default 823); what a
 * given level means in centimetres has to be measured at the exhibit.
 *
 * Reads are cheap (one GPIO level) and safe from the LVGL task; configuration
 * is a handful of I2C writes, done at init and when the reach changes.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RADAR_REACH_LEVELS 10        /* 1..10 on SETTINGS */
#define RADAR_REACH_DEFAULT 6        /* level 6 = threshold 823, the vendor default */

typedef struct {
    bool present;          /* AT581X answered on the dock bus and took its config */
    bool detected;         /* output pin, last poll */
    int reach;             /* 1..RADAR_REACH_LEVELS */
    int threshold;         /* the chip value that level maps to */
    uint32_t dwell_ms, rearm_ms;
    uint32_t rises;        /* output low->high transitions since boot */
    uint32_t high_ms;      /* how long the output has been high (0 when low) */
    char error[32];        /* "" or why the radar is unavailable */
} radar_status_t;

/* Probe + configure. Safe to call when the dock is absent: status says so. */
void radar_presence_init(void);
/* Poll the output pin (call from one task, ~100 ms). Returns detected. */
bool radar_presence_poll(uint32_t now_ms);
void radar_presence_get(radar_status_t *out);
/* Change reach (1..10), store it in NVS and reconfigure the chip. */
void radar_presence_set_reach(int level);

#ifdef __cplusplus
}
#endif
