#include "exhibit_audio.h"
#include "audio_test_session.h"
#include "audio_playback.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "exhibit_audio";

/* Internal stack, same 8 KB voice_detect gave audio_test_run(): SD/FATFS, I2S
 * writes and Lincoln's one-time HTTPS media fetch (TLS handshake) run here.
 * Still a net saving - voice_detect, voice_feed and the capture/upload tasks
 * are gone. The device report's stack_free_min shows the real margin. */
#define AUDIO_TASK_STACK 8192

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static exhibit_audio_status_t s_st;
static bool s_scene_pending, s_clip_pending, s_scene_running;
static char s_clip_req[16];
/* Bumped by every stop/replace: a clip still mounting the SD card when it is
 * cancelled must not start afterwards (audio_playback_stream() clears its own
 * stop flag on entry, so a stop before that point would be lost). */
static uint32_t s_gen;
static TaskHandle_t s_task;

static void set_message(const char *m)
{
    portENTER_CRITICAL(&s_mux);
    strlcpy(s_st.message, m, sizeof(s_st.message));
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "%s", m);
}

/* Clip producer: plain file reads in 256-frame chunks (no 1.4 MB buffer).
 * The generation is checked on every chunk: a stop that lands between the
 * start check and audio_playback_stream() (which clears its own stop flag on
 * entry) still ends the clip within one 10.7 ms chunk. */
typedef struct { FILE *f; uint32_t gen; bool cancelled; } clip_ctx_t;
static size_t clip_produce(int16_t *out, size_t count, void *ctx)
{
    clip_ctx_t *c = ctx;
    portENTER_CRITICAL(&s_mux);
    bool stale = c->gen != s_gen;
    portEXIT_CRITICAL(&s_mux);
    if (stale) { c->cancelled = true; return 0; }
    return fread(out, sizeof(int16_t), count, c->f);
}

static bool sd_mount(void)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false, .max_files = 2, .allocation_unit_size = 16 * 1024,
    };
    bsp_sdcard_cfg_t cfg = { .mount = &mount };
    esp_err_t err = bsp_sdcard_sdmmc_mount(&cfg);
    if (err != ESP_OK) {
        gpio_set_level(BSP_SD_POWER, 1);   /* same recovery as audio_test_run() */
        ESP_LOGW(TAG, "SD mount failed (never format): %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static void run_clip(const char *name, uint32_t gen)
{
    char msg[48];
    bool ok = false;
    if (!sd_mount()) {
        snprintf(msg, sizeof(msg), "CLIP %s: NO SD CARD", name);
        goto out;
    }
    char path[96];
    snprintf(path, sizeof(path), "%s%s/%s.pcm", BSP_SD_MOUNT_POINT, EXHIBIT_AUDIO_DIR, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(msg, sizeof(msg), "CLIP %s: FILE MISSING", name);
    } else {
        char *io = NULL;
        long bytes = (fseek(f, 0, SEEK_END) == 0) ? ftell(f) : -1;
        if (bytes <= 0 || (bytes & 1) || (unsigned long)bytes > EXHIBIT_CLIP_MAX_BYTES || fseek(f, 0, SEEK_SET) != 0) {
            snprintf(msg, sizeof(msg), "CLIP %s: BAD FILE %ld B", name, bytes);
        } else {
            /* stdio cache in PSRAM for this clip only (internal RAM is the ceiling) */
            io = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (io) setvbuf(f, io, _IOFBF, 1024);
            bool cancelled;
            portENTER_CRITICAL(&s_mux);
            cancelled = gen != s_gen;
            if (!cancelled) { s_st.clip_playing = true; s_st.clip_started_seq++; }
            portEXIT_CRITICAL(&s_mux);
            if (cancelled) {
                snprintf(msg, sizeof(msg), "CLIP %s CANCELLED", name);
            } else {
                clip_ctx_t c = { .f = f, .gen = gen };
                bool streamed = audio_playback_stream(clip_produce, &c, audio_playback_get_volume());
                portENTER_CRITICAL(&s_mux);
                bool stopped = c.cancelled || gen != s_gen;   /* stop/replace, not the end of the file */
                portEXIT_CRITICAL(&s_mux);
                ok = streamed && !stopped;
                snprintf(msg, sizeof(msg), !streamed ? "CLIP %s: SPEAKER FAILED" : stopped ? "CLIP %s STOPPED" : "CLIP %s PLAYED", name);
            }
        }
        fclose(f);          /* before free: the stream may still own io */
        free(io);
    }
    if (bsp_sdcard_unmount() != ESP_OK) ESP_LOGW(TAG, "SD unmount failed");
out:
    portENTER_CRITICAL(&s_mux);
    s_st.clip_playing = false;
    if (ok) s_st.clip_done_seq++; else s_st.clip_failed_seq++;
    portEXIT_CRITICAL(&s_mux);
    set_message(msg);
}

static void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        bool scene, clip;
        char name[16];
        uint32_t gen;
        portENTER_CRITICAL(&s_mux);
        scene = s_scene_pending; clip = s_clip_pending && !scene;
        s_scene_pending = false;
        if (clip) s_clip_pending = false;
        gen = s_gen;
        strlcpy(name, s_clip_req, sizeof(name));
        portEXIT_CRITICAL(&s_mux);
        if (scene) {
            audio_test_run();   /* blocks until Newton/Lincoln exits */
            portENTER_CRITICAL(&s_mux); s_scene_running = false; portEXIT_CRITICAL(&s_mux);
            set_message("SCENE AUDIO CLOSED");
        } else if (clip) {
            run_clip(name, gen);
        }
        portENTER_CRITICAL(&s_mux);
        s_st.busy = s_scene_pending || s_clip_pending;
        portEXIT_CRITICAL(&s_mux);
    }
}

void exhibit_audio_init(void)
{
    if (s_task) return;
    if (xTaskCreatePinnedToCore(audio_task, "exhibit_audio", AUDIO_TASK_STACK, NULL, 5, &s_task, 1) != pdPASS) {
        s_task = NULL;
        set_message("AUDIO TASK NO MEMORY");
    }
}

/* A scene outranks a clip: opening Newton while the welcome is still talking
 * stops the welcome and the scene starts straight after it. */
bool exhibit_audio_request_scene(audio_profile_t profile)
{
    bool ok = false, stop = false;
    portENTER_CRITICAL(&s_mux);
    if (s_task && !s_scene_pending && !s_scene_running) {
        audio_test_prepare_profile(profile);
        stop = s_st.clip_playing;
        s_clip_pending = false;
        s_gen++;
        s_scene_pending = true; s_scene_running = true; s_st.busy = true; ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (stop) audio_playback_stop();
    if (ok) xTaskNotifyGive(s_task);
    return ok;
}

static bool safe_name(const char *n)
{
    size_t len = n ? strlen(n) : 0;
    if (len == 0 || len >= sizeof(s_clip_req)) return false;
    for (size_t i = 0; i < len; i++)
        if (!(islower((unsigned char)n[i]) || isdigit((unsigned char)n[i]) || n[i] == '_')) return false;
    return true;
}

/* A newer clip replaces an older one (Egypt's talk gives way to Greece's);
 * only a Newton/Lincoln session refuses it. */
bool exhibit_audio_play_clip(const char *name)
{
    if (!safe_name(name)) return false;
    bool ok = false, stop = false;
    portENTER_CRITICAL(&s_mux);
    if (s_task && !s_scene_pending && !s_scene_running) {
        strlcpy(s_clip_req, name, sizeof(s_clip_req));
        strlcpy(s_st.clip, name, sizeof(s_st.clip));
        stop = s_st.clip_playing;
        s_gen++;
        s_clip_pending = true; s_st.busy = true; ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (stop) audio_playback_stop();
    if (ok) xTaskNotifyGive(s_task);
    return ok;
}

/* Stops the playing clip and drops a queued one (leaving the tour must not
 * start a talk a moment later). */
void exhibit_audio_stop_clip(void)
{
    bool playing;
    portENTER_CRITICAL(&s_mux);
    s_clip_pending = false;
    s_gen++;
    playing = s_st.clip_playing;
    portEXIT_CRITICAL(&s_mux);
    if (playing) audio_playback_stop();
}

void exhibit_audio_get_status(exhibit_audio_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_st;
    portEXIT_CRITICAL(&s_mux);
}
