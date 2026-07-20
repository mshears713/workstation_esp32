/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 05/06/07/08/09/10 — Status Deck + Live Data Deck + Black
 *        Box Recorder + Connection Deck + Earthside Handshake + Capture
 *        the Transmission
 * @details Command-and-telemetry deck: touch controls drive a single
 *          app_state_t, and a 1 Hz timer refreshes uptime. Mission 05
 *          started with five buttons (ARM/PING/DIAG/LINK/CLR) and a wider
 *          telemetry line (uptime/tick/heap/PSRAM); Mission 09 dropped the
 *          four buttons with no real subsystem behind them (leaving
 *          NET/SND/CLR) and trimmed telemetry to just uptime, freeing
 *          screen space for a compact 2-row status grid (UP/ACC/WIFI/HS)
 *          and a larger, more zoomed-in accelerometer chart - see the
 *          "Layout" section below.
 *          Mission 06 added a Live Data Deck: real acceleration magnitude
 *          from the onboard ICM42670 IMU (bsp_sensor_init/iot_sensor_hub),
 *          represented with an explicit UNKNOWN/CURRENT/STALE/ERROR status
 *          and a last-good-sample age rather than a bare number, plus a
 *          trend chart (LVGL's own bounded ring buffer, no separate array).
 *          Mission 07 turns the old flat-text event log into a Black Box
 *          Recorder: bounded structured event_record_t history (RAM-only,
 *          holds more than the compact on-screen panel shows), reset-reason
 *          + boot-count capture via esp_reset_reason(), and a small
 *          separately-persisted blackbox_summary_t written only at real
 *          boundaries (boot, state-changing commands, hard sensor faults) -
 *          never on every touch or every telemetry tick. See the "Black Box
 *          persistence" section below for why this replaces Mission 05/06's
 *          old nvs_save_state(), which rewrote the entire app_state + full
 *          text log to flash on every single button press.
 *          Mission 08 adds a Connection Deck: the Wi-Fi station lifecycle
 *          itself lives in wifi_manager.c/.h as its own DISCONNECTED /
 *          CONNECTING / ONLINE / RETRY_WAIT state machine, running on the
 *          ESP-IDF event-loop task. This file only bridges that
 *          foreign-task state into the UI (see "Connection Deck" below) -
 *          the same record-on-one-task, render-on-the-LVGL-task split
 *          Mission 06 already uses for the IMU.
 *          Mission 09 adds an Earthside Handshake: handshake_client.c/.h
 *          owns a small HTTP request state machine (its own worker task,
 *          not the Wi-Fi event task or the LVGL task), bridged into the UI
 *          with the same pending-queue pattern as Connection Deck - see
 *          "Handshake" below. Backend/request state is deliberately kept
 *          separate from Wi-Fi state: a device can be WIFI: ONLINE while a
 *          given handshake still times out, and that distinction matters.
 *          Mission 10 adds Capture the Transmission: audio_capture.c/.h owns
 *          a bounded microphone-capture state machine (IDLE/ARMING/
 *          RECORDING/COMPLETE/UPLOADING/READY/FAILED) against the verified
 *          ES7210 mic path, on its own worker task - bridged into the UI
 *          with the same pending-queue pattern as Connection Deck/
 *          Handshake, see "Audio Capture Deck" below. Completed captures
 *          upload straight to the backend over Wi-Fi (POST /api/v1/audio,
 *          same LAN link as the Handshake) rather than exporting over the
 *          serial console. The accelerometer chart lost 16px of height
 *          (64px -> 48px) to make room for the new AUD status row; nothing
 *          else about the Live Data Deck changed.
 *          Mission 11 adds Computer Is Listening: voice_control.c/.h owns a
 *          continuous local WakeNet/MultiNet pipeline (LISTENING/WAKE
 *          DETECTED/COMMAND WINDOW/COMMAND RECOGNIZED/TIMEOUT/CAPTURING/
 *          DEGRADED) against the same shared ES7210 mic handle audio
 *          capture uses - see "Voice Deck" below, same pending-queue bridge
 *          pattern as the other foreign-task modules. Saying "computer"
 *          then "start recording" now triggers the exact same
 *          audio_capture_start() the REC button does; REC keeps working
 *          unconditionally as the fallback. The accelerometer chart lost
 *          another 16px (48px -> 32px) to make room for the new VOICE row.
 *          Built on the Mission 04 first_command BSP/LVGL foundation.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "esp_mac.h"
#include "bsp/esp-bsp.h"
#include "wifi_manager.h"
#include "handshake_client.h"
#include "audio_capture.h"
#include "voice_control.h"

static const char *TAG = "status_deck";

/* ---- App state: current truth, volatile RAM only ---------------------
 * Resets to defaults every boot. Mission 06 used to persist this via NVS
 * on every command; Mission 07 stops that (see Black Box persistence
 * below) because "what mode is the console in right now" is not the kind
 * of evidence a black box needs to survive a reboot - the event history
 * and the separate summary record are.
 *
 * Mission 09 removed the ARM/PING/DIAG/LINK test buttons and the mode/
 * link_up/diag_active fields they toggled - they were Mission 05/06
 * scaffolding with no real subsystem behind them, and freeing their screen
 * space gave the chart and the real Connection Deck/Handshake panels more
 * room. last_command/command_count stay: they're generic evidence of
 * whatever the operator last pressed (now NET/SND/CLR), not tied to the
 * removed test buttons specifically. */

typedef struct {
    char last_command[16];
    uint32_t command_count;
} app_state_t;

static app_state_t app_state = {
    .last_command = "NONE",
    .command_count = 0,
};

/* ---- Black Box Recorder: bounded structured event history ------------
 * RAM-only ring of typed records, not a single ever-growing string.
 * Capacity (EVENT_HISTORY_LEN) is deliberately larger than what the
 * compact on-screen panel renders (EVENT_DISPLAY_LINES) - the recorder
 * keeps more than the cockpit display shows, same idea as a real flight
 * data recorder vs. its cockpit readout. */

typedef enum {
    EVT_BOOT,
    EVT_RESET,
    EVT_COMMAND,
    EVT_SENSOR,
    EVT_SYSTEM,
    EVT_NETWORK,
    EVT_HANDSHAKE,
    EVT_AUDIO,
    EVT_VOICE,
} event_type_t;

#define EVENT_HISTORY_LEN 8
#define EVENT_MSG_LEN 28
#define EVENT_DISPLAY_LINES 3 /* was 2 - the log panel grew, see status_deck_ui() */

typedef struct {
    event_type_t type;
    uint32_t uptime_s;
    char message[EVENT_MSG_LEN];
} event_record_t;

static event_record_t event_history[EVENT_HISTORY_LEN];
static int event_history_used = 0;

static void event_history_push(event_type_t type, const char *message)
{
    int keep = (event_history_used < EVENT_HISTORY_LEN - 1) ? event_history_used : EVENT_HISTORY_LEN - 1;
    for (int i = keep; i > 0; i--) {
        event_history[i] = event_history[i - 1];
    }
    event_history[0].type = type;
    event_history[0].uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    strncpy(event_history[0].message, message, EVENT_MSG_LEN - 1);
    event_history[0].message[EVENT_MSG_LEN - 1] = '\0';
    if (event_history_used < EVENT_HISTORY_LEN) {
        event_history_used++;
    }
}

/* forward declared: defined in the Rendering section below */
static void render_event_log(void);

/* Boot-time pushes happen before widgets exist, so they skip the render
 * step (same "record now, render later" split Mission 05 used for BOOT). */
static void event_push_only(event_type_t type, const char *message)
{
    event_history_push(type, message);
    ESP_LOGI(TAG, "event type=%d msg=%s", (int)type, message);
}

static void log_event(event_type_t type, const char *message)
{
    event_push_only(type, message);
    render_event_log();
}

/* ---- Black Box persistence: small, separate, sparsely written --------
 * A dedicated NVS namespace holding only curated high-value evidence:
 * boot count, the reset reason as of the end of the previous session,
 * the last state-changing/operator command, and the last hard sensor
 * fault. Nothing here is written on every touch or every telemetry tick:
 *   - boot_count/resetrsn: written once per boot (blackbox_boot_update).
 *   - last_significant_cmd: written for every command currently on the
 *     button row (NET/SND/CLR LOG) - `handle_command`'s `significant` flag
 *     still exists for a future non-persistent-worthy command (Mission
 *     05/06 had PING as that example before Mission 09 removed it).
 *   - last_fault: written only when the sensor hardware probe fails.
 * Each writer touches only its own 1-2 NVS keys, never a full-state
 * rewrite. At human-paced interaction rates (button presses, occasional
 * faults) this is a handful of small writes per session at most - nowhere
 * near NVS/flash wear limits (rated for ~100k erase cycles per sector).
 * The thing this deliberately avoids is what Mission 05/06 actually did:
 * nvs_save_state() rewrote the entire app_state plus the full text event
 * log on every single command, including PING, which is exactly the
 * "high-frequency NVS write path merely because persistence is available"
 * anti-pattern - harmless at manual button-press rates, but the wrong
 * habit to carry into networking missions where "session status changed"
 * could fire far more often than a human tapping a button. */

#define BLACKBOX_NVS_NAMESPACE "blackbox"

typedef struct {
    uint32_t boot_count;
    esp_reset_reason_t prev_reset_reason; /* reset reason as of the previous boot */
    char last_significant_cmd[16];
    char last_fault[EVENT_MSG_LEN];
} blackbox_summary_t;

static blackbox_summary_t blackbox = {
    .boot_count = 0,
    .prev_reset_reason = ESP_RST_UNKNOWN,
    .last_significant_cmd = "NONE",
    .last_fault = "",
};

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:    return "POWERON";
    case ESP_RST_EXT:        return "EXT PIN";
    case ESP_RST_SW:         return "SW RESTART";
    case ESP_RST_PANIC:      return "PANIC";
    case ESP_RST_INT_WDT:    return "INT WATCHDOG";
    case ESP_RST_TASK_WDT:   return "TASK WATCHDOG";
    case ESP_RST_WDT:        return "OTHER WATCHDOG";
    case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP WAKE";
    case ESP_RST_BROWNOUT:   return "BROWNOUT";
    case ESP_RST_SDIO:       return "SDIO";
    case ESP_RST_USB:        return "USB";
    case ESP_RST_JTAG:       return "JTAG";
    case ESP_RST_EFUSE:      return "EFUSE ERROR";
    case ESP_RST_PWR_GLITCH: return "PWR GLITCH";
    case ESP_RST_CPU_LOCKUP: return "CPU LOCKUP";
    default:                 return "UNKNOWN";
    }
}

/* One NVS transaction, once per boot: read last session's boot_count and
 * reset reason (-> blackbox.prev_reset_reason), then bump boot_count and
 * overwrite the stored reason with THIS boot's esp_reset_reason() so the
 * *next* boot reads it as "previous". last_significant_cmd/last_fault are
 * read here but not rewritten - they carry forward until a real command
 * or fault updates them during this session. */
static void blackbox_boot_update(void)
{
    nvs_handle_t h;
    uint32_t boot_count = 0;
    uint8_t prev_reason = ESP_RST_UNKNOWN;
    char last_cmd[16] = "NONE";
    char last_fault[EVENT_MSG_LEN] = "";

    if (nvs_open(BLACKBOX_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "bootcnt", &boot_count);
        nvs_get_u8(h, "resetrsn", &prev_reason);
        size_t len = sizeof(last_cmd);
        nvs_get_str(h, "lastcmd", last_cmd, &len);
        len = sizeof(last_fault);
        nvs_get_str(h, "lastfault", last_fault, &len);

        boot_count++;
        nvs_set_u32(h, "bootcnt", boot_count);
        nvs_set_u8(h, "resetrsn", (uint8_t)esp_reset_reason());
        nvs_commit(h);
        nvs_close(h);
    } else {
        boot_count = 1; /* first-ever boot: no NVS entry yet */
    }

    blackbox.boot_count = boot_count;
    blackbox.prev_reset_reason = (esp_reset_reason_t)prev_reason;
    strncpy(blackbox.last_significant_cmd, last_cmd, sizeof(blackbox.last_significant_cmd) - 1);
    blackbox.last_significant_cmd[sizeof(blackbox.last_significant_cmd) - 1] = '\0';
    strncpy(blackbox.last_fault, last_fault, sizeof(blackbox.last_fault) - 1);
    blackbox.last_fault[sizeof(blackbox.last_fault) - 1] = '\0';
}

/* Touches only the "lastcmd" key. Called for state-changing/operator
 * commands only, gated by handle_command's `significant` flag. */
static void blackbox_save_last_cmd(const char *name)
{
    strncpy(blackbox.last_significant_cmd, name, sizeof(blackbox.last_significant_cmd) - 1);
    blackbox.last_significant_cmd[sizeof(blackbox.last_significant_cmd) - 1] = '\0';

    nvs_handle_t h;
    if (nvs_open(BLACKBOX_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, "lastcmd", blackbox.last_significant_cmd);
    nvs_commit(h);
    nvs_close(h);
}

/* Touches only the "lastfault" key. Called only when the sensor hardware
 * probe fails at boot - not on transient STALE/RECOVERED oscillation. */
static void blackbox_save_fault(const char *text)
{
    strncpy(blackbox.last_fault, text, sizeof(blackbox.last_fault) - 1);
    blackbox.last_fault[sizeof(blackbox.last_fault) - 1] = '\0';

    nvs_handle_t h;
    if (nvs_open(BLACKBOX_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, "lastfault", blackbox.last_fault);
    nvs_commit(h);
    nvs_close(h);
}

/* ---- Live Data Deck: real acceleration magnitude from the onboard IMU -
 * Radar/presence hardware is not confirmed on this board - the official
 * esp-box-3 BSP's bsp_sensor_init() only wires up two real sensor paths
 * (HUMITURE_ID -> AHT30 on the SENSOR dock, IMU_ID -> onboard ICM42670),
 * so this uses the verified ICM42670 path rather than guess at radar.
 * Acceleration magnitude sits near 1.00g at rest (gravity) regardless of
 * orientation and jumps immediately when the board is moved/shaken, which
 * is a more interactive way to see freshness/timestamp behavior than the
 * slow-changing AHT30 temperature/humidity Mission 06 started with.
 *
 * Acquisition runs on the sensor_hub event-loop task, NOT the LVGL task,
 * so the event handler below only touches plain state behind a critical
 * section. The 1 Hz LVGL timer (already running under the LVGL lock)
 * decides freshness/staleness and does all rendering and event logging,
 * so app state stays the single source of truth and LVGL calls only ever
 * happen from the LVGL timer, matching the Mission 05 architecture. */

/* 100ms (10Hz): the ICM42670 samples this fast trivially over I2C. Must
 * stay matched to the LVGL timer period below - see the render-cadence
 * note in status_deck_ui() for why. */
#define SENSOR_ACQUIRE_PERIOD_MS 100
#define SENSOR_STALE_THRESHOLD_US ((int64_t)3 * SENSOR_ACQUIRE_PERIOD_MS * 1000)

typedef enum {
    SRC_UNKNOWN = 0,
    SRC_CURRENT,
    SRC_STALE,
    SRC_ERROR,
} source_status_t;

static portMUX_TYPE sensor_mux = portMUX_INITIALIZER_UNLOCKED;
static bool sensor_present = false;          /* hardware detected at boot */
static source_status_t sensor_status = SRC_UNKNOWN;
static volatile bool sensor_have_new_sample = false;
static float last_accel_g = 0.0f;            /* |acceleration| magnitude, unit: G */
static int64_t sensor_last_good_us = 0;

/* Runs on the sensor_hub task. No LVGL calls here. The ICM42670 also
 * posts gyro-ready events on the same handle (iot_sensor_handler_register
 * subscribes to "any event" for it); only acceleration is used here, so
 * gyro events are ignored rather than guessed at. */
static void sensor_event_handler(void *arg, sensor_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    if (event_id != SENSOR_ACCE_DATA_READY) {
        return;
    }
    sensor_data_t *data = (sensor_data_t *)event_data;
    /* icm42670.c: value = raw / sensitivity, sensitivity in LSB/g (e.g.
     * 16384 at +-2g) - confirmed in the driver, so acce.x/y/z are already
     * plain G's, matching the sensor_data_t union comment for this field
     * (unlike the AHT30 "dCelsius" comment Mission 06 found to be wrong). */
    float mag = sqrtf(data->acce.x * data->acce.x + data->acce.y * data->acce.y + data->acce.z * data->acce.z);
    portENTER_CRITICAL(&sensor_mux);
    last_accel_g = mag;
    sensor_last_good_us = esp_timer_get_time();
    sensor_have_new_sample = true;
    portEXIT_CRITICAL(&sensor_mux);
}

/* ---- Accel sample ring buffer: last ACCEL_SAMPLE_COUNT readings for the
 * Handshake payload ------------------------------------------------------
 * Separate from the chart's own LVGL-owned ring buffer above - that one
 * exists to draw a trend line and isn't readable back out; this one exists
 * purely so send_handshake_button_cb has real recent samples to ship.
 * Pushed from sensor_timer_cb (LVGL task, see below), same task that reads
 * it in send_handshake_button_cb, so no locking needed here unlike
 * last_accel_g/sensor_mux above, which cross from the sensor_hub task.
 * Tied to handshake_client.h's HANDSHAKE_MAX_ACCEL_SAMPLES (not a separate
 * "10" of its own) so this ring buffer and payload.accel_samples_g's
 * capacity can never drift out of sync - accel_samples_copy_ordered writes
 * up to accel_samples_count entries into a caller buffer sized by this
 * same constant. */
#define ACCEL_SAMPLE_COUNT HANDSHAKE_MAX_ACCEL_SAMPLES

static float accel_samples[ACCEL_SAMPLE_COUNT];
static int accel_samples_count = 0; /* valid entries so far, caps at ACCEL_SAMPLE_COUNT */
static int accel_samples_next = 0;  /* ring write index */

static void accel_samples_push(float g)
{
    accel_samples[accel_samples_next] = g;
    accel_samples_next = (accel_samples_next + 1) % ACCEL_SAMPLE_COUNT;
    if (accel_samples_count < ACCEL_SAMPLE_COUNT) {
        accel_samples_count++;
    }
}

/* Copies out up to ACCEL_SAMPLE_COUNT samples, oldest first, into `out`.
 * Returns how many were copied - may be less than ACCEL_SAMPLE_COUNT
 * (e.g. shortly after boot); never pads with fabricated values. */
static int accel_samples_copy_ordered(float *out)
{
    int start = (accel_samples_next - accel_samples_count + ACCEL_SAMPLE_COUNT) % ACCEL_SAMPLE_COUNT;
    for (int i = 0; i < accel_samples_count; i++) {
        out[i] = accel_samples[(start + i) % ACCEL_SAMPLE_COUNT];
    }
    return accel_samples_count;
}

/* ---- Connection Deck: Wi-Fi lifecycle, bridged from a foreign task ----
 * wifi_manager.c's callback runs on the ESP-IDF event-loop task (or, for a
 * manual reconnect, whichever task pressed the NET button) - never the
 * LVGL task. wifi_status_changed_cb below therefore only ever touches this
 * tiny critical-section-protected queue, mirroring how sensor_event_handler
 * above only touches sensor_mux-protected state. wifi_ui_timer_cb (LVGL
 * task, see the bottom of this file) drains it and is the only place that
 * calls log_event() or touches Wi-Fi UI widgets.
 *
 * A short queue rather than a single pending slot: a fast start -> fail ->
 * retry sequence right after boot can otherwise land two transitions
 * between two drains of a single-slot mailbox, silently losing one. */
#define WIFI_PENDING_LEN 4

typedef struct {
    char message[EVENT_MSG_LEN];
} wifi_pending_evt_t;

static wifi_pending_evt_t wifi_pending[WIFI_PENDING_LEN];
static int wifi_pending_count = 0;
static portMUX_TYPE wifi_pending_mux = portMUX_INITIALIZER_UNLOCKED;

/* If the queue is ever full (extremely unlikely at network event rates
 * against a 500ms drain), the newest transition is dropped rather than
 * overwriting an already-queued one - losing evidence that "something
 * happened" is preferable to corrupting a message already in flight. */
static void wifi_status_changed_cb(wifi_mgr_state_t new_state, const char *message, void *ctx)
{
    (void)new_state;
    (void)ctx;
    portENTER_CRITICAL(&wifi_pending_mux);
    if (wifi_pending_count < WIFI_PENDING_LEN) {
        strncpy(wifi_pending[wifi_pending_count].message, message, EVENT_MSG_LEN - 1);
        wifi_pending[wifi_pending_count].message[EVENT_MSG_LEN - 1] = '\0';
        wifi_pending_count++;
    }
    portEXIT_CRITICAL(&wifi_pending_mux);
}

/* ---- Handshake: backend request lifecycle, bridged from a foreign task -
 * handshake_client.c's callback runs on its own worker task - never the
 * LVGL task. Same bridge shape as the Connection Deck queue above, kept as
 * a separate queue/mutex/timer rather than a shared abstraction: Wi-Fi and
 * handshake events are unrelated foreign tasks with unrelated timing, and
 * two small independent queues are easier to reason about than one shared
 * one two subsystems would need to coordinate over. */
#define HANDSHAKE_PENDING_LEN 4

typedef struct {
    char message[EVENT_MSG_LEN];
} handshake_pending_evt_t;

static handshake_pending_evt_t handshake_pending[HANDSHAKE_PENDING_LEN];
static int handshake_pending_count = 0;
static portMUX_TYPE handshake_pending_mux = portMUX_INITIALIZER_UNLOCKED;

static void handshake_status_changed_cb(handshake_state_t new_state, const char *message, void *ctx)
{
    (void)new_state;
    (void)ctx;
    portENTER_CRITICAL(&handshake_pending_mux);
    if (handshake_pending_count < HANDSHAKE_PENDING_LEN) {
        strncpy(handshake_pending[handshake_pending_count].message, message, EVENT_MSG_LEN - 1);
        handshake_pending[handshake_pending_count].message[EVENT_MSG_LEN - 1] = '\0';
        handshake_pending_count++;
    }
    portEXIT_CRITICAL(&handshake_pending_mux);
}

/* ---- Audio Capture Deck: microphone state, bridged from a foreign task -
 * audio_capture.c's callback runs on its own worker task - never the LVGL
 * task. Same bridge shape as Connection Deck/Handshake above, and the same
 * reasoning for keeping it a separate queue rather than a shared one:
 * unrelated foreign task, unrelated timing. Unlike Wi-Fi/handshake, audio
 * state also carries fast-changing progress (elapsed_ms/bytes_captured)
 * while RECORDING - that part is read straight from
 * audio_capture_get_status() on every render_audio_panel() call, not
 * queued, since it is current truth rather than a discrete transition; only
 * discrete transitions (ARM/START/COMPLETE/FAILED/READY) go through this
 * queue to become Black Box events. */
#define AUDIO_PENDING_LEN 4

typedef struct {
    char message[EVENT_MSG_LEN];
} audio_pending_evt_t;

static audio_pending_evt_t audio_pending[AUDIO_PENDING_LEN];
static int audio_pending_count = 0;
static portMUX_TYPE audio_pending_mux = portMUX_INITIALIZER_UNLOCKED;

static void audio_status_changed_cb(audio_cap_state_t new_state, const char *message, void *ctx)
{
    (void)new_state;
    (void)ctx;
    portENTER_CRITICAL(&audio_pending_mux);
    if (audio_pending_count < AUDIO_PENDING_LEN) {
        strncpy(audio_pending[audio_pending_count].message, message, EVENT_MSG_LEN - 1);
        audio_pending[audio_pending_count].message[EVENT_MSG_LEN - 1] = '\0';
        audio_pending_count++;
    }
    portEXIT_CRITICAL(&audio_pending_mux);
}

/* ---- Voice Deck (Mission 11): WakeNet/MultiNet state, bridged from the
 * voice_control.c detect task - never the LVGL task. Same bridge shape as
 * the Audio Capture Deck above; render_voice_panel() reads
 * voice_control_get_status() for current truth (state word, last command),
 * this queue only carries discrete transitions into the Black Box. */
#define VOICE_PENDING_LEN 4

typedef struct {
    char message[EVENT_MSG_LEN];
} voice_pending_evt_t;

static voice_pending_evt_t voice_pending[VOICE_PENDING_LEN];
static int voice_pending_count = 0;
static portMUX_TYPE voice_pending_mux = portMUX_INITIALIZER_UNLOCKED;

static void voice_status_changed_cb(voice_state_t new_state, const char *message, void *ctx)
{
    (void)new_state;
    (void)ctx;
    portENTER_CRITICAL(&voice_pending_mux);
    if (voice_pending_count < VOICE_PENDING_LEN) {
        strncpy(voice_pending[voice_pending_count].message, message, EVENT_MSG_LEN - 1);
        voice_pending[voice_pending_count].message[EVENT_MSG_LEN - 1] = '\0';
        voice_pending_count++;
    }
    portEXIT_CRITICAL(&voice_pending_mux);
}

/* Cached on first use from the eFuse-programmed base MAC (esp_mac.h) rather
 * than anything Wi-Fi-related - a real, stable per-device identifier that's
 * available whether or not the station is currently connected. */
static char device_id_cache[24] = "";

static const char *get_device_id(void)
{
    if (device_id_cache[0] == '\0') {
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        snprintf(device_id_cache, sizeof(device_id_cache), "esp32-%02x%02x%02x", mac[3], mac[4], mac[5]);
    }
    return device_id_cache;
}

static uint32_t handshake_sequence = 0;

/* ---- UI widgets (set once in status_deck_ui) ------------------------- */

static lv_obj_t *telemetry_label;
static lv_obj_t *sensor_label;
static lv_obj_t *conn_label;
static lv_obj_t *hs_label;
static lv_obj_t *audio_label;
static lv_obj_t *voice_label;
static lv_obj_t *log_label;

/* Acceleration-magnitude trend chart. LVGL owns the sample buffer:
 * point_count fixes it at SENSOR_HISTORY_LEN entries and UPDATE_MODE_SHIFT
 * makes each new value push the oldest one out, so this can never grow
 * unbounded - same "bounded ring, oldest data quietly ages out" pattern
 * as event_history[]. */
#define SENSOR_HISTORY_LEN 100 /* ~10s of history at the 100ms acquisition cadence */
static lv_obj_t *chart;
static lv_chart_series_t *accel_series;

/* ---- Rendering: UI reads app_state / event_history, never the reverse - */

/* Renders only the newest EVENT_DISPLAY_LINES of event_history - the
 * recorder keeps EVENT_HISTORY_LEN, the compact panel shows fewer.
 * Builds with snprintf + a clamped running offset rather than strcat, so
 * it cannot overflow buf regardless of how long a line's text gets. */
static void render_event_log(void)
{
    char buf[EVENT_DISPLAY_LINES * (EVENT_MSG_LEN + 8)];
    size_t off = 0;
    int shown = event_history_used < EVENT_DISPLAY_LINES ? event_history_used : EVENT_DISPLAY_LINES;
    for (int i = 0; i < shown && off < sizeof(buf); i++) {
        int n = snprintf(buf + off, sizeof(buf) - off, "%s%3lus %s",
                          i == 0 ? "" : "\n",
                          (unsigned long)event_history[i].uptime_s, event_history[i].message);
        if (n > 0) {
            off += (size_t)n < sizeof(buf) - off ? (size_t)n : sizeof(buf) - off - 1;
        }
    }
    lv_label_set_text(log_label, shown ? buf : "(no events yet)");
}

/* Compact (right column of status row 1, alongside telemetry_label on the
 * left): status word shortened to CUR/STL since the text color already
 * carries the same distinction, same convention as WIFI/HS below it. */
static void render_sensor_panel(void)
{
    char buf[24];
    if (!sensor_present || sensor_status == SRC_ERROR) {
        snprintf(buf, sizeof(buf), "ACC: N/A");
        lv_obj_set_style_text_color(sensor_label, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (sensor_status == SRC_UNKNOWN) {
        snprintf(buf, sizeof(buf), "ACC: WAIT");
        lv_obj_set_style_text_color(sensor_label, lv_palette_main(LV_PALETTE_GREY), 0);
    } else {
        double age_s = (esp_timer_get_time() - sensor_last_good_us) / 1e6;
        const char *status_str = sensor_status == SRC_CURRENT ? "CUR" : "STL";
        lv_color_t color = sensor_status == SRC_CURRENT ? lv_palette_main(LV_PALETTE_GREEN)
                                                          : lv_palette_main(LV_PALETTE_ORANGE);
        snprintf(buf, sizeof(buf), "ACC %.2fg %s %.0fs", (double)last_accel_g, status_str, age_s);
        lv_obj_set_style_text_color(sensor_label, color, 0);
    }
    lv_label_set_text(sensor_label, buf);
}

static void log_sensor_event(const char *text)
{
    log_event(EVT_SENSOR, text);
}

/* Renders only current truth (state, IP, attempt/retry count) - the "how
 * did we get here" story lives in the Black Box event log via
 * wifi_ui_timer_cb below, not duplicated here. Compact (left column of
 * status row 2, alongside hs_label on the right): full IP shortened to its
 * last octet - the network prefix is implied and this is evidence the
 * device is on *some* address, not a substitute for the Black Box/serial
 * log if the exact IP ever matters. */
static void render_wifi_panel(void)
{
    wifi_mgr_status_t st;
    wifi_mgr_get_status(&st);

    /* 40, not 24: GCC's -Werror=format-truncation reasons from the
     * declared size of wifi_mgr_status_t's ip_addr[16]/reason_text[20]
     * fields, not their actual runtime contents (always short in
     * practice), so it wants a buffer sized for their theoretical worst
     * case. The on-screen text stays just as compact either way - this
     * only changes how big the stack buffer is, not what gets rendered. */
    char buf[40];
    lv_color_t color = lv_palette_main(LV_PALETTE_GREY);
    switch (st.state) {
    case WIFI_MGR_ONLINE: {
        const char *last_octet = strrchr(st.ip_addr, '.');
        snprintf(buf, sizeof(buf), "WIFI: OK *%s", last_octet ? last_octet : st.ip_addr);
        color = lv_palette_main(LV_PALETTE_GREEN);
        break;
    }
    case WIFI_MGR_CONNECTING:
        snprintf(buf, sizeof(buf), "WIFI: CONN #%lu", (unsigned long)st.attempt_count);
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case WIFI_MGR_RETRY_WAIT:
        snprintf(buf, sizeof(buf), "WIFI: %s %lus",
                 st.reason_text[0] ? st.reason_text : "RETRY", (unsigned long)st.retry_remaining_s);
        color = lv_palette_main(LV_PALETTE_RED);
        break;
    case WIFI_MGR_DISCONNECTED:
    default:
        snprintf(buf, sizeof(buf), "WIFI: OFF");
        break;
    }
    lv_label_set_text(conn_label, buf);
    lv_obj_set_style_text_color(conn_label, color, 0);
}

/* LVGL-task drain of wifi_pending, populated by wifi_status_changed_cb on
 * the foreign event-loop task. Logs each queued transition into the Black
 * Box in arrival order, then always re-renders the current-state line
 * (needed even with zero new transitions, so the RETRY_WAIT countdown
 * ticks down smoothly between transitions). */
static void wifi_ui_timer_cb(lv_timer_t *t)
{
    wifi_pending_evt_t drained[WIFI_PENDING_LEN];
    int count;

    portENTER_CRITICAL(&wifi_pending_mux);
    count = wifi_pending_count;
    if (count > 0) {
        memcpy(drained, wifi_pending, sizeof(wifi_pending_evt_t) * (size_t)count);
        wifi_pending_count = 0;
    }
    portEXIT_CRITICAL(&wifi_pending_mux);

    for (int i = 0; i < count; i++) {
        log_event(EVT_NETWORK, drained[i].message);
    }
    render_wifi_panel();
}

/* Same current-truth-only split as render_wifi_panel: "how did we get
 * here" lives in the Black Box via handshake_ui_timer_cb, not duplicated
 * here. Shows the *previous* result's event_id/status while a new request
 * is SENDING (deliberately not blanked), since "what's the last confirmed
 * evidence" stays useful evidence even mid-retry. Compact (right column of
 * status row 2, alongside conn_label on the left): event_id truncated to 6
 * chars (was 8) and wordier states (SERVER ERROR, UNREACHABLE, BAD
 * RESPONSE) shortened - same "color already carries the state" reasoning
 * as ACC's CUR/STL above. */
static void render_handshake_panel(void)
{
    handshake_status_t st;
    handshake_client_get_status(&st);

    char buf[24];
    lv_color_t color = lv_palette_main(LV_PALETTE_GREY);
    switch (st.state) {
    case HS_SENDING:
        snprintf(buf, sizeof(buf), "HS: SEND #%lu", (unsigned long)handshake_sequence);
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case HS_ACCEPTED: {
        double age_s = (esp_timer_get_time() - st.last_result_us) / 1e6;
        snprintf(buf, sizeof(buf), "HS: OK #%.6s %.0fs", st.last_event_id, age_s);
        color = lv_palette_main(LV_PALETTE_GREEN);
        break;
    }
    case HS_TIMEOUT:
        snprintf(buf, sizeof(buf), "HS: TIMEOUT");
        color = lv_palette_main(LV_PALETTE_RED);
        break;
    case HS_NETWORK_ERROR:
        snprintf(buf, sizeof(buf), "HS: UNREACH");
        color = lv_palette_main(LV_PALETTE_RED);
        break;
    case HS_SERVER_ERROR:
        snprintf(buf, sizeof(buf), "HS: ERR %d", st.last_http_status);
        color = lv_palette_main(LV_PALETTE_RED);
        break;
    case HS_BAD_RESPONSE:
        snprintf(buf, sizeof(buf), "HS: BAD RESP");
        color = lv_palette_main(LV_PALETTE_RED);
        break;
    case HS_IDLE:
    default:
        snprintf(buf, sizeof(buf), "HS: READY");
        break;
    }
    lv_label_set_text(hs_label, buf);
    lv_obj_set_style_text_color(hs_label, color, 0);
}

/* LVGL-task drain of handshake_pending, populated by
 * handshake_status_changed_cb on the handshake worker task. 300ms rather
 * than the Connection Deck's 500ms: a LAN request can complete in well
 * under a second, and SENDING-to-result feedback benefits more from a
 * quicker drain than a slow-changing Wi-Fi retry countdown does. */
static void handshake_ui_timer_cb(lv_timer_t *t)
{
    handshake_pending_evt_t drained[HANDSHAKE_PENDING_LEN];
    int count;

    portENTER_CRITICAL(&handshake_pending_mux);
    count = handshake_pending_count;
    if (count > 0) {
        memcpy(drained, handshake_pending, sizeof(handshake_pending_evt_t) * (size_t)count);
        handshake_pending_count = 0;
    }
    portEXIT_CRITICAL(&handshake_pending_mux);

    for (int i = 0; i < count; i++) {
        log_event(EVT_HANDSHAKE, drained[i].message);
    }
    render_handshake_panel();
}

/* Full-width row (unlike the half-width WIFI/HS panels) since a truthful
 * capture readout needs more than half the screen width: state word plus
 * either a live elapsed/target readout (RECORDING) or the artifact name
 * (READY) - see the Mission 10 directive's "let Mike answer: how long was
 * the capture / could the result be inspected" requirements. Progress
 * fields (elapsed_ms/bytes_captured) are read directly from
 * audio_capture_get_status() here rather than the pending queue, since they
 * are current truth that changes every render tick, not a discrete
 * transition - see the Audio Capture Deck comment above. */
static void render_audio_panel(void)
{
    audio_cap_status_t st;
    audio_capture_get_status(&st);

    char buf[40];
    lv_color_t color = lv_palette_main(LV_PALETTE_GREY);
    switch (st.state) {
    case AUDIO_CAP_ARMING:
        snprintf(buf, sizeof(buf), "AUD: ARM");
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case AUDIO_CAP_RECORDING:
        snprintf(buf, sizeof(buf), "AUD: REC %.1f/%.1fs pk=%.2f",
                 (double)st.elapsed_ms / 1000.0, (double)st.duration_target_ms / 1000.0,
                 (double)st.peak_amplitude);
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case AUDIO_CAP_COMPLETE:
        snprintf(buf, sizeof(buf), "AUD: DONE %luB pk=%.2f rms=%.2f",
                 (unsigned long)st.bytes_captured, (double)st.peak_amplitude, (double)st.rms_amplitude);
        color = lv_palette_main(LV_PALETTE_GREEN);
        break;
    case AUDIO_CAP_UPLOADING:
        /* No live progress readout here (unlike the old serial export) -
         * a Wi-Fi POST of ~128KB typically completes in well under a
         * second, so a blocking "uploading..." message is honest enough;
         * see upload_capture()'s comment in audio_capture.c. */
        snprintf(buf, sizeof(buf), "AUD: UPLOADING...");
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case AUDIO_CAP_READY:
        snprintf(buf, sizeof(buf), "AUD: RDY %s", st.artifact_name);
        color = lv_palette_main(LV_PALETTE_GREEN);
        break;
    case AUDIO_CAP_FAILED:
        snprintf(buf, sizeof(buf), "AUD: FAIL %s", st.fail_reason);
        color = lv_palette_main(LV_PALETTE_RED);
        break;
    case AUDIO_CAP_IDLE:
    default:
        snprintf(buf, sizeof(buf), "AUD: READY (REC to capture)");
        break;
    }
    lv_label_set_text(audio_label, buf);
    lv_obj_set_style_text_color(audio_label, color, 0);
}

/* LVGL-task drain of audio_pending, populated by audio_status_changed_cb on
 * the audio worker task. 200ms: faster than Handshake's 300ms because a 4s
 * RECORDING window benefits from a livelier on-screen elapsed-time readout,
 * without going so fast it becomes a per-chunk log (the worker task itself
 * still only pushes transitions here, not progress - see above). */
static void audio_ui_timer_cb(lv_timer_t *t)
{
    audio_pending_evt_t drained[AUDIO_PENDING_LEN];
    int count;

    portENTER_CRITICAL(&audio_pending_mux);
    count = audio_pending_count;
    if (count > 0) {
        memcpy(drained, audio_pending, sizeof(audio_pending_evt_t) * (size_t)count);
        audio_pending_count = 0;
    }
    portEXIT_CRITICAL(&audio_pending_mux);

    for (int i = 0; i < count; i++) {
        log_event(EVT_AUDIO, drained[i].message);
    }
    render_audio_panel();
}

/* Full-width row below AUD: current truth only (state word plus the last
 * recognized command once one exists) - "how did we get here" lives in the
 * Black Box via voice_ui_timer_cb below, same split as every other panel
 * here. DEGRADED is red and permanent for the boot (voice_control_init only
 * sets it once, on a real init failure) - manual REC keeps working
 * regardless, this row is just honest that the hands-free path isn't. */
static void render_voice_panel(void)
{
    voice_status_t st;
    voice_control_get_status(&st);

    /* 72, not 48: -Werror=format-truncation reasons from st.last_command's
     * and st.fail_reason's declared array sizes (VOICE_COMMAND_NAME_LEN/
     * VOICE_REASON_LEN, both 32), not their actual short runtime content -
     * same reasoning as audio_capture.c's hand-sized msg buffers. Worst
     * case is the DEGRADED branch below: "VOICE: DEGRADED " (16) + up to 31
     * chars of fail_reason + " (REC still works)" (19) + nul = 67. */
    char buf[72];
    lv_color_t color = lv_palette_main(LV_PALETTE_GREY);
    switch (st.state) {
    case VOICE_STATE_LISTENING:
        snprintf(buf, sizeof(buf), "VOICE: LISTENING (say \"computer\")");
        color = lv_palette_main(LV_PALETTE_BLUE);
        break;
    case VOICE_STATE_WAKE_DETECTED:
        snprintf(buf, sizeof(buf), "VOICE: WAKE DETECTED #%lu", (unsigned long)st.wake_count);
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case VOICE_STATE_COMMAND_WINDOW:
        snprintf(buf, sizeof(buf), "VOICE: COMMAND WINDOW");
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case VOICE_STATE_COMMAND_RECOGNIZED:
        snprintf(buf, sizeof(buf), "VOICE: CMD \"%s\" #%lu", st.last_command, (unsigned long)st.command_count);
        color = lv_palette_main(LV_PALETTE_GREEN);
        break;
    case VOICE_STATE_CAPTURING:
        snprintf(buf, sizeof(buf), "VOICE: CAPTURING (see AUD row)");
        color = lv_palette_main(LV_PALETTE_GREEN);
        break;
    case VOICE_STATE_TIMEOUT:
        snprintf(buf, sizeof(buf), "VOICE: TIMEOUT/UNRECOGNIZED");
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case VOICE_STATE_DEGRADED:
    default:
        snprintf(buf, sizeof(buf), "VOICE: DEGRADED %s (REC still works)", st.fail_reason);
        color = lv_palette_main(LV_PALETTE_RED);
        break;
    }
    lv_label_set_text(voice_label, buf);
    lv_obj_set_style_text_color(voice_label, color, 0);
}

/* LVGL-task drain of voice_pending, populated by voice_status_changed_cb on
 * the voice detect task. 200ms, same cadence as Audio Capture Deck - wake/
 * command transitions benefit from the same livelier feedback a 4s
 * RECORDING window does. */
static void voice_ui_timer_cb(lv_timer_t *t)
{
    voice_pending_evt_t drained[VOICE_PENDING_LEN];
    int count;

    portENTER_CRITICAL(&voice_pending_mux);
    count = voice_pending_count;
    if (count > 0) {
        memcpy(drained, voice_pending, sizeof(voice_pending_evt_t) * (size_t)count);
        voice_pending_count = 0;
    }
    portEXIT_CRITICAL(&voice_pending_mux);

    for (int i = 0; i < count; i++) {
        log_event(EVT_VOICE, drained[i].message);
    }
    render_voice_panel();
}

/* ---- Button handlers: touch -> app_state -> render + serial log ----- */

/* `significant` marks commands worth remembering across a reboot - real
 * state changes or explicit operator actions (currently NET, SND, CLR LOG:
 * all three qualify). A command that changes no persistent-worthy fact
 * would pass false here and never touch NVS - no current command does. */
static void handle_command(const char *name, bool significant)
{
    app_state.command_count++;
    strncpy(app_state.last_command, name, sizeof(app_state.last_command) - 1);
    app_state.last_command[sizeof(app_state.last_command) - 1] = '\0';

    /* No on-screen "LAST CMD" line as of Mission 09 - log_event below
     * already puts it on the Black Box panel, which is strictly more
     * informative (it's a history, not just the latest value). */
    char msg[EVENT_MSG_LEN];
    snprintf(msg, sizeof(msg), "CMD %s", name);
    log_event(EVT_COMMAND, msg);

    ESP_LOGI(TAG, "command=%s count=%lu", name, (unsigned long)app_state.command_count);

    if (significant) {
        blackbox_save_last_cmd(name);
    }
}

/* CLEAR LOG scope: clears the volatile on-screen/RAM event_history only.
 * The persisted blackbox_summary_t (boot_count, prev_reset_reason,
 * last_fault) is NOT touched - an operator should not be able to erase
 * the flight-recorder evidence with a screen tap, only the working log.
 * last_significant_cmd DOES get updated to "CLR LOG" itself, same as any
 * other significant command, since clearing the log is itself a real
 * operator action worth remembering. */
static void clear_log_button_cb(lv_event_t *e)
{
    event_history_used = 0;
    handle_command("CLR LOG", true);
}

/* Manual reconnect control (Mission 08 optional side mission). Explicit
 * semantics: always forces a fresh connection attempt, whether currently
 * offline, mid-retry-wait, or already online - see
 * wifi_mgr_request_reconnect's ONLINE-vs-not branch for how each case is
 * handled correctly against the real async Wi-Fi driver. */
static void net_button_cb(lv_event_t *e)
{
    handle_command("NET", true);
    wifi_mgr_request_reconnect();
}

/* SEND control. Ships the last ACCEL_SAMPLE_COUNT accelerometer readings
 * (whatever is actually in the ring buffer - 0 if the IMU isn't present or
 * no sample has landed yet, never fabricated) rather than a bare ping.
 * Only logs/counts as a command when handshake_client_send actually
 * accepts it - a press the in-flight guard rejects (a request is already
 * SENDING) had no effect, so it shouldn't show up in the command history
 * or Black Box as if it did. */
static void send_handshake_button_cb(lv_event_t *e)
{
    handshake_payload_t payload = { 0 };
    strncpy(payload.device_id, get_device_id(), sizeof(payload.device_id) - 1);
    strncpy(payload.event_type, "ACCEL_SAMPLES", sizeof(payload.event_type) - 1);
    strncpy(payload.mission, "MISSION09", sizeof(payload.mission) - 1);
    payload.device_uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
    payload.sequence = ++handshake_sequence;
    payload.sample_interval_ms = SENSOR_ACQUIRE_PERIOD_MS;
    payload.accel_sample_count = accel_samples_copy_ordered(payload.accel_samples_g);

    if (!handshake_client_send(&payload)) {
        handshake_sequence--; /* not actually sent - don't burn a sequence number */
        return;
    }
    handle_command("SND", true);
}

/* RECORD control (Mission 10). Only logs/counts as a command when
 * audio_capture_start() actually accepts it - same in-flight-guard
 * discipline as SND above: a press rejected because a capture is already
 * running (or the mic never came up) had no effect, so it should not show
 * up in the command history or Black Box as if it did. render_audio_panel
 * already carries the truthful state/progress story on its own row; this
 * handler only ever starts a capture, never blocks waiting for one. */
static void rec_button_cb(lv_event_t *e)
{
    if (!audio_capture_start()) {
        return;
    }
    handle_command("REC", true);
}

/* ---- Telemetry: 1 Hz timer, UI-only, no serial log spam --------------
 * Deliberately separate from the sensor timer below. Uptime is slow-
 * changing - refreshing it at the sensor's fast cadence would just make
 * the display update in a jittery, distracting way for no benefit.
 * Mission 09 dropped tick/heap/PSRAM from the display entirely (their own
 * numbers, not tied to any operator decision this console supports) to
 * make room for the WIFI/HS status grid; still available via ESP_LOGI if
 * ever needed for debugging. */

static void telemetry_timer_cb(lv_timer_t *t)
{
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    char buf[16];
    snprintf(buf, sizeof(buf), "UP %02u:%02u", (unsigned)(uptime_s / 60), (unsigned)(uptime_s % 60));
    lv_label_set_text(telemetry_label, buf);
}

/* ---- Sensor/chart: fast timer, matched to SENSOR_ACQUIRE_PERIOD_MS --- */

static void sensor_timer_cb(lv_timer_t *t)
{
    if (!sensor_present) {
        return;
    }

    portENTER_CRITICAL(&sensor_mux);
    bool got_sample = sensor_have_new_sample;
    sensor_have_new_sample = false;
    int64_t last_good = sensor_last_good_us;
    float accel_snapshot = last_accel_g;
    portEXIT_CRITICAL(&sensor_mux);

    if (got_sample) {
        lv_chart_set_next_value(chart, accel_series, (int32_t)lroundf(accel_snapshot * 100));
        accel_samples_push(accel_snapshot);
        if (sensor_status == SRC_UNKNOWN) {
            sensor_status = SRC_CURRENT;
            log_sensor_event("FIRST DATA");
        } else if (sensor_status == SRC_STALE) {
            sensor_status = SRC_CURRENT;
            log_sensor_event("SOURCE RECOVERED");
        }
    } else if (sensor_status == SRC_CURRENT) {
        int64_t age_us = esp_timer_get_time() - last_good;
        if (age_us > SENSOR_STALE_THRESHOLD_US) {
            sensor_status = SRC_STALE;
            log_sensor_event("SOURCE STALE");
        }
    }
    render_sensor_panel();
}

/* Boot-time hardware probe: bsp_sensor_init/iot_sensor_create fail
 * synchronously if the ICM42670 does not ACK on the onboard I2C bus,
 * which is the honest "not detected" signal - not something inferred
 * from later staleness. There is no runtime retry if this fails; that is
 * a known limitation for this mission, not a hidden assumption. */
static void live_data_deck_init(void)
{
    static sensor_handle_t imu_handle;
    bsp_sensor_config_t cfg = {
        .type = IMU_ID,
        .mode = MODE_POLLING,
        .period = SENSOR_ACQUIRE_PERIOD_MS,
    };

    esp_err_t err = bsp_sensor_init(&cfg, &imu_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ICM42670 not detected (err=%s)", esp_err_to_name(err));
        log_sensor_event("SENSOR INIT FAILED");
        blackbox_save_fault("SENSOR INIT FAILED");
        return;
    }

    err = iot_sensor_start(imu_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ICM42670 start failed (err=%s)", esp_err_to_name(err));
        log_sensor_event("SENSOR INIT FAILED");
        blackbox_save_fault("SENSOR INIT FAILED");
        return;
    }

    iot_sensor_handler_register(imu_handle, sensor_event_handler, NULL);
    sensor_present = true;
    log_sensor_event("SENSOR INIT");
}

/* ---- Layout ----------------------------------------------------------- */

void status_deck_ui(lv_obj_t *scr)
{
    blackbox_boot_update();

    /* Widgets don't exist yet: record only, render once they do. */
    char boot_msg[EVENT_MSG_LEN];
    snprintf(boot_msg, sizeof(boot_msg), "BOOT #%lu", (unsigned long)blackbox.boot_count);
    event_push_only(EVT_BOOT, boot_msg);
    char reset_msg[EVENT_MSG_LEN];
    snprintf(reset_msg, sizeof(reset_msg), "RST %s", reset_reason_str(esp_reset_reason()));
    event_push_only(EVT_RESET, reset_msg);
    ESP_LOGI(TAG, "boot #%lu, this-boot reset=%s, previous-boot reset=%s, last_significant_cmd=%s, last_fault=%s",
             (unsigned long)blackbox.boot_count, reset_reason_str(esp_reset_reason()),
             reset_reason_str(blackbox.prev_reset_reason), blackbox.last_significant_cmd,
             blackbox.last_fault[0] ? blackbox.last_fault : "(none)");

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "HOMEBOUND STATUS DECK");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    /* Faux-bold: LVGL's bundled Montserrat has no bold weight, so draw a
     * second copy offset by 1px to thicken the strokes. */
    lv_obj_t *title_bold = lv_label_create(scr);
    lv_label_set_text(title_bold, "HOMEBOUND STATUS DECK");
    lv_obj_set_style_text_font(title_bold, &lv_font_montserrat_20, 0);
    lv_obj_align(title_bold, LV_ALIGN_TOP_MID, 1, 2);

    /* Status grid: 2 rows x 2 columns (UP/ACC, then WIFI/HS), replacing the
     * five stacked single-line rows Mission 09 originally had. Same
     * TOP_LEFT/TOP_RIGHT pairing the old mode_label/link_label row used
     * back in Mission 05-08. All four panel functions were shortened to
     * fit a half-width column - see their comments. */
    telemetry_label = lv_label_create(scr);
    lv_obj_align(telemetry_label, LV_ALIGN_TOP_LEFT, 8, 26);

    sensor_label = lv_label_create(scr);
    lv_obj_align(sensor_label, LV_ALIGN_TOP_RIGHT, -8, 26);

    conn_label = lv_label_create(scr);
    lv_obj_align(conn_label, LV_ALIGN_TOP_LEFT, 8, 44);
    lv_label_set_text(conn_label, "WIFI: OFF");

    hs_label = lv_label_create(scr);
    lv_obj_align(hs_label, LV_ALIGN_TOP_RIGHT, -8, 44);
    lv_label_set_text(hs_label, "HS: READY");

    /* Audio Capture Deck (Mission 10): a third, full-width status row below
     * WIFI/HS - a truthful capture readout needs more than a half-width
     * column (state word plus elapsed/target or artifact name). The 16px
     * this needed came out of the chart below (64px -> 48px), not the log
     * panel or button row - see the chart comment just below. */
    audio_label = lv_label_create(scr);
    lv_obj_align(audio_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_label_set_text(audio_label, "AUD: READY (REC to capture)");

    /* Voice Deck (Mission 11): a fourth full-width status row below AUD -
     * same reasoning as AUD's own comment above, the WakeNet/MultiNet state
     * word plus a recognized command needs more than a half-width column.
     * The 16px this needed came out of the chart below (48px -> 32px), same
     * "borrow from the chart, not the log or button row" precedent Mission
     * 10 set. */
    voice_label = lv_label_create(scr);
    lv_obj_align(voice_label, LV_ALIGN_TOP_MID, 0, 76);
    lv_label_set_text(voice_label, "VOICE: LISTENING (say \"computer\")");

    /* Acceleration-magnitude trend line, zoomed to 0.50-1.50g (was a fixed
     * 0.00-4.00g). At rest the board reads ~1.00g regardless of
     * orientation (gravity) and real movement/shaking is usually a modest
     * excursion around that, not a swing across several g - the old 4g-
     * wide range buried that movement in a few flat-looking pixels. This
     * range is still fixed/static, not autoscaling, so a genuinely hard
     * shake can still peg or clip at an edge - a known, accepted limit of
     * a static range, same tradeoff as before, just recentered on the
     * value that actually matters. Height cut ~20% (80px -> 64px) in
     * Mission 09, another 16px (64px -> 48px) in Mission 10 for the AUD row,
     * and another 16px (48px -> 32px) in Mission 11 for the new VOICE row -
     * the chart is still full width and still shows the same ~10s of
     * history, just visually shorter each time. Bottom edge (y+height)
     * stays 124px in all three missions, so the log panel below never had
     * to move. */
    chart = lv_chart_create(scr);
    lv_obj_set_size(chart, 304, 32);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 92);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, SENSOR_HISTORY_LEN);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 50, 150);
    accel_series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    /* No "EVENT LOG" title label inside the panel (Mission 05 had one) -
     * the panel's content is self-evident without it. Shows only
     * EVENT_DISPLAY_LINES of the EVENT_HISTORY_LEN records the black box
     * actually retains. Grown from 32px/2 lines to 48px/3 lines with the
     * height the chart gave up in Mission 09 - actually uses the space for
     * more visible history, not just a bigger empty box. Y shifted up 2px
     * (130 -> 128) in Mission 10 to track the chart's new bottom edge. */
    lv_obj_t *log_panel = lv_obj_create(scr);
    lv_obj_set_size(log_panel, 304, 48);
    lv_obj_align(log_panel, LV_ALIGN_TOP_MID, 0, 128);
    lv_obj_set_style_pad_all(log_panel, 4, 0);
    lv_obj_clear_flag(log_panel, LV_OBJ_FLAG_SCROLLABLE);

    log_label = lv_label_create(log_panel);
    lv_obj_align(log_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *btn_row = lv_obj_create(scr);
    lv_obj_set_size(btn_row, 304, 56);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_pad_all(btn_row, 2, 0);
    lv_obj_set_style_pad_column(btn_row, 4, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const struct {
        const char *label;
        lv_event_cb_t cb;
    } buttons[] = {
        { "NET", net_button_cb },
        { "SND", send_handshake_button_cb },
        { "REC", rec_button_cb },
        { "CLR", clear_log_button_cb },
    };

    /* 4 buttons as of Mission 10 (NET/SND/REC/CLR) - width dropped from 90px
     * to 68px so all four still fit the same 304px row (4*68 = 272px, the
     * remaining 32px becomes gaps via LV_FLEX_ALIGN_SPACE_EVENLY, same as
     * the 3-button layout before it). */
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        lv_obj_t *btn = lv_btn_create(btn_row);
        lv_obj_set_size(btn, 68, 48);
        lv_obj_add_event_cb(btn, buttons[i].cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, buttons[i].label);
        lv_obj_center(btn_label);
    }

    render_event_log();

    live_data_deck_init();
    render_sensor_panel();

    render_wifi_panel();
    wifi_mgr_init(wifi_status_changed_cb, NULL);

    render_handshake_panel();
    handshake_client_init(handshake_status_changed_cb, NULL);

    /* Mission 11: exactly one esp_codec_dev_handle_t for the ES7210 mic,
     * created here and shared by audio_capture (bounded 4s capture) and
     * voice_control (continuous WakeNet/MultiNet listening) - see both
     * modules' Mission 11 comments for why a second
     * bsp_audio_codec_microphone_init() call would create a second handle
     * fighting the first over the same physical codec. NULL is a real,
     * handled outcome for both (audio_capture logs MIC INIT FAILED and
     * audio_capture_start() no-ops forever; voice_control reports
     * VOICE_STATE_DEGRADED), not an unchecked assumption. */
    esp_codec_dev_handle_t mic_dev = bsp_audio_codec_microphone_init();
    if (!mic_dev) {
        ESP_LOGE(TAG, "bsp_audio_codec_microphone_init failed - mic unavailable this boot");
    }

    render_audio_panel();
    audio_capture_init(mic_dev, audio_status_changed_cb, NULL);

    render_voice_panel();
    voice_control_init(mic_dev, voice_status_changed_cb, NULL);

    lv_timer_create(telemetry_timer_cb, 1000, NULL);
    lv_timer_create(sensor_timer_cb, SENSOR_ACQUIRE_PERIOD_MS, NULL);
    /* 500ms: connection state changes on human/network timescales (seconds),
     * not the IMU's 100ms cadence - fast enough for a readable RETRY_WAIT
     * countdown without a dedicated fast timer. */
    lv_timer_create(wifi_ui_timer_cb, 500, NULL);
    lv_timer_create(handshake_ui_timer_cb, 300, NULL);
    /* 200ms: see the audio_ui_timer_cb comment above for why this is
     * faster than Handshake's 300ms. */
    lv_timer_create(audio_ui_timer_cb, 200, NULL);
    /* Same 200ms cadence as Audio Capture Deck - see voice_ui_timer_cb's
     * comment. */
    lv_timer_create(voice_ui_timer_cb, 200, NULL);
}
