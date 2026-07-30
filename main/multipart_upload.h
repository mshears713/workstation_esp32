/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Shared multipart/form-data POST helper, extracted from note_client.c
 *        so voice_inbox_client.c doesn't duplicate its WAV-header-building
 *        and streaming-upload choreography.
 * @details Builds "<text fields>" + a WAV-wrapped audio part + closing
 *          boundary, streamed via esp_http_client's open/write API (a
 *          multipart body needs pieces written in order, and streaming the
 *          PCM straight from the caller's buffer avoids a second full-size
 *          copy just to prepend a WAV header). One blocking call, meant to
 *          be called synchronously from an audio worker task, same as
 *          note_client_submit()/voice_inbox_client_submit() already do.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const char *value;
} multipart_text_field_t;

/**
 * POSTs `fields` (in order) followed by one audio file part named "audio"
 * (a real WAV, header built internally from `pcm`/`len`/`sample_rate_hz`/
 * `bits_per_sample`/`channels`) to `url`. On success, fills `response_buf`
 * with up to `response_buf_len - 1` bytes of the response body (NUL
 * terminated) and `*status_out` with the HTTP status code, and returns
 * true - callers parse their own response JSON for whatever id key their
 * endpoint returns. On failure, returns false and fills `fail_reason_out`
 * with a short, ready-to-log reason ("UPLOAD TIMEOUT", "UPLOAD UNREACHABLE",
 * "UPLOAD INIT FAILED", or "UPLOAD ERR <status>" for a non-2xx response).
 */
bool multipart_upload_post(const char *url,
                            const multipart_text_field_t *fields, size_t field_count,
                            const uint8_t *pcm, size_t pcm_len,
                            uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                            uint32_t timeout_ms,
                            char *response_buf, size_t response_buf_len, int *status_out,
                            char *fail_reason_out, size_t fail_reason_out_len);

#ifdef __cplusplus
}
#endif
