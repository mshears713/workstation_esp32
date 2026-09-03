/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Upload path for voice-triggered GO recordings, which the backend
 *        transcribes and files as a GitHub issue - see issue_client.c.
 * @details Same streaming wire format as voice_inbox_client.c (chunk POSTs
 *          then one finish POST, both via stream_upload.c), with two
 *          differences that matter:
 *
 *          1. The finish body carries `repo_id` - which repository the
 *             operator selected. That is an opaque id from
 *             GET /api/v1/projects, never an "owner/name" slug: the backend
 *             resolves it against its own allowlist, so this device cannot
 *             file against a repository nobody approved, and there is no
 *             GitHub token anywhere in this firmware.
 *
 *          2. The finish call blocks far longer than the other kinds'.
 *             Unlike NOTE/SEND, which return as soon as the upload is
 *             accepted and process in the background, /api/v1/issues/finish
 *             transcribes the audio and creates the issue *before*
 *             answering, so the device can show a real issue number instead
 *             of polling for one. Measured at a few seconds for a 15s clip.
 *             Safe: the mic is closed by then, so a slow finish costs no
 *             audio - the same reason chunk and finish have different retry
 *             budgets in stream_upload.c.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Matches audio_note_chunk_fn_t (audio_capture.h) so it can be passed
 * straight to audio_capture_start_note(). Blocking, called on the audio
 * uploader task. Returns false with "ENDPOINT NOT SET" if
 * ISSUE_UPLOAD_PATH is empty.
 */
bool issue_client_submit_chunk(const char *request_id, const uint8_t *pcm, size_t len, uint32_t offset,
                                char *fail_reason_out, size_t fail_reason_out_len);

/**
 * Matches audio_note_finish_fn_t (audio_capture.h). Sends the selected
 * repository along with the usual format fields, waits for the backend to
 * transcribe and file, and stores the resulting issue number where
 * issue_client_get_last_issue() can read it.
 *
 * The repository comes from backend_catalog_repo_id() at call time rather than
 * being passed in, so this keeps audio_note_finish_fn_t's signature and can
 * be handed to audio_capture_start_note() unchanged.
 */
bool issue_client_submit_finish(const char *request_id,
                                 uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                                 char *fail_reason_out, size_t fail_reason_out_len);

/**
 * Matches audio_note_cancel_fn_t (audio_capture.h). Best-effort, fire and
 * forget - the capture is already being thrown away.
 */
void issue_client_submit_cancel(const char *request_id);

/**
 * Issue number from the most recent successful finish, or 0 if none this
 * boot. Read by the UI so the confirmation can say "ISSUE #42" rather than
 * just "SENT" - the number is the whole point of waiting for the backend.
 */
uint32_t issue_client_get_last_issue(void);

#ifdef __cplusplus
}
#endif
