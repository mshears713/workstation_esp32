/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Shared chunk+finish POST helpers for streaming NOTE/GO/SEND
 *        uploads - see stream_upload.c.
 * @details Replaces the old single-shot multipart_upload_post() call for
 *          these three recordings: a long capture is sent as many small
 *          chunk POSTs (raw PCM, no wrapper) followed by one finish POST
 *          (fields only, no audio) instead of one big multipart request -
 *          see main/audio_capture.c's streaming recording loop for why
 *          (a 10+ minute recording doesn't fit in the ESP32's PSRAM as one
 *          buffer). manual REC (audio_capture_start(), still exactly 4s)
 *          is untouched and still uses multipart_upload.c directly.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "multipart_upload.h" /* reuses multipart_text_field_t for finish's field list */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * POSTs one chunk of raw PCM bytes (the entire request body, no wrapper) to
 * `url` - see main/audio_capture.c's streaming recording loop, which calls
 * this once per ~15s of recording via the caller's own X_client_submit_chunk()
 * (note_client.c/voice_inbox_client.c/entry_client.c). Blocking, synchronous -
 * called on the audio worker task, not the LVGL task. On failure, fills
 * fail_reason_out the same way multipart_upload_post() does ("UPLOAD
 * TIMEOUT"/"UPLOAD UNREACHABLE"/"UPLOAD INIT FAILED"/"UPLOAD ERR <status>").
 */
bool stream_upload_chunk(const char *url, const uint8_t *pcm, size_t len, uint32_t offset,
                          uint32_t timeout_ms, char *fail_reason_out, size_t fail_reason_out_len);

/**
 * POSTs `fields` as a small application/x-www-form-urlencoded body (no
 * audio - the backend already has every byte via prior stream_upload_chunk()
 * calls) to `url`, telling the backend to assemble them into one WAV and
 * proceed exactly as it would for a single-shot upload. Field values are
 * sent as-is, not percent-encoded - safe today because every caller only
 * ever passes request_id (backend-validated alnum/._- already), a fixed
 * source string, numeric fields, and project_hint's small fixed enum -
 * revisit if a future field could ever contain '&' or '='. Fills
 * response_buf and *status_out the same way multipart_upload_post() does.
 */
bool stream_upload_finish(const char *url,
                           const multipart_text_field_t *fields, size_t field_count,
                           uint32_t timeout_ms,
                           char *response_buf, size_t response_buf_len, int *status_out,
                           char *fail_reason_out, size_t fail_reason_out_len);

/**
 * POSTs an empty body to `url` (the backend's `.../{request_id}/cancel`)
 * telling it to discard whatever chunks already arrived instead of
 * assembling/processing them - the CANCEL button's counterpart to
 * stream_upload_finish(). Best-effort, no retry: called from
 * audio_capture_cancel()'s path where the recording is already being
 * thrown away, so there's nothing meaningful to do with a failure beyond
 * logging it (worst case, an abandoned temp file sits on the backend -
 * harmless, not worth complicating the cancel path over).
 */
void stream_upload_cancel(const char *url, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
