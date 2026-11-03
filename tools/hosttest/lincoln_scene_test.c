#include "lincoln_scene.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
static const lincoln_timing_t timing={400,500,1480,600}; /* TEST FIXTURE, not final art */
static void reveal(lincoln_scene_t *s,uint32_t *id,unsigned *clip)
{
    lincoln_scene_tap_person(s);assert(s->phase==LINCOLN_HAT_OFF);
    assert(!lincoln_scene_take_voice(s,id,clip));
    lincoln_scene_tick(s,400);assert(s->phase==LINCOLN_LOOK);
    lincoln_scene_tick(s,500);assert(s->phase==LINCOLN_PAPER_OUT);
    assert(lincoln_scene_take_paper(s));assert(!lincoln_scene_take_paper(s));
    lincoln_scene_tick(s,1479);assert(!lincoln_scene_take_voice(s,id,clip));
    lincoln_scene_tick(s,1);assert(lincoln_scene_take_voice(s,id,clip));
    assert(!lincoln_scene_take_voice(s,id,clip));
}
int main(void)
{
    lincoln_scene_t s;uint32_t id,stopid;unsigned clip,effect;
    assert(!lincoln_scene_open(&s,&(lincoln_timing_t){0}));
    for(unsigned cycle=0;cycle<500;cycle++) {
        assert(lincoln_scene_open(&s,&timing));
        lincoln_scene_tap_person(&s);assert(s.phase==LINCOLN_IDLE&&s.failed);
        lincoln_scene_ready(&s,true);
        for(unsigned n=0;n<8;n++) {
            reveal(&s,&id,&clip);assert(clip==n%2);
            lincoln_scene_voice_started(&s,id+1,true);assert(s.phase==LINCOLN_WAIT_AUDIO);
            lincoln_scene_voice_started(&s,id,true);assert(s.phase==LINCOLN_READING);
            lincoln_scene_tick(&s,29000);assert(s.phase==LINCOLN_READING);
            lincoln_scene_voice_finished(&s,id,true);assert(s.phase==LINCOLN_RETURN);
            lincoln_scene_tick(&s,600);assert(s.phase==LINCOLN_IDLE);
        }
        reveal(&s,&id,&clip);lincoln_scene_tap_person(&s);
        assert(s.phase==LINCOLN_STOPPING&&lincoln_scene_take_stop(&s,&stopid)&&stopid==id);
        lincoln_scene_voice_started(&s,id,true);assert(s.phase==LINCOLN_STOPPING);
        for(unsigned i=0;i<100;i++)lincoln_scene_tap_person(&s);
        assert(!lincoln_scene_take_voice(&s,&id,&clip));
        lincoln_scene_voice_stopped(&s,id);lincoln_scene_tick(&s,600);
        assert(s.phase==LINCOLN_IDLE&&s.next_clip==0); /* cancelled before start */
        reveal(&s,&id,&clip);lincoln_scene_voice_started(&s,id,false);
        lincoln_scene_tick(&s,600);assert(s.phase==LINCOLN_IDLE&&s.failed&&s.next_clip==0);
        reveal(&s,&id,&clip);lincoln_scene_voice_started(&s,id,true);
        lincoln_scene_tap_person(&s);assert(lincoln_scene_take_stop(&s,&stopid));
        lincoln_scene_voice_finished(&s,id,true);assert(s.phase==LINCOLN_STOPPING);
        lincoln_scene_voice_stopped(&s,id);lincoln_scene_tick(&s,600);assert(s.next_clip==1);
        for(unsigned c=0;c<3;c++){
            assert(lincoln_scene_tap_cabin(&s));assert(!lincoln_scene_tap_cabin(&s));
            assert(lincoln_scene_take_cabin(&s,&effect)&&effect==c);
            lincoln_scene_tick(&s,3000);
        }
        reveal(&s,&id,&clip);lincoln_scene_tick(&s,2000);
        assert(s.failed&&s.phase==LINCOLN_STOPPING);lincoln_scene_voice_stopped(&s,id);
        lincoln_scene_tick(&s,600);reveal(&s,&id,&clip);
        lincoln_scene_voice_started(&s,id,true);lincoln_scene_tick(&s,UINT_MAX);
        assert(s.failed&&s.phase==LINCOLN_STOPPING);
        lincoln_scene_close(&s);assert(lincoln_scene_take_stop(&s,&stopid));
        lincoln_scene_voice_stopped(&s,id);assert(s.phase==LINCOLN_IDLE&&!s.open);
        assert(!lincoln_scene_tap_cabin(&s));assert(!lincoln_scene_take_voice(&s,&id,&clip));
    }
    puts("PASS Lincoln pure state: 500 lifecycles, two-clip callback-driven reading, stop barrier, stale acknowledgements, failure/timeout/overflow/cabin bounds");
}
