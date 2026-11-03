#include "audio_streams.h"
#include <string.h>
static FILE *open_asset(const audio_streams_t *s,const char *name,char *cache)
{
    char path[160]; int n=snprintf(path,sizeof(path),"%s/%s",s->root,name);
    if(n<0 || (size_t)n>=sizeof(path))return NULL;
    FILE *f=fopen(path,"rb");
    if(!f)return NULL;
    if(setvbuf(f,cache,_IOFBF,512)!=0){fclose(f);return NULL;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}
    long bytes=ftell(f);
    if(bytes<=0 || (bytes & 1) || fseek(f,0,SEEK_SET)!=0){fclose(f);return NULL;}
    return f;
}
static bool read_loop(FILE *f,int16_t *buf,size_t count)
{
    size_t got=0; bool rewound=false;
    while(got<count) {
        size_t n=fread(buf+got,sizeof(*buf),count-got,f); got+=n;
        if(n) {rewound=false;continue;}
        if(ferror(f) || rewound)return false;
        if(fseek(f,0,SEEK_SET)!=0)return false;
        rewound=true;
    }
    return true;
}
bool audio_streams_open(audio_streams_t *s,const char *root)
{ return audio_streams_open_profile(s,root,AUDIO_PROFILE_NEWTON); }
bool audio_streams_open_profile(audio_streams_t *s,const char *root,audio_profile_t profile)
{
    if(!s||!root||strlen(root)>=sizeof(s->root)||
       (profile!=AUDIO_PROFILE_NEWTON&&profile!=AUDIO_PROFILE_LINCOLN))return false;
    memset(s,0,sizeof(*s));strcpy(s->root,root);audio_mix_init(&s->mix);
    s->profile=profile;
    s->music=open_asset(s,"music_loop.pcm",s->music_io);return s->music!=NULL;
}
bool audio_streams_toggle_wind(audio_streams_t *s)
{
    if(!s||!s->music||s->profile!=AUDIO_PROFILE_NEWTON)return false;
    if(s->wind){fclose(s->wind);s->wind=NULL;return true;}
    s->wind=open_asset(s,"wind_loop.pcm",s->wind_io);s->wind_loop=true;return s->wind!=NULL;
}
bool audio_streams_start_voice(audio_streams_t *s)
{ return s&&audio_streams_start_voice_clip(s,s->next_voice); }
bool audio_streams_start_voice_clip(audio_streams_t *s,unsigned clip)
{
    if(!s||!s->music||s->voice)return false;
    unsigned count=s->profile==AUDIO_PROFILE_LINCOLN?LINCOLN_SPEECH_COUNT:6;
    if(clip>=count)return false;
    char name[20];snprintf(name,sizeof(name),"%s_%u.pcm",
                         s->profile==AUDIO_PROFILE_LINCOLN?"lincoln":"newton",clip+1);
    s->voice=open_asset(s,name,s->voice_io);if(!s->voice)return false;
    if (fseek(s->voice,0,SEEK_END)!=0 || ftell(s->voice)>24000L*2*30 ||
            fseek(s->voice,0,SEEK_SET)!=0) {
        fclose(s->voice);s->voice=NULL;return false;
    }
    s->next_voice=(clip+1)%count;return true;
}
void audio_streams_stop_voice(audio_streams_t *s)
{ if(s&&s->voice){fclose(s->voice);s->voice=NULL;} }
void audio_streams_stop_sfx(audio_streams_t *s)
{ if(s&&s->wind){fclose(s->wind);s->wind=NULL;} }
bool audio_streams_start_sfx(audio_streams_t *s,audio_sfx_t effect)
{
    static const char *const names[]={"cabin_door.pcm","cabin_fire.pcm","cabin_puff.pcm","paper.pcm"};
    if(!s||!s->music||s->profile!=AUDIO_PROFILE_LINCOLN||s->wind||(unsigned)effect>=4)return false;
    s->wind=open_asset(s,names[effect],s->wind_io);s->wind_loop=false;
    if(!s->wind)return false;
    /* Bounded one-shot effects; invalid files never hold the effect channel. */
    if(fseek(s->wind,0,SEEK_END)!=0||ftell(s->wind)>24000L*2*5||fseek(s->wind,0,SEEK_SET)!=0){
        audio_streams_stop_sfx(s);return false;
    }
    return true;
}
bool audio_streams_render(audio_streams_t *s,int16_t *out,size_t frames)
{
    if(!s||!s->music||!out||!frames||frames>AUDIO_STREAM_FRAMES)return false;
    if(!read_loop(s->music,s->music_buf,frames))return false;
    bool had_wind=s->wind!=NULL;
    if(s->wind) {
        if(s->wind_loop){if(!read_loop(s->wind,s->wind_buf,frames))return false;}
        else {
            memset(s->wind_buf,0,frames*sizeof(int16_t));
            size_t got=fread(s->wind_buf,sizeof(int16_t),frames,s->wind);
            if(ferror(s->wind))return false;
            if(got<frames)audio_streams_stop_sfx(s);
        }
    }
    size_t voice_frames=0;
    if(s->voice) {
        voice_frames=fread(s->voice_buf,sizeof(int16_t),frames,s->voice);
        if(ferror(s->voice))return false;
        if(voice_frames<frames) {fclose(s->voice);s->voice=NULL;}
    }
    if(voice_frames) audio_mix_render(&s->mix,out,s->music_buf,
            had_wind?s->wind_buf:NULL,s->voice_buf,voice_frames,true);
    if(voice_frames<frames) audio_mix_render(&s->mix,out+voice_frames,
            s->music_buf+voice_frames,had_wind?s->wind_buf+voice_frames:NULL,
            NULL,frames-voice_frames,false);
    return true;
}
void audio_streams_close(audio_streams_t *s)
{
    if(!s)return;
    if(s->music)fclose(s->music);
    if(s->wind)fclose(s->wind);
    if(s->voice)fclose(s->voice);
    s->music=s->wind=s->voice=NULL;
}
