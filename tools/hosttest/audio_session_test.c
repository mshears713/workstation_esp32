#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_test_session.h"
#include "audio_playback.h"
#include "lincoln_media.h"
#include "bsp/esp-bsp.h"
static int mounts,unmounts,plays,mode;
static bool mount_failure,alloc_failure;
bool lincoln_media_ensure(media_wanted_t wanted,media_status_t status){(void)status;return wanted();}
void test_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }
size_t heap_caps_get_free_size(int c) { (void)c;return 100000; }
size_t heap_caps_get_largest_free_block(int c) { return heap_caps_get_free_size(c); }
size_t heap_caps_get_minimum_free_size(int c) { return heap_caps_get_free_size(c); }
void *heap_caps_calloc(size_t n,size_t size,int caps) { assert(caps==6);return alloc_failure?NULL:calloc(n,size); }
const char *esp_err_to_name(int err) { (void)err;return "test error"; }
int gpio_set_level(int pin,int level) { assert(pin==43 && level==1);return 0; }
int bsp_sdcard_sdmmc_mount(bsp_sdcard_cfg_t *cfg)
{
    assert(cfg && cfg->mount && !cfg->mount->format_if_mount_failed);
    assert(cfg->mount->max_files==3);mounts++;return mount_failure?-1:0;
}
int bsp_sdcard_unmount(void) { unmounts++;return 0; }
bool audio_playback_stream(audio_pcm_producer_t produce,void *ctx,int volume)
{
    assert(volume==100);plays++;
    if(mode==1)return false;
    int16_t pcm[256];audio_test_status_t st;
    assert(produce(pcm,256,ctx)==256);
    audio_test_get_status(&st);assert(st.running && !st.wind && !st.voice);
    audio_test_wind();audio_test_voice();audio_test_voice();
    assert(produce(pcm,256,ctx)==256);
    audio_test_get_status(&st);assert(st.running && st.wind && st.voice && st.next_voice==2);
    assert(st.voice_started_seq==1 && st.voice_clip==1 && st.voice_failed_seq==0);
    audio_test_voice(); /* ignored during voice */
    for(int i=0;i<15;i++)assert(produce(pcm,256,ctx)==256);
    audio_test_get_status(&st);assert(!st.voice && st.wind && st.next_voice==2);
    audio_test_set_wind(true);audio_test_set_wind(true);
    assert(produce(pcm,256,ctx)==256);audio_test_get_status(&st);assert(st.wind);
    audio_test_set_wind(false);audio_test_set_wind(false);
    assert(produce(pcm,256,ctx)==256);audio_test_get_status(&st);assert(!st.wind);
    audio_test_set_wind(true);
    assert(audio_test_request_voice());assert(!audio_test_request_voice());
    assert(produce(pcm,256,ctx)==256);audio_test_get_status(&st);
    assert(st.wind && !st.voice && st.next_voice==2 && st.voice_failed_seq==1 && st.voice_started_seq==1);
    assert(strstr(st.message,"Voice file missing"));
    audio_test_cancel();assert(produce(pcm,256,ctx)==0);
    return true;
}
int main(void)
{
    audio_test_status_t st;
    audio_test_prepare();audio_test_cancel();audio_test_run();assert(mounts==0);
    mount_failure=true;audio_test_prepare();audio_test_run();
    assert(mounts==1 && unmounts==0 && plays==0);
    audio_test_get_status(&st);assert(!st.running && strstr(st.message,"SD unavailable"));
    mount_failure=false;alloc_failure=true;audio_test_prepare();audio_test_run();
    assert(mounts==2 && unmounts==1 && plays==0);
    audio_test_get_status(&st);assert(strstr(st.message,"memory"));
    alloc_failure=false;
    /* Caller provides fixture directory with music, wind, newton_1 PCM. */
    mode=1;audio_test_prepare();audio_test_run();assert(plays==1);
    audio_test_get_status(&st);assert(!st.running && strstr(st.message,"Speaker"));
    mode=0;
    for(int i=0;i<200;i++) {
        audio_test_prepare();audio_test_run();audio_test_get_status(&st);
        assert(!st.running && !st.wind && !st.voice);
    }
    assert(mounts==203 && unmounts==202 && plays==201);
    puts("PASS session: no-format mount, cancel-before-start, mount/alloc/speaker failures, duplicate coalescing, exit, 200 cycles");
}
