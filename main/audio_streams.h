#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "audio_mix.h"
#include "lincoln_speeches.h"
/* Single worker owns all calls. Caller must arrange exclusive speaker/mic access. */
#define AUDIO_STREAM_FRAMES 256
typedef enum { AUDIO_PROFILE_NEWTON, AUDIO_PROFILE_LINCOLN } audio_profile_t;
typedef enum { AUDIO_SFX_DOOR, AUDIO_SFX_FIRE, AUDIO_SFX_PUFF, AUDIO_SFX_PAPER } audio_sfx_t;
typedef struct {
    FILE *music, *wind, *voice;
    char root[128];
    unsigned next_voice;
    audio_profile_t profile;
    bool wind_loop;
    audio_mix_t mix;
    /* User-owned stdio caches live with the stream in PSRAM on the board. */
    char music_io[512], wind_io[512], voice_io[512];
    int16_t music_buf[AUDIO_STREAM_FRAMES], wind_buf[AUDIO_STREAM_FRAMES], voice_buf[AUDIO_STREAM_FRAMES];
} audio_streams_t;
/* New/closed object only: close before opening again. */
bool audio_streams_open(audio_streams_t *s,const char *root);
bool audio_streams_open_profile(audio_streams_t *s,const char *root,audio_profile_t profile);
bool audio_streams_toggle_wind(audio_streams_t *s);
bool audio_streams_start_voice(audio_streams_t *s);
bool audio_streams_start_voice_clip(audio_streams_t *s,unsigned zero_based_clip);
void audio_streams_stop_voice(audio_streams_t *s);
/* One bounded non-looping effect uses the existing wind slot; no fourth file. */
bool audio_streams_start_sfx(audio_streams_t *s,audio_sfx_t effect);
void audio_streams_stop_sfx(audio_streams_t *s);
bool audio_streams_render(audio_streams_t *s,int16_t *out,size_t frames);
void audio_streams_close(audio_streams_t *s);
