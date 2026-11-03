#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
/* 24 kHz mono signed PCM; persistent duck envelope, fixed-point Q15. */
typedef struct { int32_t music_gain; int32_t fraction; bool voice_active; } audio_mix_t;
void audio_mix_init(audio_mix_t *m);
void audio_mix_render(audio_mix_t *m, int16_t *out, const int16_t *music,
                      const int16_t *wind, const int16_t *voice, size_t frames,
                      bool voice_active);
