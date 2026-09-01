/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Upload path for voice-triggered GO recordings, bound for the
 *        backend's entry-architect/Notion pipeline (Sources + Van Build
 *        Log) - see entry_client.c.
 * @details Streaming, not single-shot: a long GO recording is sent as many
 *          small chunk POSTs followed by one finish POST instead of one big
 *          multipart request - see main/audio_capture.c's streaming
 *          recording loop for why. Same wire shape as note_client.c/
 *          voice_inbox_client.c (both build on stream_upload.c), kept as
 *          its own module for the same reason those two are separate from
 *          each other: GO's destination and eventual contract are
 *          independent of SEND's and NOTE's.
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
 * task, once per ~15s of recording. If ENTRY_UPLOAD_PATH (backend_config.h)
 * is empty, returns false immediately with fail_reason_out =
 * "ENDPOINT NOT SET" and never attempts a request.
 */
bool entry_client_submit_chunk(const char *request_id, const uint8_t *pcm, size_t len,
                                char *fail_reason_out, size_t fail_reason_out_len);

/**
 * Matches audio_note_finish_fn_t exactly (see audio_capture.h) so this can
 * be passed straight to audio_capture_start_note() as its finish_fn. Called
 * once, after the last chunk, telling the backend to assemble everything
 * and kick off the entry-architect pipeline - same synchronous-call
 * contract as entry_client_submit_chunk().
 */
bool entry_client_submit_finish(const char *request_id,
                                 uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                                 char *fail_reason_out, size_t fail_reason_out_len);

/**
 * Matches audio_note_cancel_fn_t exactly (see audio_capture.h) so this can
 * be passed straight to audio_capture_start_note() as its cancel_fn. The
 * CANCEL button's counterpart to entry_client_submit_finish() - tells the
 * backend to discard whatever chunks already arrived instead of
 * processing them. Best-effort, no return value: called on a recording
 * that's already being thrown away, so there's nothing to recover from.
 */
void entry_client_submit_cancel(const char *request_id);

#ifdef __cplusplus
}
#endif
