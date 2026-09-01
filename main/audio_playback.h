/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Speaker playback for notification audio - see audio_playback.c.
 * @details App-agnostic like audio_capture.h - knows nothing about
 *          app_state, LVGL, or where the PCM came from. Mirrors
 *          audio_capture.c's mic open/read/close shape in the write
 *          direction, against the BSP's documented speaker codec (ES8311,
 *          bsp_audio_codec_speaker_init() - a separate physical device from
 *          the ES7210 mic audio_capture.c/voice_control.c share).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Stores the speaker codec device handle for later playback calls. Call
 * once, after bsp_audio_codec_speaker_init(). `speaker_dev` may be NULL
 * (speaker unavailable this boot) - audio_playback_play() simply returns
 * false forever in that case, the rest of the console is unaffected.
 */
void audio_playback_init(esp_codec_dev_handle_t speaker_dev);

/**
 * Blocking: opens the speaker codec dev at `sample_rate_hz`/16-bit/mono,
 * writes all `len` bytes of `pcm` in chunks (checking audio_playback_stop()
 * between chunks), then closes it. Meant to be called only while nothing
 * else owns the mic/speaker - voice_control.c's notification-playback
 * branch pauses WakeNet listening for the duration first, the same
 * mic-ownership handoff SEND/NOTE already use for recording.
 *
 * Returns true if playback ran to completion, false if it was stopped early
 * via audio_playback_stop(), the speaker failed to open, or `speaker_dev`
 * is unavailable.
 */
bool audio_playback_play(const uint8_t *pcm, size_t len, uint32_t sample_rate_hz);

/**
 * Requests the in-progress playback stop now - same volatile-flag,
 * safe-from-any-task contract as audio_capture_stop() (e.g. the playback
 * overlay's STOP button, called from the LVGL task).
 */
void audio_playback_stop(void);

/**
 * Sets speaker output volume, 0-100 (esp_codec_dev_set_out_vol's own range -
 * out-of-range input is clamped into it). Takes effect on the next
 * audio_playback_play() call, which re-applies the stored volume before
 * opening the codec, same as the fixed level it replaces. Session-only -
 * resets to the default on reboot, not persisted to NVS.
 */
void audio_playback_set_volume(int volume);

/** Current speaker volume, 0-100 - for the volume control's own display. */
int audio_playback_get_volume(void);

#ifdef __cplusplus
}
#endif
