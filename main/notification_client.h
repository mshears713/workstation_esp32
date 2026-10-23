/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Low-confidence spoken-notification client: background poll +
 *        on-demand fetch/ack - see notification_client.c.
 * @details App-agnostic like handshake_client.h/graph_client.h: knows
 *          nothing about app_state or LVGL. Two very different usage
 *          shapes live here on purpose:
 *            - notification_client_get_status() is a cheap, thread-safe
 *              snapshot read, meant to be polled often (e.g. from a 200ms
 *              LVGL timer) to drive the listening ring's blue/orange color -
 *              see voice_listening_widget_set_notification().
 *            - notification_client_fetch_audio()/_ack() are blocking,
 *              one-shot network calls, meant to be called directly from
 *              voice_control.c's detect_task when "yes" is recognized while
 *              a notification is pending (same "just block the caller"
 *              shape as graph_client.c's trigger_graph_run()), never from
 *              the LVGL task.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOTIFICATION_ID_LEN 40
/* The front message's spoken_message, cut to fit (the card shows ~170
 * characters; the full message is what PLAY says). */
#define NOTIFICATION_TEXT_LEN 192

/* Fixed format of the audio notification_client_fetch_audio() returns -
 * matches app/voice/tts.py's OpenAI response_format="pcm" output exactly
 * (raw samples, no WAV header). The one source of truth for this triple;
 * notification_client.c and voice_control.c's audio_playback_play() call
 * both reference these rather than re-declaring them. */
#define NOTIFICATION_AUDIO_SAMPLE_RATE_HZ 24000
#define NOTIFICATION_AUDIO_BITS_PER_SAMPLE 16
#define NOTIFICATION_AUDIO_CHANNELS 1

typedef struct {
    bool pending;
    int count;                                /* how many notifications are pending, 0 if none */
    char notification_id[NOTIFICATION_ID_LEN]; /* oldest pending notification's id; "" if none */
    /* Whether that one's text (GET {base}/{id} -> spoken_message, fetched
     * once per new id) is known: 0 not tried yet, 1 loaded, 2 could not load.
     * The text itself: notification_client_get_text(). */
    uint8_t text_state;
    bool polled;                              /* /pending has answered at least once */
} notification_status_t;

/** Starts the background poll task. Call once, from any task. */
void notification_client_init(void);

/** Thread-safe snapshot of the most recent poll result. */
void notification_client_get_status(notification_status_t *out);
/** The front notification's text into `out` ("" unless text_state is 1).
 *  Kept out of notification_status_t, in PSRAM, so status reads stay small. */
void notification_client_get_text(char *out, size_t out_len);

/**
 * Blocking: re-polls GET {base}/pending immediately and updates the same
 * status notification_client_get_status() reads, instead of waiting for the
 * background task's next scheduled poll. Called after a successful ack so
 * the ring can drop back to blue right away when nothing else is pending,
 * rather than lagging by up to the poll interval.
 */
void notification_client_refresh_now(void);

/**
 * Blocking GET of `notification_id`'s audio into an internal buffer owned by
 * this module (allocated once at init, sized for the longest expected clip -
 * same "one reusable buffer, not allocated per fetch" choice audio_capture.c
 * makes for its own capture buffer). On success, `*pcm_out`/`*len_out` point
 * at that buffer (raw 24kHz/16-bit/mono PCM, no WAV header - matches
 * app/voice/tts.py's fixed output format) valid until the next call to this
 * function, and this returns true. On failure, returns false and fills
 * `fail_reason_out` with a short, ready-to-log reason.
 */
bool notification_client_fetch_audio(const char *notification_id,
                                      const uint8_t **pcm_out, size_t *len_out,
                                      char *fail_reason_out, size_t fail_reason_out_len);

/**
 * Blocking POST of {base}/{notification_id}/ack. Returns true only on a 2xx
 * response.
 */
bool notification_client_ack(const char *notification_id);

#ifdef __cplusplus
}
#endif
