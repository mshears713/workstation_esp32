/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 13 - First Real Commands: wake word + four-command
 *        MultiNet window (SEND/NOTE/GO/YES), with SEND, NOTE, and GO now
 *        doing real work.
 * @details Public interface for the WakeNet/MultiNet voice pipeline. App-
 *          agnostic like audio_capture.h - knows how to get from continuous
 *          microphone frames to a recognized command word. YES stays
 *          recognition-only (state + Black Box log, no side effect). SEND
 *          and NOTE both trigger a bounded voice-note recording (see
 *          run_send_command()/run_note_command() in voice_control.c, built
 *          on audio_capture.c's proven mic path) but upload to two different
 *          backend destinations - SEND to the local LangGraph pipeline,
 *          NOTE to a Notion "Voice Inbox" page (see note_client.h vs.
 *          voice_inbox_client.h). GO fires a one-shot backend graph-trigger
 *          request (see graph_client.h). None of the three blocks WakeNet
 *          for longer than the mic-ownership handoff/request actually
 *          requires.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_codec_dev.h"
#include "request_id.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_STATE_LISTENING = 0,      /* idle, WakeNet running continuously */
    VOICE_STATE_WAKE_DETECTED,      /* transient - immediately followed by COMMAND_WINDOW */
    VOICE_STATE_COMMAND_WINDOW,     /* MultiNet is listening for one of the four commands */
    VOICE_STATE_COMMAND_RECOGNIZED, /* held briefly (~1-2s) so the matching button can be shown highlighted */
    VOICE_STATE_TIMEOUT,            /* held briefly - command window closed with no command matched */
    VOICE_STATE_UNRECOGNIZED,       /* held briefly - MultiNet detected a phrase not in the four-command set */
    VOICE_STATE_SEND_ACTIVE,        /* SEND recognized - recording+upload in progress, see run_send_command() */
    VOICE_STATE_NOTE_ACTIVE,        /* NOTE recognized - recording+upload in progress, see run_note_command() */
    VOICE_STATE_GRAPH_ACTIVE,       /* GO recognized - request in flight/result on screen, see run_go_command() */
    VOICE_STATE_DEGRADED,           /* speech pipeline unavailable this boot - manual REC still works */
} voice_state_t;

/* The four recognition-test commands (Mission 12 tested six - SEND, NOTE,
 * GO, YES read reliably; RUN and TEST did not and were dropped). IDs start
 * at 1, matching esp_mn_commands_add()'s requirement, and double as the
 * array index (id-1) into the UI's four on-screen buttons - see
 * status_deck_ui.c's cmd_defs[]. */
typedef enum {
    VOICE_CMD_NONE = 0,
    VOICE_CMD_SEND = 1,
    VOICE_CMD_NOTE = 2,
    VOICE_CMD_GO   = 3,
    VOICE_CMD_YES  = 4,
} voice_command_id_t;

#define VOICE_COMMAND_COUNT 4

#define VOICE_COMMAND_NAME_LEN 32
#define VOICE_REASON_LEN 32

typedef struct {
    voice_state_t state;
    uint32_t wake_count;    /* increments once per WAKE_DETECTED, this boot only */
    uint32_t command_count; /* increments once per recognized command, this boot only */
    char last_command[VOICE_COMMAND_NAME_LEN];  /* "" until a command is recognized this boot */
    voice_command_id_t last_command_id;         /* VOICE_CMD_NONE until a command is recognized this boot */
    /* esp_timer_get_time() value (us) at which the current/most-recent
     * command window closes. Only meaningful while state ==
     * VOICE_STATE_COMMAND_WINDOW (or the transient WAKE_DETECTED just
     * before it) - the UI computes a remaining-seconds countdown from this
     * rather than voice_control.c pushing a tick every second, so a slower
     * UI poll cadence still renders a correct countdown. */
    int64_t command_window_deadline_us;
    /* Set when SEND, NOTE, or GO starts; valid for that command's cycle
     * (state == VOICE_STATE_SEND_ACTIVE / VOICE_STATE_NOTE_ACTIVE /
     * VOICE_STATE_GRAPH_ACTIVE). The "ID: ..." line on both overlays reads
     * this. */
    char active_request_id[REQUEST_ID_LEN];
    /* GO's outcome text ("GRAPH ACCEPTED", "ENDPOINT NOT SET", ...), valid
     * while state == VOICE_STATE_GRAPH_ACTIVE. SEND/NOTE's overlay does not
     * use this - it reads audio_capture_get_status() directly instead, since
     * that's already current-truth for a recording in progress. */
    char last_result[48];
    char fail_reason[VOICE_REASON_LEN];        /* "" unless state == VOICE_STATE_DEGRADED */
} voice_status_t;

/**
 * Called on the voice detect task - never the LVGL task. Same foreign-task-
 * callback contract as audio_cap_event_cb_t: implementations must not block
 * and must not touch LVGL directly.
 */
typedef void (*voice_event_cb_t)(voice_state_t new_state, const char *blackbox_message, void *user_ctx);

/**
 * Starts the continuous WakeNet/MultiNet pipeline against `mic_dev` (the
 * same ES7210 handle passed to audio_capture_init() - see that header's
 * Mission 11 comment on why this must be a single shared handle, not two).
 * Call once, after audio_capture_init(). If model init fails for any reason
 * (missing/incompatible flash models, alloc failure), this logs the failure,
 * leaves voice_control_get_status() reporting VOICE_STATE_DEGRADED for the
 * rest of the boot, and starts no tasks - the rest of the console, including
 * manual REC, is unaffected.
 */
void voice_control_init(esp_codec_dev_handle_t mic_dev, voice_event_cb_t cb, void *user_ctx);

/** Thread-safe snapshot of current status for rendering. */
void voice_control_get_status(voice_status_t *out);

/**
 * Requests a manual wake, as if the wake word had just been spoken - lets
 * the UI's TALK button open the real MultiNet command window without
 * anyone actually saying "computer". Safe to call from any task (the LVGL
 * task, for a button press). Fire-and-forget: takes effect on detect_task's
 * next poll, and is silently ignored if a command window is already open or
 * the pipeline never started (VOICE_STATE_DEGRADED - there is no
 * detect_task running to consume the request in that case).
 */
void voice_control_manual_wake(void);

#ifdef __cplusplus
}
#endif
