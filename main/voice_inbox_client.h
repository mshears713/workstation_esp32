/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Upload path for voice-triggered NOTE recordings bound for the
 *        backend's Notion "Voice Inbox" pipeline - see voice_inbox_client.c.
 * @details Deliberately separate from note_client.c even though the wire
 *          format is identical (both build on multipart_upload.c) - NOTE
 *          and SEND POST to two different backend endpoints with two
 *          different destinations (Notion vs. the local LangGraph
 *          pipeline), so keeping them as distinct client modules means a
 *          future change to one destination's contract can't accidentally
 *          affect the other.
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
 * task. If VOICE_INBOX_UPLOAD_PATH (backend_config.h) is empty, returns
 * false immediately with fail_reason_out = "ENDPOINT NOT SET" and never
 * attempts a request.
 */
bool voice_inbox_client_submit(const char *request_id, const uint8_t *pcm, size_t len,
                                uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                                char *fail_reason_out, size_t fail_reason_out_len);

#ifdef __cplusplus
}
#endif
