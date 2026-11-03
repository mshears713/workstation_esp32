#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_streams.h"
static void asset(const char *root,const char *name, int16_t value, size_t samples)
{
    char path[256];snprintf(path,sizeof(path),"%s/%s",root,name);
    FILE *f=fopen(path,"wb");assert(f);
    for(size_t i=0;i<samples;i++)assert(fwrite(&value,2,1,f)==1);
    assert(fclose(f)==0);
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *root=argv[1];
    audio_streams_t s;int16_t out[AUDIO_STREAM_FRAMES];
    assert(!audio_streams_open(&s,root));audio_streams_close(&s);
    asset(root,"music_loop.pcm",1000,3); /* Multiple rewinds within a block. */
    assert(audio_streams_open(&s,root));
    assert(!audio_streams_toggle_wind(&s));
    assert(!audio_streams_start_voice(&s));assert(s.next_voice==0);
    assert(audio_streams_render(&s,out,256));
    for(int i=0;i<256;i++)assert(out[i]==1000);
    asset(root,"wind_loop.pcm",500,7);
    assert(audio_streams_toggle_wind(&s));
    assert(audio_streams_render(&s,out,256));
    for(int i=0;i<256;i++)assert(out[i]==1500);
    for(unsigned v=1;v<=6;v++) {
        char name[20];snprintf(name,sizeof(name),"newton_%u.pcm",v);
        asset(root,name,2000+(int16_t)v,257);
        assert(audio_streams_start_voice(&s));
        assert(!audio_streams_start_voice(&s)); /* Bounded one-voice policy. */
        assert(s.next_voice==v%6);
        assert(audio_streams_render(&s,out,256));assert(s.voice);
        assert(out[0]>3000 && out[0]<4000);
        assert(audio_streams_render(&s,out,256));assert(!s.voice);
        assert(out[0]>3000 && out[255]<1501);
    }
    assert(audio_streams_toggle_wind(&s));assert(!s.wind);
    assert(!audio_streams_render(&s,out,257));
    audio_streams_close(&s);audio_streams_close(&s);
    assert(!s.music && !s.wind && !s.voice);
    for(int i=0;i<200;i++) {
        assert(audio_streams_open(&s,root));
        assert(audio_streams_start_voice(&s));
        assert(audio_streams_toggle_wind(&s));
        assert(audio_streams_render(&s,out,256));
        audio_streams_close(&s);
    }
    asset(root,"music_loop.pcm",0,0);assert(!audio_streams_open(&s,root));
    char path[256];snprintf(path,sizeof(path),"%s/music_loop.pcm",root);
    FILE *f=fopen(path,"wb");assert(f);assert(fputc(1,f)!=EOF);fclose(f);
    assert(!audio_streams_open(&s,root));audio_streams_close(&s);
    puts("PASS streams: looping, overlap, six voices, duplicate rejection, missing/odd/empty files, 200 cleanup cycles");
}
