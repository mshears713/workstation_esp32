/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Shared chunk+finish POST helpers - see stream_upload.h.
 * @details Two very different request shapes on purpose: stream_upload_chunk()
 *          is a single raw-bytes POST (no multipart, no response body needed -
 *          same "the body IS the audio" shape audio_capture.c's own
 *          upload_capture() already uses for Mission 10's /api/v1/audio),
 *          while stream_upload_finish() is a small
 *          application/x-www-form-urlencoded POST with no audio at all - the
 *          backend already has every byte via prior chunk calls (see the
 *          backend's app/api/streaming_capture.py).
 */

#include <string.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stream_upload.h"

static const char *TAG = "stream_upload";

#define FINISH_BODY_BUF_LEN 256

/* A recording that walks outside wifi range mid-chunk shouldn't die on the
 * first dropped packet - retry a transport failure (timeout/unreachable)
 * a few times before giving up. Only transport failures retry; a real HTTP
 * error response (backend reachable, request rejected) means retrying the
 * identical bytes won't help, so those still fail immediately. 4 total
 * attempts, not a measured/tuned number - "a few tries over a couple
 * seconds" is the goal, not a specific guarantee. */
#define UPLOAD_RETRY_MAX_ATTEMPTS 4
#define UPLOAD_RETRY_DELAY_MS 500

/* Chunks retry less than the finish call does, and the reason is a hard
 * constraint rather than a preference: a chunk upload runs while the
 * recording is still going, and audio_capture.c only has one spare buffer
 * of slack (AUDIO_STREAM_CHUNK_MS, 15s) before the mic has to stop. The
 * whole retry budget must therefore fit inside that slack, or a transient
 * outage stalls the recording instead of being absorbed by it.
 *
 *   2 attempts x CHUNK_UPLOAD_TIMEOUT_MS + 1 x UPLOAD_RETRY_DELAY_MS
 *   = 10,500ms worst case, against 15,000ms of slack.
 *
 * Measured before this was tuned: 4 attempts at a 10s timeout took 41,581ms
 * with the backend down, which stalled the mic for 26,588ms and froze the
 * on-screen timer for the whole time. The recording was doomed either way -
 * the backend was gone - so spending 41s to discover that was pure cost.
 * The finish call keeps the longer budget: it runs after the mic is closed,
 * so a slow retry there costs no audio. */
#define UPLOAD_CHUNK_MAX_ATTEMPTS 2

typedef struct {
    char *buf;
    size_t buf_len;
    size_t written;
} response_ctx_t;

static esp_err_t response_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && !esp_http_client_is_chunked_response(evt->client)) {
        response_ctx_t *ctx = (response_ctx_t *)evt->user_data;
        if (ctx && ctx->buf && ctx->buf_len > 0) {
            int room = (int)ctx->buf_len - 1 - (int)ctx->written;
            int copy_len = evt->data_len < room ? evt->data_len : room;
            if (copy_len > 0) {
                memcpy(ctx->buf + ctx->written, evt->data, (size_t)copy_len);
                ctx->written += (size_t)copy_len;
                ctx->buf[ctx->written] = '\0';
            }
        }
    }
    return ESP_OK;
}

/* Same fast-failure-vs-rode-out-the-timeout inference every other client
 * module in this codebase uses (handshake_client.c, notification_client.c,
 * multipart_upload.c, ...): esp_http_client_perform doesn't expose
 * "connection refused" vs. "nothing answered" directly. */
static bool report_transport_failure(const char *op, esp_err_t err, int64_t elapsed_ms, uint32_t timeout_ms,
                                      char *fail_reason_out, size_t fail_reason_out_len)
{
    if (elapsed_ms >= (int64_t)timeout_ms - 200) {
        ESP_LOGW(TAG, "%s timed out after %lldms: %s", op, (long long)elapsed_ms, esp_err_to_name(err));
        snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD TIMEOUT");
    } else {
        ESP_LOGW(TAG, "%s unreachable after %lldms: %s", op, (long long)elapsed_ms, esp_err_to_name(err));
        snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD UNREACHABLE");
    }
    return false;
}

bool stream_upload_chunk(const char *url, const uint8_t *pcm, size_t len, uint32_t offset,
                          uint32_t timeout_ms, char *fail_reason_out, size_t fail_reason_out_len)
{
    /* `offset` is where these bytes belong in the capture. The backend
     * compares it against what it has already accumulated and rejects a
     * gap or a reordering with 409 (see the backend's
     * streaming_capture.append_chunk). It also makes the retry below safe:
     * if a POST actually landed but its response was lost, the retry
     * carries the same offset and the backend treats it as an idempotent
     * no-op instead of appending the audio twice.
     *
     * Appended here rather than by each of the three clients so the wire
     * format lives in one place. */
    char url_with_offset[224];
    int n = snprintf(url_with_offset, sizeof(url_with_offset), "%s?offset=%lu", url, (unsigned long)offset);
    if (n < 0 || (size_t)n >= sizeof(url_with_offset)) {
        ESP_LOGE(TAG, "chunk url too long: %s", url);
        snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD INIT FAILED");
        return false;
    }

    for (int attempt = 1; attempt <= UPLOAD_CHUNK_MAX_ATTEMPTS; attempt++) {
        esp_http_client_config_t config = {
            .url = url_with_offset,
            .method = HTTP_METHOD_POST,
            .timeout_ms = timeout_ms,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "esp_http_client_init failed for url=%s", url);
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD INIT FAILED");
            return false;
        }
        esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
        /* Not copied by esp_http_client - pcm must stay valid until perform()
         * returns, which it does here (caller's chunk buffer, untouched during
         * upload - see audio_capture.c's streaming loop). */
        esp_http_client_set_post_field(client, (const char *)pcm, (int)len);

        int64_t start_us = esp_timer_get_time();
        esp_err_t err = esp_http_client_perform(client);
        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;

        if (err != ESP_OK) {
            esp_http_client_cleanup(client);
            if (attempt < UPLOAD_CHUNK_MAX_ATTEMPTS) {
                ESP_LOGW(TAG, "chunk upload attempt %d/%d failed (%s), retrying in %dms",
                         attempt, UPLOAD_CHUNK_MAX_ATTEMPTS, esp_err_to_name(err), UPLOAD_RETRY_DELAY_MS);
                vTaskDelay(pdMS_TO_TICKS(UPLOAD_RETRY_DELAY_MS));
                continue;
            }
            return report_transport_failure("chunk upload", err, elapsed_ms, timeout_ms, fail_reason_out, fail_reason_out_len);
        }

        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (status != 200 && status != 202) {
            /* Backend answered and rejected it - not a connectivity problem,
             * retrying identical bytes won't change the outcome. */
            ESP_LOGW(TAG, "chunk upload rejected, status=%d", status);
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD ERR %d", status);
            return false;
        }

        return true;
    }
    return false; /* unreachable - loop always returns or retries */
}

bool stream_upload_finish(const char *url,
                           const multipart_text_field_t *fields, size_t field_count,
                           uint32_t timeout_ms,
                           char *response_buf, size_t response_buf_len, int *status_out,
                           char *fail_reason_out, size_t fail_reason_out_len)
{
    char body[FINISH_BODY_BUF_LEN];
    size_t pos = 0;
    for (size_t i = 0; i < field_count; i++) {
        int n = snprintf(body + pos, sizeof(body) - pos, "%s%s=%s",
                          i == 0 ? "" : "&", fields[i].name, fields[i].value);
        if (n < 0 || pos + (size_t)n >= sizeof(body)) {
            ESP_LOGE(TAG, "finish body build failed/truncated");
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD INIT FAILED");
            return false;
        }
        pos += (size_t)n;
    }

    for (int attempt = 1; attempt <= UPLOAD_RETRY_MAX_ATTEMPTS; attempt++) {
        if (response_buf && response_buf_len > 0) {
            response_buf[0] = '\0';
        }
        response_ctx_t ctx = { .buf = response_buf, .buf_len = response_buf_len, .written = 0 };

        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .timeout_ms = timeout_ms,
            .event_handler = response_event_handler,
            .user_data = &ctx,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "esp_http_client_init failed for url=%s", url);
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD INIT FAILED");
            return false;
        }
        esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
        esp_http_client_set_post_field(client, body, (int)pos);

        int64_t start_us = esp_timer_get_time();
        esp_err_t err = esp_http_client_perform(client);
        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;

        if (err != ESP_OK) {
            esp_http_client_cleanup(client);
            if (attempt < UPLOAD_RETRY_MAX_ATTEMPTS) {
                ESP_LOGW(TAG, "finish attempt %d/%d failed (%s), retrying in %dms",
                         attempt, UPLOAD_RETRY_MAX_ATTEMPTS, esp_err_to_name(err), UPLOAD_RETRY_DELAY_MS);
                vTaskDelay(pdMS_TO_TICKS(UPLOAD_RETRY_DELAY_MS));
                continue;
            }
            return report_transport_failure("finish", err, elapsed_ms, timeout_ms, fail_reason_out, fail_reason_out_len);
        }

        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (status_out) {
            *status_out = status;
        }
        if (status != 200 && status != 202) {
            ESP_LOGW(TAG, "finish rejected, status=%d body=%s", status, response_buf ? response_buf : "");
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD ERR %d", status);
            return false;
        }

        return true;
    }
    return false; /* unreachable - loop always returns or retries */
}

void stream_upload_cancel(const char *url, uint32_t timeout_ms)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = timeout_ms,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGW(TAG, "cancel: esp_http_client_init failed for url=%s", url);
        return;
    }
    esp_http_client_set_post_field(client, "", 0);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cancel failed (harmless - backend keeps an orphaned temp file): %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}
