/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 10 - Capture the Transmission: bounded microphone capture
 * @details One persistent FreeRTOS worker task ("audio_worker" - never the
 *          LVGL task) blocks on a length-1 queue and performs each capture
 *          synchronously against the verified esp-box-3 BSP microphone path
 *          (bsp_audio_codec_microphone_init() -> the ES7210 mic ADC over
 *          I2S, esp_codec_dev_open/read/close). A length-1 queue is enough
 *          because audio_capture_start()'s in-flight guard (state != IDLE)
 *          already ensures at most one capture is ever outstanding, the
 *          same shape handshake_client.c uses for its request queue.
 *
 *          The capture buffer is allocated once, at init, sized for exactly
 *          AUDIO_CAPTURE_DURATION_MS at AUDIO_SAMPLE_RATE_HZ mono 16-bit -
 *          never grown, never allocated per-capture. Each capture reads
 *          into it in small chunks (AUDIO_READ_CHUNK_BYTES) bounded by that
 *          fixed size; the loop cannot run past the buffer regardless of
 *          how the codec driver behaves, and a run of consecutive read
 *          errors aborts the capture rather than spinning forever.
 *
 *          Audio is a stream, not a single reading (see the Mission 10
 *          directive): individual samples are far too numerous to log
 *          (32,000 samples/s at this format), so only a per-second progress
 *          line goes to the serial log while RECORDING, and Black Box
 *          events fire only at real state transitions (ARM/START/COMPLETE/
 *          UPLOADING/FAILED/UPLOAD OK) - never per sample, never per chunk.
 *
 *          Once a capture completes, the buffer is POSTed straight to the
 *          backend over the Wi-Fi link wifi_manager.c already brings up
 *          (see upload_capture() below) - an earlier revision of this
 *          mission exported over the serial console as base64 text instead,
 *          which worked but took ~18-20s per 4s capture (115200 baud is
 *          the bottleneck, not the device); Wi-Fi upload is both simpler to
 *          use (no juggling idf.py monitor vs. a listener) and dramatically
 *          faster, since raw bytes over a local network don't pay any of
 *          base64's ~33% size inflation or a slow serial link's bit rate.
 *
 *          Same foreign-task-callback contract as wifi_manager.h/
 *          handshake_client.h: the caller must bridge into the LVGL task
 *          itself, exactly as status_deck_ui.c already does for Wi-Fi and
 *          the handshake.
 *
 *          Mission 11: the mic codec handle is now created once by the
 *          caller (status_deck_ui.c) and passed in, instead of this module
 *          calling bsp_audio_codec_microphone_init() itself - voice_control.c
 *          needs the same physical ES7210 device for continuous WakeNet
 *          listening, and bsp_audio_codec_microphone_init() allocates a new
 *          esp_codec_dev_handle_t on every call, so two independent handles
 *          would both be driving the same hardware. This module still
 *          opens/reads/closes that shared handle exactly as before for each
 *          bounded capture; voice_control.c is responsible for staying off
 *          it (not calling esp_codec_dev_read on its own open session) for
 *          the duration - see voice_control.c's mic-ownership comment.
 */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "bsp/esp-bsp.h"
#include "audio_capture.h"

#if __has_include("backend_config.h")
#include "backend_config.h"
#else
#error "main/backend_config.h not found. Define BACKEND_BASE_URL there, e.g. #define BACKEND_BASE_URL \"http://192.168.1.31:8000\" - your development laptop's LAN IP, not 127.0.0.1."
#endif

static const char *TAG = "audio_capture";

/* ---- Capture format: mono 16-bit PCM at a speech-appropriate rate -----
 * The esp-box-3 BSP's bsp_audio_init() brings the physical I2S channel up
 * at its own default (22050 Hz duplex mono, see esp-box-3_idf5.c) the first
 * time any BSP audio function is called; esp_codec_dev_open() below
 * renegotiates the I2S clock/slot config to whatever esp_codec_dev_sample_info_t
 * is passed to it, which is the documented esp_codec_dev flow (open()
 * reconfigures per session) - not a custom driver or invented pin mapping. */
#define AUDIO_SAMPLE_RATE_HZ    16000
#define AUDIO_BITS_PER_SAMPLE   16
#define AUDIO_CHANNELS          1
#define AUDIO_BYTES_PER_SAMPLE  (AUDIO_BITS_PER_SAMPLE / 8)

/* 3-5s per the Mission 10 directive; 4.0s is a round middle choice. Manual
 * REC (audio_capture_start()) still always requests exactly this - Mission
 * 13 did not touch it. */
#define AUDIO_CAPTURE_DURATION_MS 4000

/* NOTE/GO/SEND's safety-cap duration - STOP (audio_capture_stop()) is the
 * everyday way one of these ends, this is only the fallback if it isn't
 * pressed. Raised from the original 60s to 20 minutes so a long voice
 * message never gets cut off mid-sentence - safe to raise freely now
 * because, since the streaming rewrite below, this is a wall-clock cap
 * enforced by the recording loop, NOT a buffer-size limit: at 16kHz/16-bit/
 * mono, 20 minutes of raw audio is ~36.6MB, nowhere close to fitting in
 * one PSRAM allocation (this board's entire PSRAM pool is 16MB), which is
 * exactly why chunked streaming uploads (AUDIO_STREAM_CHUNK_MS below)
 * replaced the old "record everything into one buffer, upload once at the
 * end" approach for these three commands. */
/* AUDIO_NOTE_MAX_DURATION_MS and AUDIO_SEND_DURATION_MS now live in
 * audio_capture.h - callers choose per command. */

/* How much audio a NOTE/GO/SEND recording accumulates before flushing a
 * chunk to the backend (see the streaming branch of perform_capture() and
 * stream_upload.c) - small enough to keep the on-device buffer tiny
 * regardless of total recording length, large enough to keep the chunk
 * count (and per-chunk HTTP overhead) reasonable: a full 20-minute
 * recording is ~80 chunks at this size. Not a measured/tuned value, same
 * "generous but bounded" spirit as the old 60s NOTE cap it replaces. */
#define AUDIO_STREAM_CHUNK_MS 15000

/* The capture buffer is sized once, at init, for the longer of manual
 * REC's 4s and one streaming chunk's 15s (~469KB from PSRAM) - never
 * grown, never allocated per-capture. A NOTE/GO/SEND recording reuses this
 * same buffer many times across its whole duration, flushing and
 * resetting it once per AUDIO_STREAM_CHUNK_MS rather than filling it once
 * for the entire capture (that's what makes AUDIO_NOTE_MAX_DURATION_MS
 * above safe to set so much higher than this buffer could ever hold
 * outright). Manual REC's 4s capture still only ever touches the first
 * ~128,000 bytes of this buffer, exactly as before. */
#define AUDIO_CAPTURE_BUFFER_MS \
    (AUDIO_CAPTURE_DURATION_MS > AUDIO_STREAM_CHUNK_MS ? AUDIO_CAPTURE_DURATION_MS : AUDIO_STREAM_CHUNK_MS)
#define AUDIO_CAPTURE_BUFFER_SAMPLES ((AUDIO_SAMPLE_RATE_HZ * AUDIO_CAPTURE_BUFFER_MS) / 1000)
#define AUDIO_CAPTURE_BUFFER_BYTES (AUDIO_CAPTURE_BUFFER_SAMPLES * AUDIO_BYTES_PER_SAMPLE * AUDIO_CHANNELS)
#define AUDIO_STREAM_CHUNK_BYTES AUDIO_CAPTURE_BUFFER_BYTES

/* 512 samples = 32ms per chunk at 16kHz - fine enough for a readable
 * progress readout without turning every chunk into a log line. */
#define AUDIO_READ_CHUNK_SAMPLES 512
#define AUDIO_READ_CHUNK_BYTES (AUDIO_READ_CHUNK_SAMPLES * AUDIO_BYTES_PER_SAMPLE)
#define AUDIO_READ_MAX_CONSEC_ERRORS 5

/* Audio read and thrown away immediately after esp_codec_dev_open(), before
 * the recording clock starts.
 *
 * The ES7210 emits a burst of full-scale (-32768) samples while it settles.
 * Measured across every capture this board has made: peak always railed at
 * exactly 1.000 and the clipped-sample count was always ~90-104 regardless
 * of whether the recording was 15s or 167s, loud or quiet - a constant that
 * obviously could not be speech. Dumping the samples of a delivered WAV
 * located them precisely: all 99 sat between 13.8ms and 26.6ms, and nothing
 * in the remaining 15 seconds came near full scale.
 *
 * So this is not a gain problem and must not be "fixed" by turning the gain
 * down - the recordings run quiet already (RMS 0.009-0.031), and less gain
 * would only hurt the real speech while the transient still railed. 64ms is
 * two AUDIO_READ_CHUNK_SAMPLES reads, comfortably past the measured 26.6ms. */
#define AUDIO_MIC_SETTLE_MAX_READS 8 /* 8 x 32ms = 256ms ceiling */

/* Recording-integrity thresholds. A capture that ran N ms of wall clock
 * should hold N ms of audio; the difference is what the mic path dropped.
 * NOTABLE is the "say so on screen and in the log" line; FAIL_PCT is the
 * "this recording is too damaged to pass off as usable" line. Deliberately
 * two different lines: a small shortfall still produces a note worth
 * keeping, and refusing to deliver it would be worse than delivering it
 * labelled. Baseline for calibration, measured 2026-09-01 on real
 * hardware: a 45,961ms NOTE delivered 43,984ms of audio (1,977ms / 4.3%
 * short), entirely at the two 15s chunk flushes. */
#define AUDIO_BYTES_PER_MS ((AUDIO_SAMPLE_RATE_HZ * AUDIO_BYTES_PER_SAMPLE * AUDIO_CHANNELS) / 1000)
#define AUDIO_SHORTFALL_NOTABLE_MS 250
#define AUDIO_SHORTFALL_FAIL_PCT 15

/* Clipping threshold: within ~0.25% of full-scale int16. */
#define AUDIO_CLIP_THRESHOLD 32760

/* Mirrors backend/main.py's POST /api/v1/audio: metadata rides the query
 * string (name/rate/bits/ch), the body is the raw PCM bytes with no
 * encoding - the backend wraps it in a WAV header itself via the same
 * _save_capture_wav() helper the old serial listener used. Generous
 * timeout (audio uploads are bigger than a handshake payload, though still
 * well under a second on a healthy LAN). */
#define AUDIO_UPLOAD_PATH "/api/v1/audio"
#define AUDIO_UPLOAD_TIMEOUT_MS 10000

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static audio_cap_status_t s_status = {
    .state = AUDIO_CAP_IDLE,
    .sample_rate_hz = AUDIO_SAMPLE_RATE_HZ,
    .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
    .channels = AUDIO_CHANNELS,
    .duration_target_ms = AUDIO_CAPTURE_DURATION_MS,
};

static audio_cap_event_cb_t s_cb = NULL;
static void *s_cb_ctx = NULL;
static QueueHandle_t s_start_queue = NULL;

/* Two capture buffers, not one. The mic fills one while the other uploads,
 * so the recording loop never stops reading - that gap is what was costing
 * ~550ms of audio per 15s chunk (issue #1, measured on hardware).
 * Manual REC is unaffected and still uses buffer 0 as a single buffer. */
#define AUDIO_CAPTURE_BUFFERS 2

typedef struct {
    uint8_t *pcm;
    uint32_t len;
    uint32_t offset;   /* byte position in the finished recording */
} chunk_job_t;

static uint8_t *s_capture_buf[AUDIO_CAPTURE_BUFFERS] = { NULL, NULL };
static uint32_t s_last_complete_bytes = 0; /* 0 until the first capture completes */

/* Ownership of a buffer is tracked by this counting semaphore rather than
 * by the queue, and the distinction matters: queue space frees up when the
 * uploader *dequeues* a job, but the buffer is only safe to overwrite once
 * the upload has actually *finished*. The uploader gives the token back
 * after the POST returns, so a token means "a buffer is genuinely free".
 * With AUDIO_CAPTURE_BUFFERS tokens, a capture that outruns the network
 * simply blocks here - which is exactly the pre-existing behavior, so the
 * worst case degrades to what it did before rather than to something new. */
static QueueHandle_t s_chunk_queue = NULL;
static SemaphoreHandle_t s_free_bufs = NULL;
static TaskHandle_t s_uploader_task = NULL;

/* Per-capture, read by the uploader task. Safe as plain statics because
 * enqueue_capture()'s busy-guard allows only one capture at a time, and
 * these are set before the first chunk is ever queued. */
static audio_note_chunk_fn_t s_chunk_fn = NULL;
static char s_chunk_name[AUDIO_CAP_ARTIFACT_NAME_LEN];
static volatile bool s_chunk_failed = false;
static char s_chunk_fail_reason[AUDIO_CAP_REASON_LEN];
static uint32_t s_flush_count = 0;      /* all three guarded by s_mux */
static uint32_t s_flush_total_ms = 0;
static uint32_t s_flush_max_ms = 0;
static esp_codec_dev_handle_t s_mic_dev = NULL;
static bool s_mic_available = false;       /* false if buffer alloc or codec bring-up failed at init */

/* Set by audio_capture_stop()/audio_capture_cancel() (any task, e.g. the
 * SEND/CANCEL buttons' LVGL callbacks), read once per read-chunk (~32ms) by
 * perform_capture() on the worker task - same bare-volatile, no-mutex-needed
 * pattern voice_control.c's s_mic_owner already uses for a single-flag
 * cross-task signal. Reset at the start of every perform_capture() call so
 * a stale request from a previous capture can never affect the next one. */
static volatile bool s_stop_requested = false;
static volatile bool s_cancel_requested = false;

/* Mission 13: what the worker task actually receives on s_start_queue -
 * queue depth is still 1 (audio_capture_start()'s in-flight guard already
 * ensures at most one capture is ever outstanding, same reasoning as
 * before, now just carrying a few more fields instead of a dummy byte). */
typedef struct {
    char name[AUDIO_CAP_ARTIFACT_NAME_LEN]; /* "" -> auto "capture_NNN" (manual REC); else used verbatim */
    uint32_t duration_ms;                   /* manual REC: capped to AUDIO_CAPTURE_BUFFER_MS. Streaming: the
                                              * overall wall-clock safety cap, independent of buffer size. */
    bool auto_upload;                       /* true: this file's own upload_capture() to AUDIO_UPLOAD_PATH */
    audio_note_chunk_fn_t chunk_fn;         /* streaming only; NULL when auto_upload is true */
    audio_note_finish_fn_t finish_fn;       /* streaming only; NULL when auto_upload is true */
    audio_note_cancel_fn_t cancel_fn;       /* streaming only; NULL when auto_upload is true */
} audio_capture_request_t;

static void notify(audio_cap_state_t new_state, const char *message)
{
    if (s_cb) {
        s_cb(new_state, message, s_cb_ctx);
    }
}

/* Only touches the fields that change during RECORDING; leaves
 * artifact_name/fail_reason/capture_seq alone (those change only at their
 * own transitions, below). */
static void update_progress(uint32_t elapsed_ms, uint32_t bytes_captured, uint32_t peak_raw, uint32_t clipped)
{
    portENTER_CRITICAL(&s_mux);
    s_status.elapsed_ms = elapsed_ms;
    s_status.bytes_captured = bytes_captured;
    s_status.samples_captured = bytes_captured / AUDIO_BYTES_PER_SAMPLE;
    s_status.peak_amplitude = (float)peak_raw / 32768.0f;
    s_status.clipped_samples = clipped;
    portEXIT_CRITICAL(&s_mux);
}

static void set_state(audio_cap_state_t state)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = state;
    portEXIT_CRITICAL(&s_mux);
}

/* Mission 13: a successful upload (or a no-upload-requested finish) must
 * clear any stale fail_reason from an earlier failed attempt - without
 * this, fail_reason only ever gets written by set_failed() below, never
 * cleared, so a caller checking "did THIS attempt fail" by reading
 * fail_reason after the fact (voice_control.c's run_note_command does
 * exactly this) could see a leftover reason from a previous, unrelated
 * failure and misreport success as failure. */
static void set_ready(const char *message)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = AUDIO_CAP_READY;
    s_status.fail_reason[0] = '\0';
    portEXIT_CRITICAL(&s_mux);
    notify(AUDIO_CAP_READY, message);
}

/* fail_reason is set (not cleared, unlike set_ready()) to the literal
 * string "CANCELLED" - not a failure, but reusing fail_reason as the
 * signal lets voice_control.c's existing "check fail_reason after IDLE"
 * result-message logic (see run_send_command() etc.) special-case it with
 * one more strcmp, the same way it already special-cases
 * "ENDPOINT NOT SET", rather than needing a second out-parameter plumbed
 * through the whole capture-status contract for one more outcome. */
static void set_cancelled(void)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = AUDIO_CAP_CANCELLED;
    strncpy(s_status.fail_reason, "CANCELLED", sizeof(s_status.fail_reason) - 1);
    s_status.fail_reason[sizeof(s_status.fail_reason) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);
    notify(AUDIO_CAP_CANCELLED, "CANCELLED");
}

static void set_failed(const char *reason)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = AUDIO_CAP_FAILED;
    strncpy(s_status.fail_reason, reason, sizeof(s_status.fail_reason) - 1);
    s_status.fail_reason[sizeof(s_status.fail_reason) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);

    /* 40, not 32: "AUDIO FAILED " (13) + up to AUDIO_CAP_REASON_LEN-1 (23)
     * + nul = 37 worst case. `reason` is a bare `const char *` here (no
     * array bound GCC can see from this call site), so -Wformat-truncation
     * never flagged this one - but "UPLOAD UNREACHABLE" (19 chars) alone
     * already needed 33 against the old 32-byte buffer, silently dropping
     * its last character. Sized by hand instead of by compiler nag. */
    char msg[40];
    snprintf(msg, sizeof(msg), "AUDIO FAILED %s", reason);
    ESP_LOGW(TAG, "%s", msg);
    notify(AUDIO_CAP_FAILED, msg);
}

/* Used for failures *during* a capture attempt (mic open, read error,
 * upload failure) - unlike the permanent init-time failures below (bad
 * buffer/mic bring-up, which leave s_mic_available false and state latched
 * FAILED on purpose), these are transient: hold FAILED on screen briefly,
 * same as the READY hold below, then return to IDLE so the next REC press
 * can retry. Without this, audio_capture_start()'s "state != IDLE" busy
 * guard would permanently lock out every future capture after the first
 * error.
 *
 * Manual REC's upload failure does NOT lose the recording - the buffer is
 * untouched and audio_capture_get_buffer() still returns it, only the
 * network step needs retrying. Streaming NOTE/GO/SEND is different: a
 * finish_fn failure means every chunk already made it to the backend (it's
 * only the final "assemble and process" call that failed, safe to retry
 * from the backend's side of the wire), while a chunk_fn failure mid-
 * recording means everything up to the failed chunk is on the backend but
 * the tail end of the recording that was still in progress is gone - the
 * capture as a whole still needs a full retry, same user-facing outcome
 * ("recording failed, try again") the old design's chunk-free path gave. */
static void fail_and_recover(const char *reason)
{
    set_failed(reason);
    vTaskDelay(pdMS_TO_TICKS(800));
    set_state(AUDIO_CAP_IDLE);
}

/* POSTs the raw PCM buffer to the backend over Wi-Fi. Same blocking
 * esp_http_client_perform() shape as handshake_client.c's perform_request -
 * this task has nothing else to do while the request is in flight, so
 * there's no reason to complicate this with a separate queue/worker the
 * way handshake_client.c needs (that module serializes requests coming
 * from the LVGL task; this one only ever calls itself, sequentially, from
 * perform_capture below). Distinguishes transport failure (no HTTP status
 * ever received) from a real server error the same way handshake_client.c
 * does, via elapsed time against the timeout ceiling. */
static bool upload_capture(const char *name, const uint8_t *buf, uint32_t len,
                            char *fail_reason_out, size_t fail_reason_out_len)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s?name=%s&rate=%u&bits=%u&ch=%u",
             BACKEND_BASE_URL, AUDIO_UPLOAD_PATH, name,
             (unsigned)AUDIO_SAMPLE_RATE_HZ, (unsigned)AUDIO_BITS_PER_SAMPLE, (unsigned)AUDIO_CHANNELS);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AUDIO_UPLOAD_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        /* Only realistic cause: a malformed BACKEND_BASE_URL. */
        ESP_LOGE(TAG, "esp_http_client_init failed for url=%s", url);
        snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD INIT FAILED");
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    /* Not copied by esp_http_client - buf must stay valid until perform()
     * returns, which it does here (buffer 0, and manual REC never runs
     * concurrently with a streaming capture). */
    esp_http_client_set_post_field(client, (const char *)buf, (int)len);

    int64_t start_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        /* Same fast-failure-vs-rode-out-the-timeout inference
         * handshake_client.c uses: esp_http_client_perform doesn't expose
         * "connection refused" vs. "nothing answered" directly. */
        if (elapsed_ms >= AUDIO_UPLOAD_TIMEOUT_MS - 200) {
            ESP_LOGW(TAG, "upload timed out after %lldms: %s", (long long)elapsed_ms, esp_err_to_name(err));
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD TIMEOUT");
        } else {
            ESP_LOGW(TAG, "upload unreachable after %lldms: %s", (long long)elapsed_ms, esp_err_to_name(err));
            snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD UNREACHABLE");
        }
        return false;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGW(TAG, "upload server error, status=%d", status);
        snprintf(fail_reason_out, fail_reason_out_len, "UPLOAD ERR %d", status);
        return false;
    }

    ESP_LOGI(TAG, "upload complete: %s, %lu bytes, %lldms", name, (unsigned long)len, (long long)elapsed_ms);
    return true;
}

/* Runs the chunk POST off the recording task. Whatever happens, the
 * buffer token goes back - a failed upload must not strand a buffer and
 * wedge the recording loop; the failure is reported through
 * s_chunk_failed, which the loop checks each pass. */
static void uploader_task(void *arg)
{
    (void)arg;
    chunk_job_t job;
    for (;;) {
        if (xQueueReceive(s_chunk_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        /* Skip the actual POST once this capture has already failed - the
         * recording is being abandoned, so more uploads are pointless -
         * but still drain the queue and return the token. */
        if (!s_chunk_failed) {
            char reason[AUDIO_CAP_REASON_LEN] = "";
            int64_t t0 = esp_timer_get_time();
            bool ok = s_chunk_fn(s_chunk_name, job.pcm, job.len, job.offset, reason, sizeof(reason));
            uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);

            portENTER_CRITICAL(&s_mux);
            s_flush_count++;
            s_flush_total_ms += ms;
            if (ms > s_flush_max_ms) {
                s_flush_max_ms = ms;
            }
            portEXIT_CRITICAL(&s_mux);

            ESP_LOGI(TAG, "chunk uploaded: %lu bytes at offset %lu in %lums%s",
                     (unsigned long)job.len, (unsigned long)job.offset, (unsigned long)ms,
                     ok ? "" : " (FAILED)");
            if (!ok) {
                strncpy(s_chunk_fail_reason, reason, sizeof(s_chunk_fail_reason) - 1);
                s_chunk_fail_reason[sizeof(s_chunk_fail_reason) - 1] = '\0';
                s_chunk_failed = true;
            }
        }
        xSemaphoreGive(s_free_bufs);
    }
}

/* Blocks until every queued chunk has finished uploading, then restores the
 * token count. The caller must already have released its own buffer token
 * (by queueing it or giving it back), or this deadlocks. */
static void drain_uploads(void)
{
    for (int i = 0; i < AUDIO_CAPTURE_BUFFERS; i++) {
        xSemaphoreTake(s_free_bufs, portMAX_DELAY);
    }
    for (int i = 0; i < AUDIO_CAPTURE_BUFFERS; i++) {
        xSemaphoreGive(s_free_bufs);
    }
}

static void perform_capture(const audio_capture_request_t *req)
{
    s_stop_requested = false;
    s_cancel_requested = false;
    bool streaming = (req->chunk_fn != NULL);

    /* req->duration_ms is caller-chosen (AUDIO_CAPTURE_DURATION_MS for
     * manual REC, AUDIO_NOTE_MAX_DURATION_MS for streaming NOTE/GO/SEND).
     * Only manual REC is clamped to the buffer's capacity - a streaming
     * capture's target is a wall-clock safety cap, not a buffer-size
     * limit, since it reuses AUDIO_CAPTURE_BUFFER_BYTES as a rolling
     * per-chunk window rather than filling it once (see the recording
     * loop below). */
    uint32_t duration_ms = req->duration_ms;
    if (!streaming && duration_ms > AUDIO_CAPTURE_BUFFER_MS) {
        duration_ms = AUDIO_CAPTURE_BUFFER_MS;
    }
    /* 64-bit intermediate deliberately: AUDIO_SAMPLE_RATE_HZ is an int and
     * duration_ms a uint32_t, so plain `16000 * duration_ms` is evaluated in
     * 32-bit unsigned and wraps for any cap past ~268s. At the intended
     * 1,200,000ms that produced 19,200,000,000 -> 2,020,130,816, capping
     * NOTE at 126s instead of 20 minutes - and because the loop then exits
     * normally, the truncated recording was reported as a clean success.
     * Suspected cause of the "note craps out around a minute and a half"
     * report captured 2026-08-28. */
    uint32_t target_samples = (uint32_t)(((uint64_t)AUDIO_SAMPLE_RATE_HZ * (uint64_t)duration_ms) / 1000u);
    uint32_t target_bytes = target_samples * AUDIO_BYTES_PER_SAMPLE * AUDIO_CHANNELS;

    portENTER_CRITICAL(&s_mux);
    s_status.duration_target_ms = duration_ms;
    portEXIT_CRITICAL(&s_mux);

    if (streaming) {
        /* Set before anything is queued, and only ever one capture at a
         * time (enqueue_capture's busy guard), so the uploader task can
         * read these without further locking. */
        s_chunk_fn = req->chunk_fn;
        strncpy(s_chunk_name, req->name, sizeof(s_chunk_name) - 1);
        s_chunk_name[sizeof(s_chunk_name) - 1] = '\0';
        s_chunk_failed = false;
        s_chunk_fail_reason[0] = '\0';
        portENTER_CRITICAL(&s_mux);
        s_status.uploader_behind = false;
        s_flush_count = 0;
        s_flush_total_ms = 0;
        s_flush_max_ms = 0;
        portEXIT_CRITICAL(&s_mux);
    }

    set_state(AUDIO_CAP_ARMING);
    ESP_LOGI(TAG, "capture arming");
    notify(AUDIO_CAP_ARMING, "AUDIO ARM");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
        .channel = AUDIO_CHANNELS,
        .channel_mask = 0,
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .mclk_multiple = 0,
    };
    int err = esp_codec_dev_open(s_mic_dev, &fs);
    if (err != ESP_CODEC_DEV_OK) {
        fail_and_recover("MIC OPEN FAILED");
        return;
    }
    /* Best-effort - a fixed starting gain is enough for this mission's
     * quiet/speech/clap validation; not exposed as a runtime control. */
    esp_codec_dev_set_in_gain(s_mic_dev, 30.0f);

    set_state(AUDIO_CAP_RECORDING);
    ESP_LOGI(TAG, "capture recording, target=%lums rate=%dHz bits=%d ch=%d",
             (unsigned long)duration_ms, AUDIO_SAMPLE_RATE_HZ, AUDIO_BITS_PER_SAMPLE, AUDIO_CHANNELS);
    notify(AUDIO_CAP_RECORDING, "AUDIO START");

    if (streaming) {
        /* Claim the buffer the mic will fill first. The uploader returns
         * tokens as uploads complete; drain_uploads() restores the count
         * on every exit path below. */
        xSemaphoreTake(s_free_bufs, portMAX_DELAY);
    }

    /* Discard the codec's settling transient before the clock starts, so it
     * never reaches the recording and never counts against shortfall_ms.
     *
     * Discards until a read comes back that is not railing, rather than for
     * a fixed time. A fixed 64ms window was tried first and was not robust:
     * it removed the transient on one capture (clipped=0) and missed it on
     * the next (clipped=84, the rail simply appearing 11ms earlier in the
     * recording), because how much settling audio arrives before the first
     * successful read varies. Testing the samples tests the actual
     * condition instead of guessing at its duration.
     *
     * Bounded so a genuinely loud start cannot eat the recording, and a read
     * error retries rather than giving up - the previous version broke out
     * of the loop on the first error, which is one way it silently
     * discarded nothing at all.
     *
     * Buffer 0 is safe to scribble on here: streaming captures start on it
     * and nothing else owns it (the uploader is drained between captures),
     * and manual REC uses it too but has not begun accumulating yet. */
    uint32_t settle_bytes = 0;
    uint32_t settle_peak = 0;
    for (int attempt = 0; attempt < AUDIO_MIC_SETTLE_MAX_READS; attempt++) {
        if (esp_codec_dev_read(s_mic_dev, s_capture_buf[0], AUDIO_READ_CHUNK_BYTES) != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        settle_bytes += AUDIO_READ_CHUNK_BYTES;
        const int16_t *sp = (const int16_t *)s_capture_buf[0];
        uint32_t peak = 0;
        for (int i = 0; i < AUDIO_READ_CHUNK_SAMPLES; i++) {
            int32_t v = sp[i] < 0 ? -(int32_t)sp[i] : (int32_t)sp[i];
            if ((uint32_t)v > peak) {
                peak = (uint32_t)v;
            }
        }
        settle_peak = peak;
        if (peak < AUDIO_CLIP_THRESHOLD) {
            break; /* codec has settled - this read is real audio */
        }
    }
    if (settle_bytes > AUDIO_READ_CHUNK_BYTES) {
        /* One read is the normal case. More means the codec took a while,
         * which is worth seeing if it ever becomes many. */
        ESP_LOGI(TAG, "mic settled after %lums (last peak %lu)",
                 (unsigned long)(settle_bytes / AUDIO_BYTES_PER_MS), (unsigned long)settle_peak);
    }

    int64_t start_us = esp_timer_get_time();
    uint32_t bytes_captured = 0;   /* cumulative across the whole capture - stats/progress/log all use this */
    uint32_t chunk_bytes = 0;      /* streaming only - resets to 0 after each chunk flush below */
    uint32_t peak_raw = 0;
    uint32_t clipped = 0;
    double sum_sq = 0.0;
    int consec_errors = 0;
    uint32_t dropped_reads = 0;    /* errored reads that were retried - each one is ~32ms of audio gone */
    int active_buf = 0;            /* streaming only - index of the buffer the mic is filling */
    int last_logged_second = -1;
    bool stopped_early = false;
    bool cancelled = false;
    bool chunk_upload_failed = false;
    char chunk_fail_reason[AUDIO_CAP_REASON_LEN] = "";

    while (bytes_captured < target_bytes) {
        if (s_cancel_requested) {
            cancelled = true;
            break;
        }
        if (s_stop_requested) {
            stopped_early = true;
            break;
        }

        uint32_t remaining = target_bytes - bytes_captured;
        /* Non-streaming: dst offset grows for the whole capture, so room
         * left in the buffer shrinks with bytes_captured. Streaming: dst
         * offset resets to 0 after every flush, so room left shrinks with
         * chunk_bytes instead - AUDIO_CAPTURE_BUFFER_BYTES equals one
         * chunk's size exactly (AUDIO_STREAM_CHUNK_BYTES), so this never
         * reaches 0 in practice: a full chunk is always flushed (and
         * chunk_bytes reset) before that could happen. */
        uint32_t buf_room = streaming ? (AUDIO_CAPTURE_BUFFER_BYTES - chunk_bytes)
                                       : (AUDIO_CAPTURE_BUFFER_BYTES - bytes_captured);
        uint32_t want = remaining < AUDIO_READ_CHUNK_BYTES ? remaining : AUDIO_READ_CHUNK_BYTES;
        uint32_t read_bytes = want < buf_room ? want : buf_room;
        if (read_bytes == 0) {
            break; /* defensive only - see the comment above */
        }

        uint8_t *dst = streaming ? (s_capture_buf[active_buf] + chunk_bytes)
                                 : (s_capture_buf[0] + bytes_captured);
        err = esp_codec_dev_read(s_mic_dev, dst, (int)read_bytes);
        if (err != ESP_CODEC_DEV_OK) {
            consec_errors++;
            dropped_reads++;
            if (consec_errors >= AUDIO_READ_MAX_CONSEC_ERRORS) {
                esp_codec_dev_close(s_mic_dev);
                if (streaming) {
                    /* Give back the buffer being filled, then wait for any
                     * in-flight upload - returning with a token still held
                     * would wedge the next capture. */
                    xSemaphoreGive(s_free_bufs);
                    drain_uploads();
                    if (req->cancel_fn) {
                        req->cancel_fn(req->name);
                    }
                }
                fail_and_recover("READ ERROR");
                return;
            }
            continue;
        }
        consec_errors = 0;

        const int16_t *samples = (const int16_t *)dst;
        int sample_count = (int)(read_bytes / AUDIO_BYTES_PER_SAMPLE);
        for (int i = 0; i < sample_count; i++) {
            int32_t s = samples[i];
            uint32_t mag = (uint32_t)(s < 0 ? -s : s);
            if (mag > peak_raw) {
                peak_raw = mag;
            }
            if (mag >= AUDIO_CLIP_THRESHOLD) {
                clipped++;
            }
            sum_sq += (double)s * (double)s;
        }

        bytes_captured += read_bytes;
        chunk_bytes += read_bytes;
        uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
        update_progress(elapsed_ms, bytes_captured, peak_raw, clipped);

        int cur_second = (int)(elapsed_ms / 1000);
        if (cur_second != last_logged_second) {
            last_logged_second = cur_second;
            ESP_LOGI(TAG, "recording %lu/%lums, %lu bytes, peak=%lu",
                     (unsigned long)elapsed_ms, (unsigned long)duration_ms,
                     (unsigned long)bytes_captured, (unsigned long)peak_raw);
        }

        if (streaming && chunk_bytes >= AUDIO_STREAM_CHUNK_BYTES) {
            /* Hand the full buffer to the uploader task and immediately
             * carry on reading into the other one. This is the whole point
             * of the change: the mic is never left unread while a chunk is
             * in flight, so the I2S DMA has nothing to overrun.
             *
             * These bytes start where the already-queued ones end;
             * bytes_captured includes the chunk still in the buffer, so
             * subtract it back off. */
            chunk_job_t job = {
                .pcm = s_capture_buf[active_buf],
                .len = chunk_bytes,
                .offset = bytes_captured - chunk_bytes,
            };
            xQueueSend(s_chunk_queue, &job, portMAX_DELAY);
            chunk_bytes = 0;

            /* Claim the next buffer. Blocks only if the uploader is still
             * busy with the one before it, i.e. only when the network can't
             * keep up - and then this degrades to exactly the old blocking
             * behavior rather than to anything new. Buffers come back in
             * FIFO order from a single uploader, so the freed one is always
             * the next index. */
            active_buf = (active_buf + 1) % AUDIO_CAPTURE_BUFFERS;
            int64_t stall_start_us = esp_timer_get_time();
            /* Published before blocking, not after: the UI timer keeps
             * running on the LVGL task while this one is stuck here, so
             * this is the only way the screen can tell the operator the
             * frozen counter is a stalled upload rather than a crash. */
            portENTER_CRITICAL(&s_mux);
            s_status.uploader_behind = true;
            portEXIT_CRITICAL(&s_mux);
            xSemaphoreTake(s_free_bufs, portMAX_DELAY);
            portENTER_CRITICAL(&s_mux);
            s_status.uploader_behind = false;
            portEXIT_CRITICAL(&s_mux);
            uint32_t stall_ms = (uint32_t)((esp_timer_get_time() - stall_start_us) / 1000);
            if (stall_ms > 0) {
                /* Non-zero means the uploader fell behind and the mic did
                 * stop. Logged loudly because it is the one path that can
                 * still lose audio. */
                ESP_LOGW(TAG, "capture waited %lums for a free buffer - uploader is behind",
                         (unsigned long)stall_ms);
            }

            /* A chunk that failed while we kept recording ends the whole
             * recording, same as before - a gapped transcript must never
             * reach transcription. */
            if (s_chunk_failed) {
                chunk_upload_failed = true;
                strncpy(chunk_fail_reason, s_chunk_fail_reason, sizeof(chunk_fail_reason) - 1);
                chunk_fail_reason[sizeof(chunk_fail_reason) - 1] = '\0';
                break;
            }
        }
    }

    /* Stop the recording clock here, not after the trailing flush below.
     * The codec is closed on the next line, so every millisecond after
     * this point is upload time, not recording time - measuring later
     * charged the trailing chunk's upload (~500ms) against the recording
     * and reported it as audio loss that never happened. */
    int64_t capture_end_us = esp_timer_get_time();
    esp_codec_dev_close(s_mic_dev);

    if (cancelled) {
        /* Unlike STOP, nothing further gets flushed or finished - the
         * trailing partial chunk in the active buffer is simply dropped,
         * and whatever already reached the backend is told to go away too.
         * Uploads in flight are waited out first: a chunk landing after
         * the cancel would resurrect the temp file the cancel just
         * deleted, leaving an orphan on the backend. */
        if (streaming) {
            xSemaphoreGive(s_free_bufs);
            drain_uploads();
            if (req->cancel_fn) {
                req->cancel_fn(req->name);
            }
        }
        ESP_LOGI(TAG, "recording cancelled by request at %lu bytes", (unsigned long)bytes_captured);
        set_cancelled();
        vTaskDelay(pdMS_TO_TICKS(800));
        set_state(AUDIO_CAP_IDLE);
        return;
    }

    if (streaming) {
        /* Queue whatever's left of the last, partial chunk, then wait for
         * the uploader to finish everything. The mic is already closed, so
         * this wait costs no audio - it is why the recording clock stopped
         * at capture_end_us above. */
        if (chunk_bytes > 0 && !chunk_upload_failed) {
            chunk_job_t job = {
                .pcm = s_capture_buf[active_buf],
                .len = chunk_bytes,
                .offset = bytes_captured - chunk_bytes,
            };
            xQueueSend(s_chunk_queue, &job, portMAX_DELAY);
        } else {
            xSemaphoreGive(s_free_bufs);
        }
        chunk_bytes = 0;
        drain_uploads();

        /* Re-check: the trailing chunk, or one still in flight when the
         * loop ended, can fail after the loop has already exited. */
        if (s_chunk_failed && !chunk_upload_failed) {
            chunk_upload_failed = true;
            strncpy(chunk_fail_reason, s_chunk_fail_reason, sizeof(chunk_fail_reason) - 1);
            chunk_fail_reason[sizeof(chunk_fail_reason) - 1] = '\0';
        }
    }

    if (chunk_upload_failed) {
        if (streaming && req->cancel_fn) {
            /* Don't leave a half-uploaded capture accumulating on the
             * backend - it would never be finished or cleaned up. */
            req->cancel_fn(req->name);
        }
        fail_and_recover(chunk_fail_reason);
        return;
    }

    if (stopped_early) {
        ESP_LOGI(TAG, "recording stopped early by request at %lu bytes", (unsigned long)bytes_captured);
    }

    uint32_t samples_captured = bytes_captured / AUDIO_BYTES_PER_SAMPLE;
    double rms_raw = samples_captured ? sqrt(sum_sq / (double)samples_captured) : 0.0;
    uint32_t final_elapsed_ms = (uint32_t)((capture_end_us - start_us) / 1000);

    /* The loop only exits three ways: cancel and chunk-failure already
     * returned above, and stopped_early means the operator pressed STOP -
     * so anything left here ran to target_bytes, i.e. hit the cap. Worth
     * distinguishing because the two are indistinguishable to the operator
     * otherwise: both just end the recording and report success. */
    bool ended_at_cap = !stopped_early;

    /* How much audio actually arrived, versus how long we were recording. */
    uint32_t audio_ms = bytes_captured / AUDIO_BYTES_PER_MS;
    uint32_t shortfall_ms = final_elapsed_ms > audio_ms ? final_elapsed_ms - audio_ms : 0;
    uint32_t shortfall_pct = final_elapsed_ms ? (shortfall_ms * 100u) / final_elapsed_ms : 0;

    uint32_t flush_count, flush_total_ms, flush_max_ms;
    portENTER_CRITICAL(&s_mux);
    flush_count = s_flush_count;
    flush_total_ms = s_flush_total_ms;
    flush_max_ms = s_flush_max_ms;
    portEXIT_CRITICAL(&s_mux);
    if (flush_count > 0) {
        /* These uploads now overlap recording instead of interrupting it,
         * so this is throughput information, not a lost-audio budget.
         * shortfall_ms below is what says whether any audio was actually
         * lost. */
        ESP_LOGI(TAG, "chunk uploads: %lu, %lums total (max %lums, mean %lums), overlapped with recording",
                 (unsigned long)flush_count, (unsigned long)flush_total_ms, (unsigned long)flush_max_ms,
                 (unsigned long)(flush_total_ms / flush_count));
    }
    if (shortfall_ms >= AUDIO_SHORTFALL_NOTABLE_MS) {
        ESP_LOGW(TAG, "AUDIO LOSS: recorded %lums but only %lums of audio arrived - %lums (%lu%%) missing, %lu dropped read(s)",
                 (unsigned long)final_elapsed_ms, (unsigned long)audio_ms,
                 (unsigned long)shortfall_ms, (unsigned long)shortfall_pct, (unsigned long)dropped_reads);
    }
    if (ended_at_cap) {
        ESP_LOGW(TAG, "recording ended by hitting the %lums cap, not by STOP", (unsigned long)duration_ms);
    }

    /* Too damaged to hand off as a usable note. Fails before finish_fn, so
     * the backend never assembles it and nothing reaches transcription -
     * the same "never send a gapped transcript" rule the chunk-failure path
     * already follows. */
    if (shortfall_pct >= AUDIO_SHORTFALL_FAIL_PCT) {
        char reason[AUDIO_CAP_REASON_LEN];
        snprintf(reason, sizeof(reason), "AUDIO LOSS %lu%%", (unsigned long)shortfall_pct);
        ESP_LOGE(TAG, "capture rejected: %lu%% of the recording is missing", (unsigned long)shortfall_pct);
        if (streaming && req->cancel_fn) {
            req->cancel_fn(req->name);
        }
        fail_and_recover(reason);
        return;
    }

    uint32_t seq;
    char artifact_name[AUDIO_CAP_ARTIFACT_NAME_LEN];
    portENTER_CRITICAL(&s_mux);
    s_status.state = AUDIO_CAP_COMPLETE;
    s_status.elapsed_ms = final_elapsed_ms;
    s_status.bytes_captured = bytes_captured;
    s_status.samples_captured = samples_captured;
    s_status.peak_amplitude = (float)peak_raw / 32768.0f;
    s_status.rms_amplitude = (float)(rms_raw / 32768.0);
    s_status.clipped_samples = clipped;
    s_status.audio_ms = audio_ms;
    s_status.shortfall_ms = shortfall_ms;
    s_status.dropped_reads = dropped_reads;
    s_status.ended_at_cap = ended_at_cap;
    s_status.capture_seq++;
    if (req->name[0] != '\0') {
        /* Voice-triggered NOTE/GO/SEND: use the request ID verbatim, so
         * the same ID on screen is the same ID in this log, the artifact
         * name, and (via note_client.c/voice_inbox_client.c/entry_client.c)
         * the upload - never re-derived or renamed along the way. */
        strncpy(s_status.artifact_name, req->name, sizeof(s_status.artifact_name) - 1);
        s_status.artifact_name[sizeof(s_status.artifact_name) - 1] = '\0';
    } else {
        snprintf(s_status.artifact_name, sizeof(s_status.artifact_name), "capture_%03lu", (unsigned long)s_status.capture_seq);
    }
    seq = s_status.capture_seq;
    strncpy(artifact_name, s_status.artifact_name, sizeof(artifact_name) - 1);
    artifact_name[sizeof(artifact_name) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);
    if (!streaming) {
        /* Streaming captures never leave the buffer holding the whole
         * recording (only the last chunk), so they don't update this -
         * audio_capture_get_buffer() keeps reflecting the most recent
         * manual REC capture instead of falsely claiming to hold
         * `bytes_captured` bytes it doesn't actually have. */
        s_last_complete_bytes = bytes_captured;
    }

    ESP_LOGI(TAG, "capture complete #%lu (%s): %lums wall / %lums audio (%lums short), %lu bytes%s, ended=%s, peak=%.3f rms=%.3f clipped=%lu",
             (unsigned long)seq, artifact_name, (unsigned long)final_elapsed_ms, (unsigned long)audio_ms,
             (unsigned long)shortfall_ms, (unsigned long)bytes_captured,
             streaming ? " (streamed)" : "", ended_at_cap ? "CAP" : "STOP",
             (double)(peak_raw / 32768.0f), (double)(rms_raw / 32768.0), (unsigned long)clipped);

    /* 64, not 34: GCC's -Wformat-truncation reasons from artifact_name's
     * declared 23-char array size rather than its short runtime content
     * ("capture_003" etc.), so the buffer has to cover the theoretical
     * worst case. Longest user here is now "NOTE SENT " (10) + 23 +
     * " (HIT CAP)" (10) + nul = 44. */
    char msg[64];
    if (shortfall_ms >= AUDIO_SHORTFALL_NOTABLE_MS) {
        /* Deliberately on the same row the operator already watches for
         * "AUDIO DONE" - a lossy capture must not read like a clean one.
         * Kept inside status_deck_ui.c's EVENT_MSG_LEN (28): worst case
         * "AUD 38400000B LOST 123456ms" is 27 + nul. The earlier wording
         * ran to 32 and got silently clipped on the Black Box row. */
        snprintf(msg, sizeof(msg), "AUD %luB LOST %lums", (unsigned long)bytes_captured,
                 (unsigned long)shortfall_ms);
    } else {
        snprintf(msg, sizeof(msg), "AUDIO DONE %luB", (unsigned long)bytes_captured);
    }
    notify(AUDIO_CAP_COMPLETE, msg);

    if (req->auto_upload) {
        /* Manual REC, unchanged from before Mission 13. */
        set_state(AUDIO_CAP_UPLOADING);
        notify(AUDIO_CAP_UPLOADING, "AUDIO UPLOADING");

        char fail_reason[AUDIO_CAP_REASON_LEN];
        if (!upload_capture(artifact_name, s_capture_buf[0], bytes_captured, fail_reason, sizeof(fail_reason))) {
            fail_and_recover(fail_reason);
            return;
        }

        snprintf(msg, sizeof(msg), "UPLOAD OK %s", artifact_name);
        set_ready(msg);
    } else {
        /* NOTE/GO/SEND - chunk_fn/finish_fn are always both set here,
         * audio_capture_start_note() is the only caller of this path and
         * never passes NULL for either (see its doc comment). Every chunk
         * is already on the backend by now (or this function already
         * returned via fail_and_recover above) - finish_fn is just the "no
         * more chunks coming, go ahead and process it" signal. */
        set_state(AUDIO_CAP_UPLOADING);
        notify(AUDIO_CAP_UPLOADING, "NOTE UPLOADING");

        char fail_reason[AUDIO_CAP_REASON_LEN];
        if (!req->finish_fn(artifact_name, AUDIO_SAMPLE_RATE_HZ, AUDIO_BITS_PER_SAMPLE, AUDIO_CHANNELS,
                             fail_reason, sizeof(fail_reason))) {
            fail_and_recover(fail_reason);
            return;
        }

        if (ended_at_cap) {
            snprintf(msg, sizeof(msg), "NOTE SENT %s (HIT CAP)", artifact_name);
        } else {
            snprintf(msg, sizeof(msg), "NOTE SENT %s", artifact_name);
        }
        set_ready(msg);
    }

    /* Hold READY on screen briefly so the operator actually sees the
     * artifact name before the console goes back to accepting the next
     * capture - matches the "IDLE -> RECORDING -> COMPLETE -> IDLE" repeat
     * cycle the Mission 10 flight narration describes. */
    vTaskDelay(pdMS_TO_TICKS(800));
    set_state(AUDIO_CAP_IDLE);
}

static void worker_task(void *arg)
{
    (void)arg;
    audio_capture_request_t req;
    for (;;) {
        if (xQueueReceive(s_start_queue, &req, portMAX_DELAY) == pdTRUE) {
            perform_capture(&req);
        }
    }
}

void audio_capture_init(esp_codec_dev_handle_t mic_dev, audio_cap_event_cb_t cb, void *user_ctx)
{
    s_cb = cb;
    s_cb_ctx = user_ctx;

    /* PSRAM first; fall back to internal RAM if it's unavailable. Verified
     * against this project's actual sdkconfig: CONFIG_SPIRAM is enabled
     * (Octal mode, matching this board's 16MB Octal-PSRAM module), so this
     * buffer (sized for AUDIO_CAPTURE_BUFFER_MS - the longer of manual
     * REC's 4s and one streaming chunk's 15s, ~469KB) comes from PSRAM, not
     * the same internal SRAM as LVGL's framebuffers, Wi-Fi, and every task
     * stack. Deliberately small even though NOTE/GO/SEND recordings can now
     * run up to AUDIO_NOTE_MAX_DURATION_MS (20 minutes) - see that
     * constant's comment for why streaming chunk uploads keep this buffer
     * from ever needing to grow with recording length. If
     * audio_capture_init() ever logs "BUFFER ALLOC FAILED" again, check
     * CONFIG_SPIRAM in sdkconfig first. */
    bool bufs_ok = true;
    for (int i = 0; i < AUDIO_CAPTURE_BUFFERS; i++) {
        s_capture_buf[i] = heap_caps_malloc(AUDIO_CAPTURE_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_capture_buf[i]) {
            s_capture_buf[i] = heap_caps_malloc(AUDIO_CAPTURE_BUFFER_BYTES, MALLOC_CAP_8BIT);
        }
        if (!s_capture_buf[i]) {
            bufs_ok = false;
        }
    }

    s_chunk_queue = xQueueCreate(AUDIO_CAPTURE_BUFFERS, sizeof(chunk_job_t));
    s_free_bufs = xSemaphoreCreateCounting(AUDIO_CAPTURE_BUFFERS, AUDIO_CAPTURE_BUFFERS);
    if (s_chunk_queue && s_free_bufs) {
        /* Same priority as the capture worker: the recording task spends
         * nearly all its time blocked in esp_codec_dev_read() waiting on
         * I2S DMA, so the uploader gets the CPU without needing to preempt
         * it. Stack matches audio_worker's - this task runs the same
         * esp_http_client path the capture task used to. */
        xTaskCreate(uploader_task, "audio_uploader", 6144, NULL, 5, &s_uploader_task);
    }
    if (!s_chunk_queue || !s_free_bufs || !s_uploader_task) {
        bufs_ok = false;
    }

    if (!bufs_ok) {
        ESP_LOGE(TAG, "capture buffer/uploader init failed (%d buffers of %d bytes)",
                 AUDIO_CAPTURE_BUFFERS, AUDIO_CAPTURE_BUFFER_BYTES);
        set_failed("BUFFER ALLOC FAILED");
    } else if (!mic_dev) {
        ESP_LOGE(TAG, "no microphone codec handle supplied");
        set_failed("MIC INIT FAILED");
    } else {
        s_mic_dev = mic_dev;
        s_mic_available = true;
        ESP_LOGI(TAG, "microphone ready: %dHz/%d-bit/%dch, REC=%dms NOTE cap=%dms, %d x %d byte buffers",
                 AUDIO_SAMPLE_RATE_HZ, AUDIO_BITS_PER_SAMPLE, AUDIO_CHANNELS,
                 AUDIO_CAPTURE_DURATION_MS, AUDIO_NOTE_MAX_DURATION_MS,
                 AUDIO_CAPTURE_BUFFERS, AUDIO_CAPTURE_BUFFER_BYTES);
    }

    s_start_queue = xQueueCreate(1, sizeof(audio_capture_request_t));
    xTaskCreate(worker_task, "audio_worker", 6144, NULL, 5, NULL);
}

void audio_capture_get_status(audio_cap_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    portEXIT_CRITICAL(&s_mux);
}

/* Shared in-flight guard + enqueue for both public start functions below -
 * identical busy check either way, only the queued request shape differs. */
static bool enqueue_capture(const audio_capture_request_t *req)
{
    if (!s_mic_available) {
        return false;
    }

    portENTER_CRITICAL(&s_mux);
    bool busy = (s_status.state != AUDIO_CAP_IDLE);
    portEXIT_CRITICAL(&s_mux);
    if (busy) {
        return false;
    }

    return xQueueSend(s_start_queue, req, 0) == pdTRUE;
}

bool audio_capture_start(void)
{
    audio_capture_request_t req = {
        .name = "",
        .duration_ms = AUDIO_CAPTURE_DURATION_MS,
        .auto_upload = true,
        .chunk_fn = NULL,
        .finish_fn = NULL,
        .cancel_fn = NULL,
    };
    return enqueue_capture(&req);
}

bool audio_capture_start_note(const char *request_id, uint32_t duration_ms, audio_note_chunk_fn_t chunk_fn,
                               audio_note_finish_fn_t finish_fn, audio_note_cancel_fn_t cancel_fn)
{
    audio_capture_request_t req = {
        .duration_ms = duration_ms,
        .auto_upload = false,
        .chunk_fn = chunk_fn,
        .finish_fn = finish_fn,
        .cancel_fn = cancel_fn,
    };
    strncpy(req.name, request_id, sizeof(req.name) - 1);
    req.name[sizeof(req.name) - 1] = '\0';
    return enqueue_capture(&req);
}

void audio_capture_stop(void)
{
    s_stop_requested = true;
}

void audio_capture_cancel(void)
{
    s_cancel_requested = true;
}

const uint8_t *audio_capture_get_buffer(size_t *len_out)
{
    if (s_last_complete_bytes == 0) {
        if (len_out) {
            *len_out = 0;
        }
        return NULL;
    }
    if (len_out) {
        *len_out = s_last_complete_bytes;
    }
    return s_capture_buf[0];
}
