#include "audio_mix.h"
#include <limits.h>
#define UNITY 32768
#define DUCK 11627 /* approximately -9 dB */
/* 100ms attack, 250ms release at 24kHz with fractional-step carry. */
#define GAIN_DELTA (UNITY - DUCK)
void audio_mix_init(audio_mix_t *m) { if(m) { m->music_gain=UNITY; m->fraction=0; m->voice_active=false; } }
void audio_mix_render(audio_mix_t *m, int16_t *out, const int16_t *music,
                      const int16_t *wind, const int16_t *voice, size_t frames,
                      bool voice_active)
{
    if(!m || !out) return;
    if(m->voice_active != voice_active) { m->voice_active=voice_active; m->fraction=0; }
    for(size_t i=0;i<frames;i++) {
        if(voice_active && m->music_gain>DUCK) {
            m->fraction+=GAIN_DELTA;
            m->music_gain-=m->fraction/2400; m->fraction%=2400;
            if(m->music_gain<DUCK)m->music_gain=DUCK;
        } else if(!voice_active && m->music_gain<UNITY) {
            m->fraction+=GAIN_DELTA;
            m->music_gain+=m->fraction/6000; m->fraction%=6000;
            if(m->music_gain>UNITY)m->music_gain=UNITY;
        }
        int32_t s=music ? ((int32_t)music[i]*m->music_gain)/UNITY : 0;
        if(wind)s+=wind[i];
        if(voice_active && voice)s+=voice[i];
        if(s>INT16_MAX)s=INT16_MAX;
        if(s<INT16_MIN)s=INT16_MIN;
        out[i]=(int16_t)s;
    }
}
