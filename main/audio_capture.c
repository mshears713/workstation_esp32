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

/* Mission 13: NOTE's safety-cap duration - STOP (audio_capture_stop()) is
 * the everyday way a note ends, this is only the fallback if it isn't
 * pressed. A real voice note needs more room than a 4s test clip; 60s was
 * chosen as generous-but-bounded, not a measured/tested limit. */
#define AUDIO_NOTE_MAX_DURATION_MS 60000

/* The capture buffer is sized once, at init, for the longer of the two
 * capture kinds (NOTE's 60s, ~1.9MB from PSRAM - comfortably inside the
 * board's 8MB) - never grown, never allocated per-capture. Each individual
 * capture still only fills as many bytes as ITS OWN requested duration
 * needs (see perform_capture's target_bytes) - manual REC's 4s capture
 * only ever touches the first ~128,000 bytes of this buffer, exactly as
 * before. */
#define AUDIO_CAPTURE_BUFFER_MS AUDIO_NOTE_MAX_DURATION_MS
#define AUDIO_CAPTURE_BUFFER_SAMPLES ((AUDIO_SAMPLE_RATE_HZ * AUDIO_CAPTURE_BUFFER_MS) / 1000)
#define AUDIO_CAPTURE_BUFFER_BYTES (AUDIO_CAPTURE_BUFFER_SAMPLES * AUDIO_BYTES_PER_SAMPLE * AUDIO_CHANNELS)

/* 512 samples = 32ms per chunk at 16kHz - fine enough for a readable
 * progress readout without turning every chunk into a log line. */
#define AUDIO_READ_CHUNK_SAMPLES 512
#define AUDIO_READ_CHUNK_BYTES (AUDIO_READ_CHUNK_SAMPLES * AUDIO_BYTES_PER_SAMPLE)
#define AUDIO_READ_MAX_CONSEC_ERRORS 5

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

static uint8_t *s_capture_buf = NULL;      /* AUDIO_CAPTURE_BUFFER_BYTES, allocated once at init */
static uint32_t s_last_complete_bytes = 0; /* 0 until the first capture completes */
static esp_codec_dev_handle_t s_mic_dev = NULL;
static bool s_mic_available = false;       /* false if buffer alloc or codec bring-up failed at init */

/* Set by audio_capture_stop() (any task, e.g. the STOP button's LVGL
 * callback), read once per read-chunk (~32ms) by perform_capture() on the
 * worker task - same bare-volatile, no-mutex-needed pattern
 * voice_control.c's s_mic_owner already uses for a single-flag
 * cross-task signal. Reset at the start of every perform_capture() call so
 * a stale request from a previous capture can never affect the next one. */
static volatile bool s_stop_requested = false;

/* Mission 13: what the worker task actually receives on s_start_queue -
 * queue depth is still 1 (audio_capture_start()'s in-flight guard already
 * ensures at most one capture is ever outstanding, same reasoning as
 * before, now just carrying a few more fields instead of a dummy byte). */
typedef struct {
    char name[AUDIO_CAP_ARTIFACT_NAME_LEN]; /* "" -> auto "capture_NNN" (manual REC); else used verbatim */
    uint32_t duration_ms;                   /* this capture's own target - never exceeds AUDIO_CAPTURE_BUFFER_MS */
    bool auto_upload;                       /* true: this file's own upload_capture() to AUDIO_UPLOAD_PATH */
    audio_note_upload_fn_t upload_fn;       /* used when auto_upload is false; NULL means "don't upload at all" */
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
 * error. Note an upload failure does NOT lose the recording - the buffer
 * is untouched and audio_capture_get_buffer() still returns it - only the
 * network step needs retrying, not the whole capture. */
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
     * returns, which it does here (s_capture_buf, untouched during upload). */
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

static void perform_capture(const audio_capture_request_t *req)
{
    s_stop_requested = false;

    /* req->duration_ms is caller-chosen (AUDIO_CAPTURE_DURATION_MS for
     * manual REC, AUDIO_NOTE_MAX_DURATION_MS for NOTE) but never trusted
     * past the buffer's actual capacity - defensive only, both current
     * callers already stay well inside AUDIO_CAPTURE_BUFFER_MS. */
    uint32_t duration_ms = req->duration_ms;
    if (duration_ms > AUDIO_CAPTURE_BUFFER_MS) {
        duration_ms = AUDIO_CAPTURE_BUFFER_MS;
    }
    uint32_t target_samples = (AUDIO_SAMPLE_RATE_HZ * duration_ms) / 1000;
    uint32_t target_bytes = target_samples * AUDIO_BYTES_PER_SAMPLE * AUDIO_CHANNELS;

    portENTER_CRITICAL(&s_mux);
    s_status.duration_target_ms = duration_ms;
    portEXIT_CRITICAL(&s_mux);

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

    int64_t start_us = esp_timer_get_time();
    uint32_t bytes_captured = 0;
    uint32_t peak_raw = 0;
    uint32_t clipped = 0;
    double sum_sq = 0.0;
    int consec_errors = 0;
    int last_logged_second = -1;
    bool stopped_early = false;

    while (bytes_captured < target_bytes) {
        if (s_stop_requested) {
            stopped_early = true;
            break;
        }

        uint32_t remaining = target_bytes - bytes_captured;
        uint32_t chunk_bytes = remaining < AUDIO_READ_CHUNK_BYTES ? remaining : AUDIO_READ_CHUNK_BYTES;

        err = esp_codec_dev_read(s_mic_dev, s_capture_buf + bytes_captured, (int)chunk_bytes);
        if (err != ESP_CODEC_DEV_OK) {
            consec_errors++;
            if (consec_errors >= AUDIO_READ_MAX_CONSEC_ERRORS) {
                esp_codec_dev_close(s_mic_dev);
                fail_and_recover("READ ERROR");
                return;
            }
            continue;
        }
        consec_errors = 0;

        const int16_t *samples = (const int16_t *)(s_capture_buf + bytes_captured);
        int sample_count = (int)(chunk_bytes / AUDIO_BYTES_PER_SAMPLE);
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

        bytes_captured += chunk_bytes;
        uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
        update_progress(elapsed_ms, bytes_captured, peak_raw, clipped);

        int cur_second = (int)(elapsed_ms / 1000);
        if (cur_second != last_logged_second) {
            last_logged_second = cur_second;
            ESP_LOGI(TAG, "recording %lu/%lums, %lu bytes, peak=%lu",
                     (unsigned long)elapsed_ms, (unsigned long)duration_ms,
                     (unsigned long)bytes_captured, (unsigned long)peak_raw);
        }
    }

    esp_codec_dev_close(s_mic_dev);
    if (stopped_early) {
        ESP_LOGI(TAG, "recording stopped early by request at %lu bytes", (unsigned long)bytes_captured);
    }

    uint32_t samples_captured = bytes_captured / AUDIO_BYTES_PER_SAMPLE;
    double rms_raw = samples_captured ? sqrt(sum_sq / (double)samples_captured) : 0.0;
    uint32_t final_elapsed_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);

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
    s_status.capture_seq++;
    if (req->name[0] != '\0') {
        /* Voice-triggered NOTE: use the request ID verbatim, so the same ID
         * on screen is the same ID in this log, the artifact name, and
         * (via note_client.c) the upload - never re-derived or renamed
         * along the way. */
        strncpy(s_status.artifact_name, req->name, sizeof(s_status.artifact_name) - 1);
        s_status.artifact_name[sizeof(s_status.artifact_name) - 1] = '\0';
    } else {
        snprintf(s_status.artifact_name, sizeof(s_status.artifact_name), "capture_%03lu", (unsigned long)s_status.capture_seq);
    }
    seq = s_status.capture_seq;
    strncpy(artifact_name, s_status.artifact_name, sizeof(artifact_name) - 1);
    artifact_name[sizeof(artifact_name) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);
    s_last_complete_bytes = bytes_captured;

    ESP_LOGI(TAG, "capture complete #%lu (%s): %lums, %lu bytes, peak=%.3f rms=%.3f clipped=%lu",
             (unsigned long)seq, artifact_name, (unsigned long)final_elapsed_ms, (unsigned long)bytes_captured,
             (double)(peak_raw / 32768.0f), (double)(rms_raw / 32768.0), (unsigned long)clipped);

    /* 48, not 32: "UPLOAD OK " (10) + up to artifact_name's declared 23
     * chars + nul = 34 worst case - GCC's -Wformat-truncation reasons from
     * artifact_name's declared array size, not its actual short runtime
     * content ("capture_003" etc.), so it wants a buffer sized for that
     * theoretical worst case. */
    char msg[48];
    snprintf(msg, sizeof(msg), "AUDIO DONE %luB", (unsigned long)bytes_captured);
    notify(AUDIO_CAP_COMPLETE, msg);

    if (req->auto_upload) {
        /* Manual REC, unchanged from before Mission 13. */
        set_state(AUDIO_CAP_UPLOADING);
        notify(AUDIO_CAP_UPLOADING, "AUDIO UPLOADING");

        char fail_reason[AUDIO_CAP_REASON_LEN];
        if (!upload_capture(artifact_name, s_capture_buf, bytes_captured, fail_reason, sizeof(fail_reason))) {
            fail_and_recover(fail_reason);
            return;
        }

        snprintf(msg, sizeof(msg), "UPLOAD OK %s", artifact_name);
        set_ready(msg);
    } else if (req->upload_fn) {
        /* NOTE: same blocking-call-from-this-task shape as upload_capture()
         * above, just handed off to the caller's own function (note_client.c)
         * instead of this file's built-in one - see audio_note_upload_fn_t's
         * doc comment in audio_capture.h for why synchronous, not queued. */
        set_state(AUDIO_CAP_UPLOADING);
        notify(AUDIO_CAP_UPLOADING, "NOTE UPLOADING");

        char fail_reason[AUDIO_CAP_REASON_LEN];
        if (!req->upload_fn(artifact_name, s_capture_buf, bytes_captured,
                             AUDIO_SAMPLE_RATE_HZ, AUDIO_BITS_PER_SAMPLE, AUDIO_CHANNELS,
                             fail_reason, sizeof(fail_reason))) {
            fail_and_recover(fail_reason);
            return;
        }

        snprintf(msg, sizeof(msg), "NOTE SENT %s", artifact_name);
        set_ready(msg);
    } else {
        /* No upload requested at all - recording finalized and left in the
         * buffer for audio_capture_get_buffer(), nothing sent anywhere. */
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
     * (Octal mode, matching this board's 8MB Octal-PSRAM module), so this
     * buffer (sized for AUDIO_CAPTURE_BUFFER_MS - the longer of manual
     * REC's 4s and NOTE's 60s safety cap, ~1.9MB) comes from PSRAM, not the
     * same internal SRAM as LVGL's framebuffers, Wi-Fi, and every task
     * stack. If audio_capture_init() ever logs "BUFFER ALLOC FAILED"
     * again, check CONFIG_SPIRAM in sdkconfig first. */
    s_capture_buf = heap_caps_malloc(AUDIO_CAPTURE_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_capture_buf) {
        s_capture_buf = heap_caps_malloc(AUDIO_CAPTURE_BUFFER_BYTES, MALLOC_CAP_8BIT);
    }
    if (!s_capture_buf) {
        ESP_LOGE(TAG, "capture buffer alloc failed (%d bytes)", AUDIO_CAPTURE_BUFFER_BYTES);
        set_failed("BUFFER ALLOC FAILED");
    } else if (!mic_dev) {
        ESP_LOGE(TAG, "no microphone codec handle supplied");
        set_failed("MIC INIT FAILED");
    } else {
        s_mic_dev = mic_dev;
        s_mic_available = true;
        ESP_LOGI(TAG, "microphone ready: %dHz/%d-bit/%dch, REC=%dms NOTE cap=%dms, buffer %d bytes",
                 AUDIO_SAMPLE_RATE_HZ, AUDIO_BITS_PER_SAMPLE, AUDIO_CHANNELS,
                 AUDIO_CAPTURE_DURATION_MS, AUDIO_NOTE_MAX_DURATION_MS, AUDIO_CAPTURE_BUFFER_BYTES);
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
        .upload_fn = NULL,
    };
    return enqueue_capture(&req);
}

bool audio_capture_start_note(const char *request_id, audio_note_upload_fn_t upload_fn)
{
    audio_capture_request_t req = {
        .duration_ms = AUDIO_NOTE_MAX_DURATION_MS,
        .auto_upload = false,
        .upload_fn = upload_fn,
    };
    strncpy(req.name, request_id, sizeof(req.name) - 1);
    req.name[sizeof(req.name) - 1] = '\0';
    return enqueue_capture(&req);
}

void audio_capture_stop(void)
{
    s_stop_requested = true;
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
    return s_capture_buf;
}
