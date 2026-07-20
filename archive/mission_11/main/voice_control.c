/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 11 - Computer Is Listening: local wake word + command
 * @details Two persistent FreeRTOS tasks, pinned to opposite cores like
 *          Espressif's own combined wake+command example
 *          (esp-skainet/examples/en_speech_commands_recognition):
 *
 *          - feed_task (core 0): reads raw 16kHz/16-bit/mono frames from the
 *            shared ES7210 mic codec and hands them to the AFE (Audio
 *            Front-End) via afe_handle->feed(). Never touches WakeNet/
 *            MultiNet directly - AFE owns that.
 *          - detect_task (core 1): calls afe_handle->fetch() to get AFE's
 *            processed audio plus WakeNet's wakeup_state, and once woken,
 *            feeds that same processed audio into MultiNet's detect() to
 *            recognize a short command. Runs single-recognition mode (see
 *            the Command Word doc): the command window closes on the first
 *            recognized command or ESP_MN_STATE_TIMEOUT, not a continuous
 *            multi-command session - matches the mission's one-shot
 *            "wake -> one command -> back to listening" interaction.
 *
 *          Mic ownership (the mission's central engineering question): the
 *          ES7210 codec is one physical device, and Mission 10's
 *          audio_capture.c already owns a clean open/read/close-per-capture
 *          cycle against it for the bounded 4s recording. Rather than have
 *          two tasks independently opening/reading the same
 *          esp_codec_dev_handle_t (a guaranteed race - esp_codec_dev has no
 *          internal locking across independently-issued sessions), this
 *          module and audio_capture.c share exactly one handle
 *          (bsp_audio_codec_microphone_init() is called exactly once, by
 *          status_deck_ui.c, and passed to both), and only one of them is
 *          ever actually reading it at a time:
 *
 *          - Normally, feed_task holds the codec open in listening format
 *            (16kHz/16-bit/mono, the same format audio_capture.c uses, so no
 *            reconfiguration is needed either way) and reads continuously.
 *          - When MultiNet recognizes "capture," detect_task (a)
 *            flips s_mic_owner to MIC_OWNER_CAPTURE, which makes feed_task
 *            stop calling esp_codec_dev_read on its next loop iteration,
 *            (b) closes its own listening session, (c) calls
 *            audio_capture_start() - which opens/reads/closes the same
 *            handle for its own 4s window exactly as Mission 10 already
 *            does, untouched - (d) polls audio_capture_get_status() until
 *            that capture (and its upload) returns to AUDIO_CAP_IDLE, then
 *            (e) reopens the codec in listening format and flips
 *            s_mic_owner back to MIC_OWNER_LISTENING.
 *
 *          detect_task blocks for that entire handoff (a few seconds); this
 *          is deliberate; there is nothing useful for it to recognize while
 *          the mic belongs to the bounded recorder.
 */

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_capture.h"
#include "voice_control.h"

static const char *TAG = "voice_control";

/* Registered via the API (esp_mn_commands_add), not the menuconfig
 * CN/EN_SPEECH_COMMAND_ID* options - keeps the actual command phrase
 * visible in source. MultiNet7 (CONFIG_SR_MN_EN_MULTINET7_QUANT, see
 * sdkconfig.bsp.esp-box-3) takes grapheme English directly; command IDs
 * must start from 1. */
#define COMMAND_ID_START_RECORDING 1
/* Was "start recording", then "record" - real testing (see the Mission 11
 * debrief) proved both unreliable, but for two different reasons found in
 * sequence:
 *   - "start recording": MultiNet7's runtime grapheme-to-phoneme conversion
 *     (no precomputed phoneme column supplied) produced a phoneme sequence
 *     too inaccurate for a two-word phrase - printed as "STnRT RcKeRDgl" at
 *     boot, never once reached ESP_MN_STATE_DETECTED.
 *   - "record": a diagnostic build that logged MultiNet's live raw-decoded
 *     phonemes during the command window (multinet->get_results() called
 *     every ~500ms while ESP_MN_STATE_DETECTING) showed real speech
 *     consistently decoding as "RgKeR"/"RgKeRD" - close to, but not an
 *     exact structural match for, the registered "RfKkD". MultiNet7's FST
 *     beam search (ESP_MN_BEAM_SEARCH_WITH_FST) requires the decoded path
 *     to align with the registered grammar, not just be acoustically
 *     close, so no confidence threshold could ever fix this - it's a
 *     structural mismatch, not a borderline one.
 * "capture" was one of five unrelated words registered as a control group
 * during that same diagnostic build, specifically to tell "record is a bad
 * phrase" apart from "the recognition pipeline itself is broken" - three of
 * them ("capture", "go", "listen", "okay" - "yes" never showed up as
 * recognized) produced real ESP_MN_STATE_DETECTED hits in that session.
 * Rather than pick just one, all four hardware-verified words are
 * registered under the same COMMAND_ID_START_RECORDING - MultiNet supports
 * multiple phrases per command ID by design (see the Command Word doc's
 * menuconfig note on assigning synonyms the same ID) - so any of them
 * triggers the identical capture action. Unlike "start recording"/"record"
 * above, none of these were picked by guessing: all four were proven live
 * before being trusted. */
#define COMMAND_TEXT_RECORD "capture" /* kept as the canonical/display name - see COMMAND_PHRASES for the full accepted set */
static const char *const COMMAND_PHRASES[] = {
    "capture", "go", "listen", "okay",
};
#define COMMAND_PHRASE_COUNT (sizeof(COMMAND_PHRASES) / sizeof(COMMAND_PHRASES[0]))

/* Per the Command Word doc's create() signature - matches Espressif's own
 * en_speech_commands_recognition example. This is the internal duration
 * MultiNet allows for a command before ESP_MN_STATE_TIMEOUT; if real testing
 * shows this feels too short/long for a spoken 2-3 word command, this is the
 * one number to change. */
#define COMMAND_WINDOW_MS 6000

/* Bounded wait for a voice-triggered capture+upload to return to IDLE
 * before giving up on the handoff - generous margin over the ~4s capture +
 * a sub-second healthy upload (see audio_capture.c), covering a slow/failed
 * upload's retry-and-recover path too. Not expected to be hit in practice;
 * exists so a genuinely stuck capture can't wedge voice control silent
 * forever instead of surfacing VOICE_STATE_DEGRADED. */
#define CAPTURE_HANDOFF_MAX_WAIT_MS 15000
#define CAPTURE_HANDOFF_POLL_MS 100

typedef enum {
    MIC_OWNER_LISTENING = 0,
    MIC_OWNER_CAPTURE,
} mic_owner_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static voice_status_t s_status = {
    .state = VOICE_STATE_DEGRADED,
    .fail_reason = "NOT STARTED",
};

static voice_event_cb_t s_cb = NULL;
static void *s_cb_ctx = NULL;

static esp_codec_dev_handle_t s_mic_dev = NULL;
static volatile mic_owner_t s_mic_owner = MIC_OWNER_LISTENING;

static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static srmodel_list_t *s_models = NULL; /* scanned once in voice_control_init, reused by detect_task */

static void notify(voice_state_t new_state, const char *message)
{
    if (s_cb) {
        s_cb(new_state, message, s_cb_ctx);
    }
}

static void set_state(voice_state_t state)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = state;
    portEXIT_CRITICAL(&s_mux);
}

static void set_degraded(const char *reason)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = VOICE_STATE_DEGRADED;
    strncpy(s_status.fail_reason, reason, sizeof(s_status.fail_reason) - 1);
    s_status.fail_reason[sizeof(s_status.fail_reason) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);

    char msg[48];
    snprintf(msg, sizeof(msg), "VOICE DEGRADED %s", reason);
    ESP_LOGE(TAG, "%s", msg);
    notify(VOICE_STATE_DEGRADED, msg);
}

/* Listening-format open/close for the shared mic handle - identical format
 * to audio_capture.c's own capture format (16kHz/16-bit/mono), so handing
 * the codec back and forth between the two never requires a format change,
 * only an open/close turn. Gain is set explicitly to the same 30dB
 * audio_capture.c's perform_capture() uses - esp_codec_dev_open() alone
 * does not guarantee a usable input gain, and WakeNet/MultiNet need real
 * signal level to detect anything, not just a technically-open stream. */
static bool voice_mic_open(void)
{
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = 16000,
        .mclk_multiple = 0,
    };
    if (esp_codec_dev_open(s_mic_dev, &fs) != ESP_CODEC_DEV_OK) {
        return false;
    }
    esp_codec_dev_set_in_gain(s_mic_dev, 30.0f);
    return true;
}

static void voice_mic_close(void)
{
    esp_codec_dev_close(s_mic_dev);
}

/* Runs on detect_task once "capture" is recognized. Blocking here
 * is deliberate - see the file header's mic-ownership section. */
static void trigger_voice_capture(void)
{
    notify(VOICE_STATE_CAPTURING, "VOICE CAPTURE START");
    s_mic_owner = MIC_OWNER_CAPTURE;
    voice_mic_close();

    if (!audio_capture_start()) {
        /* Only realistic cause: audio_capture's own busy-guard (a capture
         * already in flight, e.g. from a manual REC press racing the
         * command) or its init failed independently of us. Either way,
         * nothing to poll for - hand the mic straight back. */
        notify(VOICE_STATE_LISTENING, "VOICE CAPTURE START FAILED");
        s_mic_owner = MIC_OWNER_LISTENING;
        if (!voice_mic_open()) {
            set_degraded("MIC REOPEN FAILED");
        }
        return;
    }

    audio_cap_status_t st;
    int waited_ms = 0;
    do {
        vTaskDelay(pdMS_TO_TICKS(CAPTURE_HANDOFF_POLL_MS));
        waited_ms += CAPTURE_HANDOFF_POLL_MS;
        audio_capture_get_status(&st);
    } while (st.state != AUDIO_CAP_IDLE && waited_ms < CAPTURE_HANDOFF_MAX_WAIT_MS);

    s_mic_owner = MIC_OWNER_LISTENING;
    if (!voice_mic_open()) {
        set_degraded("MIC REOPEN FAILED");
        return;
    }
    notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
}

static void feed_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int chunksize = s_afe_handle->get_feed_chunksize(afe_data);
    int nch = s_afe_handle->get_feed_channel_num(afe_data);
    int16_t *buf = malloc((size_t)chunksize * (size_t)nch * sizeof(int16_t));
    if (!buf) {
        set_degraded("FEED BUFFER ALLOC FAILED");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        if (s_mic_owner != MIC_OWNER_LISTENING) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        int err = esp_codec_dev_read(s_mic_dev, buf, chunksize * nch * (int)sizeof(int16_t));
        if (err != ESP_CODEC_DEV_OK) {
            /* Expected transiently right at a mic-ownership handoff edge
             * (the read landed just as the codec closed) - not logged per
             * occurrence, matches audio_capture.c's own read-error handling
             * philosophy of not flooding the terminal per frame. */
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (s_mic_owner == MIC_OWNER_LISTENING) {
            s_afe_handle->feed(afe_data, buf);
        }
    }
}

static void detect_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;

    char *mn_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (!mn_name) {
        set_degraded("NO MULTINET MODEL");
        vTaskDelete(NULL);
        return;
    }

    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, COMMAND_WINDOW_MS);
    if (!model_data) {
        set_degraded("MULTINET CREATE FAILED");
        vTaskDelete(NULL);
        return;
    }

    int afe_chunksize = s_afe_handle->get_fetch_chunksize(afe_data);
    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    if (mu_chunksize != afe_chunksize) {
        ESP_LOGE(TAG, "multinet chunksize %d != afe fetch chunksize %d", mu_chunksize, afe_chunksize);
        set_degraded("MODEL CHUNKSIZE MISMATCH");
        multinet->destroy(model_data);
        vTaskDelete(NULL);
        return;
    }

    /* esp_mn_commands_alloc() initializes the speech-commands linked list
     * against this specific multinet/model_data pair - without it,
     * clear/add/update below silently operate on an uninitialized list
     * (ESP_ERR_INVALID_STATE, never checked before this fix) and no command
     * is ever actually registered, even though esp_mn_commands_update()
     * still returns success. A real bug found during Mission 11 bring-up,
     * fixed here - but not, on its own, the reason recognition failed end
     * to end; see COMMAND_TEXT_RECORD's comment above for the rest of that
     * story (the G2P/phoneme-quality issue). */
    esp_err_t alloc_err = esp_mn_commands_alloc(multinet, model_data);
    if (alloc_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mn_commands_alloc failed: %s", esp_err_to_name(alloc_err));
        set_degraded("COMMANDS ALLOC FAILED");
        multinet->destroy(model_data);
        vTaskDelete(NULL);
        return;
    }
    for (size_t i = 0; i < COMMAND_PHRASE_COUNT; i++) {
        esp_err_t add_err = esp_mn_commands_add(COMMAND_ID_START_RECORDING, COMMAND_PHRASES[i]);
        if (add_err != ESP_OK) {
            ESP_LOGE(TAG, "esp_mn_commands_add(\"%s\") failed: %s", COMMAND_PHRASES[i], esp_err_to_name(add_err));
            set_degraded("COMMAND ADD FAILED");
            multinet->destroy(model_data);
            vTaskDelete(NULL);
            return;
        }
    }
    esp_mn_error_t *mn_err = esp_mn_commands_update();
    if (mn_err && mn_err->num > 0) {
        ESP_LOGE(TAG, "%d speech command(s) failed to parse", mn_err->num);
        set_degraded("COMMAND PARSE FAILED");
        multinet->destroy(model_data);
        vTaskDelete(NULL);
        return;
    }
    multinet->print_active_speech_commands(model_data);
    /* 0.1, not the model's stricter built-in default - matches the value
     * esp-sr's own test suite uses (test_apps/esp-sr/main/test_multinet.cpp)
     * at this exact point in the sequence. Threshold turned out not to be
     * the reason "record" failed (see COMMAND_TEXT_RECORD's comment - that
     * was a structural FST/grammar mismatch, not a low-confidence one), but
     * 0.1 is the exact value "capture" was confirmed to reach real
     * ESP_MN_STATE_DETECTED hits at during on-device testing, so it stays
     * rather than guessing at an untested value. If "capture" triggers too
     * easily on unrelated speech during quiet-room validation, raise it -
     * this is a real, implemented API (confirmed called in esp-sr's own
     * tests), unlike open_log below. */
    multinet->set_det_threshold(model_data, 0.1);
    ESP_LOGI(TAG, "voice pipeline ready: multinet=%s, %d command phrase(s) -> id=%d, window=%dms",
             mn_name, (int)COMMAND_PHRASE_COUNT, COMMAND_ID_START_RECORDING, COMMAND_WINDOW_MS);
    /* multinet->open_log(model_data) was tried here as a diagnostic - it is
     * NULL in this model's esp_mn_iface_t (mn7_en does not implement it),
     * and calling through a NULL function pointer crashed the device on
     * every boot (Guru Meditation Error, InstrFetchProhibited, PC=0x0)
     * immediately after this log line. Do not call it without confirming
     * multinet->open_log is non-NULL first. */

    set_state(VOICE_STATE_LISTENING);
    notify(VOICE_STATE_LISTENING, "VOICE LISTENING");

    bool in_command_window = false;

    for (;;) {
        afe_fetch_result_t *res = s_afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!in_command_window && res->wakeup_state == WAKENET_DETECTED) {
            in_command_window = true;
            multinet->clean(model_data);
            portENTER_CRITICAL(&s_mux);
            s_status.wake_count++;
            portEXIT_CRITICAL(&s_mux);
            notify(VOICE_STATE_WAKE_DETECTED, "WAKE DETECTED");
            notify(VOICE_STATE_COMMAND_WINDOW, "COMMAND WINDOW");
            continue;
        }

        if (!in_command_window) {
            continue;
        }

        esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

        if (mn_state == ESP_MN_STATE_DETECTING) {
            continue;
        }

        if (mn_state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *mn_result = multinet->get_results(model_data);
            int command_id = mn_result->num > 0 ? mn_result->command_id[0] : -1;

            if (command_id == COMMAND_ID_START_RECORDING) {
                portENTER_CRITICAL(&s_mux);
                s_status.command_count++;
                strncpy(s_status.last_command, COMMAND_TEXT_RECORD, sizeof(s_status.last_command) - 1);
                s_status.last_command[sizeof(s_status.last_command) - 1] = '\0';
                portEXIT_CRITICAL(&s_mux);
                notify(VOICE_STATE_COMMAND_RECOGNIZED, "COMMAND CAPTURE");
                in_command_window = false;
                trigger_voice_capture();
            } else {
                ESP_LOGW(TAG, "recognized command_id=%d not mapped to an action", command_id);
                notify(VOICE_STATE_TIMEOUT, "COMMAND UNRECOGNIZED");
                in_command_window = false;
                set_state(VOICE_STATE_LISTENING);
                notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
            }
            continue;
        }

        if (mn_state == ESP_MN_STATE_TIMEOUT) {
            in_command_window = false;
            notify(VOICE_STATE_TIMEOUT, "COMMAND TIMEOUT");
            set_state(VOICE_STATE_LISTENING);
            notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
            continue;
        }
    }
}

void voice_control_init(esp_codec_dev_handle_t mic_dev, voice_event_cb_t cb, void *user_ctx)
{
    s_cb = cb;
    s_cb_ctx = user_ctx;

    if (!mic_dev) {
        set_degraded("NO MIC HANDLE");
        return;
    }
    s_mic_dev = mic_dev;

    s_models = esp_srmodel_init("model");
    if (!s_models) {
        set_degraded("NO MODELS IN FLASH");
        return;
    }

    /* Single mono mic channel, no playback-echo reference - matches
     * audio_capture.c's own AUDIO_CHANNELS=1. WakeNet model itself is not
     * selected in code: afe_config_init picks up whichever WN9 model(s) are
     * both Kconfig-enabled and present in the flashed "model" partition (see
     * CONFIG_SR_WN_WN9_COMPUTER_TTS in sdkconfig.bsp.esp-box-3). */
    afe_config_t *afe_config = afe_config_init("M", s_models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_config) {
        set_degraded("AFE CONFIG FAILED");
        return;
    }
    if (!afe_config->wakenet_model_name) {
        ESP_LOGE(TAG, "no WakeNet model found in flash models - check CONFIG_SR_WN_WN9_COMPUTER_TTS and the model partition");
        afe_config_free(afe_config);
        set_degraded("NO WAKENET MODEL");
        return;
    }
    ESP_LOGI(TAG, "wakenet model: %s", afe_config->wakenet_model_name);

    s_afe_handle = esp_afe_handle_from_config(afe_config);
    s_afe_data = s_afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);
    if (!s_afe_data) {
        set_degraded("AFE CREATE FAILED");
        return;
    }

    if (!voice_mic_open()) {
        set_degraded("MIC OPEN FAILED");
        return;
    }

    portENTER_CRITICAL(&s_mux);
    s_status.state = VOICE_STATE_LISTENING;
    s_status.fail_reason[0] = '\0';
    portEXIT_CRITICAL(&s_mux);

    xTaskCreatePinnedToCore(detect_task, "voice_detect", 8192, s_afe_data, 5, NULL, 1);
    xTaskCreatePinnedToCore(feed_task, "voice_feed", 4096, s_afe_data, 5, NULL, 0);
}

void voice_control_get_status(voice_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    portEXIT_CRITICAL(&s_mux);
}
