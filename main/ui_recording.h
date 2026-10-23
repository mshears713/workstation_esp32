/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief The SEND / NOTE / GO recording overlay (gallery concept 09 Health
 *        Rail): a big timer, and down the left the four things a capture
 *        depends on - network, backend, microphone, upload. Pure LVGL;
 *        status_deck_ui.c maps voice_control + audio_capture + wifi_mgr +
 *        backend_health into ui_rec_view_t, the simulator maps fixtures.
 * @details Exit rules are the firmware's, unchanged: the overlay is on screen
 *          while voice_control says a capture is active and goes when it
 *          says so. CANCEL and (NOTE only) SEND are the only controls; the
 *          rail is display-only here, never a way out of a capture.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui_diag_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_REC_STARTING = 0,  /* command set, audio worker not started yet */
    UI_REC_ARMING,
    UI_REC_RECORDING,
    UI_REC_UPLOADING,     /* COMPLETE / UPLOADING: finishing the stream */
    UI_REC_READY,         /* upload accepted (transient) */
    UI_REC_CANCELLED,
    UI_REC_FAILED,        /* fail_reason says why; "ENDPOINT NOT SET" is its own case */
    UI_REC_RESULT,        /* capture over; voice_control holds `result` on screen */
} ui_rec_phase_t;

typedef struct {
    const char *command;        /* "SEND" / "NOTE" / "GO" */
    ui_rec_phase_t phase;
    uint32_t elapsed_ms;
    uint32_t target_ms;         /* duration_target_ms */
    bool uploader_behind;       /* mic stalled waiting for a free buffer */
    const char *fail_reason;    /* audio_cap fail_reason (FAILED) */
    const char *result;         /* voice_control last_result (RESULT) */
    const char *selector;       /* NOTE: project label, GO: repo label, else NULL */
    ui_diag_wifi_t wifi;
    ui_diag_api_t api;
} ui_rec_view_t;

typedef struct {
    lv_event_cb_t cancel, send, sel_prev, sel_next;
} ui_rec_actions_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *seg[4], *seg_glyph[4], *seg_word[4], *seg_tick[4];
    lv_obj_t *command, *status, *dest;
    lv_obj_t *timer_row, *dot, *timer, *aside, *aside2, *bar;
    lv_obj_t *stall;
    lv_obj_t *card, *card_word, *card_detail;
    lv_obj_t *sel, *sel_caption, *sel_label;
    lv_obj_t *cancel, *send;
} ui_recording_t;

/** Builds the overlay on `scr`, hidden. */
void ui_recording_build(ui_recording_t *r, lv_obj_t *scr, const ui_rec_actions_t *a);
/** Shows it (moved to the front) and fills it. */
void ui_recording_apply(ui_recording_t *r, const ui_rec_view_t *v);
void ui_recording_hide(ui_recording_t *r);

/** Above this a capture is long-form and ends with SEND (NOTE's 20 min cap
 *  is a safety stop); at or below it, it ends by itself. As status_deck_ui.c. */
#define UI_REC_AUTO_STOP_MAX_MS 60000

#ifdef __cplusplus
}
#endif
