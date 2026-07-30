/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Shared multipart/form-data POST helper - see multipart_upload.h.
 * @details Extracted from note_client.c once voice_inbox_client.c needed the
 *          exact same WAV-wrapping/streaming-upload logic against a
 *          different URL and field set - the only per-caller differences
 *          were the URL, the text fields, and which response key to parse
 *          (left to the caller).
 */

#include <string.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "multipart_upload.h"

static const char *TAG = "multipart_upload";

#define MULTIPART_BOUNDARY "----WorkstationUpload7d1f2a9c"
#define WAV_HEADER_LEN 44
/* Must comfortably fit every text field's Content-Disposition block plus the
 * audio part's own header (name/filename/Content-Type lines) - the boundary
 * string alone appears once per field plus once for the audio part, so this
 * needs more headroom than it looks like at a glance. Four fields
 * (request_id, source, duration_seconds, sample_rate_hz) plus the audio
 * header measured at ~518 bytes with real values - 512 was too tight (and
 * was already tight before this boundary string existed), so this leaves
 * real headroom for a future field rather than being tuned to the current
 * exact byte count. The calling task (audio_capture.c's audio_worker) has a
 * 6144-byte stack, so this costs nothing that matters. */
#define PREAMBLE_BUF_LEN 768

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

/* Builds every "--BOUNDARY\r\nContent-Disposition: ...\r\n\r\nvalue\r\n" text
 * part in order, ending right where the WAV bytes need to start - built as
 * one string so its exact length is known before esp_http_client_open()
 * needs a Content-Length. */
static int build_preamble(char *out, size_t out_len,
                           const multipart_text_field_t *fields, size_t field_count)
{
    size_t pos = 0;
    for (size_t i = 0; i < field_count; i++) {
        int n = snprintf(out + pos, out_len - pos,
            "--" MULTIPART_BOUNDARY "\r\n"
            "Content-Disposition: form-data; name=\"%s\"\r\n\r\n"
            "%s\r\n",
            fields[i].name, fields[i].value);
        if (n < 0 || pos + (size_t)n >= out_len) {
            return -1;
        }
        pos += (size_t)n;
    }
    int n = snprintf(out + pos, out_len - pos,
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"audio\"; filename=\"note.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n");
    if (n < 0 || pos + (size_t)n >= out_len) {
        return -1;
    }
    pos += (size_t)n;
    return (int)pos;
}

bool multipart_upload_post(const char *url,
                            const multipart_text_field_t *fields, size_t field_count,
                            const uint8_t *pcm, size_t pcm_len,
                            uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels,
                            uint32_t timeout_ms,
                            char *response_buf, size_t response_buf_len, int *status_out,
                            char *fail_reason_out, size_t fail_reason_out_len)
{
    char preamble[PREAMBLE_BUF_LEN];
    int preamble_len = build_preamble(preamble, sizeof(preamble), fields, field_count);
    if (preamble_len < 0) {
        ESP_LOGE(TAG, "multipart preamble build failed/truncated");
        snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD INIT FAILED");
        return false;
    }

    static const char postamble[] = "\r\n--" MULTIPART_BOUNDARY "--\r\n";
    uint8_t wav_header[WAV_HEADER_LEN];
    build_wav_header(wav_header, (uint32_t)pcm_len, sample_rate_hz, bits_per_sample, channels);

    int total_len = preamble_len + WAV_HEADER_LEN + (int)pcm_len + (int)(sizeof(postamble) - 1);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = timeout_ms,
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
              && http_write_all(client, (const char *)pcm, (int)pcm_len)
              && http_write_all(client, postamble, (int)(sizeof(postamble) - 1));

    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;

    if (!ok) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (elapsed_ms >= (int64_t)timeout_ms - 200) {
            ESP_LOGW(TAG, "upload timed out after %lldms", (long long)elapsed_ms);
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD TIMEOUT");
        } else {
            ESP_LOGW(TAG, "upload failed after %lldms", (long long)elapsed_ms);
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD UNREACHABLE");
        }
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    int response_len = 0;
    if (content_length != 0) {
        int n;
        while (response_len < (int)response_buf_len - 1
               && (n = esp_http_client_read(client, response_buf + response_len, response_buf_len - 1 - response_len)) > 0) {
            response_len += n;
        }
    }
    response_buf[response_len] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    *status_out = status;
    if (status != 200 && status != 202) {
        ESP_LOGW(TAG, "upload rejected, status=%d body=%s", status, response_buf);
        snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD ERR %d", status);
        return false;
    }

    return true;
}
