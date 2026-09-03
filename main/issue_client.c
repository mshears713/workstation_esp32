/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Upload path for voice-triggered GO recordings, which the backend
 *        transcribes and files as a GitHub issue - see issue_client.h.
 * @details Same shape as voice_inbox_client.c: build this endpoint's field
 *          list, call stream_upload.c's shared chunk/finish helpers, parse
 *          the finish response. What differs is the repo_id field and the
 *          much longer finish timeout - see the constants below.
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "stream_upload.h"
#include "backend_catalog.h"
#include "issue_client.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h not found. Define BACKEND_BASE_URL there, e.g. #define BACKEND_BASE_URL \"http://192.168.1.31:8000\" - your development laptop's LAN IP, not 127.0.0.1."
#endif

static const char *TAG = "issue_client";

/* Chunks use the shared CHUNK_UPLOAD_TIMEOUT_MS like every other kind - they
 * run while the recording is still going and must fit inside the buffering
 * slack (see stream_upload.c's UPLOAD_CHUNK_MAX_ATTEMPTS).
 *
 * Finish does not. This endpoint transcribes the audio and creates the
 * GitHub issue before it answers, so 10s - fine for the other kinds, which
 * only accept an upload and process later - would time out on a perfectly
 * healthy request. Measured at a few seconds for a 15s clip; 45s leaves room
 * for a slow model or a slow GitHub without the operator waiting forever.
 *
 * Costs nothing: the mic is closed by the time finish runs, so a slow finish
 * loses no audio. Same reasoning that gave chunk and finish different retry
 * budgets in stream_upload.c. */
#define ISSUE_FINISH_TIMEOUT_MS 45000
#define ISSUE_SOURCE "esp32-box3"

/* Bigger than the other clients' 256: the finish response carries the issue
 * URL as well as the usual ids, and a truncated body would lose the issue
 * number - the one thing the operator is waiting for. */
#define RESPONSE_BUF_LEN 512

static uint32_t s_last_issue_number = 0;

uint32_t issue_client_get_last_issue(void)
{
    return s_last_issue_number;
}

bool issue_client_submit_chunk(const char *request_id, const uint8_t *pcm, size_t len, uint32_t offset,
                                char *fail_reason_out, size_t fail_reason_out_len)
{
    if (ISSUE_UPLOAD_PATH[0] == '\0') {
        ESP_LOGW(TAG, "ISSUE_UPLOAD_PATH not configured - not attempting a request");
        snprintf(fail_reason_out, fail_reason_out_len, "ENDPOINT NOT SET");
        return false;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s%s/%s/chunk", BACKEND_BASE_URL, ISSUE_UPLOAD_PATH, request_id);

    return stream_upload_chunk(url, pcm, len, offset, CHUNK_UPLOAD_TIMEOUT_MS,
                                fail_reason_out, fail_reason_out_len);
}

bool issue_client_submit_finish(const char *request_id,
                                 uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                                 char *fail_reason_out, size_t fail_reason_out_len)
{
    if (ISSUE_UPLOAD_PATH[0] == '\0') {
        ESP_LOGW(TAG, "ISSUE_UPLOAD_PATH not configured - not attempting a request");
        snprintf(fail_reason_out, fail_reason_out_len, "ENDPOINT NOT SET");
        return false;
    }

    const char *repo_id = backend_catalog_repo_id();
    if (repo_id[0] == '\0') {
        /* Nothing selected, or the catalog never loaded. Refusing here is
         * better than letting the backend reject it after transcription has
         * already been paid for. */
        ESP_LOGW(TAG, "no repository selected - not submitting");
        snprintf(fail_reason_out, fail_reason_out_len, "NO REPO SELECTED");
        return false;
    }

    char sample_rate_str[16], bits_str[8], channels_str[8];
    snprintf(sample_rate_str, sizeof(sample_rate_str), "%u", (unsigned)sample_rate_hz);
    snprintf(bits_str, sizeof(bits_str), "%u", (unsigned)bits_per_sample);
    snprintf(channels_str, sizeof(channels_str), "%u", (unsigned)channels);

    const multipart_text_field_t fields[] = {
        { "request_id", request_id },
        { "source", ISSUE_SOURCE },
        { "sample_rate_hz", sample_rate_str },
        { "bits_per_sample", bits_str },
        { "channels", channels_str },
        /* An opaque catalog id, never an "owner/name" slug. The backend
         * resolves it against its own allowlist, which is what stops this
         * device from filing against a repository nobody approved. */
        { "repo_id", repo_id },
    };

    char url[160];
    snprintf(url, sizeof(url), "%s%s/finish", BACKEND_BASE_URL, ISSUE_UPLOAD_PATH);

    char response_buf[RESPONSE_BUF_LEN];
    int status = 0;
    int64_t start_us = esp_timer_get_time();
    bool ok = stream_upload_finish(url, fields, sizeof(fields) / sizeof(fields[0]),
                                    ISSUE_FINISH_TIMEOUT_MS,
                                    response_buf, sizeof(response_buf), &status,
                                    fail_reason_out, fail_reason_out_len);
    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    if (!ok) {
        return false;
    }

    /* {request_id, repo, title, status, issue:{number,url}, duplicate}.
     * Unlike the other clients, this response is not parsed just for the log
     * - the issue number goes on screen, so the operator learns the capture
     * became a real issue rather than only that an upload was accepted. */
    uint32_t issue_number = 0;
    cJSON *resp = cJSON_Parse(response_buf);
    if (resp) {
        cJSON *issue = cJSON_GetObjectItemCaseSensitive(resp, "issue");
        if (cJSON_IsObject(issue)) {
            cJSON *num = cJSON_GetObjectItemCaseSensitive(issue, "number");
            if (cJSON_IsNumber(num) && num->valuedouble > 0) {
                issue_number = (uint32_t)num->valuedouble;
            }
        }
        cJSON_Delete(resp);
    }

    if (issue_number == 0) {
        /* The backend said yes but we could not read a number out of it -
         * report that honestly rather than showing a confident "SENT" for a
         * response we did not understand. */
        ESP_LOGW(TAG, "issue finish accepted (status=%d) but no issue number in response", status);
        snprintf(fail_reason_out, fail_reason_out_len, "NO ISSUE NUMBER");
        return false;
    }

    s_last_issue_number = issue_number;
    ESP_LOGI(TAG, "issue created: id=%s repo=%s number=%lu status=%d %lldms",
             request_id, repo_id, (unsigned long)issue_number, status, (long long)elapsed_ms);
    return true;
}

void issue_client_submit_cancel(const char *request_id)
{
    if (ISSUE_UPLOAD_PATH[0] == '\0') {
        return;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s%s/%s/cancel", BACKEND_BASE_URL, ISSUE_UPLOAD_PATH, request_id);
    stream_upload_cancel(url, CHUNK_UPLOAD_TIMEOUT_MS);
    ESP_LOGI(TAG, "issue capture cancelled: id=%s", request_id);
}
