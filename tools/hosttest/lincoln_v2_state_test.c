#include "lincoln_scene.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
static void open_scene(lincoln_scene_t *s)
{lincoln_timing_t t=lincoln_scene_native_timing();assert(lincoln_scene_open(s,&t));lincoln_scene_ready(s,true);}
static uint32_t start(lincoln_scene_t *s,unsigned expected)
{
    uint32_t id;unsigned clip;lincoln_scene_tap_person(s);
    lincoln_scene_tick(s,2720);assert(s->phase==LINCOLN_LOOK&&lincoln_scene_actor_frame(s)==16);
    lincoln_scene_tick(s,1460);assert(s->phase==LINCOLN_PAPER_OUT&&lincoln_scene_take_paper(s));
    lincoln_scene_tick(s,299);assert(!lincoln_scene_take_voice(s,&id,&clip));
    lincoln_scene_tick(s,1);assert(lincoln_scene_actor_frame(s)==21&&lincoln_scene_take_voice(s,&id,&clip)&&clip==expected);
    assert(!lincoln_scene_take_voice(s,&id,&clip));lincoln_scene_voice_started(s,id+1,true);assert(s->phase==LINCOLN_WAIT_AUDIO);
    lincoln_scene_voice_started(s,id,true);assert(s->phase==LINCOLN_READING);return id;
}
static void idle(lincoln_scene_t *s)
{
    assert(s->phase==LINCOLN_RETURN&&s->return_count<=40&&s->timing.return_ms<=10000);
    for(unsigned t=0;s->phase!=LINCOLN_IDLE&&t<10001;t+=10){assert(lincoln_scene_actor_frame(s)<54);lincoln_scene_tick(s,10);}
    assert(s->phase==LINCOLN_IDLE&&lincoln_scene_actor_frame(s)==0);
}
int main(void)
{
    lincoln_scene_t s;uint32_t id,stop;unsigned clip;
    for(unsigned cycle=0;cycle<500;cycle++){
        open_scene(&s);
        for(unsigned n=0;n<4;n++){
            id=start(&s,n%2);lincoln_scene_tick(&s,25920);assert(s.phase==LINCOLN_READING);
            unsigned before=lincoln_scene_actor_frame(&s);
            lincoln_scene_voice_finished(&s,id,true);assert(lincoln_scene_actor_frame(&s)==before);idle(&s);
        }
        id=start(&s,0);lincoln_scene_tick(&s,UINT_MAX);assert(s.failed&&s.phase==LINCOLN_STOPPING);
        assert(lincoln_scene_take_stop(&s,&stop)&&stop==id);lincoln_scene_voice_stopped(&s,id);idle(&s);
        lincoln_scene_close(&s);assert(lincoln_scene_take_stop(&s,&stop));lincoln_scene_voice_stopped(&s,id);assert(!s.open);
    }
    bool seen[54]={0};unsigned stops=0;
    for(unsigned at=0;at<4480;at+=20){
        open_scene(&s);lincoln_scene_tap_person(&s);lincoln_scene_tick(&s,at);unsigned f=lincoln_scene_actor_frame(&s);
        seen[f]=true;lincoln_scene_tap_person(&s);assert(s.phase==LINCOLN_STOPPING&&lincoln_scene_actor_frame(&s)==f);
        assert(lincoln_scene_take_stop(&s,&id));assert(!lincoln_scene_take_stop(&s,&id));
        assert(!lincoln_scene_take_voice(&s,&id,&clip));lincoln_scene_tick(&s,200);assert(lincoln_scene_actor_frame(&s)==f);
        lincoln_scene_voice_stopped(&s,id);assert(lincoln_scene_actor_frame(&s)==f);idle(&s);assert(s.next_clip==0);stops++;
    }
    for(unsigned f=4;f<=20;f++)assert(seen[f]);
    for(unsigned at=0;at<16000;at+=20){
        open_scene(&s);id=start(&s,0);lincoln_scene_tick(&s,at);unsigned f=lincoln_scene_actor_frame(&s);seen[f]=true;
        lincoln_scene_tap_person(&s);assert(lincoln_scene_take_stop(&s,&stop)&&id==stop);
        lincoln_scene_voice_finished(&s,id,true);assert(s.phase==LINCOLN_STOPPING);
        lincoln_scene_voice_stopped(&s,id);assert(lincoln_scene_actor_frame(&s)==f);idle(&s);assert(s.next_clip==1);stops++;
    }
    for(unsigned f=21;f<=41;f++)assert(seen[f]);
    open_scene(&s);id=start(&s,0);
    const unsigned duration[]={2800,1760,1740};
    for(unsigned n=0;n<3;n++){
        assert(lincoln_scene_tap_cabin(&s));assert(!lincoln_scene_tap_cabin(&s));unsigned effect;
        assert(lincoln_scene_cabin_frame(&s)==0&&!lincoln_scene_take_cabin(&s,&effect));
        lincoln_scene_tick(&s,399);assert(!lincoln_scene_take_cabin(&s,&effect));
        lincoln_scene_tick(&s,1);assert(lincoln_scene_take_cabin(&s,&effect)&&effect==n);
        for(unsigned ms=400;ms<duration[n];ms+=20){assert(lincoln_scene_cabin_frame(&s)<16);lincoln_scene_tick(&s,20);}
        assert(!s.cabin_left_ms&&lincoln_scene_cabin_frame(&s)==0&&s.phase==LINCOLN_READING);
    }
    printf("PASS v2 state: 500 lifecycles, exactly two completion-driven speeches, %u preparation/reading cancellations, all20 reading poses, independent cabin clock, bounds and stop acknowledgements\n",stops);
}
