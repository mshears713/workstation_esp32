/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 13 - First Real Commands: wake word + four-command
 *        MultiNet window (SEND/NOTE/GO/YES); SEND, NOTE, and GO now do real
 *        work.
 * @details feed_task/detect_task split is unchanged since Mission 11 (see
 *          feed_task/detect_task below). Mission 12 tested six words; RUN
 *          and TEST didn't read reliably and are dropped, leaving these
 *          four. YES is recognition-only unless a notification is pending
 *          (see run_notification_command()). SEND (run_send_command), NOTE
 *          (run_note_command), and GO (run_go_command) all block
 *          detect_task while they run, all for the same reason - they need
 *          the mic (see the mic-ownership note on run_send_command). All
 *          three share the exact same recording mechanics (built on
 *          audio_capture_start_note()), differing only in how long they
 *          record and where they upload:
 *
 *            SEND - AUDIO_SEND_DURATION_MS (15s), auto-stop, Voice Inbox
 *            NOTE - long cap, ends on STOP,        Voice Inbox
 *            GO   - long cap, ends on STOP,        entries/van build log
 *
 *          SEND and NOTE are deliberately the same capture at different
 *          lengths: one quick, one long-form, both landing in the AI-OS
 *          Voice Inbox (see voice_inbox_client.h).
 *
 *          Two upload paths are kept in the tree but have no caller:
 *          note_client.h's /api/v1/notes (the local-LangGraph notes route
 *          SEND used before it moved to the Voice Inbox) and
 *          graph_client.h's one-shot design-review trigger (what GO did
 *          before it recorded anything). Both dormant, neither deleted.
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
#include "voice_inbox_client.h"
#include "entry_client.h"
#include "issue_client.h"
#include "repo_selector.h"
#include "notification_client.h"
#include "audio_playback.h"
#include "voice_control.h"

static const char *TAG = "voice_control";

/* Registered via the API (esp_mn_commands_add), not the menuconfig
 * CN/EN_SPEECH_COMMAND_ID* options - keeps the actual command phrase
 * visible in source. MultiNet7 (CONFIG_SR_MN_EN_MULTINET7_QUANT, see
 * sdkconfig.bsp.esp-box-3) takes grapheme English directly; command IDs
 * must start from 1.
 *
 * Mission 12 tested six words (SEND/NOTE/RUN/TEST/GO/YES); RUN and TEST
 * didn't read reliably on real hardware and are dropped, leaving these
 * four. "go" was also one of the words hardware-verified back in Mission
 * 11 (see the archived mission_11/main/voice_control.c debrief). */
typedef struct {
    voice_command_id_t id;
    const char *phrase; /* lowercase grapheme text registered with MultiNet */
    const char *label;  /* uppercase name for logs/Black Box messages */
} voice_command_def_t;

static const voice_command_def_t COMMAND_DEFS[] = {
    { VOICE_CMD_SEND, "send", "SEND" },
    { VOICE_CMD_NOTE, "note", "NOTE" },
    { VOICE_CMD_GO,   "go",   "GO"   },
    { VOICE_CMD_YES,  "yes",  "YES"  },
};
#define COMMAND_DEF_COUNT (sizeof(COMMAND_DEFS) / sizeof(COMMAND_DEFS[0]))

/* Per the Command Word doc's create() signature - matches Espressif's own
 * en_speech_commands_recognition example. This is the internal duration
 * MultiNet allows for a command before ESP_MN_STATE_TIMEOUT. ~10s (Mission
 * 12), to give Mike comfortable room to hear the wake chime, glance at the
 * screen, and speak one of four words rather than one fixed phrase. */
/* Fetches between forced yields inside the command window. 10 at ~30
 * fetches/sec is 3 yields a second - ample for IDLE1, which only has to run
 * once per 5s watchdog period. */
#define DETECT_YIELD_EVERY 10

#define COMMAND_WINDOW_MS 10000

/* YES highlight hold ("approximately one to two seconds" per the
 * directive). SEND/NOTE/GO don't use this - their screen time is the
 * recording/request itself plus RESULT_DISPLAY_MS below. */
#define COMMAND_HIGHLIGHT_MS 1500

/* TIMEOUT/UNRECOGNIZED hold before returning to LISTENING. */
#define COMMAND_BRIEF_MS 1000

/* SEND, NOTE, and GO's final-result hold ("briefly (5 seconds)" per the directive). */
#define RESULT_DISPLAY_MS 5000

/* Bounded wait for a SEND/NOTE/GO recording+upload to return to IDLE before
 * giving up on the mic handoff - must clear AUDIO_NOTE_MAX_DURATION_MS (20
 * minutes, audio_capture.c) plus room for the ~80 chunk uploads a
 * full-length streaming recording makes along the way (each one typically
 * well under a second, but generously margined here for a slow LAN) plus
 * the finish call plus the result hold, or this would reopen the mic for
 * listening while the worker task is still mid-recording. Not expected to
 * be hit in practice (STOP or the 20-minute cap should always resolve
 * first). */
#define CAPTURE_HANDOFF_MAX_WAIT_MS 1380000
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

/* Mission 14: set by voice_control_manual_wake() (LVGL task, TALK button),
 * consumed by detect_task (below) the same way s_mic_owner is read across
 * tasks elsewhere in this file - a single-word flag, no critical section
 * needed. Only ever cleared by detect_task itself, at the point it actually
 * acts on the request, so a press that arrives while a command window is
 * already open is not lost - it just waits for the next opportunity. */
static volatile bool s_manual_wake_requested = false;

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

/* command_id -> COMMAND_DEFS entry, or NULL if it isn't one of the four
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

/* SEND: same mic-ownership handoff Mission 11's "capture" command used
 * (flip s_mic_owner, close the listening session, let audio_capture.c own
 * the mic, wait for it to finish, reopen listening), now around
 * audio_capture_start_note() instead of plain audio_capture_start(). STOP
 * (status_deck_ui.c) calls audio_capture_stop() directly, not through here. */
/* Holds a result on screen without letting the AFE ring back up.
 *
 * The plain vTaskDelay() this replaces is the cause of issue #9. By this
 * point the mic has been handed back, so feed_task is filling the AFE ring
 * again - but detect_task is the only thing that ever calls fetch(), and it
 * was asleep for the whole hold. Five seconds of feeding with nothing
 * draining overruns the ring ("Ringbuffer of AFE(FEED) is full", 121 of them
 * measured after one 20-minute NOTE) and leaves the pipeline seconds behind
 * real time when it wakes.
 *
 * Fetching and discarding for the same duration keeps the ring drained and
 * the pipeline at real time. Nothing is lost: audio spoken while a result is
 * being displayed is not wanted anyway.
 *
 * The 5ms floor per iteration is a safety net, not pacing. fetch() is
 * supposed to block until a chunk is ready, which paces this loop by itself;
 * the floor only matters if it ever stops blocking, so that this can never
 * become the spin it is meant to cure.
 */
static void hold_result_draining(uint32_t hold_ms)
{
    int64_t deadline_us = esp_timer_get_time() + (int64_t)hold_ms * 1000;
    while (esp_timer_get_time() < deadline_us) {
        int64_t t0 = esp_timer_get_time();
        afe_fetch_result_t *res = s_afe_handle->fetch(s_afe_data);
        (void)res; /* deliberately discarded - see above */
        if ((esp_timer_get_time() - t0) < 5000) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

static void run_send_command(void)
{
    char request_id[REQUEST_ID_LEN];
    generate_request_id("SEND", request_id, sizeof(request_id));

    portENTER_CRITICAL(&s_mux);
    s_status.state = VOICE_STATE_SEND_ACTIVE;
    s_status.command_count++;
    s_status.last_command_id = VOICE_CMD_SEND;
    strncpy(s_status.last_command, "SEND", sizeof(s_status.last_command) - 1);
    s_status.last_command[sizeof(s_status.last_command) - 1] = '\0';
    strncpy(s_status.active_request_id, request_id, sizeof(s_status.active_request_id) - 1);
    s_status.active_request_id[sizeof(s_status.active_request_id) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);

    char msg[40];
    snprintf(msg, sizeof(msg), "SEND START %s", request_id);
    ESP_LOGI(TAG, "%s", msg);
    notify(VOICE_STATE_SEND_ACTIVE, msg);

    s_mic_owner = MIC_OWNER_CAPTURE;
    voice_mic_close();

    /* Voice Inbox, not note_client's /api/v1/notes: SEND and NOTE are the
     * same kind of capture with different lengths, and both belong in the
     * AI-OS Voice Inbox. The legacy local-LangGraph notes route stays
     * available in note_client.c but nothing calls it now.
     *
     * AUDIO_SEND_DURATION_MS rather than the long cap: SEND ends by
     * itself. The recording overlay's SEND/CANCEL buttons still work as an
     * early finish or abort, but pressing nothing is the normal path. */
    if (!audio_capture_start_note(request_id, AUDIO_SEND_DURATION_MS,
                                   voice_inbox_client_submit_chunk, voice_inbox_client_submit_finish,
                                   voice_inbox_client_submit_cancel)) {
        /* Only realistic cause: audio_capture's own busy-guard (a capture
         * already in flight, e.g. a manual REC press racing the wake word)
         * or its init failed independently of us. Either way, nothing to
         * poll for - hand the mic straight back. */
        notify(VOICE_STATE_SEND_ACTIVE, "SEND START FAILED");
        s_mic_owner = MIC_OWNER_LISTENING;
        if (!voice_mic_open()) {
            set_degraded("MIC REOPEN FAILED");
            return;
        }
        set_state(VOICE_STATE_LISTENING);
        notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
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

    /* fail_reason is cleared on every successful finish (see
     * audio_capture.c's set_ready()), so a non-empty reason here reliably
     * means THIS attempt failed, not a stale one from an earlier note. */
    char result_msg[56];
    if (st.state != AUDIO_CAP_IDLE) {
        snprintf(result_msg, sizeof(result_msg), "SEND DID NOT FINISH %s", request_id);
    } else if (strcmp(st.fail_reason, "CANCELLED") == 0) {
        snprintf(result_msg, sizeof(result_msg), "SEND CANCELLED %s", request_id);
    } else if (strcmp(st.fail_reason, "ENDPOINT NOT SET") == 0) {
        snprintf(result_msg, sizeof(result_msg), "SEND READY - ENDPOINT NOT SET");
    } else if (st.fail_reason[0] != '\0') {
        snprintf(result_msg, sizeof(result_msg), "SEND UPLOAD FAILED: %s", st.fail_reason);
    } else {
        /* "SENT," not "SAVED" - the backend queues transcription/graph
         * processing after accepting the upload; this firmware doesn't
         * poll for that result, so it can only truthfully claim the
         * upload itself was accepted. */
        snprintf(result_msg, sizeof(result_msg), "SEND SENT %s", request_id);
    }
    ESP_LOGI(TAG, "send complete: id=%s bytes=%lu elapsed=%lums -> %s",
             request_id, (unsigned long)st.bytes_captured, (unsigned long)st.elapsed_ms, result_msg);
    notify(VOICE_STATE_SEND_ACTIVE, result_msg);
    hold_result_draining(RESULT_DISPLAY_MS);

    set_state(VOICE_STATE_LISTENING);
    notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
}

/* NOTE: identical mic-ownership handoff and recording mechanics to
 * run_send_command() above - same audio_capture_start_note() call, same
 * CAPTURE_HANDOFF_* polling - but streams via voice_inbox_client.c's
 * chunk/finish pair instead of note_client.c's, landing in the backend's
 * Notion "Voice Inbox" pipeline instead of its local LangGraph pipeline. */
static void run_note_command(void)
{
    char request_id[REQUEST_ID_LEN];
    generate_request_id("NOTE", request_id, sizeof(request_id));

    portENTER_CRITICAL(&s_mux);
    s_status.state = VOICE_STATE_NOTE_ACTIVE;
    s_status.command_count++;
    s_status.last_command_id = VOICE_CMD_NOTE;
    strncpy(s_status.last_command, "NOTE", sizeof(s_status.last_command) - 1);
    s_status.last_command[sizeof(s_status.last_command) - 1] = '\0';
    strncpy(s_status.active_request_id, request_id, sizeof(s_status.active_request_id) - 1);
    s_status.active_request_id[sizeof(s_status.active_request_id) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);

    char msg[40];
    snprintf(msg, sizeof(msg), "NOTE START %s", request_id);
    ESP_LOGI(TAG, "%s", msg);
    notify(VOICE_STATE_NOTE_ACTIVE, msg);

    s_mic_owner = MIC_OWNER_CAPTURE;
    voice_mic_close();

    if (!audio_capture_start_note(request_id, AUDIO_NOTE_MAX_DURATION_MS,
                                   voice_inbox_client_submit_chunk, voice_inbox_client_submit_finish,
                                   voice_inbox_client_submit_cancel)) {
        /* Only realistic cause: audio_capture's own busy-guard (a capture
         * already in flight, e.g. a manual REC press racing the wake word)
         * or its init failed independently of us. Either way, nothing to
         * poll for - hand the mic straight back. */
        notify(VOICE_STATE_NOTE_ACTIVE, "NOTE START FAILED");
        s_mic_owner = MIC_OWNER_LISTENING;
        if (!voice_mic_open()) {
            set_degraded("MIC REOPEN FAILED");
            return;
        }
        set_state(VOICE_STATE_LISTENING);
        notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
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

    /* fail_reason is cleared on every successful finish (see
     * audio_capture.c's set_ready()), so a non-empty reason here reliably
     * means THIS attempt failed, not a stale one from an earlier note. */
    char result_msg[56];
    if (st.state != AUDIO_CAP_IDLE) {
        snprintf(result_msg, sizeof(result_msg), "NOTE DID NOT FINISH %s", request_id);
    } else if (strcmp(st.fail_reason, "CANCELLED") == 0) {
        snprintf(result_msg, sizeof(result_msg), "NOTE CANCELLED %s", request_id);
    } else if (strcmp(st.fail_reason, "ENDPOINT NOT SET") == 0) {
        snprintf(result_msg, sizeof(result_msg), "NOTE READY - ENDPOINT NOT SET");
    } else if (st.fail_reason[0] != '\0') {
        snprintf(result_msg, sizeof(result_msg), "NOTE UPLOAD FAILED: %s", st.fail_reason);
    } else {
        /* "SENT," not "SAVED" - the backend queues transcription/Notion-page
         * creation after accepting the upload; this firmware doesn't poll
         * for that result, so it can only truthfully claim the upload
         * itself was accepted. */
        snprintf(result_msg, sizeof(result_msg), "NOTE SENT %s", request_id);
    }
    ESP_LOGI(TAG, "note complete: id=%s bytes=%lu elapsed=%lums -> %s",
             request_id, (unsigned long)st.bytes_captured, (unsigned long)st.elapsed_ms, result_msg);
    notify(VOICE_STATE_NOTE_ACTIVE, result_msg);
    hold_result_draining(RESULT_DISPLAY_MS);

    set_state(VOICE_STATE_LISTENING);
    notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
}

/* GO: repurposed from its original one-shot design-review graph trigger
 * (trigger_graph_run(), still present in graph_client.c/backend /runs but
 * kept dormant - no longer called here, see the file header comment) to a
 * full recording+upload command, identical in mechanics to SEND/NOTE - same
 * mic-ownership handoff, same audio_capture_start_note() call - just
 * uploading to a third backend destination (entry_client.c ->
 * ENTRY_UPLOAD_PATH) that runs the recording through the backend's
 * entry-architect/Notion pipeline (Sources + Van Build Log) instead of the
 * local LangGraph design-review pipeline or the Notion Voice Inbox. The
 * VOICE_STATE_GRAPH_ACTIVE state name predates this change and is kept
 * as-is rather than renamed. */
static void run_go_command(void)
{
    char request_id[REQUEST_ID_LEN];
    generate_request_id("GO", request_id, sizeof(request_id));

    portENTER_CRITICAL(&s_mux);
    s_status.state = VOICE_STATE_GRAPH_ACTIVE;
    s_status.command_count++;
    s_status.last_command_id = VOICE_CMD_GO;
    strncpy(s_status.last_command, "GO", sizeof(s_status.last_command) - 1);
    s_status.last_command[sizeof(s_status.last_command) - 1] = '\0';
    strncpy(s_status.active_request_id, request_id, sizeof(s_status.active_request_id) - 1);
    s_status.active_request_id[sizeof(s_status.active_request_id) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);

    /* Checked before the mic is taken, not after the recording: making
     * someone speak for 15 seconds and only then saying "no repository" is
     * the wrong order. Happens when the backend has never been reachable
     * since boot, so the catalog never loaded. */
    if (repo_selector_count() <= 0) {
        ESP_LOGW(TAG, "GO refused: no repository catalog loaded");
        notify(VOICE_STATE_GRAPH_ACTIVE, "GO - NO REPO LIST");
        hold_result_draining(RESULT_DISPLAY_MS);
        set_state(VOICE_STATE_LISTENING);
        notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
        return;
    }

    char msg[40];
    snprintf(msg, sizeof(msg), "GO START %s", request_id);
    ESP_LOGI(TAG, "%s", msg);
    notify(VOICE_STATE_GRAPH_ACTIVE, msg);

    s_mic_owner = MIC_OWNER_CAPTURE;
    voice_mic_close();

    /* AUDIO_GO_DURATION_MS, not the long cap: GO is a quick "file this as an
     * issue", the same shape as SEND. Auto-stop, no STOP press needed.
     *
     * issue_client rather than entry_client: GO used to record into the
     * van build log / entries pipeline. That pipeline still works and is
     * kept deliberately - see the dormant-functionality section in the
     * README - it simply has no trigger now. */
    if (!audio_capture_start_note(request_id, AUDIO_GO_DURATION_MS,
                                   issue_client_submit_chunk, issue_client_submit_finish,
                                   issue_client_submit_cancel)) {
        /* Only realistic cause: audio_capture's own busy-guard (a capture
         * already in flight, e.g. a manual REC press racing the wake word)
         * or its init failed independently of us. Either way, nothing to
         * poll for - hand the mic straight back. */
        notify(VOICE_STATE_GRAPH_ACTIVE, "GO START FAILED");
        s_mic_owner = MIC_OWNER_LISTENING;
        if (!voice_mic_open()) {
            set_degraded("MIC REOPEN FAILED");
            return;
        }
        set_state(VOICE_STATE_LISTENING);
        notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
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

    /* fail_reason is cleared on every successful finish (see
     * audio_capture.c's set_ready()), so a non-empty reason here reliably
     * means THIS attempt failed, not a stale one from an earlier entry. */
    char result_msg[56];
    if (st.state != AUDIO_CAP_IDLE) {
        snprintf(result_msg, sizeof(result_msg), "GO DID NOT FINISH %s", request_id);
    } else if (strcmp(st.fail_reason, "CANCELLED") == 0) {
        snprintf(result_msg, sizeof(result_msg), "GO CANCELLED %s", request_id);
    } else if (strcmp(st.fail_reason, "ENDPOINT NOT SET") == 0) {
        snprintf(result_msg, sizeof(result_msg), "GO READY - ENDPOINT NOT SET");
    } else if (st.fail_reason[0] != '\0') {
        snprintf(result_msg, sizeof(result_msg), "GO UPLOAD FAILED: %s", st.fail_reason);
    } else {
        /* "SENT," not "SAVED" - the backend queues transcription/entry-
         * architect/Notion processing after accepting the upload; this
         * firmware doesn't poll for that result, so it can only truthfully
         * claim the upload itself was accepted. */
        uint32_t issue_number = issue_client_get_last_issue();
        if (issue_number > 0) {
            /* "ISSUE #42", not "SENT": this endpoint waits for the issue to
             * actually exist, so the device can report the thing that was
             * created rather than that an upload was accepted. */
            snprintf(result_msg, sizeof(result_msg), "GO ISSUE #%lu", (unsigned long)issue_number);
        } else {
            snprintf(result_msg, sizeof(result_msg), "GO SENT %s", request_id);
        }
    }
    ESP_LOGI(TAG, "go complete: id=%s bytes=%lu elapsed=%lums -> %s",
             request_id, (unsigned long)st.bytes_captured, (unsigned long)st.elapsed_ms, result_msg);
    notify(VOICE_STATE_GRAPH_ACTIVE, result_msg);
    hold_result_draining(RESULT_DISPLAY_MS);

    set_state(VOICE_STATE_LISTENING);
    notify(VOICE_STATE_LISTENING, "LISTENING RESTORED");
}

/* YES-while-pending: fetch the oldest pending notification's audio, play it
 * over the speaker, and ack it - see notification_client.h/audio_playback.h.
 * Same mic-ownership handoff SEND/NOTE use around their recording (close
 * the listening session, do the thing, reopen listening) even though
 * playback uses the speaker (ES8311) rather than the mic (ES7210) and so
 * doesn't strictly need the mic closed - the directive is to keep mic and
 * speaker mutually exclusive rather than run them concurrently, and this is
 * the same handoff shape already proven for SEND/NOTE. Blocks detect_task
 * for the fetch+playback+ack cycle, same as every other command here. */
static void run_notification_command(const char *notification_id)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = VOICE_STATE_NOTIFICATION_ACTIVE;
    s_status.command_count++;
    s_status.last_command_id = VOICE_CMD_YES;
    strncpy(s_status.last_command, "YES", sizeof(s_status.last_command) - 1);
    s_status.last_command[sizeof(s_status.last_command) - 1] = '\0';
    strncpy(s_status.active_request_id, notification_id, sizeof(s_status.active_request_id) - 1);
    s_status.active_request_id[sizeof(s_status.active_request_id) - 1] = '\0';
    strncpy(s_status.last_result, "FETCHING", sizeof(s_status.last_result) - 1);
    s_status.last_result[sizeof(s_status.last_result) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "NOTIFICATION START %s", notification_id);
    notify(VOICE_STATE_NOTIFICATION_ACTIVE, "NOTIFICATION START");

    s_mic_owner = MIC_OWNER_CAPTURE;
    voice_mic_close();

    /* fail_reason's declared 40-byte capacity is what GCC's
     * -Wformat-truncation sizes against below, not the short values it
     * actually ever holds - same "size for the worst case the compiler can
     * see" reasoning render_recording_overlay()'s title_buf comment
     * documents, so this needs room for "NOTIFICATION FETCH FAILED: " (28
     * chars) + a full 39-char fail_reason, not just what's typical. Only
     * s_status.last_result (48 bytes) is what's actually shown/stored
     * downstream - this is truncated into that via strncpy same as
     * everywhere else in this file. */
    char result_msg[80];
    const uint8_t *pcm = NULL;
    size_t pcm_len = 0;
    char fail_reason[40] = "";

    if (!notification_client_fetch_audio(notification_id, &pcm, &pcm_len, fail_reason, sizeof(fail_reason))) {
        snprintf(result_msg, sizeof(result_msg), "NOTIFICATION FETCH FAILED: %s", fail_reason);
        ESP_LOGW(TAG, "notification fetch failed: id=%s reason=%s", notification_id, fail_reason);
    } else {
        portENTER_CRITICAL(&s_mux);
        strncpy(s_status.last_result, "PLAYING", sizeof(s_status.last_result) - 1);
        s_status.last_result[sizeof(s_status.last_result) - 1] = '\0';
        portEXIT_CRITICAL(&s_mux);
        notify(VOICE_STATE_NOTIFICATION_ACTIVE, "NOTIFICATION PLAYING");

        bool played = audio_playback_play(pcm, pcm_len, NOTIFICATION_AUDIO_SAMPLE_RATE_HZ);
        if (!played) {
            snprintf(result_msg, sizeof(result_msg), "NOTIFICATION PLAYBACK STOPPED");
        } else if (!notification_client_ack(notification_id)) {
            snprintf(result_msg, sizeof(result_msg), "NOTIFICATION PLAYED - ACK FAILED");
        } else {
            snprintf(result_msg, sizeof(result_msg), "NOTIFICATION DELIVERED");
            /* Re-poll now rather than waiting for the background task's
             * next scheduled pass, so the ring can drop back to blue right
             * away when nothing else is pending - see
             * notification_client_refresh_now()'s doc comment. */
            notification_client_refresh_now();
        }
    }

    s_mic_owner = MIC_OWNER_LISTENING;
    if (!voice_mic_open()) {
        set_degraded("MIC REOPEN FAILED");
        return;
    }

    ESP_LOGI(TAG, "notification complete: id=%s -> %s", notification_id, result_msg);
    portENTER_CRITICAL(&s_mux);
    strncpy(s_status.last_result, result_msg, sizeof(s_status.last_result) - 1);
    s_status.last_result[sizeof(s_status.last_result) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);
    notify(VOICE_STATE_NOTIFICATION_ACTIVE, result_msg);
    hold_result_draining(RESULT_DISPLAY_MS);

    set_state(VOICE_STATE_LISTENING);
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

/* Registers all four Mission 13 command words. Continues past a single
 * failed phrase rather than aborting the whole pipeline on it (the
 * directive: "report any command that fails registration rather than
 * silently omitting it") - only if literally none of the four register does
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

    ESP_LOGI(TAG, "four-command registration: %d/%d succeeded", registered, (int)COMMAND_DEF_COUNT);
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
     * at this exact point in the sequence. "go" was hardware-verified at
     * this threshold back in Mission 11; SEND/NOTE/YES were hardware-
     * verified reading reliably at it during Mission 12's six-word test
     * (which is also how RUN/TEST got cut). Carried forward unchanged into
     * Mission 13 per the directive ("do not change the detection threshold
     * before collecting baseline results unless the current code already
     * uses a deliberate tested threshold" - it does). */
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
    uint32_t detect_since_yield = 0;   /* see the yield in the command window below */
    uint32_t fetch_calls = 0;          /* issue #9 diagnostic, rolled up once a second */
    uint32_t fetch_blocked_us = 0;
    int64_t fetch_rollup_us = esp_timer_get_time();

    for (;;) {
        /* Diagnostic for issue #9: fetch() is meant to block until a chunk
         * of audio is ready, and that block is the only thing pacing this
         * loop. If it ever stops blocking - which is what a backed-up AFE
         * ring causes - this loop spins and starves IDLE1 on core 1, which
         * is exactly the watchdog trip observed during a command window.
         * Rolled up once a second so the answer is a measurement rather
         * than an assumption. */
        int64_t fetch_start_us = esp_timer_get_time();
        afe_fetch_result_t *res = s_afe_handle->fetch(afe_data);
        int64_t now_us = esp_timer_get_time();
        fetch_calls++;
        fetch_blocked_us += (uint32_t)(now_us - fetch_start_us);
        if (now_us - fetch_rollup_us >= 1000000) {
            uint32_t window_ms = (uint32_t)((now_us - fetch_rollup_us) / 1000);
            uint32_t blocked_ms = fetch_blocked_us / 1000;
            /* blocked_ms close to window_ms means fetch() is blocking and
             * the loop is real-time. Near zero means it is spinning. */
            ESP_LOGI(TAG, "afe fetch: %lu calls in %lums, blocked %lums (%lu%%)%s",
                     (unsigned long)fetch_calls, (unsigned long)window_ms,
                     (unsigned long)blocked_ms,
                     (unsigned long)(window_ms ? (blocked_ms * 100 / window_ms) : 0),
                     in_command_window ? " [command window]" : "");
            fetch_calls = 0;
            fetch_blocked_us = 0;
            fetch_rollup_us = now_us;
        }
        if (!res || res->ret_value == ESP_FAIL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!in_command_window && (res->wakeup_state == WAKENET_DETECTED || s_manual_wake_requested)) {
            bool manual = s_manual_wake_requested;
            s_manual_wake_requested = false;
            in_command_window = true;
            multinet->clean(model_data);
            portENTER_CRITICAL(&s_mux);
            s_status.wake_count++;
            portEXIT_CRITICAL(&s_mux);
            notify(VOICE_STATE_WAKE_DETECTED, manual ? "WAKE (MANUAL)" : "WAKE DETECTED");
            enter_command_window();
            notify(VOICE_STATE_COMMAND_WINDOW, "COMMAND WINDOW OPEN");
            continue;
        }

        if (!in_command_window) {
            continue;
        }

        /* multinet->detect() is expensive: measured at ~78% of core 1 for
         * the whole command window, with fetch() blocking only ~22%. That
         * is not enough for IDLE1 (priority 0) to be scheduled, so the task
         * watchdog fires every 5s during any window that runs its full
         * length. It was never a backlog artifact - the very first window
         * after boot, with an empty ring, already showed it.
         *
         * One tick every DETECT_YIELD_EVERY fetches is enough for IDLE1 to
         * run and reset the watchdog, while costing ~30ms per second of
         * detection time against the ~220ms of headroom measured - so
         * recognition is not pushed behind real time. */
        if (++detect_since_yield >= DETECT_YIELD_EVERY) {
            detect_since_yield = 0;
            vTaskDelay(1);
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
                char msg[40];
                snprintf(msg, sizeof(msg), "CMD %s id=%d p=%.2f", def->label, primary_id, (double)primary_prob);
                notify(VOICE_STATE_COMMAND_RECOGNIZED, msg);

                if (def->id == VOICE_CMD_SEND) {
                    run_send_command(); /* owns its own result display + return to LISTENING */
                    continue;
                }
                if (def->id == VOICE_CMD_NOTE) {
                    run_note_command(); /* owns its own result display + return to LISTENING */
                    continue;
                }
                if (def->id == VOICE_CMD_GO) {
                    run_go_command(); /* owns its own result display + return to LISTENING */
                    continue;
                }
                if (def->id == VOICE_CMD_YES) {
                    notification_status_t nst;
                    notification_client_get_status(&nst);
                    if (nst.pending) {
                        run_notification_command(nst.notification_id); /* owns its own result display + return to LISTENING */
                        continue;
                    }
                    /* fall through: nothing pending, YES stays recognition-only below */
                }

                /* YES with nothing pending (or any other recognized command
                 * reaching here, which per COMMAND_DEFS is only ever YES):
                 * recognition-only. */
                set_command_recognized(def);
                hold_result_draining(COMMAND_HIGHLIGHT_MS);
            } else {
                ESP_LOGW(TAG, "recognized command_id=%d not one of the four registered commands", primary_id);
                set_state(VOICE_STATE_UNRECOGNIZED);
                notify(VOICE_STATE_UNRECOGNIZED, "COMMAND UNRECOGNIZED");
                hold_result_draining(COMMAND_BRIEF_MS);
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
            hold_result_draining(COMMAND_BRIEF_MS);
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

void voice_control_manual_wake(void)
{
    s_manual_wake_requested = true;
}
