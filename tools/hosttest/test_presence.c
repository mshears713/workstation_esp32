/* Host test for main/presence_logic.c: the radar welcome's dwell / one-shot /
 * re-arm rules, sampled at the firmware's 100 ms radar poll. */
#include "presence_logic.h"
#include <assert.h>
#include <stdio.h>

static presence_logic_t l;
static uint32_t now;
static unsigned fired;

/* Feed `ms` of samples at 100 ms with a fixed detection value. */
static void run(uint32_t ms, bool detected, bool can)
{
    for (uint32_t t = 0; t < ms; t += 100) {
        now += 100;
        if (presence_sample(&l, detected, can, now)) fired++;
    }
}

int main(void)
{
    presence_init(&l, (presence_params_t){ 0 });
    assert(l.p.dwell_ms == 6000 && l.p.gap_ms == 2500 && l.p.rearm_ms == 30000);

    /* Walk-past: 3 s of presence then gone - no welcome. */
    run(3000, true, true); run(5000, false, true);
    assert(fired == 0 && l.state == PRESENCE_IDLE);

    /* Stays 6 s: exactly one welcome, at the dwell, not before. */
    run(5900, true, true); assert(fired == 0);
    run(200, true, true);  assert(fired == 1 && l.state == PRESENCE_LATCHED);

    /* Same visitor stays 2 minutes, with Doppler dropouts: still one welcome. */
    for (int i = 0; i < 24; i++) { run(4000, true, true); run(1000, false, true); }
    assert(fired == 1);

    /* Leaves for 20 s (< 30 s re-arm) and returns: no second welcome. */
    run(20000, false, true); run(8000, true, true);
    assert(fired == 1);

    /* Leaves for 31 s: re-armed, next visitor welcomed once. */
    run(31000, false, true); assert(l.state == PRESENCE_IDLE && l.rearms == 1);
    run(6100, true, true);   assert(fired == 2);

    /* Brief dropout (< gap) while waiting is forgiven; a long one resets. */
    run(31000, false, true);
    run(3000, true, true); run(2000, false, true); run(1100, true, true);
    assert(fired == 3);
    run(31000, false, true);
    run(3000, true, true); run(3000, false, true); run(3000, true, true);
    assert(fired == 3 && l.state == PRESENCE_CANDIDATE);   /* timer restarted at 3 s */
    run(3100, true, true); assert(fired == 4);

    /* Ready while HOME is busy (tour open): held, fires when HOME returns. */
    run(31000, false, true);
    run(9000, true, false); assert(fired == 4 && l.state == PRESENCE_CANDIDATE);
    run(100, true, true);   assert(fired == 5);

    /* A touch first: no welcome for that visitor. */
    run(31000, false, true);
    run(2000, true, true); presence_engaged(&l, now); run(10000, true, true);
    assert(fired == 5 && l.state == PRESENCE_LATCHED);

    /* Sensor missing = never detected: never fires, never crashes. */
    presence_init(&l, (presence_params_t){ 0 }); fired = 0;
    run(600000, false, true); assert(fired == 0);

    /* lv_tick wrap: dwell measured across 0xFFFFFFFF. */
    presence_init(&l, (presence_params_t){ 0 }); fired = 0; now = 0xFFFFFFFFu - 3000;
    run(6100, true, true); assert(fired == 1);

    puts("PRESENCE_LOGIC_PASS");
    return 0;
}
