#include "newton_scene.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    newton_scene_t s; newton_saved_t saved = {0};
    newton_scene_open(&s, &saved);
    newton_scene_tap_person(&s); assert(!newton_scene_take_voice_request(&s));
    newton_scene_audio_state(&s, true, true, false);
    for (unsigned clip = 1; clip <= 6; ++clip) {
        for (int i = 0; i < 100; ++i) newton_scene_tap_person(&s);
        assert(newton_scene_take_voice_request(&s));
        assert(!newton_scene_take_voice_request(&s));
        newton_scene_voice_ack(&s, true, clip); assert(s.speaking && s.clip == clip);
        assert(strcmp(newton_scene_action_name(s.action), "idle"));
        for (int i = 0; i < 100; ++i) newton_scene_tap_person(&s);
        assert(!newton_scene_take_voice_request(&s));
        newton_scene_tick(&s, 1800);
        newton_scene_audio_state(&s, true, true, false);
        assert(newton_scene_take_voice_request(&s)); /* just one queued */
        newton_scene_voice_ack(&s, false, 0);
        assert(s.request_failed && !newton_scene_take_voice_request(&s));
        if (clip == 3) assert(s.person_offset == NEWTON_WALK_DISTANCE);
        if (clip == 4) assert(s.person_offset == 0);
    }
    assert(newton_scene_drop(&s)); assert(!newton_scene_drop(&s));
    assert(newton_scene_fruit_phase(&s, 0) == NEWTON_ATTACHED);
    newton_scene_tick(&s, 1600);
    assert(s.person_offset==NEWTON_WALK_DISTANCE && newton_scene_fruit_phase(&s,0)==NEWTON_FALLING);
    newton_scene_tick(&s, 595);
    assert(s.fall_ms-s.approach_ms==595); /* native last fall frame contacts crown */
    newton_scene_tick(&s, 485);
    assert(newton_scene_fruit_phase(&s, 0) == NEWTON_ON_GROUND);
    newton_scene_save(&s, &saved); newton_scene_close(&s);
    newton_scene_tick(&s, UINT32_MAX); assert(s.saved.regrow_left_ms[0] == saved.regrow_left_ms[0]);
    newton_scene_open(&s, &saved); assert(newton_scene_fruit_phase(&s, 0) == NEWTON_ON_GROUND);
    newton_scene_tick(&s, 17000); assert(newton_scene_fruit_phase(&s, 0) == NEWTON_ON_GROUND);
    assert(newton_scene_drop(&s));assert(!newton_scene_drop(&s));
    assert(newton_scene_fruit_phase(&s, 0) == NEWTON_REGROWING);
    newton_scene_tick(&s, UINT32_MAX); assert(newton_scene_fruit_phase(&s, 0) == NEWTON_ATTACHED);
    newton_scene_audio_state(&s, true, false, false);
    newton_scene_tap_person(&s); assert(newton_scene_take_voice_request(&s));
    newton_scene_tick(&s, 2000); assert(s.request_failed && !s.voice_pending);
    newton_scene_voice_ack(&s, true, 2); assert(!s.speaking); /* stale acknowledgement */
    assert(newton_scene_drop(&s));newton_scene_tick(&s,3000);
    assert(newton_scene_fruit_phase(&s,0)==NEWTON_ON_GROUND);
    for (int i = 0; i < 500; ++i) {
        newton_scene_save(&s, &saved); newton_scene_close(&s); newton_scene_open(&s, &saved);
        assert(!s.voice_pending && !s.queued_voice && !s.speaking && s.falling == -1);
    }
    puts("PASS Newton state: six acknowledged actions, one queued voice, failure/timeout, fruit fall/regrow/persistence, elapsed overflow, 500 lifecycles");
}
