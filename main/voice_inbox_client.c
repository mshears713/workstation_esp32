/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Upload path for voice-triggered NOTE recordings bound for the
 *        backend's Notion "Voice Inbox" pipeline - see voice_inbox_client.h.
 * @details Same shape as note_client.c: builds this endpoint's field list,
 *          calls multipart_upload.c's shared POST helper, then parses its
 *          own response for voice_inbox_id. The backend fills in Notion's
 *          Name/Captured At/Source/Original Transcript itself from the
 *          transcript + this request's metadata - this firmware only
 *          reports whether the upload was accepted, same as note_client.c
 *          does for its own endpoint.
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "multipart_upload.h"
#include "voice_inbox_client.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h not found. Define BACKEND_BASE_URL there, e.g. #define BACKEND_BASE_URL \"http://192.168.1.31:8000\" - your development laptop's LAN IP, not 127.0.0.1."
#endif

static const char *TAG = "voice_inbox_client";

#define VOICE_INBOX_UPLOAD_TIMEOUT_MS 10000
#define VOICE_INBOX_SOURCE "esp32-box3"
#define RESPONSE_BUF_LEN 256

bool voice_inbox_client_submit(const char *request_id, const uint8_t *pcm, size_t len,
                                uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                                char *fail_reason_out, size_t fail_reason_out_len)
{
    if (VOICE_INBOX_UPLOAD_PATH[0] == '\0') {
        ESP_LOGW(TAG, "VOICE_INBOX_UPLOAD_PATH not configured - not attempting a request");
        snprintf(fail_reason_out, fail_reason_out_len, "ENDPOINT NOT SET");
        return false;
    }

    uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    float duration_seconds = block_align > 0 ? (float)len / (float)(sample_rate_hz * block_align) : 0.0f;

    char duration_str[16];
    char sample_rate_str[16];
    snprintf(duration_str, sizeof(duration_str), "%.2f", (double)duration_seconds);
    snprintf(sample_rate_str, sizeof(sample_rate_str), "%u", (unsigned)sample_rate_hz);

    const multipart_text_field_t fields[] = {
        { "request_id", request_id },
        { "source", VOICE_INBOX_SOURCE },
        { "duration_seconds", duration_str },
        { "sample_rate_hz", sample_rate_str },
    };

    char url[160];
    snprintf(url, sizeof(url), "%s%s", BACKEND_BASE_URL, VOICE_INBOX_UPLOAD_PATH);

    char response_buf[RESPONSE_BUF_LEN];
    int status = 0;
    int64_t start_us = esp_timer_get_time();
    bool ok = multipart_upload_post(url, fields, sizeof(fields) / sizeof(fields[0]),
                                     pcm, len, sample_rate_hz, bits_per_sample, channels,
                                     VOICE_INBOX_UPLOAD_TIMEOUT_MS,
                                     response_buf, sizeof(response_buf), &status,
                                     fail_reason_out, fail_reason_out_len);
    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    if (!ok) {
        return false;
    }

    /* {voice_inbox_id, status, status_url, result_url, duplicate, ...} -
     * parsed best-effort for the log only, same as note_client.c. This
     * firmware reports whether the upload was accepted, not whether the
     * Notion page was ultimately created - it does not poll status_url/
     * result_url. */
    char voice_inbox_id[40] = "?";
    cJSON *resp = cJSON_Parse(response_buf);
    if (resp) {
        cJSON *id_item = cJSON_GetObjectItemCaseSensitive(resp, "voice_inbox_id");
        if (cJSON_IsString(id_item) && id_item->valuestring) {
            strncpy(voice_inbox_id, id_item->valuestring, sizeof(voice_inbox_id) - 1);
            voice_inbox_id[sizeof(voice_inbox_id) - 1] = '\0';
        }
        cJSON_Delete(resp);
    }

    ESP_LOGI(TAG, "voice-inbox upload accepted: id=%s voice_inbox_id=%s status=%d dur=%ums %u bytes, %lldms",
             request_id, voice_inbox_id, status, (unsigned)(duration_seconds * 1000), (unsigned)len, (long long)elapsed_ms);
    return true;
}
