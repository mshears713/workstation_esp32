/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 12 - Six-Command Recognition Test: local wake word +
 *        bounded MultiNet command window over six fixed command words.
 * @details Two persistent FreeRTOS tasks, pinned to opposite cores like
 *          Espressif's own combined wake+command example
 *          (esp-skainet/examples/en_speech_commands_recognition), unchanged
 *          from Mission 11:
 *
 *          - feed_task (core 0): reads raw 16kHz/16-bit/mono frames from the
 *            shared ES7210 mic codec and hands them to the AFE (Audio
 *            Front-End) via afe_handle->feed(). Never touches WakeNet/
 *            MultiNet directly - AFE owns that.
 *          - detect_task (core 1): calls afe_handle->fetch() to get AFE's
 *            processed audio plus WakeNet's wakeup_state, and once woken,
 *            feeds that same processed audio into MultiNet's detect() to
 *            recognize one of six short command words. Runs single-
 *            recognition mode: the command window closes on the first
 *            recognized command or ESP_MN_STATE_TIMEOUT, not a continuous
 *            multi-command session - matches the "wake -> one command word
 *            -> back to listening" interaction this test is built around.
 *
 *          Mission 12 scope: this pass is a pure recognition/visual-
 *          selection test for six command words (SEND/NOTE/RUN/TEST/GO/YES),
 *          each with its own command ID. Recognizing a command here only
 *          updates voice_status_t (name, ID, count) and fires notify() for
 *          the UI/Black Box - it deliberately does NOT call
 *          audio_capture_start() the way Mission 11's single "capture"
 *          command did. trigger_voice_capture() and the MIC_OWNER_CAPTURE
 *          mic-ownership handoff below are kept exactly as Mission 11 left
 *          them (untouched, still correct) but are simply not called from
 *          any command path this pass - see the directive's explicit
 *          non-goal "audio recording triggered by these commands." Manual
 *          REC (status_deck_ui.c's REC button, straight into audio_capture.c)
 *          is completely unaffected either way.
 *
 *          Mic ownership (unchanged from Mission 11): the ES7210 codec is
 *          one physical device, and Mission 10's audio_capture.c already
 *          owns a clean open/read/close-per-capture cycle against it for the
 *          bounded ~4s recording. Rather than have two tasks independently
 *          opening/reading the same esp_codec_dev_handle_t (a guaranteed
 *          race - esp_codec_dev has no internal locking across
 *          independently-issued sessions), this module and audio_capture.c
 *          share exactly one handle (bsp_audio_codec_microphone_init() is
 *          called exactly once, by status_deck_ui.c, and passed to both),
 *          and only one of them is ever actually reading it at a time - see
 *          trigger_voice_capture() below for the (currently unused this
 *          pass) handoff sequence.
 */

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
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
 * must start from 1.
 *
 * The exact six words required by the Mission 12 directive, each its own
 * command ID (unlike Mission 11's four synonyms sharing one ID). "go" was
 * one of the four words hardware-verified to produce real
 * ESP_MN_STATE_DETECTED hits during Mission 11 bring-up (see the archived
 * mission_11/main/voice_control.c for that debrief) - the other five
 * (send/note/run/test/yes) are new for this pass and untested until Mike's
 * hardware validation, which is the entire point of this build. */
typedef struct {
    voice_command_id_t id;
    const char *phrase; /* lowercase grapheme text registered with MultiNet */
    const char *label;  /* uppercase name for logs/Black Box messages */
} voice_command_def_t;

static const voice_command_def_t COMMAND_DEFS[] = {
    { VOICE_CMD_SEND, "send", "SEND" },
    { VOICE_CMD_NOTE, "note", "NOTE" },
    { VOICE_CMD_RUN,  "run",  "RUN"  },
    { VOICE_CMD_TEST, "test", "TEST" },
    { VOICE_CMD_GO,   "go",   "GO"   },
    { VOICE_CMD_YES,  "yes",  "YES"  },
};
#define COMMAND_DEF_COUNT (sizeof(COMMAND_DEFS) / sizeof(COMMAND_DEFS[0]))

/* Per the Command Word doc's create() signature - matches Espressif's own
 * en_speech_commands_recognition example. This is the internal duration
 * MultiNet allows for a command before ESP_MN_STATE_TIMEOUT. Mission 12
 * directive: ~10s (was Mission 11's 6000ms), to give Mike more comfortable
 * room to hear the wake chime, glance at the screen, and speak one of six
 * words rather than one fixed phrase. */
#define COMMAND_WINDOW_MS 10000

/* How long a recognized command's button stays visibly highlighted before
 * the console returns to LISTENING - the directive asks for "approximately
 * one to two seconds." detect_task blocks here deliberately (same
 * "blocking during a short, bounded UI beat is fine" precedent
 * trigger_voice_capture already sets below) - WakeNet/MultiNet simply are
 * not being fed fresh detection decisions for this brief window, which is
 * the intended behavior, not a bug. */
#define COMMAND_HIGHLIGHT_MS 1500

/* Same idea for the plain TIMEOUT/UNRECOGNIZED indication - shorter than
 * the highlight hold since there's no specific result to read, just enough
 * for "TIMEOUT"/"UNRECOGNIZED" to be visible before the screen clears. */
#define COMMAND_BRIEF_MS 1000

/* Bounded wait for a voice-triggered capture+upload to return to IDLE
 * before giving up on the handoff - generous margin over the ~4s capture +
 * a sub-second healthy upload (see audio_capture.c), covering a slow/failed
 * upload's retry-and-recover path too. Not expected to be hit in practice;
 * exists so a genuinely stuck capture can't wedge voice control silent
 * forever instead of surfacing VOICE_STATE_DEGRADED. Unused this pass (see
 * trigger_voice_capture's header comment) but kept with the function it
 * belongs to. */
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

/* Sets state + the command-window countdown deadline together, under one
 * critical section, so the UI never reads a state==COMMAND_WINDOW with a
 * stale/zero deadline. */
static void enter_command_window(void)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = VOICE_STATE_COMMAND_WINDOW;
    s_status.command_window_deadline_us = esp_timer_get_time() + (int64_t)COMMAND_WINDOW_MS * 1000;
    portEXIT_CRITICAL(&s_mux);
}

/* Sets state + the recognized command's name/ID/count together, under one
 * critical section, for the same reason as enter_command_window above. */
static void set_command_recognized(const voice_command_def_t *def)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = VOICE_STATE_COMMAND_RECOGNIZED;
    s_status.command_count++;
    s_status.last_command_id = def->id;
    strncpy(s_status.last_command, def->label, sizeof(s_status.last_command) - 1);
    s_status.last_command[sizeof(s_status.last_command) - 1] = '\0';
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

/* command_id -> COMMAND_DEFS entry, or NULL if it isn't one of the six
 * registered this pass (defensive - MultiNet's FST-constrained decode
 * should only ever return a command_id we registered, but a phrase
 * recognized under an ID we don't recognize is treated as UNRECOGNIZED
 * rather than trusted blindly). */
static const voice_command_def_t *command_def_for_id(int command_id)
{
    for (size_t i = 0; i < COMMAND_DEF_COUNT; i++) {
        if ((int)COMMAND_DEFS[i].id == command_id) {
            return &COMMAND_DEFS[i];
        }
    }
    return NULL;
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

/* Mission 11's capture trigger, kept intact but unused this pass - see the
 * file header and voice_control.h's Mission 12 comment. Nothing in the
 * Mission 12 detect_task loop below calls this; it would only run if a
 * future pass deliberately re-wires one of the six commands (or a new one)
 * back to the Mission 10 recording path. Blocking here on detect_task is
 * deliberate - see the file header's mic-ownership section. */
static void __attribute__((unused)) trigger_voice_capture(void)
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

/* Registers all six Mission 12 command words. Continues past a single
 * failed phrase rather than aborting the whole pipeline on it (the
 * directive: "report any command that fails registration rather than
 * silently omitting it") - only if literally none of the six register does
 * this fall back to VOICE_STATE_DEGRADED, since a partially-populated
 * command set is still a useful recognition test and better than none.
 * esp_mn_commands_add() already calls the model's check_speech_command()
 * internally before accepting a phrase (see esp-sr's
 * esp_mn_speech_commands.c) and fails with ESP_ERR_INVALID_STATE if the
 * model can't tokenize it - that's the "verify each command can be
 * accepted/tokenized by the active model" check the directive asks for;
 * there is no separate public check_speech_command call needed on top of
 * it. Returns the number of commands that registered successfully. */
static int register_commands(void)
{
    int registered = 0;
    for (size_t i = 0; i < COMMAND_DEF_COUNT; i++) {
        esp_err_t add_err = esp_mn_commands_add((int)COMMAND_DEFS[i].id, COMMAND_DEFS[i].phrase);
        if (add_err != ESP_OK) {
            ESP_LOGE(TAG, "esp_mn_commands_add(id=%d, \"%s\") FAILED: %s - this command will not be recognized this boot",
                     (int)COMMAND_DEFS[i].id, COMMAND_DEFS[i].phrase, esp_err_to_name(add_err));
            continue;
        }
        registered++;
    }

    esp_mn_error_t *mn_err = esp_mn_commands_update();
    if (mn_err && mn_err->num > 0) {
        for (int i = 0; i < mn_err->num; i++) {
            esp_mn_phrase_t *bad = mn_err->phrases[i];
            ESP_LOGE(TAG, "command update REJECTED phrase \"%s\" (id=%d) - will not be recognized this boot",
                     bad ? bad->string : "?", bad ? bad->command_id : -1);
        }
        registered -= mn_err->num;
    }

    ESP_LOGI(TAG, "six-command registration: %d/%d succeeded", registered, (int)COMMAND_DEF_COUNT);
    for (size_t i = 0; i < COMMAND_DEF_COUNT; i++) {
        ESP_LOGI(TAG, "  id=%d phrase=\"%s\" label=%s", (int)COMMAND_DEFS[i].id, COMMAND_DEFS[i].phrase, COMMAND_DEFS[i].label);
    }
    return registered;
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
     * fixed there and still required here. */
    esp_err_t alloc_err = esp_mn_commands_alloc(multinet, model_data);
    if (alloc_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mn_commands_alloc failed: %s", esp_err_to_name(alloc_err));
        set_degraded("COMMANDS ALLOC FAILED");
        multinet->destroy(model_data);
        vTaskDelete(NULL);
        return;
    }

    int registered_count = register_commands();
    if (registered_count == 0) {
        set_degraded("ALL COMMANDS FAILED REGISTRATION");
        multinet->destroy(model_data);
        vTaskDelete(NULL);
        return;
    }
    multinet->print_active_speech_commands(model_data);
    /* 0.1, not the model's stricter built-in default - matches the value
     * esp-sr's own test suite uses (test_apps/esp-sr/main/test_multinet.cpp)
     * at this exact point in the sequence, and the exact value "go" (one of
     * this pass's six words) was confirmed to reach real
     * ESP_MN_STATE_DETECTED hits at during Mission 11's on-device testing.
     * Carried forward unchanged for Mission 12's baseline per the
     * directive ("do not change the detection threshold before collecting
     * baseline results") - the other five words are untested at this
     * threshold until Mike's hardware validation below; if any of them
     * trigger too easily (or not easily enough) on real speech, that's
     * exactly the evidence this pass exists to collect, and threshold is
     * the one number to revisit afterward. */
    multinet->set_det_threshold(model_data, 0.1);
    ESP_LOGI(TAG, "voice pipeline ready: multinet=%s, %d/%d command(s) registered, window=%dms",
             mn_name, registered_count, (int)COMMAND_DEF_COUNT, COMMAND_WINDOW_MS);
    /* multinet->open_log(model_data) was tried here as a diagnostic during
     * Mission 11 - it is NULL in this model's esp_mn_iface_t (mn7_en does
     * not implement it), and calling through a NULL function pointer
     * crashed the device on every boot (Guru Meditation Error,
     * InstrFetchProhibited, PC=0x0) immediately after this log line. Do not
     * call it without confirming multinet->open_log is non-NULL first. */

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
            enter_command_window();
            notify(VOICE_STATE_COMMAND_WINDOW, "COMMAND WINDOW OPEN");
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
            in_command_window = false; /* stop accepting further commands for this window */

            esp_mn_results_t *mn_result = multinet->get_results(model_data);
            int primary_id = mn_result->num > 0 ? mn_result->command_id[0] : -1;
            int primary_phrase_id = mn_result->num > 0 ? mn_result->phrase_id[0] : -1;
            float primary_prob = mn_result->num > 0 ? mn_result->prob[0] : 0.0f;

            ESP_LOGI(TAG, "MultiNet DETECTED: command_id=%d phrase_id=%d prob=%.3f string=\"%s\" raw=\"%s\" alt_count=%d",
                     primary_id, primary_phrase_id, (double)primary_prob,
                     mn_result->string, mn_result->raw_string,
                     mn_result->num > 0 ? mn_result->num - 1 : 0);
            for (int i = 1; i < mn_result->num; i++) {
                const voice_command_def_t *alt_def = command_def_for_id(mn_result->command_id[i]);
                ESP_LOGI(TAG, "  alt[%d]: command_id=%d phrase_id=%d prob=%.3f (%s)",
                         i, mn_result->command_id[i], mn_result->phrase_id[i], (double)mn_result->prob[i],
                         alt_def ? alt_def->label : "?");
            }

            const voice_command_def_t *def = command_def_for_id(primary_id);
            if (def) {
                set_command_recognized(def);
                char msg[40];
                snprintf(msg, sizeof(msg), "CMD %s id=%d p=%.2f", def->label, primary_id, (double)primary_prob);
                notify(VOICE_STATE_COMMAND_RECOGNIZED, msg);
                vTaskDelay(pdMS_TO_TICKS(COMMAND_HIGHLIGHT_MS));
            } else {
                ESP_LOGW(TAG, "recognized command_id=%d not one of the six registered commands", primary_id);
                set_state(VOICE_STATE_UNRECOGNIZED);
                notify(VOICE_STATE_UNRECOGNIZED, "COMMAND UNRECOGNIZED");
                vTaskDelay(pdMS_TO_TICKS(COMMAND_BRIEF_MS));
            }

            set_state(VOICE_STATE_LISTENING);
            notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
            continue;
        }

        if (mn_state == ESP_MN_STATE_TIMEOUT) {
            in_command_window = false;
            ESP_LOGI(TAG, "MultiNet TIMEOUT: no command matched within %dms window", COMMAND_WINDOW_MS);
            set_state(VOICE_STATE_TIMEOUT);
            notify(VOICE_STATE_TIMEOUT, "COMMAND TIMEOUT");
            vTaskDelay(pdMS_TO_TICKS(COMMAND_BRIEF_MS));
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
