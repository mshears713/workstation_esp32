#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "audio_mix.h"
int main(void)
{
    audio_mix_t m; audio_mix_init(&m);
    int16_t music[256], voice[256], wind[256], out[256];
    for(int i=0;i<256;i++){music[i]=30000;voice[i]=30000;wind[i]=30000;}
    audio_mix_render(&m,out,music,wind,voice,256,true);
    for(int i=0;i<256;i++)assert(out[i]==32767);
    for(int i=0;i<10;i++)audio_mix_render(&m,out,music,0,voice,256,true);
    assert(m.music_gain==11627);
    for(int i=0;i<24;i++)audio_mix_render(&m,out,music,0,0,256,false);
    assert(m.music_gain==32768);
    audio_mix_render(&m,out,0,0,0,256,false);
    for(int i=0;i<256;i++)assert(out[i]==0);
    puts("PASS audio mixer saturation, duck ramp, release, silence");
    return 0;
}
