/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Speaker playback for notification audio - see audio_playback.h.
 * @details Follows the esp-box-3 BSP's own documented speaker example
 *          verbatim in call order (set volume, then open, then write, then
 *          close - see include/bsp/esp-box-3.h's g03_audio doc comment):
 *
 *              esp_codec_dev_set_out_vol(spk_codec_dev, DEFAULT_VOLUME);
 *              esp_codec_dev_open(spk_codec_dev, &fs);
 *              esp_codec_dev_write(spk_codec_dev, wav_bytes, bytes_read_from_spiffs);
 *              esp_codec_dev_close(spk_codec_dev);
 *
 *          The only departure from that one-shot example is writing in
 *          bounded chunks instead of the whole buffer in a single call, so
 *          audio_playback_stop() (the playback overlay's STOP button) can
 *          take effect between chunks instead of only after the entire clip
 *          has been written - the same reason audio_capture.c's mic read
 *          loop is chunked rather than one big read.
 */

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "audio_playback.h"

static const char *TAG = "audio_playback";

#define PLAYBACK_BITS_PER_SAMPLE 16
#define PLAYBACK_CHANNELS 1

/* ~85ms per chunk at 24kHz/16-bit/mono - fine enough for STOP to feel
 * responsive without turning every chunk into per-call overhead. */
#define PLAYBACK_WRITE_CHUNK_BYTES 4096

/* 0-100 int scale per esp_codec_dev_set_out_vol's documented range - a
 * fixed starting volume for this mission, not exposed as a runtime
 * control, same "fixed, not a measured/tested limit" choice audio_capture.c
 * makes for its own mic gain. */
#define PLAYBACK_VOLUME 70

static esp_codec_dev_handle_t s_spk_dev = NULL;

/* Set by audio_playback_stop() (any task, e.g. the STOP button's LVGL
 * callback), read once per write-chunk by audio_playback_play() - same
 * bare-volatile, no-mutex-needed pattern audio_capture.c's s_stop_requested
 * uses, safe because only one playback is ever in flight (it blocks the
 * calling task, see audio_playback.h). */
static volatile bool s_stop_requested = false;

void audio_playback_init(esp_codec_dev_handle_t speaker_dev)
{
    s_spk_dev = speaker_dev;
    if (!speaker_dev) {
        ESP_LOGW(TAG, "no speaker handle - notification playback unavailable this boot");
    }
}

void audio_playback_stop(void)
{
    s_stop_requested = true;
}

bool audio_playback_play(const uint8_t *pcm, size_t len, uint32_t sample_rate_hz)
{
    if (!s_spk_dev || !pcm || len == 0) {
        return false;
    }

    s_stop_requested = false;

    esp_codec_dev_set_out_vol(s_spk_dev, PLAYBACK_VOLUME);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = PLAYBACK_BITS_PER_SAMPLE,
        .channel = PLAYBACK_CHANNELS,
        .channel_mask = 0,
        .sample_rate = sample_rate_hz,
        .mclk_multiple = 0,
    };
    if (esp_codec_dev_open(s_spk_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "speaker open failed");
        return false;
    }

    ESP_LOGI(TAG, "playback start: %u bytes at %uHz/%dbit/%dch",
             (unsigned)len, (unsigned)sample_rate_hz, PLAYBACK_BITS_PER_SAMPLE, PLAYBACK_CHANNELS);

    size_t written = 0;
    bool stopped_early = false;
    while (written < len) {
        if (s_stop_requested) {
            stopped_early = true;
            break;
        }

        size_t remaining = len - written;
        size_t chunk = remaining < PLAYBACK_WRITE_CHUNK_BYTES ? remaining : PLAYBACK_WRITE_CHUNK_BYTES;

        int err = esp_codec_dev_write(s_spk_dev, (void *)(pcm + written), (int)chunk);
        if (err != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "speaker write error at byte %u: %d", (unsigned)written, err);
            break;
        }
        written += chunk;
    }

    esp_codec_dev_close(s_spk_dev);

    if (stopped_early) {
        ESP_LOGI(TAG, "playback stopped early at %u/%u bytes", (unsigned)written, (unsigned)len);
        return false;
    }
    if (written < len) {
        ESP_LOGW(TAG, "playback ended early on a write error at %u/%u bytes", (unsigned)written, (unsigned)len);
        return false;
    }

    ESP_LOGI(TAG, "playback complete: %u bytes", (unsigned)written);
    return true;
}
