#include "audio_test_session.h"
#include "audio_streams.h"
#include "audio_playback.h"
#include "lincoln_media.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <stdlib.h>

/* GPIO43 powers the SENSOR SD slot. Never compile a UART0-TX console here. */
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || !CONFIG_FATFS_LFN_HEAP
#error "Audio Test requires USB Serial/JTAG console and FATFS heap long filenames"
#endif
#ifdef CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL
#error "SD formatting must remain disabled"
#endif
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static audio_test_status_t status;
static bool wanted, wind_pending, voice_pending;
static bool wind_set_pending, wind_desired;
static bool stop_pending,sfx_pending;
static uint32_t requested_id,stop_id,active_id,completed_pending;
static unsigned requested_clip;
static audio_sfx_t requested_sfx;
static const char *TAG = "audio_test";

void audio_test_prepare(void)
{ audio_test_prepare_profile(AUDIO_PROFILE_NEWTON); }
void audio_test_prepare_profile(audio_profile_t profile)
{
    portENTER_CRITICAL(&lock);
    status = (audio_test_status_t){.next_voice = 1,.profile=profile};
    snprintf(status.message, sizeof(status.message), "Opening SD card...");
    wanted = true; wind_pending = voice_pending = false;
    wind_set_pending = wind_desired = false;
    stop_pending=sfx_pending=false;requested_id=stop_id=active_id=completed_pending=0;
    portEXIT_CRITICAL(&lock);
}
bool audio_test_lincoln_voice(uint32_t id,unsigned clip)
{
    bool accepted=false;
    portENTER_CRITICAL(&lock);
    if(wanted&&status.running&&status.profile==AUDIO_PROFILE_LINCOLN&&clip<LINCOLN_SPEECH_COUNT&&id&&!status.voice&&!voice_pending&&!stop_pending){
        requested_id=id;requested_clip=clip;voice_pending=true;accepted=true;
    }
    portEXIT_CRITICAL(&lock);return accepted;
}
void audio_test_lincoln_stop(uint32_t id)
{
    portENTER_CRITICAL(&lock);
    if(wanted&&status.profile==AUDIO_PROFILE_LINCOLN){voice_pending=false;sfx_pending=false;stop_pending=true;stop_id=id;}
    portEXIT_CRITICAL(&lock);
}
bool audio_test_lincoln_sfx(audio_sfx_t effect)
{
    bool accepted=false;portENTER_CRITICAL(&lock);
    if(wanted&&status.running&&status.profile==AUDIO_PROFILE_LINCOLN&&(unsigned)effect<4&&!sfx_pending&&
       (!status.wind||effect==AUDIO_SFX_PAPER)){
        requested_sfx=effect;sfx_pending=true;accepted=true;
    }
    portEXIT_CRITICAL(&lock);return accepted;
}
void audio_test_cancel(void)
{
    portENTER_CRITICAL(&lock);
    wanted = false; wind_pending = voice_pending = false;
    wind_set_pending = false;
    stop_pending=sfx_pending=false;
    portEXIT_CRITICAL(&lock);
}
void audio_test_wind(void)
{
    portENTER_CRITICAL(&lock);
    if (wanted && status.running) wind_pending = true;
    portEXIT_CRITICAL(&lock);
}
void audio_test_voice(void)
{
    (void)audio_test_request_voice();
}
void audio_test_set_wind(bool enabled)
{
    portENTER_CRITICAL(&lock);
    if(wanted && status.running) { wind_desired=enabled;wind_set_pending=true; }
    portEXIT_CRITICAL(&lock);
}
bool audio_test_request_voice(void)
{
    bool accepted = false;
    portENTER_CRITICAL(&lock);
    if (wanted && status.running && status.profile==AUDIO_PROFILE_NEWTON && !status.voice && !voice_pending) {
        voice_pending = true; accepted = true;
    }
    portEXIT_CRITICAL(&lock);
    return accepted;
}
void audio_test_get_status(audio_test_status_t *out)
{
    portENTER_CRITICAL(&lock); *out = status; portEXIT_CRITICAL(&lock);
}
void audio_test_error(const char *message)
{
    portENTER_CRITICAL(&lock);
    snprintf(status.message, sizeof(status.message), "%s", message);
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "status: %s", message);
}
static bool is_wanted(void)
{
    portENTER_CRITICAL(&lock); bool out = wanted; portEXIT_CRITICAL(&lock);
    return out;
}
static size_t produce(int16_t *out, size_t count, void *ctx)
{
    audio_streams_t *streams = ctx;
    bool wind, voice, run, set_wind, desired,stop,sfx;
    uint32_t id,stop_request;unsigned clip;audio_sfx_t effect;
    portENTER_CRITICAL(&lock);
    run = wanted; wind = wind_pending; voice = voice_pending;
    set_wind=wind_set_pending;desired=wind_desired;wind_set_pending=false;
    stop=stop_pending;stop_request=stop_id;stop_pending=false;
    sfx=sfx_pending;effect=requested_sfx;sfx_pending=false;
    id=requested_id;clip=requested_clip;
    if(completed_pending){status.voice_completed_seq++;status.completed_id=completed_pending;completed_pending=0;}
    wind_pending = voice_pending = false;
    portEXIT_CRITICAL(&lock);
    if (!run) return 0;
    if(stop){
        audio_streams_stop_voice(streams);audio_streams_stop_sfx(streams);active_id=completed_pending=0;voice=sfx=false;
        portENTER_CRITICAL(&lock);status.voice_stopped_seq++;status.stopped_id=stop_request;portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG,"Lincoln voice stop acknowledged id=%lu",(unsigned long)stop_request);
    }
    if(sfx){
        if(effect==AUDIO_SFX_PAPER)audio_streams_stop_sfx(streams);
        if(!audio_streams_start_sfx(streams,effect))audio_test_error("Effect unavailable");
        else audio_test_error("Lincoln effect playing");
    }
    if ((set_wind && (streams->wind != NULL) != desired) || (!set_wind && wind)) {
        if (!audio_streams_toggle_wind(streams)) audio_test_error("Wind file missing / unreadable");
        ESP_LOGI(TAG, "wind %s", streams->wind ? "on" : "off");
    }
    if (voice && !streams->voice) {
        bool lincoln=streams->profile==AUDIO_PROFILE_LINCOLN;
        unsigned number = lincoln?clip+1:streams->next_voice + 1;
        bool started=lincoln?audio_streams_start_voice_clip(streams,clip):audio_streams_start_voice(streams);
        if (!started) {
            audio_test_error("Voice file missing / unreadable");
            portENTER_CRITICAL(&lock); status.voice_failed_seq++;status.failed_id=id; portEXIT_CRITICAL(&lock);
            ESP_LOGW(TAG, "voice %u open failed", number);
        } else {
            portENTER_CRITICAL(&lock);
            status.voice_clip = number; status.voice_started_seq++;
            status.started_id=id;active_id=id;
            portEXIT_CRITICAL(&lock);
            ESP_LOGI(TAG, "voice %u started", number);
            if(lincoln)audio_test_error("Lincoln reading");
        }
    }
    bool was_voice = streams->voice != NULL;
    if (!audio_streams_render(streams, out, count)) {
        audio_test_error("SD read failed - exit and retry");
        return 0;
    }
    if (was_voice && !streams->voice) {completed_pending=active_id;ESP_LOGI(TAG, "voice EOF; completion after final write");}
    portENTER_CRITICAL(&lock);
    status.running = true;
    status.wind = streams->wind != NULL; status.voice = streams->voice != NULL;
    status.next_voice = streams->next_voice + 1;
    portEXIT_CRITICAL(&lock);
    return count;
}
void audio_test_run(void)
{
    bool mounted = false;
    audio_streams_t *streams = NULL;
    if (!is_wanted()) return;
    ESP_LOGI(TAG, "before SD: internal=%u largest=%u min=%u PSRAM=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false, .max_files = 3,
        .allocation_unit_size = 16 * 1024,
    };
    bsp_sdcard_cfg_t cfg = {.mount = &mount};
    esp_err_t err = bsp_sdcard_sdmmc_mount(&cfg);
    if (err != ESP_OK) {
        gpio_set_level(BSP_SD_POWER, 1);
        ESP_LOGW(TAG, "SD mount failed (never format): %s", esp_err_to_name(err));
        audio_test_error("SD unavailable - insert card, retry");
        goto done;
    }
    mounted = true;
    ESP_LOGI(TAG, "SD mounted; reading /sdcard/dottie/audio only");
    ESP_LOGI(TAG, "heap after SD mount: internal=%u largest=%u PSRAM=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (!is_wanted()) goto done;
    streams = heap_caps_calloc(1, sizeof(*streams), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!streams) { audio_test_error("Not enough audio memory"); goto done; }
    audio_profile_t profile;portENTER_CRITICAL(&lock);profile=status.profile;portEXIT_CRITICAL(&lock);
    if(profile==AUDIO_PROFILE_LINCOLN&&!lincoln_media_ensure(is_wanted,audio_test_error))goto done;
    if(!is_wanted())goto done;
    const char *root=profile==AUDIO_PROFILE_LINCOLN?LINCOLN_MEDIA_ROOT:BSP_SD_MOUNT_POINT "/dottie/audio";
    if (!audio_streams_open_profile(streams, root,profile)) {
        audio_test_error("music_loop.pcm missing / invalid"); goto done;
    }
    audio_test_error("Music playing - speaker 100%");
    ESP_LOGI(TAG, "start: 24000Hz mono PCM, mixer=%u bytes PSRAM, volume=100",
             (unsigned)sizeof(*streams));
    if (!audio_playback_stream(produce, streams, 100))
        audio_test_error("Speaker unavailable / write failed");
done:
    if (streams) { audio_streams_close(streams); free(streams); }
    if (mounted) {
        err = bsp_sdcard_unmount();
        if (err != ESP_OK) {
            audio_test_error("SD unmount failed");
            ESP_LOGE(TAG, "SD unmount: %s", esp_err_to_name(err));
        }
    }
    portENTER_CRITICAL(&lock);
    status.running = status.wind = status.voice = false;
    wanted = wind_pending = voice_pending = false;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "session closed: all PCM files and speaker released");
    ESP_LOGI(TAG, "after SD: internal=%u largest=%u min=%u PSRAM=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
