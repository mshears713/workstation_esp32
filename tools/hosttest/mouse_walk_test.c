#include "ui_mouse_walk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    ui_mouse_walk_t s; ui_mouse_walk_reset(&s);
    int visits[2][3]={{0}},left=0,right=0;
    for(int n=0;n<10000;n++) {
        if(n%13==0) {
            s.paused=true;ui_mouse_walk_t before=s;
            for(int k=0;k<20;k++)ui_mouse_walk_step(&s);
            assert(!memcmp(&before,&s,sizeof s));s.paused=false;
        }
        int old=s.station;ui_mouse_walk_step(&s);
        assert(s.x>=8&&s.x<=264&&s.frame>=0&&s.frame<6);
        assert(s.station>=-1&&s.station<3&&s.dwell>=0&&s.dwell<=5);
        if(old<0&&s.station>=0)visits[s.direction<0][s.station]++;
        left+=s.x==8;right+=s.x==264;
    }
    for(int d=0;d<2;d++)for(int i=0;i<3;i++)assert(visits[d][i]>20);
    assert(left>20&&right>20);
    puts("MOUSE WALK PASS: 10000 ticks, every station both directions, bounded turns, pause freezes walk/dwell");
}
