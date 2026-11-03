#include "audio_streams.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void asset(const char *root,const char *name,int16_t value,size_t n)
{
    char path[256];snprintf(path,sizeof(path),"%s/%s",root,name);
    FILE *f=fopen(path,"wb");assert(f);
    for(size_t i=0;i<n;i++)assert(fwrite(&value,2,1,f)==1);
    assert(fclose(f)==0);
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *root=argv[1];audio_streams_t s;int16_t out[256];
    asset(root,"music_loop.pcm",1000,3);
    for(unsigned n=1;n<=2;n++){
        char name[20];snprintf(name,sizeof(name),"lincoln_%u.pcm",n);asset(root,name,2000,257);
    }
    asset(root,"cabin_door.pcm",500,7);asset(root,"paper.pcm",300,1);
    assert(audio_streams_open_profile(&s,root,AUDIO_PROFILE_LINCOLN));
    assert(!audio_streams_toggle_wind(&s));assert(!audio_streams_start_voice_clip(&s,2));
    for(unsigned n=0;n<8;n++){
        assert(audio_streams_start_voice(&s));assert(s.next_voice==(n+1)%2);
        assert(audio_streams_start_sfx(&s,AUDIO_SFX_DOOR));
        assert(!audio_streams_start_sfx(&s,AUDIO_SFX_PAPER));
        assert(audio_streams_render(&s,out,256));assert(!s.wind&&s.voice);
        assert(out[0]>out[7]); /* Last short effect block still mixed, no repeat. */
        audio_streams_stop_voice(&s);assert(!s.voice);
        assert(audio_streams_render(&s,out,256));
    }
    assert(!audio_streams_start_sfx(&s,AUDIO_SFX_FIRE));
    assert(!audio_streams_start_sfx(&s,(audio_sfx_t)9));
    asset(root,"lincoln_1.pcm",1,720001);assert(!audio_streams_start_voice_clip(&s,0));
    asset(root,"lincoln_1.pcm",1,720000);assert(audio_streams_start_voice_clip(&s,0));
    audio_streams_stop_voice(&s);
    asset(root,"cabin_fire.pcm",1,120001);assert(!audio_streams_start_sfx(&s,AUDIO_SFX_FIRE));
    assert(audio_streams_start_sfx(&s,AUDIO_SFX_PAPER));audio_streams_stop_sfx(&s);
    audio_streams_close(&s);
    for(unsigned n=0;n<500;n++){
        assert(audio_streams_open_profile(&s,root,AUDIO_PROFILE_LINCOLN));
        assert(audio_streams_start_voice_clip(&s,1));assert(audio_streams_start_sfx(&s,AUDIO_SFX_PAPER));
        assert(audio_streams_render(&s,out,256));audio_streams_close(&s);audio_streams_close(&s);
    }
    puts("PASS Lincoln streams: two clips, one-shot SFX, 3-file bound, strict 30s speech cap, cancellation, failure, 500 cleanup cycles");
}
