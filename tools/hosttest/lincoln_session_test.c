#include "audio_test_session.h"
#include "audio_playback.h"
#include "lincoln_media.h"
#include "bsp/esp-bsp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned mounts,unmounts;
void test_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
size_t heap_caps_get_free_size(int c){(void)c;return 100000;}
size_t heap_caps_get_largest_free_block(int c){return heap_caps_get_free_size(c);}
size_t heap_caps_get_minimum_free_size(int c){return heap_caps_get_free_size(c);}
void *heap_caps_calloc(size_t n,size_t z,int caps){assert(caps==6);return calloc(n,z);}
const char *esp_err_to_name(int e){(void)e;return "test";}
int gpio_set_level(int pin,int value){assert(pin==43&&value==1);return 0;}
int bsp_sdcard_sdmmc_mount(bsp_sdcard_cfg_t *cfg){assert(!cfg->mount->format_if_mount_failed&&cfg->mount->max_files==3);mounts++;return 0;}
int bsp_sdcard_unmount(void){unmounts++;return 0;}
bool lincoln_media_ensure(media_wanted_t wanted,media_status_t status){(void)status;return wanted();}
bool audio_playback_stream(audio_pcm_producer_t produce,void *ctx,int volume)
{
    assert(volume==100);int16_t pcm[256];audio_test_status_t st;
    assert(produce(pcm,256,ctx)==256);audio_test_get_status(&st);
    assert(st.running&&st.profile==AUDIO_PROFILE_LINCOLN&&!audio_test_request_voice());
    assert(audio_test_lincoln_sfx(AUDIO_SFX_DOOR));assert(!audio_test_lincoln_sfx(AUDIO_SFX_PUFF));
    assert(audio_test_lincoln_voice(100,0));assert(!audio_test_lincoln_voice(101,1));
    assert(produce(pcm,256,ctx)==256);audio_test_get_status(&st);
    assert(st.started_id==100&&st.voice_started_seq==1&&st.voice);
    assert(audio_test_lincoln_sfx(AUDIO_SFX_PAPER)); /* priority replaces effect only */
    for(unsigned n=0;n<8;n++)assert(produce(pcm,256,ctx)==256);
    audio_test_get_status(&st);assert(!st.voice&&st.completed_id==100&&st.voice_completed_seq==1);
    assert(audio_test_lincoln_voice(101,1));audio_test_lincoln_stop(101);
    assert(produce(pcm,256,ctx)==256);audio_test_get_status(&st);
    assert(!st.voice&&st.stopped_id==101&&st.voice_started_seq==1);
    assert(audio_test_lincoln_voice(102,1));assert(produce(pcm,256,ctx)==256);
    audio_test_lincoln_stop(102);assert(produce(pcm,256,ctx)==256);audio_test_get_status(&st);
    assert(!st.voice&&st.stopped_id==102&&st.voice_stopped_seq==2&&st.voice_completed_seq==1);
    assert(!audio_test_lincoln_voice(103,2));
    assert(rename(LINCOLN_MEDIA_ROOT "/lincoln_2.pcm",LINCOLN_MEDIA_ROOT "/missing-test.pcm")==0);
    assert(audio_test_lincoln_voice(104,1));assert(produce(pcm,256,ctx)==256);audio_test_get_status(&st);
    assert(st.failed_id==104&&st.voice_failed_seq==1); /* missing selected clip must fail honestly */
    assert(rename(LINCOLN_MEDIA_ROOT "/missing-test.pcm",LINCOLN_MEDIA_ROOT "/lincoln_2.pcm")==0);
    audio_test_cancel();assert(produce(pcm,256,ctx)==0);return true;
}
int main(void)
{
    for(unsigned i=0;i<200;i++){
        audio_test_prepare_profile(AUDIO_PROFILE_LINCOLN);audio_test_run();audio_test_status_t st;audio_test_get_status(&st);assert(!st.running&&!st.voice&&!st.wind);
    }
    assert(mounts==200&&unmounts==200);
    puts("PASS Lincoln actual session: one owner, exact clip IDs, EOF-after-write completion, queued/start cancellation, effect priority, missing clip, 200 clean exits");
}
