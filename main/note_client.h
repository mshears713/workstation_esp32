/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 13 - dedicated upload path for voice-triggered NOTE
 *        recordings.
 * @details Deliberately its own small function, not a reuse of
 *          audio_capture.c's built-in upload_capture() (manual REC's
 *          path) - separately configurable (backend_config.h's
 *          NOTE_UPLOAD_PATH) so a future dedicated note endpoint (one that
 *          knows about request IDs, maybe kicks off transcription) can
 *          replace today's reuse of Mission 10's /api/v1/audio without
 *          touching audio_capture.c or voice_control.c at all.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Matches audio_note_upload_fn_t exactly (see audio_capture.h) so this can
 * be passed straight to audio_capture_start_note() as its upload_fn.
 * Blocking, synchronous - called on the audio worker task, not the LVGL
 * task. If NOTE_UPLOAD_PATH (backend_config.h) is empty, returns false
 * immediately with fail_reason_out = "ENDPOINT NOT SET" and never attempts
 * a request.
 */
bool note_client_submit(const char *request_id, const uint8_t *pcm, size_t len,
                         uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                         char *fail_reason_out, size_t fail_reason_out_len);

#ifdef __cplusplus
}
#endif
