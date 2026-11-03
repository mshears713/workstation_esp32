#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "audio_streams.h"
typedef struct {
    bool running, wind, voice;
    unsigned next_voice;
    unsigned voice_clip;
    uint32_t voice_started_seq, voice_failed_seq;
    uint32_t voice_completed_seq, voice_stopped_seq;
    uint32_t started_id, failed_id, completed_id, stopped_id;
    audio_profile_t profile;
    char message[64];
} audio_test_status_t;
/* Control methods are nonblocking; all files/codec work runs on voice_detect. */
void audio_test_prepare(void);
void audio_test_prepare_profile(audio_profile_t profile);
bool audio_test_lincoln_voice(uint32_t request_id,unsigned clip);
void audio_test_lincoln_stop(uint32_t request_id);
bool audio_test_lincoln_sfx(audio_sfx_t effect);
void audio_test_cancel(void);
void audio_test_wind(void);
void audio_test_set_wind(bool enabled);
void audio_test_voice(void);
bool audio_test_request_voice(void);
void audio_test_get_status(audio_test_status_t *out);
void audio_test_error(const char *message);
void audio_test_run(void);
