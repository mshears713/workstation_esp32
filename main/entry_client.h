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
 * @details Same wire shape as note_client.c/voice_inbox_client.c (both
 *          build on multipart_upload.c), kept as its own module for the
 *          same reason those two are separate from each other: GO's
 *          destination and eventual contract are independent of SEND's and
 *          NOTE's, so a future change to one can't accidentally affect the
 *          others.
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
 * task. If ENTRY_UPLOAD_PATH (backend_config.h) is empty, returns false
 * immediately with fail_reason_out = "ENDPOINT NOT SET" and never attempts
 * a request.
 */
bool entry_client_submit(const char *request_id, const uint8_t *pcm, size_t len,
                          uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                          char *fail_reason_out, size_t fail_reason_out_len);

#ifdef __cplusplus
}
#endif
