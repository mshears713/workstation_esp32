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
 *          Mission 14 splits the single crowded dashboard into three pages
 *          behind a bottom nav bar (HOME/SENS/LOG), each a plain lv_obj_t
 *          container toggled via LV_OBJ_FLAG_HIDDEN - the same show/hide
 *          pattern the Command Window/recording/GO overlays already used, just
 *          for mutually-exclusive pages instead of transient overlays. HOME
 *          keeps the title and the UP/WIFI/HS/AUD/VOICE rows plus NET/SND;
 *          SENS holds the ACC reading and its trend chart; LOG holds the
 *          Black Box panel plus CLR. The overlays are unaffected - they
 *          stay direct children of `scr`, full-screen, so they still cover
 *          the nav bar exactly as before. REC is retired: the same manual
 *          capture is now reached by pressing TALK (a new nav-bar button),
 *          which fakes a wake-word detection via
 *          voice_control_manual_wake() so the real MultiNet command window
 *          opens without actually saying "computer" - see that function's
 *          comment in voice_control.c. Unlike REC, TALK has no effect while
 *          VOICE_STATE_DEGRADED (there is no detect_task running to consume
 *          the manual-wake flag), so a voice-pipeline failure now also
 *          removes the only path to a capture - a real, accepted tradeoff
 *          of dropping REC, not an oversight.
 *          Mission 15 reworks all three pages. HOME: UP and WIFI merge onto
 *          one row (HS drops entirely - no more on-screen handshake status,
 *          though handshake transitions still reach the Black Box), and
 *          NET/SND move to LOG, next to CLR. The title becomes "OPERATION
 *          HOMEBOUND" in blue. SENS drops the old numeric ACC readout
 *          (sensor_label) and becomes three stacked trend charts, each a
 *          third of the page: ACCEL (unchanged data), plus new TEMP and
 *          HUMIDITY charts against the AHT30 on the SENSOR dock
 *          (HUMITURE_ID) - the path Mission 06's comment named but never
 *          wired up, brought in now with the same boot-probe-only,
 *          no-runtime-retry contract as the IMU (see the Environment Deck
 *          comment). LVGL 9's lv_chart has no numeric axis-tick-label API
 *          (removed from v8), so each chart gets a one-line caption instead
 *          stating its unit and fixed range. LOG's Black Box panel grows to
 *          fill nearly the whole page (112px, was 48px) and now shows all
 *          EVENT_HISTORY_LEN records instead of a truncated 3. The LVGL
 *          built-in FPS/CPU overlay (CONFIG_LV_USE_PERF_MONITOR), previously
 *          always on screen regardless of page, is now explicitly shown only
 *          while LOG is active (see set_active_page()'s lv_sysmon_show/
 *          hide_performance calls).
 *          Mission 16 replaces HOME's dynamic VOICE status line with a
 *          VoiceListeningWidget (voice_listening_widget.c/.h): a rotating
 *          blue ring with a glisten highlight traveling around it, a white
 *          mic icon at its center, and a static "VOICE LISTENING" caption -
 *          purely decorative, always animating, no longer a live readout of
 *          voice_control_get_status() (the Command Window/recording/GO overlays
 *          and the Black Box still carry that). See render_command_overlay's
 *          neighboring comment for what was dropped along with it.
 *          Mission 17 clears the rest of HOME's center for that widget: the
 *          AUD status row above it is dropped (same on-screen-vs-Black-Box
 *          tradeoff as Mission 16's VOICE row - see audio_ui_timer_cb's
 *          neighboring comment) and the widget's own caption is dropped too
 *          (see voice_listening_widget.h). The ring grows from 76px to
 *          130px to fill the freed space instead of leaving it empty.
 *          Mission 18 adds detail to that same widget without touching
 *          HOME's layout otherwise: a thin static inner ring, a subtle
 *          halo, and a navy disc behind the mic icon (see
 *          voice_listening_widget.c for the full layer breakdown), plus a
 *          16px nudge upward (y=44 -> y=28) for more clearance above the
 *          nav bar - see this file's own comment at that alignment call for
 *          why that's safe despite the UP/WIFI row occupying an overlapping
 *          y-range.
 *          Mission 19 touches three unrelated things in one pass. HOME:
 *          the VoiceListeningWidget keeps its y=28 top (already right) but
 *          grows 130px -> 148px so its bottom now sits just above the nav
 *          bar instead of leaving a gap. SENS: TEMP's axis moves from
 *          0-50 Celsius to 60-90 Fahrenheit (a realistic indoor band,
 *          converted from the AHT30's native Celsius reading - see
 *          humiture_timer_cb), and all three charts gain a live numeric
 *          value label to their left (*_value_label - narrowed each chart
 *          from 304px to 250px to make room). LOG: the built-in FPS/CPU
 *          readout (CONFIG_LV_USE_PERF_MONITOR) moves off its LVGL-default
 *          fixed bottom-right corner of the whole display (which landed in
 *          the nav bar's band, not this page's own button row) into the
 *          NET/SND/CLR row itself - see that row's comment in
 *          status_deck_ui() and the lv_display_private.h include above for
 *          how and why.
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
#include "voice_listening_widget.h"
#include "notification_client.h"
#include "remote_client.h"
#include "audio_playback.h"
#include "project_selector.h"
/* Mission 19: lv_sysmon's public API (lv_sysmon_show/hide_performance) has
 * no accessor for the FPS/CPU label object itself, only show/hide - moving
 * it into the LOG page's button row (see status_deck_ui()) needs the actual
 * lv_obj_t*, which only exists on lv_display_t's private struct. This
 * header is "private" by LVGL's own naming convention, not by build-system
 * access control - the lvgl component's INCLUDE_DIRS exposes all of src/,
 * private headers included, to every other component in this project, same
 * as every other lvgl header used above. Reaching in here is a real,
 * accepted fragility (a future LVGL upgrade could rename/restructure this
 * field), not an oversight - see the reparenting comment in
 * status_deck_ui() for why it was worth it anyway. */
#include "display/lv_display_private.h"

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
 * Capacity is EVENT_HISTORY_LEN; the LOG page's panel renders
 * EVENT_DISPLAY_LINES of it, which as of Mission 15 is the same number -
 * see EVENT_DISPLAY_LINES's own comment for why that panel no longer needs
 * to show fewer than the recorder keeps. */

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
/* Mission 15: the LOG page now gives the log panel most of the screen (see
 * status_deck_ui()), so the compact panel no longer has to show fewer lines
 * than the recorder keeps - this now equals EVENT_HISTORY_LEN, i.e. every
 * retained record is visible at once. */
#define EVENT_DISPLAY_LINES EVENT_HISTORY_LEN

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

/* ---- Environment Deck (Mission 15): temperature/humidity from the AHT30
 * on the SENSOR dock (HUMITURE_ID - see the Live Data Deck comment above,
 * which named this path back in Mission 06 but never wired it up). Same
 * boot-time-probe-only, no-runtime-retry contract and the same
 * record-on-a-foreign-task/render-on-the-LVGL-task split as the IMU above,
 * just on a HUMITURE_ACQUIRE_PERIOD_MS cadence instead of the IMU's fast
 * 100ms one - temperature/humidity simply don't move within a second the
 * way acceleration can. Temperature and humidity share one status/staleness
 * pair rather than two: the AHT30 driver samples both in the same physical
 * read, so iot_sensor_hub posting them as two separate events is a hub
 * convention, not evidence of two independent freshness stories. */
#define HUMITURE_ACQUIRE_PERIOD_MS 1000
#define HUMITURE_STALE_THRESHOLD_US ((int64_t)3 * HUMITURE_ACQUIRE_PERIOD_MS * 1000)
#define HUMITURE_HISTORY_LEN 60 /* ~60s of history at the 1000ms acquisition cadence */

static portMUX_TYPE humiture_mux = portMUX_INITIALIZER_UNLOCKED;
static bool humiture_present = false;        /* hardware detected at boot */
static source_status_t humiture_status = SRC_UNKNOWN;
static volatile bool temp_have_new_sample = false;
static volatile bool humi_have_new_sample = false;
static float last_temperature_c = 0.0f;
static float last_humidity_pct = 0.0f;
static int64_t humiture_last_good_us = 0;

/* Runs on the sensor_hub task. No LVGL calls here - same contract as
 * sensor_event_handler above. */
static void humiture_event_handler(void *arg, sensor_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    sensor_data_t *data = (sensor_data_t *)event_data;
    portENTER_CRITICAL(&humiture_mux);
    if (event_id == SENSOR_TEMP_DATA_READY) {
        last_temperature_c = data->temperature;
        temp_have_new_sample = true;
        humiture_last_good_us = esp_timer_get_time();
    } else if (event_id == SENSOR_HUMI_DATA_READY) {
        last_humidity_pct = data->humidity;
        humi_have_new_sample = true;
        humiture_last_good_us = esp_timer_get_time();
    }
    portEXIT_CRITICAL(&humiture_mux);
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
 * unrelated foreign task, unrelated timing. Only discrete transitions
 * (ARM/START/COMPLETE/FAILED/READY) go through this queue to become Black
 * Box events - fast-changing progress (elapsed_ms/bytes_captured) while
 * RECORDING has no on-screen reader as of Mission 17 (see audio_ui_timer_cb
 * below), but render_recording_overlay() still reads it directly from
 * audio_capture_get_status() for the recording overlay's live timer. */
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
 * the Audio Capture Deck above; render_command_overlay/render_recording_overlay
 * below read voice_control_get_status() directly for current truth (state
 * word, last command), this queue only carries discrete transitions into
 * the Black Box. */
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
static lv_obj_t *conn_label;
static lv_obj_t *log_label;

/* Mission 14: three pages behind a bottom nav bar, replacing the single
 * crowded dashboard - see the file header comment. Each page is a plain
 * lv_obj_t container, shown/hidden as a group via set_active_page() below;
 * nav_buttons[] holds the three tab buttons (HOME/SENS/LOG only - TALK is a
 * one-shot action, not a tab, so it isn't highlighted). */
typedef enum {
    APP_PAGE_HOME = 0,
    APP_PAGE_SENS,
    APP_PAGE_LOG,
    APP_PAGE_COUNT,
} app_page_t;

static lv_obj_t *page_home;
static lv_obj_t *page_sens;
static lv_obj_t *page_log;
static lv_obj_t *nav_buttons[APP_PAGE_COUNT];

/* Command Window overlay (Mission 12): four-button recognition-test grid,
 * hidden by default, shown full-screen over the dashboard for the ~10s
 * MultiNet command window - see render_command_overlay() below. */
static lv_obj_t *cmd_overlay;
static lv_obj_t *cmd_status_label;
static lv_obj_t *cmd_buttons[VOICE_COMMAND_COUNT];

/* Voice-triggered recording overlay (Mission 13): shared by SEND and NOTE -
 * mic ownership is exclusive, so only one can ever be mid-recording at a
 * time - see render_recording_overlay() below. */
static lv_obj_t *recording_overlay;
static lv_obj_t *recording_title;
static lv_obj_t *recording_id_label;
static lv_obj_t *recording_dot;
static lv_obj_t *recording_status_label;

/* Notification-playback overlay: same shape as the recording overlay
 * (reads voice_status_t.last_result directly - run_notification_command()
 * blocks for the whole fetch+play+ack cycle) plus a STOP button, since
 * playback runs long enough (5-20s) to be worth interrupting - see
 * render_notification_overlay() below. */
static lv_obj_t *notification_overlay;
static lv_obj_t *notification_id_label;
static lv_obj_t *notification_status_label;

/* HOME page's animated listening ring - kept as a file-scope handle (not
 * just a local in status_deck_ui()) so voice_ui_timer_cb can recolor it via
 * voice_listening_widget_set_notification() every tick. */
static lv_obj_t *voice_widget;

/* Notification-playback volume control (small up/down buttons flanking a
 * numeric readout, to the right of the listening ring) - see
 * volume_up_button_cb/volume_down_button_cb below. */
static lv_obj_t *volume_label;

/* Project selector (VAN1/VAN2/GEN/NONE), to the left of the listening ring -
 * a scroller matching the volume control's own up/label/down shape (current
 * selection in the center, arrows above/below step through the four
 * options) rather than four separate buttons - see
 * project_up_button_cb/project_down_button_cb below. */
static lv_obj_t *project_label;

/* SENS page (Mission 15): three stacked trend charts - acceleration
 * (unchanged from Mission 06), temperature and humidity (new, see the
 * Environment Deck comment above). All three are LVGL-owned ring buffers:
 * point_count fixes each at its history length and UPDATE_MODE_SHIFT pushes
 * the oldest value out on every new one, same "bounded ring" pattern as
 * event_history[]. No numeric tick marks - LVGL 9's lv_chart dropped the v8
 * axis-tick-label API, and a companion lv_scale widget doesn't fit next to
 * a ~40px-tall chart at this display width - so each chart gets a one-line
 * caption above it instead, stating the unit and the fixed axis range.
 * Mission 19 adds a live numeric readout to the left of each chart
 * (*_value_label) - unlike the caption, this updates every acquisition
 * tick from the same snapshot already used to push the chart's next point,
 * in sensor_timer_cb/humiture_timer_cb. */
#define SENSOR_HISTORY_LEN 100 /* ~10s of history at the 100ms acquisition cadence */
static lv_obj_t *chart;
static lv_chart_series_t *accel_series;
static lv_obj_t *accel_value_label;

static lv_obj_t *temp_chart;
static lv_chart_series_t *temp_series;
static lv_obj_t *temp_value_label;
static lv_obj_t *humi_chart;
static lv_chart_series_t *humi_series;
static lv_obj_t *humi_value_label;

/* ---- Rendering: UI reads app_state / event_history, never the reverse - */

/* Renders the newest EVENT_DISPLAY_LINES of event_history - as of Mission
 * 15 that's all of EVENT_HISTORY_LEN, so this shows the recorder's full
 * retained history, not a truncated view of it. Builds with snprintf + a
 * clamped running offset rather than strcat, so it cannot overflow buf
 * regardless of how long a line's text gets. */
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

static void log_sensor_event(const char *text)
{
    log_event(EVT_SENSOR, text);
}

/* Renders only current truth (state, IP, attempt/retry count) - the "how
 * did we get here" story lives in the Black Box event log via
 * wifi_ui_timer_cb below, not duplicated here. Compact (right column of the
 * HOME page's top row, alongside telemetry_label on the left as of Mission
 * 15): full IP shortened to its last octet - the network prefix is implied
 * and this is evidence the device is on *some* address, not a substitute
 * for the Black Box/serial log if the exact IP ever matters. */
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

/* Mission 15: no more on-screen HS status label (dropped from HOME along
 * with hs_label - see the file header comment) - handshake transitions
 * still reach the operator, just only through the Black Box/LOG page now,
 * via the drain loop below. */

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
}

/* Mission 17: no more on-screen AUD status label (dropped from HOME to give
 * the VoiceListeningWidget most of the page - see the file header comment)
 * - same tradeoff already made for ACC/HS/VOICE's old text rows: capture
 * transitions still reach the operator, just only through the Black
 * Box/LOG page now, via the drain loop below. */

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
}

/* Mission 16: no more on-screen dynamic VOICE status label - the HOME page
 * center now shows a VoiceListeningWidget instead (a decorative, always-
 * animating ring; see voice_listening_widget.c), not a text readout of
 * voice_control_get_status(). Voice transitions still reach the operator:
 * the Command Window/recording/GO overlays already render their own state
 * directly from voice_control_get_status() independent of this, and every
 * transition still lands in the Black Box via the drain loop in
 * voice_ui_timer_cb below. The one thing genuinely lost is on-screen
 * DEGRADED visibility when no overlay is showing (previously this row's red
 * text) - that state is now only visible via the Black Box/LOG page. */

/* Four Mission 13 command words (Mission 12 tested six; RUN/TEST didn't
 * read reliably and were dropped), in on-screen grid order (2 columns x 2
 * rows: SEND/NOTE, GO/YES). cmd_buttons[i] is built from cmd_defs[i] in
 * this same order, so render_command_overlay() below can find "the button
 * for id X" by scanning this array rather than assuming id-1 == index
 * (keeps the two decoupled in case a future pass reorders the grid). */
static const struct {
    voice_command_id_t id;
    const char *label;
} cmd_defs[VOICE_COMMAND_COUNT] = {
    { VOICE_CMD_SEND, "SEND" },
    { VOICE_CMD_NOTE, "NOTE" },
    { VOICE_CMD_GO,   "GO"   },
    { VOICE_CMD_YES,  "YES"  },
};

#define CMD_BTN_INACTIVE_BG lv_palette_darken(LV_PALETTE_GREY, 2)
#define CMD_BTN_ACTIVE_BG   lv_palette_main(LV_PALETTE_GREEN)

/* Command Window overlay (Mission 12): shown full-screen the instant a
 * command window opens (or is about to - WAKE_DETECTED is included so
 * there is no one-tick flash of the plain dashboard between the wake word
 * and the button grid appearing), hidden the instant the console is back
 * to plain LISTENING. Countdown/status text and per-button highlight are
 * both derived from voice_control_get_status() current truth, same
 * current-truth-only split every other render_*_panel function here uses -
 * "how did we get here" is the Black Box's job, not this overlay's. Driven
 * off the same 200ms voice_ui_timer_cb as render_recording_overlay/
 * render_notification_overlay below, so all stay in lockstep. */
static void render_command_overlay(void)
{
    voice_status_t st;
    voice_control_get_status(&st);

    bool show = true;
    char status_buf[40];
    lv_color_t status_color = lv_palette_main(LV_PALETTE_BLUE);

    switch (st.state) {
    case VOICE_STATE_WAKE_DETECTED:
    case VOICE_STATE_COMMAND_WINDOW: {
        int64_t remain_us = st.command_window_deadline_us - esp_timer_get_time();
        int remain_s = remain_us > 0 ? (int)((remain_us + 999999) / 1000000) : 0;
        snprintf(status_buf, sizeof(status_buf), "Listening... %d", remain_s);
        status_color = lv_palette_main(LV_PALETTE_BLUE);
        break;
    }
    case VOICE_STATE_COMMAND_RECOGNIZED:
        snprintf(status_buf, sizeof(status_buf), "%s", st.last_command);
        status_color = lv_palette_main(LV_PALETTE_GREEN);
        break;
    case VOICE_STATE_TIMEOUT:
        snprintf(status_buf, sizeof(status_buf), "TIMEOUT");
        status_color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case VOICE_STATE_UNRECOGNIZED:
        snprintf(status_buf, sizeof(status_buf), "UNRECOGNIZED");
        status_color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    default:
        show = false;
        status_buf[0] = '\0';
        break;
    }

    if (show) {
        lv_obj_clear_flag(cmd_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(cmd_overlay);
    } else {
        lv_obj_add_flag(cmd_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(cmd_status_label, status_buf);
    lv_obj_set_style_text_color(cmd_status_label, status_color, 0);

    for (int i = 0; i < VOICE_COMMAND_COUNT; i++) {
        bool highlighted = (st.state == VOICE_STATE_COMMAND_RECOGNIZED) && (cmd_defs[i].id == st.last_command_id);
        lv_obj_set_style_bg_color(cmd_buttons[i], highlighted ? CMD_BTN_ACTIVE_BG : CMD_BTN_INACTIVE_BG, 0);
    }
}

/* Voice-triggered recording overlay (Mission 13, generalized when NOTE
 * gained its own recording flow): visible for the whole SEND or NOTE cycle
 * (voice_control's VOICE_STATE_SEND_ACTIVE / VOICE_STATE_NOTE_ACTIVE /
 * VOICE_STATE_GRAPH_ACTIVE cover recording through the upload attempt and
 * its brief result hold - mic ownership is exclusive, so the three states
 * never overlap, and one shared overlay covers all three without losing
 * any information. GRAPH_ACTIVE joined SEND/NOTE here once GO became a
 * recording command like them - see run_go_command()'s comment in
 * voice_control.c). The recording/upload detail line is deliberately read
 * from audio_capture_get_status() directly, not from anything
 * voice_control.c stores - same current-truth-only split every other
 * render_*_panel here uses, and (as of Mission 17) it is the only
 * on-screen place that shows live capture progress at all, full-screen
 * with a STOP button. Which command triggered it (vst.last_command,
 * "SEND"/"NOTE"/"GO") drives the title and result text so none of the
 * three is mislabeled as another. */
static void render_recording_overlay(void)
{
    voice_status_t vst;
    voice_control_get_status(&vst);

    bool show = (vst.state == VOICE_STATE_SEND_ACTIVE || vst.state == VOICE_STATE_NOTE_ACTIVE ||
                 vst.state == VOICE_STATE_GRAPH_ACTIVE);
    if (show) {
        lv_obj_clear_flag(recording_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(recording_overlay);
    } else {
        lv_obj_add_flag(recording_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* vst.last_command is a VOICE_COMMAND_NAME_LEN (32-byte) field - GCC's
     * -Wformat-truncation sizes buffers against that declared capacity, not
     * the short "SEND"/"NOTE"/"GO"/"YES" values it actually ever holds, so
     * these need enough room for the worst case the compiler can see, not
     * just the worst case that can happen at runtime. */
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "RECORDING %s", vst.last_command);
    lv_label_set_text(recording_title, title_buf);

    char id_buf[40];
    snprintf(id_buf, sizeof(id_buf), "ID: %s", vst.active_request_id);
    lv_label_set_text(recording_id_label, id_buf);

    audio_cap_status_t ast;
    audio_capture_get_status(&ast);

    char status_buf[64]; /* see title_buf's comment above on why this must exceed 56 */
    lv_color_t color = lv_palette_main(LV_PALETTE_BLUE);
    bool recording = false;
    switch (ast.state) {
    case AUDIO_CAP_ARMING:
        snprintf(status_buf, sizeof(status_buf), "ARMING...");
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case AUDIO_CAP_RECORDING: {
        uint32_t s = ast.elapsed_ms / 1000;
        snprintf(status_buf, sizeof(status_buf), "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
        color = lv_palette_main(LV_PALETTE_RED);
        recording = true;
        break;
    }
    case AUDIO_CAP_COMPLETE:
    case AUDIO_CAP_UPLOADING:
        snprintf(status_buf, sizeof(status_buf), "UPLOADING...");
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case AUDIO_CAP_READY:
        snprintf(status_buf, sizeof(status_buf), "%s SENT", vst.last_command);
        color = lv_palette_main(LV_PALETTE_GREEN);
        break;
    case AUDIO_CAP_FAILED:
        if (strcmp(ast.fail_reason, "ENDPOINT NOT SET") == 0) {
            snprintf(status_buf, sizeof(status_buf), "%s READY - ENDPOINT NOT SET", vst.last_command);
        } else {
            snprintf(status_buf, sizeof(status_buf), "UPLOAD FAILED: %s", ast.fail_reason);
        }
        color = lv_palette_main(LV_PALETTE_ORANGE);
        break;
    case AUDIO_CAP_IDLE:
    default:
        /* Brief window right after VOICE_STATE_SEND_ACTIVE/NOTE_ACTIVE is
         * set but before audio_capture's worker task has picked up the
         * request yet, or right after the result hold ends but before
         * voice_control.c returns to LISTENING. */
        snprintf(status_buf, sizeof(status_buf), "STARTING...");
        break;
    }
    lv_label_set_text(recording_status_label, status_buf);
    lv_obj_set_style_text_color(recording_status_label, color, 0);

    if (recording) {
        lv_obj_clear_flag(recording_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(recording_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Notification-playback overlay - see the static globals' comment above.
 * Same current-truth-from-voice_status_t reasoning as render_recording_overlay:
 * run_notification_command() blocks for the whole fetch+play+ack cycle. */
static void render_notification_overlay(void)
{
    voice_status_t vst;
    voice_control_get_status(&vst);

    if (vst.state != VOICE_STATE_NOTIFICATION_ACTIVE) {
        lv_obj_add_flag(notification_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(notification_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(notification_overlay);

    char id_buf[40];
    snprintf(id_buf, sizeof(id_buf), "ID: %s", vst.active_request_id);
    lv_label_set_text(notification_id_label, id_buf);

    lv_color_t color = lv_palette_main(LV_PALETTE_ORANGE);
    if (strcmp(vst.last_result, "NOTIFICATION DELIVERED") == 0) {
        color = lv_palette_main(LV_PALETTE_GREEN);
    } else if (strcmp(vst.last_result, "NOTIFICATION FETCHING") == 0 ||
               strcmp(vst.last_result, "FETCHING") == 0 ||
               strcmp(vst.last_result, "PLAYING") == 0) {
        color = lv_palette_main(LV_PALETTE_BLUE);
    } else if (strncmp(vst.last_result, "NOTIFICATION FETCH FAILED", 26) == 0 ||
               strcmp(vst.last_result, "NOTIFICATION PLAYBACK STOPPED") == 0 ||
               strcmp(vst.last_result, "NOTIFICATION PLAYED - ACK FAILED") == 0) {
        color = lv_palette_main(LV_PALETTE_RED);
    }
    lv_label_set_text(notification_status_label, vst.last_result);
    lv_obj_set_style_text_color(notification_status_label, color, 0);
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
    render_command_overlay();
    render_recording_overlay();
    render_notification_overlay();

    /* Cheap thread-safe struct read, not a network call - the background
     * poll task in notification_client.c is what actually talks to the
     * backend (see its own interval). Just recolors the ring; never reads
     * anything aloud on its own, per the directive. */
    notification_status_t nst;
    notification_client_get_status(&nst);
    voice_listening_widget_set_notification(voice_widget, nst.pending);
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

/* Notification-playback volume (Mission 20 side control). 10-point steps -
 * coarse enough that a single tap is felt, fine enough that the full 0-100
 * range takes a reasonable number of taps; a fixed choice, not a
 * measured/tested one, same as PLAYBACK_VOLUME_DEFAULT itself
 * (audio_playback.c). Refreshes volume_label immediately so the readout
 * never lags behind what audio_playback_get_volume() would report. */
#define VOLUME_STEP 10

static void refresh_volume_label(void)
{
    lv_label_set_text_fmt(volume_label, "%d", audio_playback_get_volume());
}

static void volume_up_button_cb(lv_event_t *e)
{
    audio_playback_set_volume(audio_playback_get_volume() + VOLUME_STEP);
    refresh_volume_label();
}

static void volume_down_button_cb(lv_event_t *e)
{
    audio_playback_set_volume(audio_playback_get_volume() - VOLUME_STEP);
    refresh_volume_label();
}

/* Project selector (Mission 20 side control) - see project_selector.h.
 * PROJECT_OPTIONS fixes both the cycle order and each option's on-screen
 * text in one place, so project_up_button_cb/project_down_button_cb and
 * refresh_project_label() never have to be kept in sync by hand. */
typedef struct {
    project_selection_t value;
    const char *text;
} project_option_t;

static const project_option_t PROJECT_OPTIONS[] = {
    { PROJECT_SELECTION_VAN1, "VAN1" },
    { PROJECT_SELECTION_VAN2, "VAN2" },
    { PROJECT_SELECTION_GENERAL, "GEN" },
    { PROJECT_SELECTION_NONE, "NONE" },
};
#define PROJECT_OPTION_COUNT (sizeof(PROJECT_OPTIONS) / sizeof(PROJECT_OPTIONS[0]))

static int project_option_index(project_selection_t sel)
{
    for (size_t i = 0; i < PROJECT_OPTION_COUNT; i++) {
        if (PROJECT_OPTIONS[i].value == sel) {
            return (int)i;
        }
    }
    return 0;
}

static void refresh_project_label(void)
{
    int idx = project_option_index(project_selector_get());
    lv_label_set_text(project_label, PROJECT_OPTIONS[idx].text);
}

static void project_up_button_cb(lv_event_t *e)
{
    int idx = project_option_index(project_selector_get());
    idx = (idx + 1) % (int)PROJECT_OPTION_COUNT;
    project_selector_set(PROJECT_OPTIONS[idx].value);
    refresh_project_label();
}

static void project_down_button_cb(lv_event_t *e)
{
    int idx = project_option_index(project_selector_get());
    idx = (idx - 1 + (int)PROJECT_OPTION_COUNT) % (int)PROJECT_OPTION_COUNT;
    project_selector_set(PROJECT_OPTIONS[idx].value);
    refresh_project_label();
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

/* ---- Nav bar (Mission 14): HOME/SENS/LOG page switching + TALK -------
 * set_active_page shows the requested page's container and hides the other
 * two, and re-colors nav_buttons[] so the current tab stays visibly
 * highlighted - same "current truth, no history" spirit as the render_*
 * panels above, just for which page is on screen rather than a data value. */
static void set_active_page(app_page_t page)
{
    lv_obj_t *pages[APP_PAGE_COUNT] = { page_home, page_sens, page_log };
    for (int i = 0; i < APP_PAGE_COUNT; i++) {
        if (i == (int)page) {
            lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_bg_color(nav_buttons[i], i == (int)page ? lv_palette_main(LV_PALETTE_BLUE)
                                                                   : lv_palette_main(LV_PALETTE_GREY), 0);
    }

    /* Mission 15: the LVGL-builtin FPS/CPU readout (CONFIG_LV_USE_PERF_
     * MONITOR) is normally a permanent overlay on the display's system
     * layer, independent of which screen/page is active - lv_display_create
     * turns it on unconditionally at boot, before this file ever runs. To
     * make it "on the LOG page" specifically, explicitly show/hide it here
     * on every page switch rather than leaving LVGL's own always-on
     * behavior in place. Mission 19 additionally reparents the label itself
     * into LOG's own NET/SND/CLR row (see status_deck_ui()) - this call
     * still just toggles LV_OBJ_FLAG_HIDDEN on it either way, regardless of
     * which object is currently its parent. */
    if (page == APP_PAGE_LOG) {
        lv_sysmon_show_performance(NULL);
    } else {
        lv_sysmon_hide_performance(NULL);
    }
}

static void home_nav_button_cb(lv_event_t *e)
{
    set_active_page(APP_PAGE_HOME);
}

static void sens_nav_button_cb(lv_event_t *e)
{
    set_active_page(APP_PAGE_SENS);
}

static void log_nav_button_cb(lv_event_t *e)
{
    set_active_page(APP_PAGE_LOG);
}

/* TALK: retires the old manual REC button. Rather than starting a capture
 * directly, this fakes a wake-word detection so the real MultiNet command
 * window opens (see voice_control_manual_wake()'s comment in
 * voice_control.c) - pressing TALK then saying "send" is now how a manual
 * capture happens. No in-flight guard needed here the way rec_button_cb had
 * one: voice_control_manual_wake() is a fire-and-forget request that
 * detect_task silently ignores if a window is already open or the pipeline
 * never started, so there is no "rejected press" case for handle_command to
 * avoid logging. */
static void talk_button_cb(lv_event_t *e)
{
    voice_control_manual_wake();
}

/* STOP control on the shared SEND/NOTE recording overlay (Mission 13).
 * Calls straight into audio_capture.c - no reason to route through
 * voice_control.c, which is already just polling audio_capture_get_status()
 * waiting for this to take effect. Harmless if pressed outside RECORDING
 * (see audio_capture_stop()'s doc comment). */
static void recording_stop_button_cb(lv_event_t *e)
{
    audio_capture_stop();
}

/* STOP control on the notification-playback overlay - same "call straight
 * into the owning module, harmless if pressed outside PLAYING" shape as
 * recording_stop_button_cb above. */
static void notification_stop_button_cb(lv_event_t *e)
{
    audio_playback_stop();
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
        /* Mission 19: live numeric readout, left of the chart - updated
         * whenever a fresh sample lands, same as the chart point next to
         * it; left showing the last real value (not blanked) while stale,
         * same "last confirmed evidence" convention render_wifi_panel etc.
         * use for their own current-truth reads. */
        char accel_buf[8];
        snprintf(accel_buf, sizeof(accel_buf), "%.2fg", (double)accel_snapshot);
        lv_label_set_text(accel_value_label, accel_buf);
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

/* ---- Environment/chart: slow timer, matched to HUMITURE_ACQUIRE_PERIOD_MS
 * -------------------------------------------------------------------------
 * Same shape as sensor_timer_cb above, just against the shared humiture
 * status/staleness pair instead of the IMU's - see the Environment Deck
 * comment for why temperature and humidity aren't tracked separately. */
static void humiture_timer_cb(lv_timer_t *t)
{
    if (!humiture_present) {
        return;
    }

    portENTER_CRITICAL(&humiture_mux);
    bool got_temp = temp_have_new_sample;
    bool got_humi = humi_have_new_sample;
    temp_have_new_sample = false;
    humi_have_new_sample = false;
    int64_t last_good = humiture_last_good_us;
    float temp_snapshot = last_temperature_c;
    float humi_snapshot = last_humidity_pct;
    portEXIT_CRITICAL(&humiture_mux);

    if (got_temp) {
        /* AHT30 reports plain Celsius (see the Environment Deck comment) -
         * converted to Fahrenheit here, once, and reused for both the
         * chart point and the value label so they can never disagree. */
        float temp_f = temp_snapshot * 9.0f / 5.0f + 32.0f;
        lv_chart_set_next_value(temp_chart, temp_series, (int32_t)lroundf(temp_f));
        char temp_buf[12];
        snprintf(temp_buf, sizeof(temp_buf), "%.1fF", (double)temp_f);
        lv_label_set_text(temp_value_label, temp_buf);
    }
    if (got_humi) {
        lv_chart_set_next_value(humi_chart, humi_series, (int32_t)lroundf(humi_snapshot));
        char humi_buf[8];
        snprintf(humi_buf, sizeof(humi_buf), "%.0f%%", (double)humi_snapshot);
        lv_label_set_text(humi_value_label, humi_buf);
    }

    if (got_temp || got_humi) {
        if (humiture_status == SRC_UNKNOWN) {
            humiture_status = SRC_CURRENT;
            log_sensor_event("HUMITURE FIRST DATA");
        } else if (humiture_status == SRC_STALE) {
            humiture_status = SRC_CURRENT;
            log_sensor_event("HUMITURE RECOVERED");
        }
    } else if (humiture_status == SRC_CURRENT) {
        int64_t age_us = esp_timer_get_time() - last_good;
        if (age_us > HUMITURE_STALE_THRESHOLD_US) {
            humiture_status = SRC_STALE;
            log_sensor_event("HUMITURE STALE");
        }
    }
}

/* Boot-time hardware probe, same contract as live_data_deck_init above: the
 * AHT30 on the SENSOR dock is optional add-on hardware, so a failed probe
 * here (dock not attached) is an expected, honestly-reported outcome, not
 * an error - the TEMP/HUMIDITY charts simply stay empty for the boot, same
 * as the ACCEL chart would if the IMU ever failed to probe. */
static void humiture_deck_init(void)
{
    static sensor_handle_t humiture_handle;
    bsp_sensor_config_t cfg = {
        .type = HUMITURE_ID,
        .mode = MODE_POLLING,
        .period = HUMITURE_ACQUIRE_PERIOD_MS,
    };

    esp_err_t err = bsp_sensor_init(&cfg, &humiture_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AHT30 not detected (err=%s)", esp_err_to_name(err));
        log_sensor_event("HUMITURE INIT FAILED");
        blackbox_save_fault("HUMITURE INIT FAILED");
        return;
    }

    err = iot_sensor_start(humiture_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AHT30 start failed (err=%s)", esp_err_to_name(err));
        log_sensor_event("HUMITURE INIT FAILED");
        blackbox_save_fault("HUMITURE INIT FAILED");
        return;
    }

    iot_sensor_handler_register(humiture_handle, humiture_event_handler, NULL);
    humiture_present = true;
    log_sensor_event("HUMITURE INIT");
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

    /* Mission 14: three pages (320 x 180, TOP_LEFT at 0,0 - same origin
     * `scr` itself used before) stacked in the same area above the nav bar,
     * only one visible at a time via set_active_page(). Borderless/
     * transparent so their contents look exactly as they did floating
     * directly on `scr` before this split. 180 matches the nav bar's own
     * geometry below (56 tall, BOTTOM_MID -4 => top edge at 240-4-56 =
     * 180), so pages and nav bar meet with no gap or overlap. */
    lv_obj_t *pages_init[APP_PAGE_COUNT];
    for (int i = 0; i < APP_PAGE_COUNT; i++) {
        lv_obj_t *page = lv_obj_create(scr);
        lv_obj_set_size(page, 320, 180);
        lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_radius(page, 0, 0);
        lv_obj_set_style_pad_all(page, 0, 0);
        lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
        pages_init[i] = page;
    }
    page_home = pages_init[APP_PAGE_HOME];
    page_sens = pages_init[APP_PAGE_SENS];
    page_log = pages_init[APP_PAGE_LOG];

    /* ---- HOME: title + UP/WIFI + VoiceListeningWidget -------------------- */

    /* Mission 15: renamed from "HOMEBOUND STATUS DECK", set in blue - both
     * the front label and its faux-bold offset copy get the color so the
     * bold effect (see comment below) still reads as one solid-looking
     * title, not two mismatched layers. */
    lv_obj_t *title = lv_label_create(page_home);
    lv_label_set_text(title, "OPERATION HOMEBOUND");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    /* Faux-bold: LVGL's bundled Montserrat has no bold weight, so draw a
     * second copy offset by 1px to thicken the strokes. */
    lv_obj_t *title_bold = lv_label_create(page_home);
    lv_label_set_text(title_bold, "OPERATION HOMEBOUND");
    lv_obj_set_style_text_font(title_bold, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_bold, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(title_bold, LV_ALIGN_TOP_MID, 1, 2);

    /* UP/WIFI (Mission 15): now one row (was UP alone, with WIFI on its own
     * row below) - HS dropped entirely (see the file header comment), which
     * freed the row WIFI used to occupy. */
    telemetry_label = lv_label_create(page_home);
    lv_obj_align(telemetry_label, LV_ALIGN_TOP_LEFT, 8, 26);

    conn_label = lv_label_create(page_home);
    lv_obj_align(conn_label, LV_ALIGN_TOP_RIGHT, -8, 26);
    lv_label_set_text(conn_label, "WIFI: OFF");

    /* Mission 17: AUD's status row and the widget's own caption/label are
     * both dropped (see the file header comment) so the VoiceListeningWidget
     * can take over nearly all of the remaining page. Mission 18 nudges it
     * up from y=44 to y=28 for more clearance above the nav bar below the
     * page - safe to sit this close under the UP/WIFI row above despite the
     * y-ranges overlapping on paper, since UP/WIFI sit at the left/right
     * edges (x=8 / x=-8) while the widget is horizontally centered; their
     * actual pixels never touch. Mission 19 keeps this same y=28 top (it
     * was already right) and grows the widget itself (130px -> 148px, see
     * voice_listening_widget.c) so the bottom now reaches y=176, just
     * above the page's own bottom edge at 180 (and the nav bar right
     * below it), instead of leaving a visible gap. */
    voice_widget = voice_listening_widget_create(page_home);
    lv_obj_align(voice_widget, LV_ALIGN_TOP_MID, 0, 28);

    /* Volume control: small up/label/down column in the gap to the right of
     * the 148px-diameter ring (ring's own right edge sits at x=234 on this
     * 320px-wide page, see voice_listening_widget.c's RING_DIAM) - x=105
     * offset from page center (pulled in from an earlier 117 to sit closer
     * to the ring, alongside the project selector below getting the same
     * inward nudge on the opposite side) puts this column at roughly
     * x=240-280, clear of the ring with a small margin to spare. Container
     * height (130) roughly matches the ring's own vertical span so the
     * up/down buttons land near its top/bottom rather than floating off to
     * one side. */
    lv_obj_t *volume_ctrl = lv_obj_create(page_home);
    lv_obj_set_size(volume_ctrl, 50, 130);
    lv_obj_align(volume_ctrl, LV_ALIGN_TOP_MID, 105, 38);
    lv_obj_set_style_bg_opa(volume_ctrl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(volume_ctrl, 0, 0);
    lv_obj_set_style_pad_all(volume_ctrl, 0, 0);
    lv_obj_clear_flag(volume_ctrl, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *volume_up_btn = lv_btn_create(volume_ctrl);
    lv_obj_set_size(volume_up_btn, 40, 36);
    lv_obj_align(volume_up_btn, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_event_cb(volume_up_btn, volume_up_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *volume_up_label = lv_label_create(volume_up_btn);
    lv_label_set_text(volume_up_label, "+");
    lv_obj_set_style_text_font(volume_up_label, &lv_font_montserrat_20, 0);
    lv_obj_center(volume_up_label);

    volume_label = lv_label_create(volume_ctrl);
    lv_obj_align(volume_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *volume_down_btn = lv_btn_create(volume_ctrl);
    lv_obj_set_size(volume_down_btn, 40, 36);
    lv_obj_align(volume_down_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(volume_down_btn, volume_down_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *volume_down_label = lv_label_create(volume_down_btn);
    lv_label_set_text(volume_down_label, "-");
    lv_obj_set_style_text_font(volume_down_label, &lv_font_montserrat_20, 0);
    lv_obj_center(volume_down_label);

    refresh_volume_label();

    /* Project selector: same up/label/down scroller shape as the volume
     * control, mirrored to the ring's left (ring's own left edge sits at
     * x=86, mirroring the volume control's x=105 offset above). Current
     * selection shows in the center; the arrows cycle through
     * PROJECT_OPTIONS instead of stepping a number. */
    lv_obj_t *project_ctrl = lv_obj_create(page_home);
    lv_obj_set_size(project_ctrl, 50, 130);
    lv_obj_align(project_ctrl, LV_ALIGN_TOP_MID, -105, 38);
    lv_obj_set_style_bg_opa(project_ctrl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(project_ctrl, 0, 0);
    lv_obj_set_style_pad_all(project_ctrl, 0, 0);
    lv_obj_clear_flag(project_ctrl, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *project_up_btn = lv_btn_create(project_ctrl);
    lv_obj_set_size(project_up_btn, 40, 36);
    lv_obj_align(project_up_btn, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_event_cb(project_up_btn, project_up_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *project_up_label = lv_label_create(project_up_btn);
    lv_label_set_text(project_up_label, "+");
    lv_obj_set_style_text_font(project_up_label, &lv_font_montserrat_20, 0);
    lv_obj_center(project_up_label);

    project_label = lv_label_create(project_ctrl);
    lv_obj_set_style_text_font(project_label, &lv_font_montserrat_14, 0);
    lv_obj_align(project_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *project_down_btn = lv_btn_create(project_ctrl);
    lv_obj_set_size(project_down_btn, 40, 36);
    lv_obj_align(project_down_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(project_down_btn, project_down_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *project_down_label = lv_label_create(project_down_btn);
    lv_label_set_text(project_down_label, "-");
    lv_obj_set_style_text_font(project_down_label, &lv_font_montserrat_20, 0);
    lv_obj_center(project_down_label);

    refresh_project_label();

    /* ---- SENS: three stacked trend charts, each a third of the page ----- */

    /* Band layout: 180 / 3 = 60px each (caption + value/chart row), same
     * for all three - ACCEL (y 0-60), TEMP (y 60-120), HUMIDITY (y
     * 120-180). Caption stays centered above the row (quantity, unit, fixed
     * axis range - see the chart globals' comment for why a caption
     * substitutes for real tick labels); below it, a live numeric reading
     * (48px, left) sits beside the chart (250px, right) instead of the
     * chart alone spanning the full 304px width. */
    lv_obj_t *accel_caption = lv_label_create(page_sens);
    lv_label_set_text(accel_caption, "ACCEL (G)  0.50 - 1.50");
    lv_obj_align(accel_caption, LV_ALIGN_TOP_MID, 0, 2);

    accel_value_label = lv_label_create(page_sens);
    lv_label_set_text(accel_value_label, "--");
    lv_obj_align(accel_value_label, LV_ALIGN_TOP_LEFT, 8, 30);

    /* Acceleration-magnitude trend line, zoomed to 0.50-1.50g (was a fixed
     * 0.00-4.00g). At rest the board reads ~1.00g regardless of
     * orientation (gravity) and real movement/shaking is usually a modest
     * excursion around that, not a swing across several g - the old 4g-
     * wide range buried that movement in a few flat-looking pixels. This
     * range is still fixed/static, not autoscaling, so a genuinely hard
     * shake can still peg or clip at an edge - a known, accepted limit of
     * a static range, same tradeoff as before, just recentered on the
     * value that actually matters. */
    chart = lv_chart_create(page_sens);
    lv_obj_set_size(chart, 250, 40);
    lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 62, 18);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, SENSOR_HISTORY_LEN);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 50, 150);
    accel_series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    /* Mission 19: Fahrenheit, not Celsius - range narrowed to 60-90F (a
     * realistic indoor band) from the old 0-50C, same "zoom to where the
     * real signal lives" reasoning as ACCEL's 0.50-1.50g range above.
     * humiture_timer_cb converts the AHT30's plain-Celsius reading (see the
     * Environment Deck comment on sensor_data_t's "dCelsius" field being
     * mislabeled) to F before pushing it here or into temp_value_label. */
    lv_obj_t *temp_caption = lv_label_create(page_sens);
    lv_label_set_text(temp_caption, "TEMP (F)  60 - 90");
    lv_obj_align(temp_caption, LV_ALIGN_TOP_MID, 0, 62);

    temp_value_label = lv_label_create(page_sens);
    lv_label_set_text(temp_value_label, "--");
    lv_obj_align(temp_value_label, LV_ALIGN_TOP_LEFT, 8, 90);

    temp_chart = lv_chart_create(page_sens);
    lv_obj_set_size(temp_chart, 250, 40);
    lv_obj_align(temp_chart, LV_ALIGN_TOP_LEFT, 62, 78);
    lv_chart_set_type(temp_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(temp_chart, HUMITURE_HISTORY_LEN);
    lv_chart_set_update_mode(temp_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_axis_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 60, 90);
    temp_series = lv_chart_add_series(temp_chart, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *humi_caption = lv_label_create(page_sens);
    lv_label_set_text(humi_caption, "HUMIDITY (%)  0 - 100");
    lv_obj_align(humi_caption, LV_ALIGN_TOP_MID, 0, 122);

    humi_value_label = lv_label_create(page_sens);
    lv_label_set_text(humi_value_label, "--");
    lv_obj_align(humi_value_label, LV_ALIGN_TOP_LEFT, 8, 150);

    humi_chart = lv_chart_create(page_sens);
    lv_obj_set_size(humi_chart, 250, 40);
    lv_obj_align(humi_chart, LV_ALIGN_TOP_LEFT, 62, 138);
    lv_chart_set_type(humi_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(humi_chart, HUMITURE_HISTORY_LEN);
    lv_chart_set_update_mode(humi_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_axis_range(humi_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    humi_series = lv_chart_add_series(humi_chart, lv_palette_main(LV_PALETTE_CYAN), LV_CHART_AXIS_PRIMARY_Y);

    /* ---- LOG: Black Box panel (as tall as the page allows) + NET/SND/CLR */

    /* No "EVENT LOG" title label inside the panel (Mission 05 had one) -
     * the panel's content is self-evident without it. Shows all
     * EVENT_DISPLAY_LINES == EVENT_HISTORY_LEN records the black box
     * retains (Mission 15 grew this panel to fill nearly the whole page,
     * leaving just enough room below for the button row - see
     * EVENT_DISPLAY_LINES's comment). */
    lv_obj_t *log_panel = lv_obj_create(page_log);
    lv_obj_set_size(log_panel, 304, 112);
    lv_obj_align(log_panel, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_pad_all(log_panel, 4, 0);
    lv_obj_clear_flag(log_panel, LV_OBJ_FLAG_SCROLLABLE);

    log_label = lv_label_create(log_panel);
    lv_obj_align(log_label, LV_ALIGN_TOP_LEFT, 0, 0);

    /* NET/SND/CLR (Mission 15): NET and SND moved here from HOME, next to
     * CLR - same button callbacks the old shared bottom row used. Mission
     * 19 widens this row to the full 304px content width and left-packs
     * the buttons (FLEX_ALIGN_START, was SPACE_EVENLY, and each button
     * narrowed 68px -> 54px) to leave room on the right for the FPS/CPU
     * label reparented in below. */
    lv_obj_t *log_btn_row = lv_obj_create(page_log);
    lv_obj_set_size(log_btn_row, 304, 48);
    lv_obj_align(log_btn_row, LV_ALIGN_TOP_MID, 0, 126);
    lv_obj_set_style_pad_all(log_btn_row, 2, 0);
    lv_obj_set_style_pad_column(log_btn_row, 5, 0);
    lv_obj_clear_flag(log_btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(log_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(log_btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const struct {
        const char *label;
        lv_event_cb_t cb;
    } log_buttons[] = {
        { "NET", net_button_cb },
        { "SND", send_handshake_button_cb },
        { "CLR", clear_log_button_cb },
    };
    for (size_t i = 0; i < sizeof(log_buttons) / sizeof(log_buttons[0]); i++) {
        lv_obj_t *btn = lv_btn_create(log_btn_row);
        lv_obj_set_size(btn, 54, 44);
        lv_obj_add_event_cb(btn, log_buttons[i].cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, log_buttons[i].label);
        lv_obj_center(btn_label);
    }

    /* Mission 19: reparent the built-in FPS/CPU label into this same row,
     * as its 4th flex child, instead of leaving it in LVGL's own fixed
     * bottom-right corner of the whole display (CONFIG_LV_PERF_MONITOR_
     * ALIGN_BOTTOM_RIGHT) - which for LOG specifically landed in the nav
     * bar's band below this page, not next to these buttons. Safe to do
     * exactly once here: bsp_display_start() (called from main.c before
     * status_deck_ui() runs) already triggered lv_display_create(), which
     * unconditionally shows the performance monitor once at boot, so
     * disp->perf_label already exists by this point - see the
     * lv_display_private.h include above for why reaching into it is
     * necessary and accepted. Reparenting a plain lv_obj_t label doesn't
     * disturb the FPS/CPU auto-update observer bound to it in lv_sysmon.c
     * (it tracks the label object itself, not its parent), so it keeps
     * updating right where it now sits, and set_active_page()'s existing
     * show/hide-on-page-switch calls keep working unchanged too. */
    lv_display_t *disp = lv_display_get_default();
    if (disp != NULL && disp->perf_label != NULL) {
        lv_obj_set_parent(disp->perf_label, log_btn_row);
    }

    /* ---- Nav bar: HOME/SENS/LOG/TALK, always on screen, own row on `scr`
     * (not inside any page) - same size/position the old NET/SND/REC/CLR
     * row used, so the overlays below (already sized to cover it) still
     * cover exactly the same area. */
    lv_obj_t *nav_row = lv_obj_create(scr);
    lv_obj_set_size(nav_row, 304, 56);
    lv_obj_align(nav_row, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_pad_all(nav_row, 2, 0);
    lv_obj_set_style_pad_column(nav_row, 4, 0);
    lv_obj_clear_flag(nav_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(nav_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const struct {
        const char *label;
        lv_event_cb_t cb;
    } nav_button_defs[] = {
        { "HOME", home_nav_button_cb },
        { "SENS", sens_nav_button_cb },
        { "LOG",  log_nav_button_cb },
        { "TALK", talk_button_cb },
    };
    for (size_t i = 0; i < sizeof(nav_button_defs) / sizeof(nav_button_defs[0]); i++) {
        lv_obj_t *btn = lv_btn_create(nav_row);
        lv_obj_set_size(btn, 68, 48);
        lv_obj_add_event_cb(btn, nav_button_defs[i].cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, nav_button_defs[i].label);
        lv_obj_center(btn_label);

        if (i < (size_t)APP_PAGE_COUNT) {
            nav_buttons[i] = btn;
        }
    }

    /* Command Window overlay (Mission 12): full-screen, built last so it
     * naturally sits on top of the dashboard - render_command_overlay also
     * calls lv_obj_move_foreground on every show, so this ordering isn't
     * load-bearing on its own, just tidy. Hidden by default: LISTENING
     * (the plain dashboard) is the normal state, this only appears for the
     * ~10s command window plus its brief result/timeout indication. */
    cmd_overlay = lv_obj_create(scr);
    lv_obj_set_size(cmd_overlay, 320, 240);
    lv_obj_align(cmd_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cmd_overlay, lv_palette_darken(LV_PALETTE_BLUE_GREY, 4), 0);
    lv_obj_set_style_bg_opa(cmd_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cmd_overlay, 0, 0);
    lv_obj_set_style_border_width(cmd_overlay, 0, 0);
    lv_obj_set_style_pad_all(cmd_overlay, 8, 0);
    lv_obj_clear_flag(cmd_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cmd_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *cmd_title = lv_label_create(cmd_overlay);
    lv_label_set_text(cmd_title, "COMMAND");
    lv_obj_set_style_text_font(cmd_title, &lv_font_montserrat_20, 0);
    lv_obj_align(cmd_title, LV_ALIGN_TOP_MID, 0, 4);

    /* 2 columns x 2 rows (SEND/NOTE, GO/YES) via flex wrap - two 136px
     * buttons plus an 8px gap is 280px, comfortably inside the 304px usable
     * width. No per-button description text (directive: keep this screen
     * simple) and not clickable (directive: touch activation isn't
     * required this pass and none existed here before - clearing
     * LV_OBJ_FLAG_CLICKABLE means a stray tap can't be confused with a
     * real MultiNet recognition highlight). */
    lv_obj_t *cmd_grid = lv_obj_create(cmd_overlay);
    lv_obj_set_size(cmd_grid, 288, 96);
    lv_obj_align(cmd_grid, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_pad_all(cmd_grid, 0, 0);
    lv_obj_set_style_pad_row(cmd_grid, 8, 0);
    lv_obj_set_style_pad_column(cmd_grid, 8, 0);
    lv_obj_set_style_bg_opa(cmd_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cmd_grid, 0, 0);
    lv_obj_clear_flag(cmd_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cmd_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cmd_grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < VOICE_COMMAND_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(cmd_grid);
        lv_obj_set_size(btn, 136, 44);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(btn, CMD_BTN_INACTIVE_BG, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, lv_palette_main(LV_PALETTE_GREY), 0);

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, cmd_defs[i].label);
        lv_obj_center(btn_label);

        cmd_buttons[i] = btn;
    }

    /* Countdown while COMMAND_WINDOW is open ("Listening... 10" ... "...1"),
     * then briefly replaced with the recognized word / TIMEOUT /
     * UNRECOGNIZED - see render_command_overlay(). */
    cmd_status_label = lv_label_create(cmd_overlay);
    lv_label_set_text(cmd_status_label, "");
    lv_obj_align(cmd_status_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* Shared SEND/NOTE recording overlay (Mission 13) - same full-screen
     * style as cmd_overlay above, own content: a title naming which command
     * is active, ID, a recording dot + elapsed timer (or ARMING/UPLOADING/
     * result text - see render_recording_overlay()), and a real touchable
     * STOP button. */
    recording_overlay = lv_obj_create(scr);
    lv_obj_set_size(recording_overlay, 320, 240);
    lv_obj_align(recording_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(recording_overlay, lv_palette_darken(LV_PALETTE_BLUE_GREY, 4), 0);
    lv_obj_set_style_bg_opa(recording_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(recording_overlay, 0, 0);
    lv_obj_set_style_border_width(recording_overlay, 0, 0);
    lv_obj_clear_flag(recording_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(recording_overlay, LV_OBJ_FLAG_HIDDEN);

    recording_title = lv_label_create(recording_overlay);
    lv_label_set_text(recording_title, "RECORDING");
    lv_obj_set_style_text_font(recording_title, &lv_font_montserrat_20, 0);
    lv_obj_align(recording_title, LV_ALIGN_TOP_MID, 0, 8);

    recording_id_label = lv_label_create(recording_overlay);
    lv_label_set_text(recording_id_label, "ID:");
    lv_obj_align(recording_id_label, LV_ALIGN_TOP_MID, 0, 40);

    /* Small filled circle, shown only while actually RECORDING - see
     * render_recording_overlay(). A real shape rather than a Unicode glyph
     * so it renders regardless of which characters the bundled font covers. */
    recording_dot = lv_obj_create(recording_overlay);
    lv_obj_set_size(recording_dot, 14, 14);
    lv_obj_set_style_radius(recording_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(recording_dot, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(recording_dot, 0, 0);
    lv_obj_clear_flag(recording_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(recording_dot, LV_ALIGN_CENTER, -40, -10);

    recording_status_label = lv_label_create(recording_overlay);
    lv_label_set_text(recording_status_label, "");
    lv_obj_set_style_text_font(recording_status_label, &lv_font_montserrat_20, 0);
    lv_obj_align(recording_status_label, LV_ALIGN_CENTER, 10, -10);

    lv_obj_t *stop_btn = lv_btn_create(recording_overlay);
    lv_obj_set_size(stop_btn, 220, 56);
    lv_obj_align(stop_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(stop_btn, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(stop_btn, recording_stop_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_20, 0);
    lv_obj_center(stop_label);

    /* Notification-playback overlay - same full-screen style as the others,
     * a STOP button like the recording overlay's since playback runs long
     * enough (5-20s) to be worth interrupting - see
     * render_notification_overlay() and notification_stop_button_cb(). */
    notification_overlay = lv_obj_create(scr);
    lv_obj_set_size(notification_overlay, 320, 240);
    lv_obj_align(notification_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(notification_overlay, lv_palette_darken(LV_PALETTE_BLUE_GREY, 4), 0);
    lv_obj_set_style_bg_opa(notification_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(notification_overlay, 0, 0);
    lv_obj_set_style_border_width(notification_overlay, 0, 0);
    lv_obj_clear_flag(notification_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(notification_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *notification_title = lv_label_create(notification_overlay);
    lv_label_set_text(notification_title, "NOTIFICATION");
    lv_obj_set_style_text_font(notification_title, &lv_font_montserrat_20, 0);
    lv_obj_align(notification_title, LV_ALIGN_TOP_MID, 0, 8);

    notification_id_label = lv_label_create(notification_overlay);
    lv_label_set_text(notification_id_label, "ID:");
    lv_obj_align(notification_id_label, LV_ALIGN_TOP_MID, 0, 40);

    notification_status_label = lv_label_create(notification_overlay);
    lv_label_set_text(notification_status_label, "");
    lv_obj_set_style_text_font(notification_status_label, &lv_font_montserrat_20, 0);
    lv_obj_align(notification_status_label, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *notification_stop_btn = lv_btn_create(notification_overlay);
    lv_obj_set_size(notification_stop_btn, 220, 56);
    lv_obj_align(notification_stop_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(notification_stop_btn, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(notification_stop_btn, notification_stop_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *notification_stop_label = lv_label_create(notification_stop_btn);
    lv_label_set_text(notification_stop_label, "STOP");
    lv_obj_set_style_text_font(notification_stop_label, &lv_font_montserrat_20, 0);
    lv_obj_center(notification_stop_label);

    render_event_log();

    live_data_deck_init();
    humiture_deck_init();

    render_wifi_panel();
    wifi_mgr_init(wifi_status_changed_cb, NULL);

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

    audio_capture_init(mic_dev, audio_status_changed_cb, NULL);

    /* Speaker path, new alongside the long-standing mic path above: a
     * separate physical codec (ES8311, not the mic's ES7210), so this is an
     * independent bsp_audio_codec_speaker_init() call, not a second handle
     * fighting over the mic. NULL is handled the same way a missing mic_dev
     * is - audio_playback_play() logs and no-ops forever, nothing else is
     * affected. */
    esp_codec_dev_handle_t spk_dev = bsp_audio_codec_speaker_init();
    if (!spk_dev) {
        ESP_LOGE(TAG, "bsp_audio_codec_speaker_init failed - notification playback unavailable this boot");
    }
    audio_playback_init(spk_dev);
    notification_client_init();
    remote_client_init();

    render_command_overlay();
    render_recording_overlay();
    render_notification_overlay();
    voice_control_init(mic_dev, voice_status_changed_cb, NULL);

    lv_timer_create(telemetry_timer_cb, 1000, NULL);
    lv_timer_create(sensor_timer_cb, SENSOR_ACQUIRE_PERIOD_MS, NULL);
    lv_timer_create(humiture_timer_cb, HUMITURE_ACQUIRE_PERIOD_MS, NULL);
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

    /* Boots on HOME - hides SENS/LOG and sets the nav bar's initial
     * highlight, same as set_active_page does on every later press. */
    set_active_page(APP_PAGE_HOME);
}
