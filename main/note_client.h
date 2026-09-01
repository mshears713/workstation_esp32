/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 13 - dedicated upload path for voice-triggered NOTE (now
 *        SEND) recordings.
 * @details Streaming, not single-shot: a long SEND recording is sent as
 *          many small chunk POSTs followed by one finish POST instead of
 *          one big multipart request - see main/audio_capture.c's
 *          streaming recording loop for why. All the WAV-header/streaming
 *          choreography lives in stream_upload.c now, shared with
 *          voice_inbox_client.c/entry_client.c.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Matches audio_note_chunk_fn_t exactly (see audio_capture.h) so this can
 * be passed straight to audio_capture_start_note() as its chunk_fn.
 * Blocking, synchronous - called on the audio worker task, not the LVGL
 * task, once per ~15s of recording. If NOTE_UPLOAD_PATH (backend_config.h)
 * is empty, returns false immediately with fail_reason_out =
 * "ENDPOINT NOT SET" and never attempts a request.
 */
bool note_client_submit_chunk(const char *request_id, const uint8_t *pcm, size_t len,
                               char *fail_reason_out, size_t fail_reason_out_len);

/**
 * Matches audio_note_finish_fn_t exactly (see audio_capture.h) so this can
 * be passed straight to audio_capture_start_note() as its finish_fn. Called
 * once, after the last chunk - same synchronous-call contract as
 * note_client_submit_chunk().
 */
bool note_client_submit_finish(const char *request_id,
                                uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                                char *fail_reason_out, size_t fail_reason_out_len);

/**
 * Matches audio_note_cancel_fn_t exactly (see audio_capture.h) so this can
 * be passed straight to audio_capture_start_note() as its cancel_fn - see
 * entry_client_submit_cancel()'s doc comment for the shared contract.
 */
void note_client_submit_cancel(const char *request_id);

#ifdef __cplusplus
}
#endif
