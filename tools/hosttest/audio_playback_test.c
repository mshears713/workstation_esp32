#include "audio_playback.h"
#include <assert.h>
#include <stdio.h>
static int opens,closes,writes,produced,last_volume,mode,delays;
static int64_t clock_us;
void test_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }
int64_t esp_timer_get_time(void) { return clock_us+=100; }
void vTaskDelay(unsigned ticks) { assert(ticks==1);delays++; }
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t d,int v) { assert(d);last_volume=v;return mode==1?-1:0; }
int esp_codec_dev_open(esp_codec_dev_handle_t d,esp_codec_dev_sample_info_t *f)
{
    assert(d && f->sample_rate==24000 && f->channel==1 && f->bits_per_sample==16);
    opens++;return mode==2?-1:0;
}
int esp_codec_dev_write(esp_codec_dev_handle_t d,void *buf,int n)
{
    assert(d && buf && n==512);writes++;
    assert(((int16_t *)buf)[0]==123);
    return mode==3?-1:0;
}
int esp_codec_dev_close(esp_codec_dev_handle_t d) { assert(d);closes++;return 0; }
static size_t produce(int16_t *out,size_t cap,void *ctx)
{
    (void)ctx;assert(cap==256);
    if(produced++==32)return 0;
    for(size_t i=0;i<cap;i++)out[i]=123;
    if(mode==4)return 257;
    if(mode==5)audio_playback_stop();
    return cap;
}
static void reset(int m){mode=m;opens=closes=writes=produced=delays=0;}
int main(void)
{
    audio_playback_init(NULL);assert(!audio_playback_stream(produce,NULL,100));
    audio_playback_init((void *)1);audio_playback_set_volume(70);
    assert(!audio_playback_stream(NULL,NULL,100));
    reset(1);assert(!audio_playback_stream(produce,NULL,100));assert(opens==0);
    reset(2);assert(!audio_playback_stream(produce,NULL,100));assert(opens==1 && closes==0 && last_volume==70);
    reset(3);assert(!audio_playback_stream(produce,NULL,100));assert(writes==1 && closes==1 && last_volume==70);
    reset(4);assert(!audio_playback_stream(produce,NULL,100));assert(writes==0 && closes==1);
    reset(5);assert(audio_playback_stream(produce,NULL,100));assert(writes==1 && closes==1);
    reset(0);assert(audio_playback_stream(produce,NULL,101));
    assert(writes==32 && opens==1 && closes==1 && delays==2 && last_volume==70);
    for(int i=0;i<200;i++){reset(0);assert(audio_playback_stream(produce,NULL,100));assert(closes==1);}
    puts("PASS player: missing codec, volume/open/write failures, invalid producer, stop, bounded yield, volume restoration, 200 cycles");
}
