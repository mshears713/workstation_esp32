/*
 * Artifacts & Instruments speaker owner. Replaces voice_control.c's role as
 * the one task that touches the speaker and the SD card, now that the exhibit
 * build has no microphone pipeline (no WakeNet/MultiNet, no recording).
 *
 * Two kinds of work, strictly one at a time on one task:
 *   scene sessions - Newton / Lincoln, exactly the audio_test_session.c code
 *                    they always used (music + voice + effects mixer from SD);
 *   clips          - a single spoken PCM file from SD, e.g. the radar welcome
 *                    or a Tour A narration: /sdcard/dottie/exhibit/<name>.pcm,
 *                    24000 Hz signed 16-bit little-endian mono (the existing
 *                    ElevenLabs -> device format), at most 30 s.
 *
 * Everything works offline: no backend, no Wi-Fi. A missing card or file is a
 * reported outcome (status.message), never a crash, and the written text on
 * screen is always complete without the audio.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "audio_streams.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXHIBIT_AUDIO_DIR "/dottie/exhibit"   /* under the SD mount point */
#define EXHIBIT_CLIP_MAX_BYTES (24000u * 2u * 30u)

typedef struct {
    bool busy;                 /* a scene session or clip owns the speaker */
    bool clip_playing;
    char clip[16];             /* name of the current / last clip */
    uint32_t clip_started_seq, clip_done_seq, clip_failed_seq;
    char message[48];          /* last outcome, for LOG / DIAGNOSTICS */
} exhibit_audio_status_t;

void exhibit_audio_init(void);

/* Newton / Lincoln: prepares audio_test_session and runs it on the audio task.
 * false if anything already owns the speaker. */
bool exhibit_audio_request_scene(audio_profile_t profile);

/* One spoken clip. `name` is a file stem ("welcome", "egypt"). false if busy
 * or the name is unsafe; true means queued - the outcome arrives in status. */
bool exhibit_audio_play_clip(const char *name);
/* Stops the current clip (harmless if none). Scene sessions stop through
 * audio_test_cancel() as before. */
void exhibit_audio_stop_clip(void);

void exhibit_audio_get_status(exhibit_audio_status_t *out);

#ifdef __cplusplus
}
#endif
