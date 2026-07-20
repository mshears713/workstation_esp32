/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 11 - Computer Is Listening: local wake word + command
 * @details Public interface for the WakeNet/MultiNet voice pipeline. App-
 *          agnostic like audio_capture.h - knows how to get from continuous
 *          microphone frames to a recognized command, and to trigger the
 *          existing bounded capture-and-upload state machine when that
 *          command is "start recording," but nothing about LVGL/app_state.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_STATE_LISTENING = 0,   /* idle, WakeNet running continuously */
    VOICE_STATE_WAKE_DETECTED,   /* transient - immediately followed by COMMAND_WINDOW */
    VOICE_STATE_COMMAND_WINDOW,  /* MultiNet is listening for a short command */
    VOICE_STATE_COMMAND_RECOGNIZED, /* transient */
    VOICE_STATE_TIMEOUT,         /* transient - command window closed unrecognized/timed out */
    VOICE_STATE_CAPTURING,       /* a voice-triggered capture/upload is in flight - see AUD row for detail */
    VOICE_STATE_DEGRADED,        /* speech pipeline unavailable this boot - manual REC still works */
} voice_state_t;

#define VOICE_COMMAND_NAME_LEN 32
#define VOICE_REASON_LEN 32

typedef struct {
    voice_state_t state;
    uint32_t wake_count;    /* increments once per WAKE_DETECTED, this boot only */
    uint32_t command_count; /* increments once per recognized command, this boot only */
    char last_command[VOICE_COMMAND_NAME_LEN]; /* "" until a command is recognized this boot */
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

#ifdef __cplusplus
}
#endif
