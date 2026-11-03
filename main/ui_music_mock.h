/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Stand-in for a laptop bridge, so the music remote can be used before
 *        one exists. Pure C + an LVGL timer; nothing leaves the device.
 * @details It answers each command after ack_ms as a real bridge would
 *          (pending first, then a result) and keeps its own play state and
 *          volume. It always reports UI_MUSIC_LINK_SIMULATED - the screen then
 *          says SIMULATED - whatever link value it was seeded with, so the
 *          mock can never put CONNECTED on screen.
 *
 *          Prev/next do not change the (absent) title: the mock knows no songs.
 */
#pragma once

#include <stdint.h>
#include "lvgl.h"
#include "ui_music.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_MUSIC_MOCK_ACK = 0,   /* answers "sent" after ack_ms */
    UI_MUSIC_MOCK_FAIL,      /* answers "failed" after ack_ms */
    UI_MUSIC_MOCK_SILENT,    /* never answers; "no answer" after timeout_ms */
} ui_music_mock_outcome_t;

typedef struct {
    ui_music_view_t view;               /* the simulated state on screen */
    uint32_t ack_ms;                    /* simulated round trip, default 400 */
    uint32_t timeout_ms;                /* SILENT: give up after this, default 3000 */
    int volume_step;                    /* per tap, default 5 (a mock value, not a measured laptop step) */
    ui_music_mock_outcome_t outcome;
    lv_timer_t *timer;
} ui_music_mock_t;

/** Defaults: paused, laptop volume 40, no title, SIMULATED. */
void ui_music_mock_init(ui_music_mock_t *m);
/** A ui_music_command_fn: pass the mock as ctx to ui_music_open(). */
void ui_music_mock_command(ui_music_cmd_t cmd, void *ctx);
/** Puts the mock's state on screen (link forced to SIMULATED). */
void ui_music_mock_show(ui_music_mock_t *m);
/** Stops the pending answer (call before ui_music_close()). */
void ui_music_mock_stop(ui_music_mock_t *m);

#ifdef __cplusplus
}
#endif
