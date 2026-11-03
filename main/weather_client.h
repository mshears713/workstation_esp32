/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief HOME's weather (1.3.0): polls the backend's GET /api/v1/weather every
 *        ten minutes and keeps the last good answer.
 * @details The backend does all the choosing (slots, rounding, icon keys,
 *          hour labels - see the backend's app/api/weather.py); this only
 *          copies fields. Its task stack lives in PSRAM, so it costs no
 *          internal RAM beyond the transient HTTP/cJSON allocations every
 *          other client makes.
 *
 *          The first fetch waits for the updater's first finished manifest
 *          check (or WEATHER_FIRST_FETCH_MAX_S of uptime if the updater never
 *          started). The 1.2.1 lesson: nothing that runs "once the backend
 *          answers" may be able to crash-loop ahead of the OTA check.
 *
 *          App-agnostic: knows nothing about LVGL. status_deck_ui.c reads a
 *          snapshot and fills ui_home_weather_t.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_SLOTS 2

typedef struct {
    char label[8];        /* "3 PM" */
    int temp;
    char icon[8];
    int rain;
} weather_slot_t;

typedef struct {
    bool valid;           /* at least one good answer since boot */
    uint32_t manual_success; /* successful explicit upstream refreshes */
    uint32_t manual_failure; /* failed explicit refreshes */
    uint32_t seq;         /* bumps on every good answer */
    uint32_t age_s;       /* seconds since the backend fetched it from Open-Meteo */
    char location[16];
    char as_of[12];       /* backend local time of that fetch, "11:40 PM" */
    int now_temp;
    char now_icon[8];
    char now_text[20];
    int now_rain;
    weather_slot_t slot[WEATHER_SLOTS];
    int tmrw_hi;
    int tmrw_lo;
    char tmrw_icon[8];
    int tmrw_rain;
} weather_status_t;

void weather_client_init(void);
/* Nonblocking; coalesces taps while a request is pending. */
typedef enum { WEATHER_REFRESH_ACCEPTED, WEATHER_REFRESH_PENDING, WEATHER_REFRESH_COOLDOWN, WEATHER_REFRESH_UNAVAILABLE } weather_refresh_request_t;
weather_refresh_request_t weather_client_request_refresh(void);

/** Copies the current snapshot. Safe from any task. `age_s` is current: the
 *  backend's age at fetch time plus the time since. */
void weather_client_get(weather_status_t *out);

#ifdef __cplusplus
}
#endif
