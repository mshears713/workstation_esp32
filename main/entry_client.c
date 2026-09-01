/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Upload path for voice-triggered GO recordings - see
 *        entry_client.h.
 * @details Same shape as note_client.c/voice_inbox_client.c: builds this
 *          endpoint's field list, calls stream_upload.c's shared chunk/
 *          finish POST helpers, then parses the finish response for
 *          entry_id. The backend fills in Notion's Sources/Van Build Log
 *          records itself from the transcript - this firmware only reports
 *          whether the upload was accepted, same as the other two upload
 *          paths.
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "stream_upload.h"
#include "entry_client.h"
#include "project_selector.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h not found. Define BACKEND_BASE_URL there, e.g. #define BACKEND_BASE_URL \"http://192.168.1.31:8000\" - your development laptop's LAN IP, not 127.0.0.1."
#endif

static const char *TAG = "entry_client";

#define ENTRY_UPLOAD_TIMEOUT_MS 10000
#define ENTRY_SOURCE "esp32-box3"
#define RESPONSE_BUF_LEN 256

bool entry_client_submit_chunk(const char *request_id, const uint8_t *pcm, size_t len,
                                char *fail_reason_out, size_t fail_reason_out_len)
{
    if (ENTRY_UPLOAD_PATH[0] == '\0') {
        ESP_LOGW(TAG, "ENTRY_UPLOAD_PATH not configured - not attempting a request");
        snprintf(fail_reason_out, fail_reason_out_len, "ENDPOINT NOT SET");
        return false;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s%s/%s/chunk", BACKEND_BASE_URL, ENTRY_UPLOAD_PATH, request_id);

    return stream_upload_chunk(url, pcm, len, ENTRY_UPLOAD_TIMEOUT_MS, fail_reason_out, fail_reason_out_len);
}

bool entry_client_submit_finish(const char *request_id,
                                 uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                                 char *fail_reason_out, size_t fail_reason_out_len)
{
    if (ENTRY_UPLOAD_PATH[0] == '\0') {
        ESP_LOGW(TAG, "ENTRY_UPLOAD_PATH not configured - not attempting a request");
        snprintf(fail_reason_out, fail_reason_out_len, "ENDPOINT NOT SET");
        return false;
    }

    char sample_rate_str[16], bits_str[8], channels_str[8];
    snprintf(sample_rate_str, sizeof(sample_rate_str), "%u", (unsigned)sample_rate_hz);
    snprintf(bits_str, sizeof(bits_str), "%u", (unsigned)bits_per_sample);
    snprintf(channels_str, sizeof(channels_str), "%u", (unsigned)channels);

    const multipart_text_field_t fields[] = {
        { "request_id", request_id },
        { "source", ENTRY_SOURCE },
        { "sample_rate_hz", sample_rate_str },
        { "bits_per_sample", bits_str },
        { "channels", channels_str },
        /* Mike's VAN1/VAN2/GEN/NONE button selection (see
         * project_selector.h) - tells the backend's entry architect which
         * project this recording is for instead of leaving it to guess
         * van_or_scope from the transcript alone. */
        { "project_hint", project_selector_get_hint() },
    };

    char url[160];
    snprintf(url, sizeof(url), "%s%s/finish", BACKEND_BASE_URL, ENTRY_UPLOAD_PATH);

    char response_buf[RESPONSE_BUF_LEN];
    int status = 0;
    int64_t start_us = esp_timer_get_time();
    bool ok = stream_upload_finish(url, fields, sizeof(fields) / sizeof(fields[0]),
                                    ENTRY_UPLOAD_TIMEOUT_MS,
                                    response_buf, sizeof(response_buf), &status,
                                    fail_reason_out, fail_reason_out_len);
    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    if (!ok) {
        return false;
    }

    /* {entry_id, status, status_url, result_url, duplicate, ...} - parsed
     * best-effort for the log only, same as note_client.c/
     * voice_inbox_client.c. This firmware reports whether the upload was
     * accepted, not the eventual entry-architect/Notion/verifier result -
     * it does not poll status_url/result_url. */
    char entry_id[40] = "?";
    cJSON *resp = cJSON_Parse(response_buf);
    if (resp) {
        cJSON *id_item = cJSON_GetObjectItemCaseSensitive(resp, "entry_id");
        if (cJSON_IsString(id_item) && id_item->valuestring) {
            strncpy(entry_id, id_item->valuestring, sizeof(entry_id) - 1);
            entry_id[sizeof(entry_id) - 1] = '\0';
        }
        cJSON_Delete(resp);
    }

    ESP_LOGI(TAG, "entry finish accepted: id=%s entry_id=%s status=%d %lldms",
             request_id, entry_id, status, (long long)elapsed_ms);
    return true;
}

void entry_client_submit_cancel(const char *request_id)
{
    if (ENTRY_UPLOAD_PATH[0] == '\0') {
        return;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s%s/%s/cancel", BACKEND_BASE_URL, ENTRY_UPLOAD_PATH, request_id);
    stream_upload_cancel(url, ENTRY_UPLOAD_TIMEOUT_MS);
    ESP_LOGI(TAG, "entry cancelled: id=%s", request_id);
}
