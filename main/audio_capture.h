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
 *
 *          Mission 13: manual REC (audio_capture_start()) is completely
 *          unchanged - still exactly 4s, still auto-uploads to
 *          /api/v1/audio via this file's own upload_capture(). A second
 *          entry point, audio_capture_start_note(), shares the same mic
 *          open/read/close loop and mic-ownership contract but diverges for
 *          voice-triggered NOTE/GO/SEND recordings: a caller-chosen name
 *          (the request ID, so it becomes the artifact name/filename),
 *          early termination via audio_capture_stop() (the STOP button),
 *          and - since these can now run up to AUDIO_NOTE_MAX_DURATION_MS
 *          (20 minutes; too long to hold in one PSRAM buffer, see that
 *          constant's own comment) - streaming chunk uploads via a
 *          caller-supplied chunk_fn/finish_fn pair instead of one big
 *          upload at the end. note_client.c/voice_inbox_client.c/
 *          entry_client.c each implement that pair against their own
 *          endpoint.
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
    AUDIO_CAP_CANCELLED, /* audio_capture_cancel() was called - discarded, never uploaded/finished - transient like READY */
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
 * Called on the audio worker task once per filled chunk during a NOTE/GO/
 * SEND recording (never the LVGL task - same contract as
 * audio_cap_event_cb_t). Must POST/return, not queue-and-return -
 * audio_capture.c calls this synchronously from inside the recording loop,
 * roughly once per AUDIO_STREAM_CHUNK_MS of audio, so a slow chunk upload
 * simply delays the next mic read rather than losing samples (the codec's
 * own internal buffering absorbs that gap - see audio_capture.c's
 * streaming comment for the known trade-off here). A false return aborts
 * the whole recording (audio_cap_status_t moves to AUDIO_CAP_FAILED with
 * fail_reason_out as the reason) rather than silently dropping the chunk,
 * so a partial/gapped transcript is never sent for transcription.
 * `request_id` is the same string passed to audio_capture_start_note().
 */
typedef bool (*audio_note_chunk_fn_t)(const char *request_id, const uint8_t *pcm, size_t len,
                                       char *fail_reason_out, size_t fail_reason_out_len);

/**
 * Called once, after the last chunk (whether the recording ended via STOP
 * or the AUDIO_NOTE_MAX_DURATION_MS safety cap) - tells the backend no
 * more chunks are coming so it can assemble them into one WAV and start
 * transcription. Same synchronous-call contract as audio_note_chunk_fn_t;
 * a false return is handled the same way (AUDIO_CAP_FAILED).
 */
typedef bool (*audio_note_finish_fn_t)(const char *request_id,
                                        uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                                        char *fail_reason_out, size_t fail_reason_out_len);

/**
 * Called at most once, only when audio_capture_cancel() ends the recording
 * instead of it finishing normally - tells the backend to discard whatever
 * chunks already arrived rather than assembling/processing them. Never
 * called alongside finish_fn for the same capture (they're mutually
 * exclusive outcomes). No return value: this runs on a recording that's
 * already being thrown away, so a failure here has nothing to recover -
 * see stream_upload_cancel()'s own doc comment for why it's fire-and-forget.
 */
typedef void (*audio_note_cancel_fn_t)(const char *request_id);

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
 * Starts one streaming capture for a voice-triggered NOTE/GO/SEND: same
 * in-flight guard as audio_capture_start(), but named `request_id` (used
 * verbatim as the artifact name/filename instead of the auto "capture_NNN"
 * sequence) and capped at AUDIO_NOTE_MAX_DURATION_MS (see audio_capture.c,
 * currently 20 minutes - a wall-clock safety cap now, not a buffer-size
 * limit) rather than the manual-REC duration - meant to be ended early via
 * audio_capture_stop(), the cap is only the safety fallback if it isn't.
 * Unlike manual REC, this never holds the whole recording in RAM: `chunk_fn`
 * is called synchronously roughly every AUDIO_STREAM_CHUNK_MS with just
 * that chunk's bytes, and `finish_fn` once at the end - see their own doc
 * comments above for the streaming contract. Both are required (unlike the
 * old single upload_fn, neither can be NULL) - a NOTE/GO/SEND recording
 * that isn't going anywhere isn't a case this module supports, since the
 * chunks would otherwise need to be buffered somewhere for no purpose.
 */
bool audio_capture_start_note(const char *request_id, audio_note_chunk_fn_t chunk_fn,
                               audio_note_finish_fn_t finish_fn, audio_note_cancel_fn_t cancel_fn);

/**
 * Requests that the capture currently in progress end now, as if it had
 * just reached its target duration - the buffer keeps whatever was
 * captured up to this point (never discarded), and the normal finalize/
 * upload sequence runs immediately after. Has no effect if no capture is
 * running. Safe to call from any task, including the LVGL task (the
 * recording overlay's SEND button's event callback - status_deck_ui.c
 * still names the function/callback "stop" since that's what it does to
 * the recording; only the on-screen label changed to SEND, since ending
 * the recording here is also what sends it).
 */
void audio_capture_stop(void);

/**
 * Requests that the capture currently in progress be discarded entirely -
 * the CANCEL button's counterpart to audio_capture_stop(). Whatever chunks
 * already reached the backend are told to be dropped (see
 * audio_note_cancel_fn_t), the trailing partial chunk still sitting in the
 * on-device buffer is never flushed, and finish_fn is never called - the
 * capture ends in AUDIO_CAP_CANCELLED, not AUDIO_CAP_READY. Has no effect
 * if no capture is running (or if the capture is manual REC, which has no
 * cancel_fn to call - cancelling that just ends it without uploading, same
 * as never having a REC button do anything more than audio_capture_stop()
 * already made possible). Safe to call from any task, including the LVGL
 * task (the recording overlay's CANCEL button's event callback).
 */
void audio_capture_cancel(void);

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
