/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 10 — Capture the Transmission: bounded microphone capture
 * @details Public interface for the audio-capture state machine described in
 *          audio_capture.c. Deliberately app-agnostic (knows nothing about
 *          app_state or LVGL) - it owns the microphone-to-bounded-buffer
 *          boundary and, as of this revision, uploads the result to the
 *          backend over the Wi-Fi link wifi_manager.c already brings up,
 *          mirroring handshake_client.c's HTTP POST shape rather than
 *          duplicating it (audio bytes are too large for handshake_client's
 *          small JSON payload, so this module makes its own short-lived
 *          esp_http_client request instead of reusing that module).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_CAP_IDLE = 0,
    AUDIO_CAP_ARMING,
    AUDIO_CAP_RECORDING,
    AUDIO_CAP_COMPLETE,
    AUDIO_CAP_UPLOADING, /* POSTing the buffer to the backend over Wi-Fi - see upload_capture() in audio_capture.c */
    AUDIO_CAP_READY,     /* upload accepted, artifact name valid - transient, worker returns to IDLE right after */
    AUDIO_CAP_FAILED,
} audio_cap_state_t;

#define AUDIO_CAP_ARTIFACT_NAME_LEN 24
#define AUDIO_CAP_REASON_LEN 24

typedef struct {
    audio_cap_state_t state;
    uint32_t sample_rate_hz;
    uint8_t bits_per_sample;
    uint8_t channels;
    uint32_t duration_target_ms;
    uint32_t elapsed_ms;          /* meaningful while RECORDING, holds final value at COMPLETE/READY */
    uint32_t bytes_captured;
    uint32_t samples_captured;
    float peak_amplitude;         /* 0.0-1.0, normalized against full-scale 16-bit */
    float rms_amplitude;          /* 0.0-1.0, normalized, meaningful once COMPLETE */
    uint32_t clipped_samples;     /* samples within a hair of full-scale */
    uint32_t capture_seq;         /* increments once per completed capture, this boot only */
    char artifact_name[AUDIO_CAP_ARTIFACT_NAME_LEN]; /* "" until COMPLETE, e.g. "capture_003" */
    char fail_reason[AUDIO_CAP_REASON_LEN];          /* "" unless state == AUDIO_CAP_FAILED */
} audio_cap_status_t;

/**
 * Called on the audio worker task - never the LVGL task. Implementations
 * must not block and must not touch LVGL. `blackbox_message` is short,
 * ready-to-log, valid only for the duration of the call.
 */
typedef void (*audio_cap_event_cb_t)(audio_cap_state_t new_state, const char *blackbox_message, void *user_ctx);

/**
 * Allocates the bounded capture buffer and starts the persistent worker
 * task. `mic_dev` is the ES7210 microphone codec handle from
 * bsp_audio_codec_microphone_init() - as of Mission 11, this module no
 * longer creates that handle itself, because voice_control.c needs the same
 * physical codec for continuous WakeNet listening. Exactly one handle must
 * exist for the mic; the caller (status_deck_ui.c) creates it once and
 * hands it to both this module and voice_control_init(), which is also the
 * single owner deciding whose turn it is to actually read from it (see
 * voice_control.c's mic-ownership comment). Call once, from any task, after
 * bsp_display_start() (BSP audio init shares the I2C bus with
 * bsp_i2c_init(), already brought up for the display/touch/IMU path). If
 * `mic_dev` is NULL or buffer allocation fails, this logs the failure and
 * audio_capture_start() will always return false afterward - the rest of
 * the console keeps running regardless.
 */
void audio_capture_init(esp_codec_dev_handle_t mic_dev, audio_cap_event_cb_t cb, void *user_ctx);

/** Thread-safe snapshot of current status for rendering. */
void audio_capture_get_status(audio_cap_status_t *out);

/**
 * Starts one bounded capture (AUDIO_CAPTURE_DURATION_MS, see audio_capture.c).
 * Returns false and does nothing if a capture is already in progress (the
 * in-flight guard) or if init failed to bring up the mic/buffer - callers
 * should treat that as "button press had no effect," not an error worth
 * reporting on its own.
 */
bool audio_capture_start(void);

/**
 * Read-only access to the most recently completed capture buffer, valid
 * from AUDIO_CAP_COMPLETE onward until the next audio_capture_start()
 * overwrites it in place. Returns NULL if no capture has completed yet this
 * boot. audio_capture.c uses this internally to upload the buffer once a
 * capture completes; exposed publicly too in case a later mission wants
 * the raw bytes for something else (transcription, local playback, etc.)
 * without this module needing to know anything about that use case.
 */
const uint8_t *audio_capture_get_buffer(size_t *len_out);

#ifdef __cplusplus
}
#endif
