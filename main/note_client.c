/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 13 - dedicated upload path for voice-triggered NOTE
 *        recordings - see note_client.h.
 * @details POST /api/v1/notes wants multipart/form-data (request_id,
 *          source, duration_seconds, sample_rate_hz, and an `audio` file
 *          part - a real WAV, not raw PCM), so this uses esp_http_client's
 *          streaming open/write API instead of the single
 *          set_post_field()+perform() call the other clients in this
 *          project use - a multipart body needs pieces written in order
 *          (text fields, WAV header, then the PCM bytes), and streaming the
 *          PCM straight from audio_capture.c's existing buffer avoids
 *          building a second full-size copy just to prepend a WAV header.
 *          Still one blocking call, same as before - called synchronously
 *          from perform_capture() on the audio worker task.
 */

#include <string.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "note_client.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h not found. Define BACKEND_BASE_URL there, e.g. #define BACKEND_BASE_URL \"http://192.168.1.31:8000\" - your development laptop's LAN IP, not 127.0.0.1."
#endif

static const char *TAG = "note_client";

#define NOTE_UPLOAD_TIMEOUT_MS 10000
#define NOTE_SOURCE "esp32-box3"
#define MULTIPART_BOUNDARY "----WorkstationNote7d1f2a9c"
#define RESPONSE_BUF_LEN 256
#define WAV_HEADER_LEN 44

static void write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* Standard 44-byte canonical PCM WAV header - the four capture chunks
 * (RIFF/fmt /data sizes + format fields) any WAV reader expects before the
 * raw samples. */
static void build_wav_header(uint8_t header[WAV_HEADER_LEN], uint32_t data_len,
                              uint32_t sample_rate_hz, uint16_t bits_per_sample, uint16_t channels)
{
    uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    uint32_t byte_rate = sample_rate_hz * block_align;

    memcpy(header, "RIFF", 4);
    write_le32(header + 4, 36 + data_len);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    write_le32(header + 16, 16);
    write_le16(header + 20, 1); /* PCM */
    write_le16(header + 22, channels);
    write_le32(header + 24, sample_rate_hz);
    write_le32(header + 28, byte_rate);
    write_le16(header + 32, block_align);
    write_le16(header + 34, bits_per_sample);
    memcpy(header + 36, "data", 4);
    write_le32(header + 40, data_len);
}

/* esp_http_client_write() writes at most one TCP segment's worth per call
 * for a large buffer - loop until the whole chunk is sent or a real error
 * occurs, rather than assuming one call covers it (that assumption would
 * have silently truncated the audio part on a big recording). */
static bool http_write_all(esp_http_client_handle_t client, const char *buf, int len)
{
    int written = 0;
    while (written < len) {
        int n = esp_http_client_write(client, buf + written, len - written);
        if (n <= 0) {
            return false;
        }
        written += n;
    }
    return true;
}

bool note_client_submit(const char *request_id, const uint8_t *pcm, size_t len,
                         uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                         char *fail_reason_out, size_t fail_reason_out_len)
{
    if (NOTE_UPLOAD_PATH[0] == '\0') {
        ESP_LOGW(TAG, "NOTE_UPLOAD_PATH not configured - not attempting a request");
        snprintf(fail_reason_out, fail_reason_out_len, "ENDPOINT NOT SET");
        return false;
    }

    uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    float duration_seconds = block_align > 0 ? (float)len / (float)(sample_rate_hz * block_align) : 0.0f;

    /* Every field the /api/v1/notes contract wants, in order, ending right
     * where the WAV bytes need to start - built as one string so its exact
     * length is known before esp_http_client_open() needs a Content-Length. */
    char preamble[512];
    int preamble_len = snprintf(preamble, sizeof(preamble),
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"request_id\"\r\n\r\n"
        "%s\r\n"
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"source\"\r\n\r\n"
        NOTE_SOURCE "\r\n"
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"duration_seconds\"\r\n\r\n"
        "%.2f\r\n"
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"sample_rate_hz\"\r\n\r\n"
        "%u\r\n"
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"audio\"; filename=\"note.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n",
        request_id, (double)duration_seconds, (unsigned)sample_rate_hz);
    if (preamble_len < 0 || (size_t)preamble_len >= sizeof(preamble)) {
        ESP_LOGE(TAG, "multipart preamble build failed/truncated");
        snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD INIT FAILED");
        return false;
    }

    static const char postamble[] = "\r\n--" MULTIPART_BOUNDARY "--\r\n";
    uint8_t wav_header[WAV_HEADER_LEN];
    build_wav_header(wav_header, (uint32_t)len, sample_rate_hz, bits_per_sample, channels);

    int total_len = preamble_len + WAV_HEADER_LEN + (int)len + (int)(sizeof(postamble) - 1);

    char url[160];
    snprintf(url, sizeof(url), "%s%s", BACKEND_BASE_URL, NOTE_UPLOAD_PATH);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = NOTE_UPLOAD_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed for url=%s", url);
        snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD INIT FAILED");
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", "multipart/form-data; boundary=" MULTIPART_BOUNDARY);

    int64_t start_us = esp_timer_get_time();

    bool ok = esp_http_client_open(client, total_len) == ESP_OK
              && http_write_all(client, preamble, preamble_len)
              && http_write_all(client, (const char *)wav_header, WAV_HEADER_LEN)
              && http_write_all(client, (const char *)pcm, (int)len)
              && http_write_all(client, postamble, (int)(sizeof(postamble) - 1));

    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;

    if (!ok) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (elapsed_ms >= NOTE_UPLOAD_TIMEOUT_MS - 200) {
            ESP_LOGW(TAG, "note upload timed out after %lldms", (long long)elapsed_ms);
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD TIMEOUT");
        } else {
            ESP_LOGW(TAG, "note upload failed after %lldms", (long long)elapsed_ms);
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD UNREACHABLE");
        }
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    char response_buf[RESPONSE_BUF_LEN];
    int response_len = 0;
    if (content_length != 0) {
        int n;
        while (response_len < (int)sizeof(response_buf) - 1
               && (n = esp_http_client_read(client, response_buf + response_len, sizeof(response_buf) - 1 - response_len)) > 0) {
            response_len += n;
        }
    }
    response_buf[response_len] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    /* {note_id/run_id, status, status_url, result_url, duplicate, ...} -
     * parsed best-effort for the log only. This firmware reports whether
     * the upload was accepted, not the eventual transcription/graph
     * result - it does not poll status_url/result_url. */
    char note_id[40] = "?";
    cJSON *resp = cJSON_ParseWithLength(response_buf, (size_t)response_len);
    if (resp) {
        cJSON *id_item = cJSON_GetObjectItemCaseSensitive(resp, "note_id");
        if (!cJSON_IsString(id_item)) {
            id_item = cJSON_GetObjectItemCaseSensitive(resp, "run_id");
        }
        if (cJSON_IsString(id_item) && id_item->valuestring) {
            strncpy(note_id, id_item->valuestring, sizeof(note_id) - 1);
            note_id[sizeof(note_id) - 1] = '\0';
        }
        cJSON_Delete(resp);
    }

    if (status != 200 && status != 202) {
        ESP_LOGW(TAG, "note upload rejected, status=%d body=%s", status, response_buf);
        snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD ERR %d", status);
        return false;
    }

    ESP_LOGI(TAG, "note upload accepted: id=%s note_id=%s status=%d dur=%ums %u bytes, %lldms",
             request_id, note_id, status, (unsigned)(duration_seconds * 1000), (unsigned)len, (long long)elapsed_ms);
    return true;
}
