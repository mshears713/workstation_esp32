#include "lincoln_scene.h"
#include <string.h>
#include "lincoln_motion_data.inc"

lincoln_timing_t lincoln_scene_native_timing(void)
{ return (lincoln_timing_t)LINCOLN_V2_TIMING; }

static unsigned sample(const uint8_t *frames,const uint16_t *dur,unsigned count,uint32_t ms,uint32_t *left)
{
    for(unsigned i=0;i<count;i++){
        if(ms<dur[i]||i+1==count){if(left)*left=ms<dur[i]?dur[i]-ms:1;return frames[i];}
        ms-=dur[i];
    }
    if(left)*left=1;
    return 0;
}
static unsigned range(unsigned first,unsigned last,uint32_t ms,uint32_t *left)
{
    for(unsigned f=first;f<=last;f++){
        if(ms<frame_ms[f]||f==last){if(left)*left=ms<frame_ms[f]?frame_ms[f]-ms:1;return f;}
        ms-=frame_ms[f];
    }
    return first;
}
static unsigned pose(lincoln_phase_t phase,uint32_t ms,uint32_t *left)
{
    switch(phase){
    case LINCOLN_HAT_OFF:return range(4,15,ms,left);
    case LINCOLN_LOOK:return range(16,19,ms,left);
    case LINCOLN_PAPER_OUT:return range(20,20,ms,left);
    case LINCOLN_READING:{
        uint32_t period=1200;for(unsigned f=22;f<=41;f++)period+=frame_ms[f];
        ms%=period;
        for(unsigned first=22;first<=37;first+=5){
            if(ms<300){if(left)*left=300-ms;return 21;}ms-=300;
            uint32_t duration=0;for(unsigned f=first;f<first+5;f++)duration+=frame_ms[f];
            if(ms<duration)return range(first,first+4,ms,left);
            ms-=duration;
        }
        break;
    }
    case LINCOLN_WAIT_AUDIO:if(left)*left=300;return 21;
    default:return range(0,3,ms%LINCOLN_IDLE_MS,left);
    }
    if(left)*left=300;
    return 21;
}
unsigned lincoln_scene_actor_frame(const lincoln_scene_t *s)
{
    if(s->phase==LINCOLN_STOPPING)return pose(s->cancel_phase,s->cancel_elapsed_ms,NULL);
    if(s->phase==LINCOLN_RETURN)return sample(s->return_frames,s->return_ms,s->return_count,s->elapsed_ms,NULL);
    return pose(s->phase,s->elapsed_ms,NULL);
}
static void add_return(lincoln_scene_t *s,unsigned frame,unsigned ms)
{
    if(s->return_count>=40)return; /* Plans are bounded to at most32 native frames. */
    s->return_frames[s->return_count]=(uint8_t)frame;s->return_ms[s->return_count++]=(uint16_t)ms;
    s->timing.return_ms+=ms;
}
static void plan_return(lincoln_scene_t *s)
{
    uint32_t left=1;unsigned f=pose(s->cancel_phase,s->cancel_elapsed_ms,&left);
    s->return_count=0;s->timing.return_ms=0;
    add_return(s,f,left);
    if(f>=4&&f<=20){for(unsigned n=f+1;n<=20;n++)add_return(s,n,frame_ms[n]);add_return(s,21,300);}
    else if(f>=22&&f<=41){unsigned end=26+5*((f-22)/5);if(f!=end&&f!=end-4)add_return(s,end,frame_ms[end]);add_return(s,21,300);}
    for(unsigned i=0;i<sizeof(closing_frames);i++)add_return(s,closing_frames[i],frame_ms[closing_frames[i]]);
}
static uint32_t cabin_duration(unsigned action)
{ const uint32_t durations[]={2800,1760,1740};return durations[action%3]; }
unsigned lincoln_scene_cabin_frame(const lincoln_scene_t *s)
{
    if(!s->cabin_left_ms)return 0;
    unsigned a=s->cabin_active%3;uint32_t total=cabin_duration(a),elapsed=total-(s->cabin_left_ms<total?s->cabin_left_ms:total);
    if(a==0)return sample(door_frames,door_ms,sizeof(door_frames),elapsed,NULL);
    if(a==1)return sample(hearth_frames,hearth_ms,sizeof(hearth_frames),elapsed,NULL);
    return sample(puff_frames,puff_ms,sizeof(puff_frames),elapsed,NULL);
}

static void phase(lincoln_scene_t *s, lincoln_phase_t p)
{ s->phase=p;s->elapsed_ms=0; }
static void stop(lincoln_scene_t *s)
{
    s->cancel_phase=s->phase;s->cancel_elapsed_ms=s->elapsed_ms;
    s->request_voice=s->request_paper=false;
    s->request_stop=true;
    phase(s,LINCOLN_STOPPING);
}
bool lincoln_scene_open(lincoln_scene_t *s,const lincoln_timing_t *t)
{
    if(!s||!t||!t->hat_ms||!t->look_ms||!t->paper_ms||!t->return_ms||
       t->hat_ms>5000||t->look_ms>5000||t->paper_ms>5000||t->return_ms>5000)return false;
    memset(s,0,sizeof(*s));s->open=true;s->timing=*t;return true;
}
void lincoln_scene_close(lincoln_scene_t *s)
{
    if(!s)return;
    s->open=s->ready=s->request_voice=s->request_paper=s->cabin_request=false;
    s->request_stop=true;s->cabin_left_ms=0;phase(s,LINCOLN_STOPPING);
}
void lincoln_scene_ready(lincoln_scene_t *s,bool ready)
{
    if(!s||!s->open)return;
    s->ready=ready;
    if(!ready && s->phase!=LINCOLN_IDLE && s->phase!=LINCOLN_STOPPING){s->failed=true;stop(s);}
}
void lincoln_scene_tap_person(lincoln_scene_t *s)
{
    if(!s||!s->open)return;
    if(s->phase==LINCOLN_STOPPING||s->phase==LINCOLN_RETURN)return;
    if(s->phase!=LINCOLN_IDLE){stop(s);return;}
    if(!s->ready){s->failed=true;return;}
    s->failed=false;s->cancel_phase=LINCOLN_IDLE;s->clip=s->next_clip;s->request_id++;
    if(!s->request_id)s->request_id=1;
    phase(s,LINCOLN_HAT_OFF);
}
void lincoln_scene_tick(lincoln_scene_t *s,uint32_t ms)
{
    if(!s||!s->open)return;
    s->cabin_left_ms=ms>=s->cabin_left_ms?0:s->cabin_left_ms-ms;
    if(s->phase==LINCOLN_IDLE){s->elapsed_ms=(s->elapsed_ms+ms%LINCOLN_IDLE_MS)%LINCOLN_IDLE_MS;return;}
    /* At most four finite art phases; never fast-forward through worker events. */
    for(unsigned n=0;n<5;n++) {
        uint32_t limit=0;
        switch(s->phase) {
        case LINCOLN_HAT_OFF:limit=s->timing.hat_ms;break;
        case LINCOLN_LOOK:limit=s->timing.look_ms;break;
        case LINCOLN_PAPER_OUT:limit=s->timing.paper_ms;break;
        case LINCOLN_RETURN:limit=s->timing.return_ms;break;
        case LINCOLN_WAIT_AUDIO:limit=2000;break;
        case LINCOLN_READING:limit=32000;break; /* Safety only, never normal end. */
        default:return;
        }
        uint32_t left=limit-s->elapsed_ms;
        if(ms<left){s->elapsed_ms+=ms;return;}
        ms-=left;
        switch(s->phase) {
        case LINCOLN_HAT_OFF:phase(s,LINCOLN_LOOK);break;
        case LINCOLN_LOOK:phase(s,LINCOLN_PAPER_OUT);s->request_paper=true;break;
        case LINCOLN_PAPER_OUT:phase(s,LINCOLN_WAIT_AUDIO);s->request_voice=true;return;
        case LINCOLN_RETURN:phase(s,LINCOLN_IDLE);return;
        default:s->failed=true;stop(s);return;
        }
    }
}
bool lincoln_scene_take_voice(lincoln_scene_t *s,uint32_t *id,unsigned *clip)
{
    if(!s||!id||!clip||!s->open||s->phase!=LINCOLN_WAIT_AUDIO||!s->request_voice)return false;
    s->request_voice=false;*id=s->request_id;*clip=s->clip;return true;
}
bool lincoln_scene_take_stop(lincoln_scene_t *s,uint32_t *id)
{
    if(!s||!id||!s->request_stop)return false;
    s->request_stop=false;*id=s->request_id;return true;
}
bool lincoln_scene_take_paper(lincoln_scene_t *s)
{ if(!s||!s->open||!s->request_paper)return false;s->request_paper=false;return true; }
void lincoln_scene_voice_started(lincoln_scene_t *s,uint32_t id,bool success)
{
    if(!s||!s->open||id!=s->request_id||s->phase!=LINCOLN_WAIT_AUDIO)return;
    if(success){s->next_clip=(s->clip+1)%LINCOLN_SPEECH_COUNT;phase(s,LINCOLN_READING);}
    else{s->failed=true;s->cancel_phase=LINCOLN_WAIT_AUDIO;s->cancel_elapsed_ms=0;plan_return(s);phase(s,LINCOLN_RETURN);}
}
void lincoln_scene_voice_finished(lincoln_scene_t *s,uint32_t id,bool success)
{
    if(!s||!s->open||id!=s->request_id||s->phase!=LINCOLN_READING)return;
    if(!success)s->failed=true;
    s->cancel_phase=s->phase;s->cancel_elapsed_ms=s->elapsed_ms;plan_return(s);phase(s,LINCOLN_RETURN);
}
void lincoln_scene_voice_stopped(lincoln_scene_t *s,uint32_t id)
{
    if(!s||id!=s->request_id||s->phase!=LINCOLN_STOPPING)return;
    if(s->open)plan_return(s);
    phase(s,s->open?LINCOLN_RETURN:LINCOLN_IDLE);
}
bool lincoln_scene_tap_cabin(lincoln_scene_t *s)
{
    if(!s||!s->open||!s->ready||s->cabin_left_ms||s->cabin_request)return false;
    s->cabin_active=s->cabin_next;s->cabin_next=(s->cabin_next+1)%3;
    s->cabin_left_ms=cabin_duration(s->cabin_active);s->cabin_request=true;return true;
}
bool lincoln_scene_take_cabin(lincoln_scene_t *s,unsigned *effect)
{
    if(!s||!effect||!s->open||!s->cabin_request||s->cabin_left_ms>cabin_duration(s->cabin_active)-400)return false;
    s->cabin_request=false;*effect=s->cabin_active;return true;
}
