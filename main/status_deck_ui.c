/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 05/06/07/08/09/10 â€” Status Deck + Live Data Deck + Black
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
#include "esp_heap_caps.h"
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
#include "ui_kit.h"
#include "ui_home.h"
#include "ui_menu.h"
#include "ui_theme.h"
#include "weather_client.h"
#include "weather_icons.h"
#include "ui_weather_page.h"
#include "ui_firmware_page.h"
#include "ui_diagnostics.h"
#include "ui_sensors.h"
#include "ui_log.h"
#include "ui_settings.h"
#include "ui_recording.h"
#include "ui_notify.h"
#include "ui_survey.h"
#include "ui_adventure.h"
#include "ui_adventure_b.h"
#include "ui_tour_selector.h"
#include "ui_music.h"
#include "ui_audio_test.h"
#include "audio_test_session.h"
#include "ui_newton.h"
#include "ui_lincoln.h"
#include "lincoln_art.h"
#include "newton_art.h"
#include "newton_store.h"
#include "ui_music_mock.h"
#include "ui_mouse.h"
#include "esp_wifi.h"
#include "backend_health.h"
#include "backend_catalog.h"
#include "notification_client.h"
#include "remote_client.h"
#include "firmware_identity.h"
#include "ota_service.h"
#include "device_config.h"
#include "audio_playback.h"
#include "project_selector.h"
#include "backend_catalog.h"
#include "report_client.h"
#include "diag.h"
#include "lvgl_psram_alloc.h"
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
    EVT_BACKEND,   /* 1.2.0: backend lost / back / address switched */
    EVT_REPORT,    /* 1.2.0: device report sent / failed */
} event_type_t;

static const char *event_type_name(event_type_t t)
{
    switch (t) {
    case EVT_BOOT:      return "BOOT";
    case EVT_RESET:     return "RESET";
    case EVT_COMMAND:   return "COMMAND";
    case EVT_SENSOR:    return "SENSOR";
    case EVT_SYSTEM:    return "SYSTEM";
    case EVT_NETWORK:   return "NETWORK";
    case EVT_HANDSHAKE: return "HANDSHAKE";
    case EVT_AUDIO:     return "AUDIO";
    case EVT_VOICE:     return "VOICE";
    case EVT_BACKEND:   return "BACKEND";
    case EVT_REPORT:    return "REPORT";
    default:            return "?";
    }
}

/* 1.2.0: 32, up from 8, so a device report carries the story of an outage
 * and not just its last few lines. Held in PSRAM (allocated at the top of
 * status_deck_ui()) - ~1.3KB that internal RAM cannot spare. The LOG page
 * still shows the newest EVENT_DISPLAY_LINES. */
#define EVENT_HISTORY_LEN 32
/* 32, up from 28. The failure *reason* was the part getting clipped, which
 * is the part worth reading - "NOTE UPLOAD FAILED: UPLOAD" lost its
 * "TIMEOUT", and "GO UPLOAD FAILED: UPLOAD ER" lost the status code.
 *
 * Bounded by the panel, not by taste: log_panel is 304px with 4px padding,
 * so ~296px at montserrat_14, and each line is prefixed "%3lus " (5 chars).
 * 31 message chars puts the longest realistic line around 289px. Going much
 * further would wrap, and a wrapped line costs one of the eight history rows
 * this panel exists to show. The messages themselves were shortened too -
 * see voice_control.c - since "UPLOAD FAILED: UPLOAD TIMEOUT" said UPLOAD
 * twice. */
#define EVENT_MSG_LEN 32
/* Mission 15 made this equal to EVENT_HISTORY_LEN (8 then). 1.2.0 grows the
 * recorder to 32 but keeps the panel at the newest 8: the full history goes
 * out in the device report, where there is room to read it. */
#define EVENT_DISPLAY_LINES EVENT_HISTORY_LEN   /* UI suite: LOG pages through all of it */

typedef struct {
    event_type_t type;
    uint32_t uptime_s;
    char message[EVENT_MSG_LEN];
} event_record_t;

static event_record_t *event_history;   /* EVENT_HISTORY_LEN records, PSRAM */
static int event_history_used = 0;
/* Events recorded since boot (keeps rising past the ring): LOG uses it to keep
 * a paged-back view steady while new lines arrive. */
static uint32_t event_seq = 0;
/* The LOG page's view of the ring, rebuilt on each render. PSRAM, beside the
 * ring it points into. */
static ui_log_entry_t *log_entries;

static void event_history_alloc(void)
{
    if (event_history != NULL) {
        return;
    }
    event_history = heap_caps_calloc(EVENT_HISTORY_LEN, sizeof(event_record_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (event_history == NULL) {
        /* No PSRAM would already have stopped the audio pipeline; keep the
         * Black Box working anyway rather than dereferencing NULL. */
        event_history = heap_caps_calloc(EVENT_HISTORY_LEN, sizeof(event_record_t), MALLOC_CAP_8BIT);
    }
    log_entries = heap_caps_calloc(EVENT_HISTORY_LEN, sizeof(ui_log_entry_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void event_history_push(event_type_t type, const char *message)
{
    if (event_history == NULL) {
        return;
    }
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
    event_seq++;
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
    /* 1.2.0: the previous boot ended in backend_health.c's no-backend
     * self-restart. Read from (and then erased from) the "selfrst" key. */
    bool prev_was_self_restart;
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
        /* Written by backend_health.c just before its self-restart; read
         * once and erased so it describes exactly one boot. */
        uint8_t selfrst = 0;
        if (nvs_get_u8(h, "selfrst", &selfrst) == ESP_OK) {
            nvs_erase_key(h, "selfrst");
        }
        blackbox.prev_was_self_restart = (selfrst != 0);

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
static bool sensor_probed = false;           /* DIAGNOSTICS: probe has run (present or not) */
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
static bool humiture_probed = false;         /* DIAGNOSTICS: probe has run (present or not) */
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

/* Dormant since 1.2.0: handshake_client_init() is no longer called (its
 * button became RPT, and its 6KB worker stack went to report_client). Kept
 * compiled so the handshake can be brought back by restoring one call. */
__attribute__((unused))
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

/* ---- Backend connection (1.2.0), bridged from a foreign task ----------
 * backend_health.c's callback runs on its poll task (or on whichever upload
 * task reported a result) - never the LVGL task. Same bridge shape as the
 * queues above; drained by wifi_ui_timer_cb as EVT_BACKEND records.
 *
 * `auto_report` marks a recovery after an outage long enough to be worth a
 * report (BACKEND_AUTO_REPORT_OUTAGE_S) - the drain turns it into a pending
 * "auto-reconnect" report once the event itself is in the ring, so the
 * report carries the line that caused it. */
#define BACKEND_PENDING_LEN 4
#define BACKEND_AUTO_REPORT_OUTAGE_S 120

typedef struct {
    char message[EVENT_MSG_LEN];
    bool auto_report;
} backend_pending_evt_t;

static backend_pending_evt_t backend_pending[BACKEND_PENDING_LEN];
static int backend_pending_count = 0;
static portMUX_TYPE backend_pending_mux = portMUX_INITIALIZER_UNLOCKED;

static void backend_event_cb(backend_health_event_t ev, uint32_t outage_s, const char *message, void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&backend_pending_mux);
    if (backend_pending_count < BACKEND_PENDING_LEN) {
        strncpy(backend_pending[backend_pending_count].message, message, EVENT_MSG_LEN - 1);
        backend_pending[backend_pending_count].message[EVENT_MSG_LEN - 1] = '\0';
        backend_pending[backend_pending_count].auto_report =
            (ev == BACKEND_HEALTH_EVT_BACK && outage_s > BACKEND_AUTO_REPORT_OUTAGE_S);
        backend_pending_count++;
    }
    portEXIT_CRITICAL(&backend_pending_mux);
}

/* An automatic report waiting for its moment: set by the drain above
 * ("auto-reconnect") or at boot after a self-restart ("auto-watchdog"),
 * fired by report_try_pending() once the backend is answering and the
 * report worker is free. LVGL task only. */
static const char *report_pending_trigger = NULL;
static uint32_t report_seen_seq = 0;

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

/* UI suite pages (ui_sensors.c, ui_log.c, ui_settings.c, ui_notify.c) and
 * overlays (ui_recording.c, ui_notify.c). */
/* One block, allocated in PSRAM by status_deck_ui() before anything is
 * built: ~0.9 KB of object handles that internal RAM - the ceiling on this
 * board - does not have to hold. NULL until then; code that can run before
 * the UI exists checks it. */
static struct {
    ui_sensors_page_t sens;
    ui_log_page_t log;
    ui_settings_page_t settings;
    ui_notify_page_t notify;
    ui_recording_t rec;
    ui_notify_overlay_t notif;
    ui_notify_done_t notify_done[4];   /* NOTIFY_HISTORY_LEN; the card stack borrows them */
} *ui;

/* Mission 14: three pages behind a bottom nav bar, replacing the single
 * crowded dashboard - see the file header comment. Each page is a plain
 * lv_obj_t container, shown/hidden as a group via set_active_page() below;
 * nav_buttons[] holds the three tab buttons (HOME/SENS/LOG only - TALK is a
 * one-shot action, not a tab, so it isn't highlighted). */
typedef enum {
    APP_PAGE_HOME = 0,
    APP_PAGE_SENS,
    APP_PAGE_LOG,
    APP_PAGE_SET,
    /* Reached from a button on SETTINGS rather than from a corner of its own -
     * there are only four corners, and browsing projects is something you do
     * occasionally, not something you need one tap away from anywhere. */
    APP_PAGE_PROJECTS,
    /* Firmware identity and wireless-update state. Same reasoning as
     * PROJECTS: reached from a button on SETTINGS, not from a corner of its
     * own. You look at this page when you are deploying, or working out why
     * a deployment did not land - rare, deliberate, and not worth a corner. */
    APP_PAGE_FIRMWARE,
    /* 1.3.0: HOME is weather + microphone + MENU, and MENU is how every page
     * above is reached. Last so the earlier page numbers keep their values. */
    APP_PAGE_MENU,
    /* Link, sensor freshness, uptime, internal heap - reached from MENU. */
    APP_PAGE_DIAG,
    /* UI suite: MENU's second page, and the notification card stack. */
    APP_PAGE_MORE,
    APP_PAGE_NOTIFY,
    APP_PAGE_MORE3,
    APP_PAGE_WEATHER,
    APP_PAGE_COUNT,
} app_page_t;

static lv_obj_t *page_home;
static lv_obj_t *page_sens;
static lv_obj_t *page_log;
static lv_obj_t *page_set;          /* SETTINGS - replaced TALK in the nav */
static lv_obj_t *page_projects;     /* PROJECTS - reached from SETTINGS */
static lv_obj_t *page_firmware;     /* FIRMWARE - reached from SETTINGS */
static lv_obj_t *page_menu;         /* MENU - 1.3.0, reached from HOME */
static lv_obj_t *page_diag;         /* DIAGNOSTICS - reached from MENU */
static lv_obj_t *page_more;         /* MENU's second page */
static lv_obj_t *page_more3, *page_weather;
static ui_home_t weather_ui;
static bool weather_force_update;
static app_page_t current_app_page;
static lv_obj_t *page_notify;       /* MESSAGES card stack - from MORE */
static lv_obj_t *app_screen;        /* where experiments open full-screen */

/* DIAGNOSTICS page objects (ui_diagnostics.h). Refreshed on entry and once a
 * second while visible - see refresh_diag_page(). */
static ui_diagnostics_page_t diag_page;

/* FIRMWARE page labels (ui_firmware_page.h). Refreshed once a second while
 * the page is visible, from firmware_identity.h and ota_service.h - both of
 * which are explicitly safe to read from any task, this one included. */
static ui_firmware_page_t fw_page;
/* 1.3.0: the four corner buttons became MENU on HOME plus this one "< HOME"
 * button, shown on every page but HOME (ui_back_button_create()). */
static lv_obj_t *back_button;

/* Command Window overlay (Mission 12): four-button recognition-test grid,
 * hidden by default, shown full-screen over the dashboard for the ~10s
 * MultiNet command window - see render_command_overlay() below. */
static lv_obj_t *cmd_overlay;
static lv_obj_t *cmd_status_label;
static lv_obj_t *cmd_buttons[VOICE_COMMAND_COUNT];

/* The PROJECTS page's selection and its cue (the recording overlay shows the
 * same selection through ui_recording.c). */
static lv_obj_t *projects_page_label;
static lv_obj_t *projects_page_cue;

/* HOME page's animated listening ring - kept as a file-scope handle (not
 * just a local in status_deck_ui()) so voice_ui_timer_cb can recolor it via
 * voice_listening_widget_set_notification() every tick. */
static lv_obj_t *voice_widget;
/* 1.3.0: HOME's objects - weather column, the widget above (home_ui.mic) and
 * its link caption. Filled by render_weather() and voice_ui_timer_cb. */
static ui_home_t home_ui;

/* Notifications handled this boot, newest first, for the card stack's
 * "earlier" cards: the device keeps no other history of them. PSRAM. */
#define NOTIFY_HISTORY_LEN 4
typedef struct {
    ui_notify_outcome_t outcome;
    uint32_t uptime_s;
    char text[NOTIFICATION_TEXT_LEN];
    char reason[48];
    bool has_text;
} notify_done_t;
static notify_done_t *notify_history;
static int notify_history_used;
/* The message text the card stack and the playback overlay show (PSRAM). */
static char *notify_front_text;
static char *notify_playing_text;

/* SENS trends: the charts live in ui_sensors.c (LVGL-owned ring buffers),
 * fed a point per acquisition. */
#define SENSOR_HISTORY_LEN 100 /* ~10s of history at the 100ms acquisition cadence */

/* ---- Device report (1.2.0) -------------------------------------------
 * Fills the parts of a report only this task owns - the Black Box ring and
 * summary - and hands it to report_client's worker. LVGL task only, which
 * is what makes reading event_history here safe. false if a report is
 * already in flight. */
static bool report_start(const char *trigger)
{
    report_request_t *r = report_client_begin();
    if (r == NULL) {
        return false;
    }
    strlcpy(r->trigger, trigger, sizeof(r->trigger));
    r->boot_count = blackbox.boot_count;
    strlcpy(r->reset_reason, reset_reason_str(esp_reset_reason()), sizeof(r->reset_reason));
    strlcpy(r->prev_reset_reason, reset_reason_str(blackbox.prev_reset_reason), sizeof(r->prev_reset_reason));
    r->prev_was_self_restart = blackbox.prev_was_self_restart;
    strlcpy(r->last_fault, blackbox.last_fault, sizeof(r->last_fault));
    strlcpy(r->last_cmd, blackbox.last_significant_cmd, sizeof(r->last_cmd));

    int n = event_history_used < REPORT_MAX_EVENTS ? event_history_used : REPORT_MAX_EVENTS;
    for (int i = 0; i < n; i++) {
        r->events[i].uptime_s = event_history[i].uptime_s;
        strlcpy(r->events[i].type, event_type_name(event_history[i].type), sizeof(r->events[i].type));
        strlcpy(r->events[i].message, event_history[i].message, sizeof(r->events[i].message));
    }
    r->event_count = n;
    report_client_commit();
    ESP_LOGI(TAG, "report (%s) queued with %d events", trigger, n);
    return true;
}

/* 1.2.2: no automatic report before this much uptime AND one finished OTA
 * check, so an update can always land before any report work runs. 1.2.1's
 * report path crashed ~25 s into every boot and OTA never got its turn. */
#define AUTO_REPORT_MIN_UPTIME_S 120

/* True once automatic reports may run this boot. Latches: the OTA check
 * only has to have happened once. LVGL task only. */
static bool auto_report_allowed(void)
{
    static bool ota_checked = false;
    if (diag_auto_reports_off()) {
        return false;      /* last boot died inside a report (diag.h) */
    }
    if (esp_timer_get_time() < (int64_t)AUTO_REPORT_MIN_UPTIME_S * 1000000) {
        return false;
    }
    if (!ota_checked) {
        ota_status_t o;
        ota_service_get_status(&o);
        /* checks counts attempts as they start; IDLE/FAILED means the one
         * counted has finished (and no install is under way). */
        ota_checked = o.checks > 0 && (o.state == OTA_STATE_IDLE || o.state == OTA_STATE_FAILED);
    }
    return ota_checked;
}

/* Fires a waiting automatic report once there is somewhere to send it and
 * auto_report_allowed(). RPT does not come through here. */
static void report_try_pending(void)
{
    if (report_pending_trigger == NULL) {
        return;
    }
    if (!auto_report_allowed()) {
        return;            /* stays pending; tried again each second */
    }
    backend_health_status_t bh;
    backend_health_get_status(&bh);
    if (bh.state == BACKEND_HEALTH_OK && report_start(report_pending_trigger)) {
        report_pending_trigger = NULL;
    }
}

/* 1.2.1: the two automatic reports diagnostics adds, queued through the same
 * report_pending_trigger slot as auto-reconnect/auto-watchdog so they wait
 * for the backend in exactly the same way.
 *   after-crash: the boot after a PANIC/WDT/brownout, until the backend has
 *                stored one (report_client marks it delivered). A failed
 *                POST is retried a minute later.
 *   heartbeat:   hourly. Carries heap min-free, stack marks and counters.
 * Called from telemetry_timer_cb, once a second. LVGL task only. */
static void diag_report_schedule(uint32_t uptime_s)
{
    static uint32_t next_heartbeat_s = DIAG_HEARTBEAT_S;
    static uint32_t next_crash_try_s = 0;

    if (report_pending_trigger == NULL && diag_after_crash_pending() && uptime_s >= next_crash_try_s) {
        report_pending_trigger = "after-crash";
        next_crash_try_s = uptime_s + 60;
    }
    if (report_pending_trigger == NULL && uptime_s >= next_heartbeat_s) {
        report_pending_trigger = "heartbeat";
        next_heartbeat_s = uptime_s + DIAG_HEARTBEAT_S;
    }
    report_try_pending();
}

/* ---- Rendering: UI reads app_state / event_history, never the reverse - */

/* LOG (UI suite): hands the whole ring to ui_log.c, which pages through it.
 * The entries point into event_history, which only this task changes. */
static void render_event_log(void)
{
    if (ui == NULL || ui->log.list == NULL) {
        return;   /* page not built yet (boot-time events) */
    }
    /* Rebuilding the list costs a full layout pass; nobody sees a hidden
     * LOG, and it is rebuilt on entry (log_nav_button_cb). */
    if (page_log == NULL || lv_obj_has_flag(page_log, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    int n = (event_history && log_entries) ? event_history_used : 0;
    for (int i = 0; i < n; i++) {
        log_entries[i].type = (int)event_history[i].type;
        log_entries[i].uptime_s = event_history[i].uptime_s;
        log_entries[i].message = event_history[i].message;
    }
    ui_log_show(&ui->log, log_entries, n, event_seq);
}

static void log_sensor_event(const char *text)
{
    log_event(EVT_SENSOR, text);
}

/* SETTINGS (UI suite, ui_settings.c): everything on it is read from
 * somewhere else, so it is only re-read - on the 500 ms Wi-Fi timer while it
 * is on screen, and straight after one of its own keys. */
static void refresh_settings_page(void)
{
    if (ui == NULL || page_set == NULL || ui->settings.vol_value == NULL) {
        return;
    }
    wifi_mgr_status_t ws;
    wifi_mgr_get_status(&ws);
    backend_health_status_t bh;
    backend_health_get_status(&bh);
    backend_health_stats_t bs;
    backend_health_get_stats(&bs);
    const char *octet = strrchr(ws.ip_addr, '.');
    ui_settings_view_t v = {
        .uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL),
        .wifi = ws.state == WIFI_MGR_ONLINE       ? UI_DIAG_WIFI_ONLINE
              : ws.state == WIFI_MGR_CONNECTING   ? UI_DIAG_WIFI_JOINING
              : ws.state == WIFI_MGR_RETRY_WAIT   ? UI_DIAG_WIFI_RETRY
                                                  : UI_DIAG_WIFI_OFF,
        .ip_octet = octet ? octet : ws.ip_addr,
        .wifi_attempts = ws.attempt_count,
        .wifi_reason = ws.reason_text,
        .wifi_retry_s = ws.retry_remaining_s,
        .api = bh.state == BACKEND_HEALTH_OK         ? UI_DIAG_API_OK
             : bh.state == BACKEND_HEALTH_DOWN       ? UI_DIAG_API_DOWN
             : bh.state == BACKEND_HEALTH_NO_NETWORK ? UI_DIAG_API_NO_NET
                                                     : UI_DIAG_API_UNKNOWN,
        .api_latency_ms = bh.latency_ms,
        .api_ever_ok = bs.ever_ok,
        .api_secs_since_ok = bs.secs_since_ok,
        .volume = audio_playback_get_volume(),
        .repo = backend_catalog_repo_label(),
        .repo_count = backend_catalog_repo_count(),
    };
    ui_settings_apply(&ui->settings, &v);
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

    backend_pending_evt_t bdrained[BACKEND_PENDING_LEN];
    int bcount;
    portENTER_CRITICAL(&backend_pending_mux);
    bcount = backend_pending_count;
    if (bcount > 0) {
        memcpy(bdrained, backend_pending, sizeof(backend_pending_evt_t) * (size_t)bcount);
        backend_pending_count = 0;
    }
    portEXIT_CRITICAL(&backend_pending_mux);
    for (int i = 0; i < bcount; i++) {
        log_event(EVT_BACKEND, bdrained[i].message);
        if (bdrained[i].auto_report && report_pending_trigger == NULL) {
            report_pending_trigger = "auto-reconnect";
        }
    }
    report_try_pending();

    if (page_set != NULL && !lv_obj_has_flag(page_set, LV_OBJ_FLAG_HIDDEN)) {
        refresh_settings_page();
    }
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

    /* 1.2.0: the device report's result, as a Black Box line at the top of
     * the LOG page - "REPORT SENT #12" or "REPORT FAIL: HTTP_CONNECT". */
    report_status_t rs;
    report_client_get_status(&rs);
    if (rs.seq != report_seen_seq) {
        report_seen_seq = rs.seq;
        log_event(EVT_REPORT, rs.message);
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
 * (keeps the two decoupled in case a future pass reorders the grid).
 *
 * `hint` (added after Mike said he kept forgetting which word does what):
 * three words, one line at montserrat_12, saying where each command's
 * recording actually ends up. Kept current deliberately - these went stale
 * once already and a wrong hint is worse than none:
 *   SEND - 15s auto-stop  -> Notion Voice Inbox
 *   NOTE - long, manual   -> Notion Voice Inbox, with a project
 *   GO   - 15s auto-stop  -> a GitHub issue on the selected repository
 *   YES  - not a recording; answers a pending spoken notification */
static const struct {
    voice_command_id_t id;
    const char *label;
    const char *hint;
} cmd_defs[VOICE_COMMAND_COUNT] = {
    { VOICE_CMD_SEND, "SEND", "Quick voice note" },
    { VOICE_CMD_NOTE, "NOTE", "Long note + project" },
    { VOICE_CMD_GO,   "GO",   "New GitHub issue" },
    { VOICE_CMD_YES,  "YES",  "Answer a message" },
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
/* Defined further down with the rest of the project-selector helpers;
 * render_recording_overlay() needs it to keep the overlay's copy of the
 * label in step while a NOTE is recording. */
/* Above this, a capture is long-form and ends when the operator says so
 * (NOTE's cap is 20 minutes); at or below it, the capture stops itself.
 * Used to decide whether the SEND button is meaningful and whether the
 * counter should show a target. */
#define AUTO_STOP_MAX_MS 60000

/* The panel look, page panel and page title live in ui_kit.c. */

static void refresh_project_label(void);
static void refresh_firmware_page(void);
static void refresh_diag_page(void);

/* Recording overlay (UI suite, ui_recording.c): voice_control says whether
 * a capture is on and which command it is, audio_capture what the mic and
 * uploader are doing, wifi_mgr / backend_health feed the rail. Shown and
 * hidden exactly when it was before: while SEND/NOTE/GO is active. */
static void render_recording_overlay(void)
{
    if (ui == NULL) {
        return;
    }
    voice_status_t vst;
    voice_control_get_status(&vst);
    bool show = (vst.state == VOICE_STATE_SEND_ACTIVE || vst.state == VOICE_STATE_NOTE_ACTIVE ||
                 vst.state == VOICE_STATE_GRAPH_ACTIVE);
    if (!show) {
        ui_recording_hide(&ui->rec);
        return;
    }
    audio_cap_status_t ast;
    audio_capture_get_status(&ast);
    wifi_mgr_status_t ws;
    wifi_mgr_get_status(&ws);
    backend_health_status_t bh;
    backend_health_get_status(&bh);

    ui_rec_phase_t phase;
    switch (ast.state) {
    case AUDIO_CAP_ARMING:    phase = UI_REC_ARMING; break;
    case AUDIO_CAP_RECORDING: phase = UI_REC_RECORDING; break;
    case AUDIO_CAP_COMPLETE:
    case AUDIO_CAP_UPLOADING: phase = UI_REC_UPLOADING; break;
    case AUDIO_CAP_READY:     phase = UI_REC_READY; break;
    case AUDIO_CAP_CANCELLED: phase = UI_REC_CANCELLED; break;
    case AUDIO_CAP_FAILED:    phase = UI_REC_FAILED; break;
    case AUDIO_CAP_IDLE:
    default:
        /* Over, with voice_control holding the result up; or genuinely
         * still starting (the worker has not picked the request up). */
        phase = vst.last_result[0] != '\0' ? UI_REC_RESULT : UI_REC_STARTING;
        break;
    }
    ui_rec_view_t v = {
        .command = vst.last_command,
        .phase = phase,
        .elapsed_ms = ast.elapsed_ms,
        .target_ms = ast.duration_target_ms,
        .uploader_behind = ast.uploader_behind,
        .fail_reason = ast.fail_reason,
        .result = vst.last_result,
        .selector = vst.state == VOICE_STATE_NOTE_ACTIVE  ? backend_catalog_project_label()
                  : vst.state == VOICE_STATE_GRAPH_ACTIVE ? backend_catalog_repo_label()
                                                          : NULL,
        .wifi = ws.state == WIFI_MGR_ONLINE ? UI_DIAG_WIFI_ONLINE
              : ws.state == WIFI_MGR_CONNECTING ? UI_DIAG_WIFI_JOINING
              : ws.state == WIFI_MGR_RETRY_WAIT ? UI_DIAG_WIFI_RETRY : UI_DIAG_WIFI_OFF,
        .api = bh.state == BACKEND_HEALTH_OK ? UI_DIAG_API_OK
             : bh.state == BACKEND_HEALTH_DOWN ? UI_DIAG_API_DOWN
             : bh.state == BACKEND_HEALTH_NO_NETWORK ? UI_DIAG_API_NO_NET : UI_DIAG_API_UNKNOWN,
    };
    ui_recording_apply(&ui->rec, &v);
}

/* Notification-playback overlay - see the static globals' comment above.
 * Same current-truth-from-voice_status_t reasoning as render_recording_overlay:
 * run_notification_command() blocks for the whole fetch+play+ack cycle. */
static void refresh_notify_page(void);

/* Records one finished playback for the card stack's "earlier" cards. */
static void notify_history_push(ui_notify_outcome_t outcome, const char *text, const char *reason)
{
    if (notify_history == NULL) {
        return;
    }
    int keep = notify_history_used < NOTIFY_HISTORY_LEN - 1 ? notify_history_used : NOTIFY_HISTORY_LEN - 1;
    for (int i = keep; i > 0; i--) {
        notify_history[i] = notify_history[i - 1];
    }
    notify_done_t *d = &notify_history[0];
    d->outcome = outcome;
    d->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    d->has_text = text && text[0];
    strlcpy(d->text, d->has_text ? text : "", sizeof(d->text));
    strlcpy(d->reason, reason ? reason : "", sizeof(d->reason));
    if (notify_history_used < NOTIFY_HISTORY_LEN) {
        notify_history_used++;
    }
}

/* Notification playback overlay (UI suite, ui_notify.c). voice_control's
 * last_result is the only progress signal (run_notification_command() blocks
 * for the whole fetch+play+ack), mapped to FETCH -> PLAY -> ACK steps. The
 * message text is the one notification_client loaded for the front message,
 * kept from the moment playback starts (the front moves on after the ack). */
static void render_notification_overlay(void)
{
    if (ui == NULL) {
        return;
    }
    static bool active, recorded;
    char *text = notify_playing_text;
    if (text == NULL) {
        return;
    }
    voice_status_t vst;
    voice_control_get_status(&vst);

    /* The house introduction is not a notification: keep the user's page
     * visible and never add it to notification history. */
    if (vst.state == VOICE_STATE_NOTIFICATION_ACTIVE &&
        strcmp(vst.active_request_id, "__mouse_intro__") == 0) {
        static bool warned;
        active = false;
        ui_notify_overlay_hide(&ui->notif);
        if (strncmp(vst.last_result, "MOUSE AUDIO FAILED:", 19) == 0) {
            if (!warned) ui_mouse_home_refresh_notice("VOICE UNAVAILABLE");
            warned = true;
        } else warned = false;
        return;
    }

    if (vst.state != VOICE_STATE_NOTIFICATION_ACTIVE) {
        active = false;
        ui_notify_overlay_hide(&ui->notif);
        return;
    }
    notification_status_t nst;
    notification_client_get_status(&nst);
    if (!active) {
        active = true;
        recorded = false;
        bool same = strcmp(nst.notification_id, vst.active_request_id) == 0 && nst.text_state == 1;
        if (same) {
            notification_client_get_text(text, NOTIFICATION_TEXT_LEN);
        } else {
            text[0] = '\0';
        }
    }

    const char *r = vst.last_result;
    ui_notify_pb_view_t v = { .text = text[0] ? text : NULL, .remaining = nst.pending ? nst.count : 0 };
    bool done = true;
    if (strcmp(r, "FETCHING") == 0 || strcmp(r, "NOTIFICATION FETCHING") == 0) {
        v.phase = UI_NOTIFY_PB_FETCHING;
        done = false;
    } else if (strcmp(r, "PLAYING") == 0) {
        v.phase = UI_NOTIFY_PB_PLAYING;
        done = false;
    } else if (strcmp(r, "NOTIFICATION DELIVERED") == 0) {
        v.phase = UI_NOTIFY_PB_DONE;
        v.outcome = UI_NOTIFY_DELIVERED;
    } else if (strcmp(r, "NOTIFICATION PLAYED - ACK FAILED") == 0) {
        v.phase = UI_NOTIFY_PB_DONE;
        v.outcome = UI_NOTIFY_ACK_FAILED;
    } else if (strcmp(r, "NOTIFICATION PLAYBACK STOPPED") == 0) {
        v.phase = UI_NOTIFY_PB_DONE;
        v.outcome = UI_NOTIFY_STOPPED;
    } else {
        /* "NOTIFICATION FETCH FAILED: <reason>" and anything unexpected */
        v.phase = UI_NOTIFY_PB_DONE;
        v.outcome = UI_NOTIFY_FETCH_FAILED;
        v.reason = r;
    }
    if (done && !recorded) {
        recorded = true;
        notify_history_push(v.outcome, text, v.outcome == UI_NOTIFY_FETCH_FAILED ? r : NULL);
        refresh_notify_page();
    }
    ui_notify_overlay_apply(&ui->notif, &v);
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

    /* Backend reachability, shown through the widget rather than another
     * status row - see voice_listening_widget_set_link(). Wi-Fi state is
     * shown separately on SETTINGS and DIAGNOSTICS; this is deliberately a
     * different signal, because the two fail independently and "router is
     * fine, uvicorn is not running" was previously indistinguishable from
     * a healthy console. Another cheap struct read, not a network call:
     * backend_health.c's own task does the probing. */
    backend_health_status_t bh;
    backend_health_get_status(&bh);
    voice_widget_link_t link;
    switch (bh.state) {
    case BACKEND_HEALTH_OK:         link = VOICE_WIDGET_LINK_OK; break;
    case BACKEND_HEALTH_DOWN:       link = VOICE_WIDGET_LINK_DOWN; break;
    case BACKEND_HEALTH_NO_NETWORK: link = VOICE_WIDGET_LINK_NO_NET; break;
    case BACKEND_HEALTH_UNKNOWN:
    default:                        link = VOICE_WIDGET_LINK_UNKNOWN; break;
    }
    if (weather_ui.mic) { ui_weather_page_set_link(&weather_ui,link); voice_listening_widget_set_notification(weather_ui.mic,nst.pending); }
    ui_home_set_link(&home_ui, link);   /* the widget, plus its caption */
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
    diag_set_cmd(name);   /* 1.2.1 crash breadcrumb (RTC memory, not NVS) */

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

#if DIAG_TEST_CRASH
/* ---- DIAG_TEST_CRASH: commissioning only, remove after 1.2.1 is proven --
 * Holding RPT for DIAG_TEST_CRASH_HOLD_MS (5 s) deliberately abort()s, so
 * the crash breadcrumb and the after-crash report can be checked end to end
 * on the real device. A normal tap still sends a report as before (CLICKED
 * fires on release, long before 5 s). To remove: set DIAG_TEST_CRASH to 0
 * in diag.h, or delete this block and the one in the log-button loop. */
static uint32_t rpt_pressed_at;
static void report_button_hold_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        rpt_pressed_at = lv_tick_get();
    } else if (code == LV_EVENT_PRESSING && lv_tick_elaps(rpt_pressed_at) >= DIAG_TEST_CRASH_HOLD_MS) {
        diag_test_crash("RPT held 5 s on LOG page");
    }
}
#endif

/* RPT (1.2.0, took SND's place): send a device report now. The result
 * arrives as a Black Box line via handshake_ui_timer_cb; a press while one
 * is already in flight says REPORT BUSY rather than silently doing nothing. */
static void report_button_cb(lv_event_t *e)
{
    (void)e;
    handle_command("RPT", true);   /* first, so the report includes it */
    if (!report_start("button")) {
        log_event(EVT_REPORT, "REPORT BUSY");
    }
}

/* Notification-playback volume (Mission 20 side control). 10-point steps -
 * coarse enough that a single tap is felt, fine enough that the full 0-100
 * range takes a reasonable number of taps; a fixed choice, not a
 * measured/tested one, same as PLAYBACK_VOLUME_DEFAULT itself
 * (audio_playback.c). Refreshes SETTINGS immediately so the readout
 * never lags behind what audio_playback_get_volume() would report. */
#define VOLUME_STEP 10

static void volume_up_button_cb(lv_event_t *e)
{
    audio_playback_set_volume(audio_playback_get_volume() + VOLUME_STEP);
    refresh_settings_page();
}

static void volume_down_button_cb(lv_event_t *e)
{
    audio_playback_set_volume(audio_playback_get_volume() - VOLUME_STEP);
    refresh_settings_page();
}

/* Project selector (Mission 20 side control), now driven by the catalog the
 * backend fetches from the AI-OS rather than a compiled-in list of four - see
 * backend_catalog.h. The options, their order and their labels all come from
 * Notion now, so there is nothing left here to keep in sync by hand.
 *
 * Two labels: the HOME control and the one on the recording overlay. Both are
 * driven from the same selection, so whichever is tapped, the other agrees -
 * there is one selection, shown in two places, never two to reconcile. (The
 * HOME one is currently unbuilt; the guard is what makes that harmless.) */
static void refresh_project_label(void)
{
    const char *text = backend_catalog_project_label();
    if (projects_page_label) {
        lv_label_set_text(projects_page_label, text);
    }
    if (projects_page_cue) {
        /* Say so plainly when there is no cue. An empty panel reads as broken;
         * "no cue set" is a fact about that AI-OS page worth knowing, and the
         * fix for it is one edit away in Notion. */
        const char *cue = backend_catalog_project_cue();
        lv_label_set_text(projects_page_cue, (cue && cue[0] != '\0') ? cue : "no cue set");
    }
}

static void recording_repo_next_cb(lv_event_t *e)
{
    backend_catalog_repo_next();
    refresh_settings_page();
    render_recording_overlay();
}

static void recording_repo_prev_cb(lv_event_t *e)
{
    backend_catalog_repo_prev();
    refresh_settings_page();
    render_recording_overlay();
}

static void project_up_button_cb(lv_event_t *e)
{
    (void)e;
    backend_catalog_project_next();
    refresh_project_label();
}

static void project_down_button_cb(lv_event_t *e)
{
    (void)e;
    backend_catalog_project_prev();
    refresh_project_label();
}

/* The recording overlay's one chooser: the project during a NOTE, the
 * repository during a GO (they were two overlapping widgets before). */
static void rec_sel_prev_cb(lv_event_t *e)
{
    voice_status_t vst;
    voice_control_get_status(&vst);
    if (vst.state == VOICE_STATE_GRAPH_ACTIVE) {
        recording_repo_prev_cb(e);
    } else {
        project_down_button_cb(e);
        render_recording_overlay();
    }
}

static void rec_sel_next_cb(lv_event_t *e)
{
    voice_status_t vst;
    voice_control_get_status(&vst);
    if (vst.state == VOICE_STATE_GRAPH_ACTIVE) {
        recording_repo_next_cb(e);
    } else {
        project_up_button_cb(e);
        render_recording_overlay();
    }
}

/* SEND control. Ships the last ACCEL_SAMPLE_COUNT accelerometer readings
 * (whatever is actually in the ring buffer - 0 if the IMU isn't present or
 * no sample has landed yet, never fabricated) rather than a bare ping.
 * Only logs/counts as a command when handshake_client_send actually
 * accepts it - a press the in-flight guard rejects (a request is already
 * SENDING) had no effect, so it shouldn't show up in the command history
 * or Black Box as if it did. */
__attribute__((unused))   /* dormant since 1.2.0 - see handshake_status_changed_cb */
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
    lv_obj_t *pages[APP_PAGE_COUNT] = {
        page_home, page_sens, page_log, page_set, page_projects, page_firmware, page_menu, page_diag,
        page_more, page_notify, page_more3, page_weather,
    };
    /* 1.2.1: which page was on screen, for the crash breadcrumb. Same
     * order as app_page_t. */
    static const char *const page_names[APP_PAGE_COUNT] = {
        "HOME", "SENS", "LOG", "SET", "PROJECTS", "FIRMWARE", "MENU", "DIAG", "MORE", "NOTIFY", "MORE3", "WEATHER",
    };
    current_app_page=page;
    diag_set_page(page_names[page]);
    ui_mouse_home_set_active(page == APP_PAGE_HOME);
    ui_home_set_active(page == APP_PAGE_HOME);
    for (int i = 0; i < APP_PAGE_COUNT; i++) {
        if (i == (int)page) {
            lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* The way back is on every page except the one it leads to. */
    if (page == APP_PAGE_HOME || page == APP_PAGE_WEATHER) {
        lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(back_button,0),page==APP_PAGE_MORE?LV_SYMBOL_LEFT " MENU":page==APP_PAGE_MORE3?LV_SYMBOL_LEFT " MORE":LV_SYMBOL_LEFT " HOME");
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

static void back_nav_cb(lv_event_t *e)
{
    (void)e;
    set_active_page(current_app_page==APP_PAGE_MORE?APP_PAGE_MENU:current_app_page==APP_PAGE_MORE3?APP_PAGE_MORE:APP_PAGE_HOME);
}

static void menu_button_cb(lv_event_t *e)
{
    (void)e;
    set_active_page(APP_PAGE_MENU);
}

static void sens_nav_button_cb(lv_event_t *e)
{
    set_active_page(APP_PAGE_SENS);
}

static void log_nav_button_cb(lv_event_t *e)
{
    set_active_page(APP_PAGE_LOG);
    render_event_log();   /* built only while on screen - see render_event_log() */
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
static void set_nav_button_cb(lv_event_t *e)
{
    (void)e;
    refresh_settings_page();   /* current on the first frame, not 500 ms later */
    set_active_page(APP_PAGE_SET);
}

static void projects_nav_button_cb(lv_event_t *e)
{
    (void)e;
    /* The list may have refreshed since this page was last drawn, and it is
     * only redrawn on a selection change otherwise. */
    refresh_project_label();
    set_active_page(APP_PAGE_PROJECTS);
}

static void firmware_nav_button_cb(lv_event_t *e)
{
    (void)e;
    refresh_firmware_page();
    set_active_page(APP_PAGE_FIRMWARE);
}

static void diag_nav_button_cb(lv_event_t *e)
{
    (void)e;
    refresh_diag_page();   /* current on the first frame, not a second later */
    set_active_page(APP_PAGE_DIAG);
}

static void more_nav_button_cb(lv_event_t *e)
{
    (void)e;
    set_active_page(APP_PAGE_MORE);
}

static void render_weather(void);
static void more3_nav_button_cb(lv_event_t *e) { (void)e; set_active_page(APP_PAGE_MORE3); }
static void weather_nav_button_cb(lv_event_t *e) { (void)e; weather_force_update=true; set_active_page(APP_PAGE_WEATHER); render_weather(); }

static __attribute__((unused)) void notify_nav_button_cb(lv_event_t *e)
{
    (void)e;
    refresh_notify_page();
    set_active_page(APP_PAGE_NOTIFY);
}

/* PLAY on the front card: the same fetch/play/ack a spoken YES runs. */
static void notify_play_cb(lv_event_t *e)
{
    (void)e;
    voice_control_play_notification();
}

/* ---- Experiments (UI suite): built full-screen on open, deleted on close,
 * so they cost nothing while unused. Each returns to MORE. ---------------- */

/* SURVEY: sample questions; "saved" means kept on this device - one serial
 * log line per response and a Black Box line. Nothing is sent anywhere. */
static uint32_t survey_responses;

static bool survey_submit(const int *a, uint32_t *no, void *ctx)
{
    (void)ctx;
    *no = ++survey_responses;
    ESP_LOGI(TAG, "SURVEY,%lu,%d,%d,%d (sample questions; kept on device only)", (unsigned long)*no, a[0], a[1],
             a[2]);
    char msg[EVENT_MSG_LEN];
    snprintf(msg, sizeof(msg), "SURVEY RESPONSE #%lu", (unsigned long)*no);
    log_event(EVT_SYSTEM, msg);
    return true;
}

static void experiment_closed(void *unused)
{
    (void)unused;
    set_active_page(APP_PAGE_MORE);
}

static void survey_exit(void *ctx)
{
    (void)ctx;
    ui_survey_close();
    experiment_closed(NULL);
}

static const ui_survey_config_t survey_cfg = {
    .questions = ui_survey_sample_questions,
    .qr_url = "https://example.com/feedback",        /* SAMPLE until the real link is decided */
    .qr_caption = "Sample link:\nexample.com",
    .saved_note = "Saved on this device only (sample mode).",
    .submit = survey_submit,
    .exit = survey_exit,
    .idle_reset_s = 90,
    .thanks_reset_s = 30,
};

static lv_obj_t *tour_selector;
static void tour_selector_deleted(lv_event_t *e)
{
    (void)e;
    tour_selector = NULL;
}
static void tour_selector_close(void)
{
    if (tour_selector) lv_obj_delete(tour_selector);
}
static void tour_back_cb(lv_event_t *e)
{
    (void)e;
    tour_selector_close();
}
static void tour_a_cb(lv_event_t *e)
{
    (void)e;
    tour_selector_close();
    ui_adventure_open(app_screen, experiment_closed, NULL);
}
static void tour_b_cb(lv_event_t *e)
{
    (void)e;
    tour_selector_close();
    ui_adventure_b_open(app_screen, experiment_closed, NULL);
}
static void adventure_open_cb(lv_event_t *e)
{
    (void)e;
    if (tour_selector) return;
    tour_selector = ui_tour_selector_create(app_screen, tour_a_cb, tour_b_cb,
                                            tour_back_cb, tour_selector_deleted);
}

static void survey_open_cb(lv_event_t *e)
{
    (void)e;
    ui_survey_open(app_screen, &survey_cfg);
}

/* MUSIC: simulated - the mock answers, nothing reaches a laptop. */
static ui_music_mock_t *music_mock;   /* PSRAM, only while MUSIC is open */

static void music_exit(void *ctx)
{
    (void)ctx;
    if (music_mock) {
        ui_music_mock_stop(music_mock);
    }
    ui_music_close();
    heap_caps_free(music_mock);
    music_mock = NULL;
    experiment_closed(NULL);
}

static void __attribute__((unused)) music_open_cb(lv_event_t *e)
{
    (void)e;
    if (music_mock == NULL) {
        music_mock = heap_caps_calloc(1, sizeof(*music_mock), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (music_mock == NULL) {
            return;
        }
    }
    ui_music_mock_init(music_mock);
    ui_music_open(app_screen, &music_mock->view, ui_music_mock_command, music_exit, music_mock);
}

static lv_timer_t *audio_test_timer;
static void audio_test_open_cb(lv_event_t *e);
static lv_timer_t *newton_timer;
static newton_scene_t newton_scene;
static newton_saved_t newton_saved;
static bool newton_loaded, newton_manual_wind, newton_requested_wind;
static uint32_t newton_last_tick, newton_start_seq, newton_fail_seq;
static uint32_t newton_max_poll_ms, newton_poll_count;
static void newton_poll(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now=lv_tick_get();
    uint32_t delta=now-newton_last_tick;
    if(delta>newton_max_poll_ms)newton_max_poll_ms=delta;
    newton_poll_count++;
    newton_scene_tick(&newton_scene,delta);newton_last_tick=now;
    audio_test_status_t audio;audio_test_get_status(&audio);
    if(audio.voice_started_seq!=newton_start_seq) {
        newton_start_seq=audio.voice_started_seq;
        newton_scene_voice_ack(&newton_scene,true,audio.voice_clip);
    }
    if(audio.voice_failed_seq!=newton_fail_seq) {
        newton_fail_seq=audio.voice_failed_seq;
        newton_scene_voice_ack(&newton_scene,false,0);
    }
    newton_scene_audio_state(&newton_scene,audio.running,audio.wind,audio.voice);
    if(newton_scene_take_voice_request(&newton_scene) && !audio_test_request_voice())
        newton_scene_voice_ack(&newton_scene,false,0);
    bool wind=newton_manual_wind || newton_scene.gust_left_ms>0;
    if(audio.running && wind!=newton_requested_wind) {
        audio_test_set_wind(wind);newton_requested_wind=wind;
    }
    if(newton_scene.dirty) {
        newton_scene_save(&newton_scene,&newton_saved);
        newton_scene.storage_failed=!newton_store_save(&newton_saved);
        newton_scene.dirty=false; /* bounded event writes, never every frame */
    }
    ui_newton_update(&newton_scene,audio.message);
}
static void newton_action(ui_newton_action_t action,void *ctx)
{
    (void)ctx;
    if(action==UI_NEWTON_SPEAK)newton_scene_tap_person(&newton_scene);
    else if(action==UI_NEWTON_WIND)newton_manual_wind=!newton_manual_wind;
    else if(action==UI_NEWTON_TREE)(void)newton_scene_drop(&newton_scene);
    else {
        ESP_LOGI(TAG,"Newton: exit, cancel audio and release scene");
        audio_test_cancel();
        if(newton_timer)lv_timer_delete(newton_timer);
        newton_timer=NULL;
        newton_scene_save(&newton_scene,&newton_saved);
        newton_scene_close(&newton_scene);ui_newton_close();
        (void)newton_store_save(&newton_saved);
        ESP_LOGI(TAG,"Newton UI closed: polls=%lu max_interval_ms=%lu internal=%u largest=%u PSRAM=%u",
            (unsigned long)newton_poll_count,(unsigned long)newton_max_poll_ms,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        audio_test_open_cb(NULL);
    }
}
static void newton_open(void)
{
    if(newton_timer)return;
    if(!newton_loaded) { (void)newton_store_load(&newton_saved);newton_loaded=true; }
    newton_scene_open(&newton_scene,&newton_saved);
    audio_test_status_t audio;audio_test_get_status(&audio);
    newton_start_seq=audio.voice_started_seq;newton_fail_seq=audio.voice_failed_seq;
    newton_manual_wind=newton_requested_wind=audio.wind;
    newton_scene_audio_state(&newton_scene,audio.running,audio.wind,audio.voice);
    if(audio.voice) {
        newton_scene.voice_pending=true;
        newton_scene_voice_ack(&newton_scene,true,audio.voice_clip);
    }
    ui_newton_open(app_screen,newton_art_get(),newton_action,NULL);
    newton_last_tick=lv_tick_get();
    newton_max_poll_ms=newton_poll_count=0;
    newton_timer=lv_timer_create(newton_poll,50,NULL);
    ESP_LOGI(TAG,"Newton: entry using existing SD audio owner; native_art=%d",ui_newton_art_valid(newton_art_get()));
    ESP_LOGI(TAG,"Newton UI open: internal=%u largest=%u PSRAM=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    newton_poll(NULL);
}
static lv_timer_t *lincoln_timer;
static lincoln_scene_t lincoln;
static uint32_t lincoln_last_tick,lincoln_start_seq,lincoln_fail_seq,lincoln_done_seq,lincoln_stop_seq;
static void lincoln_poll(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now=lv_tick_get();lincoln_scene_tick(&lincoln,now-lincoln_last_tick);lincoln_last_tick=now;
    audio_test_status_t audio;audio_test_get_status(&audio);
    bool ready=audio.running&&audio.profile==AUDIO_PROFILE_LINCOLN;
    lincoln_scene_ready(&lincoln,ready);
    if(audio.voice_started_seq!=lincoln_start_seq){lincoln_start_seq=audio.voice_started_seq;lincoln_scene_voice_started(&lincoln,audio.started_id,true);}
    if(audio.voice_failed_seq!=lincoln_fail_seq){lincoln_fail_seq=audio.voice_failed_seq;lincoln_scene_voice_started(&lincoln,audio.failed_id,false);}
    if(audio.voice_completed_seq!=lincoln_done_seq){lincoln_done_seq=audio.voice_completed_seq;lincoln_scene_voice_finished(&lincoln,audio.completed_id,true);}
    if(audio.voice_stopped_seq!=lincoln_stop_seq){lincoln_stop_seq=audio.voice_stopped_seq;lincoln_scene_voice_stopped(&lincoln,audio.stopped_id);}
    uint32_t id;unsigned clip,effect;
    if(lincoln_scene_take_stop(&lincoln,&id)){
        if(ready)audio_test_lincoln_stop(id);else lincoln_scene_voice_stopped(&lincoln,id);
    }
    if(lincoln_scene_take_voice(&lincoln,&id,&clip)&&!audio_test_lincoln_voice(id,clip))lincoln_scene_voice_started(&lincoln,id,false);
    if(lincoln_scene_take_paper(&lincoln)&&!audio_test_lincoln_sfx(AUDIO_SFX_PAPER))audio_test_error("Paper effect unavailable");
    if(lincoln_scene_take_cabin(&lincoln,&effect)&&!audio_test_lincoln_sfx((audio_sfx_t)effect)){
        lincoln.cabin_left_ms=0;audio_test_error("Effect busy - tap again shortly");
    }
    bool error=strstr(audio.message,"unavailable")||strstr(audio.message,"busy")||strstr(audio.message,"missing")||strstr(audio.message,"failed");
    ui_lincoln_update(&lincoln,(!ready||error)?audio.message:NULL);
}
static void lincoln_action(ui_lincoln_event_t event,void *ctx)
{
    (void)ctx;
    if(event==UI_LINCOLN_PERSON)lincoln_scene_tap_person(&lincoln);
    else if(event==UI_LINCOLN_CABIN)(void)lincoln_scene_tap_cabin(&lincoln);
    else {
        audio_test_cancel();lincoln_scene_close(&lincoln);
        if(lincoln_timer)lv_timer_delete(lincoln_timer);
        lincoln_timer=NULL;ui_lincoln_close();
        ESP_LOGI(TAG,"Lincoln exit: internal=%u largest=%u PSRAM=%u",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        audio_test_open_cb(NULL);
    }
}
static void lincoln_open(void)
{
    if(lincoln_timer)return;
    const lincoln_timing_t timing=lincoln_scene_native_timing();
    (void)lincoln_scene_open(&lincoln,&timing);
    audio_test_status_t audio;audio_test_get_status(&audio);
    lincoln_start_seq=audio.voice_started_seq;lincoln_fail_seq=audio.voice_failed_seq;
    lincoln_done_seq=audio.voice_completed_seq;lincoln_stop_seq=audio.voice_stopped_seq;
    ui_lincoln_open(app_screen,lincoln_art_get(),lincoln_action,NULL);lincoln_last_tick=lv_tick_get();
    lincoln_timer=lv_timer_create(lincoln_poll,50,NULL);lincoln_poll(NULL);
    ESP_LOGI(TAG,"Lincoln draft open: internal=%u largest=%u PSRAM=%u",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
static void audio_test_poll(lv_timer_t *timer)
{
    (void)timer;
    ui_audio_test_view_t view={.message="Choose a scene"};ui_audio_test_update(&view);
}
static void audio_test_action(ui_audio_action_t action,void *ctx)
{
    (void)ctx;
    if(action==UI_AUDIO_GARDEN||action==UI_AUDIO_LINCOLN){
        bool accepted=action==UI_AUDIO_GARDEN?voice_control_request_audio_test():voice_control_request_lincoln();
        if(!accepted){ui_audio_test_view_t view={.message="Audio busy - try again shortly"};ui_audio_test_update(&view);return;}
        if(audio_test_timer)lv_timer_delete(audio_test_timer);
        audio_test_timer=NULL;ui_audio_test_close();
        if(action==UI_AUDIO_GARDEN)newton_open();else lincoln_open();
    } else if(action==UI_AUDIO_BACK){
        if(audio_test_timer)lv_timer_delete(audio_test_timer);
        audio_test_timer=NULL;ui_audio_test_close();experiment_closed(NULL);
    }
}
static void audio_test_open_cb(lv_event_t *e)
{
    (void)e;if(audio_test_timer)return;
    ui_audio_test_open(app_screen,audio_test_action,NULL);
    audio_test_timer=lv_timer_create(audio_test_poll,1000,NULL);audio_test_poll(NULL);
}

/* The standalone Mouse and Face screens are retired. Shared HOME mouse and
 * notification/audio ownership remain in their existing modules. */

/* Asks the updater to fetch CLAWBOX's manifest now rather than waiting for
 * the next poll. It does NOT force an install - the device still installs
 * only the version CLAWBOX has named as desired, so this button is "look
 * again", not "update me". */
static void firmware_check_button_cb(lv_event_t *e)
{
    (void)e;
    ota_service_request_check();
}

/* A tapped command tile. The command id rides in the event user data rather
 * than being looked up from the button, so the grid can be reordered without
 * this needing to know. */
static void cmd_tile_cb(lv_event_t *e)
{
    voice_command_id_t id = (voice_command_id_t)(intptr_t)lv_event_get_user_data(e);
    voice_control_manual_command(id);
}

static void talk_button_cb(lv_event_t *e)
{
    voice_control_manual_wake();
}

/* SEND control on the shared SEND/NOTE/GO recording overlay (Mission 13,
 * relabeled from STOP - see the button's own on-screen text). Calls
 * straight into audio_capture.c - no reason to route through
 * voice_control.c, which is already just polling audio_capture_get_status()
 * waiting for this to take effect. Harmless if pressed outside RECORDING
 * (see audio_capture_stop()'s doc comment). */
static void recording_stop_button_cb(lv_event_t *e)
{
    audio_capture_stop();
}

/* CANCEL control on the shared SEND/NOTE/GO recording overlay - discards
 * the recording instead of sending it, see audio_capture_cancel()'s doc
 * comment. Same "call straight into audio_capture.c" shape as SEND above. */
static void recording_cancel_button_cb(lv_event_t *e)
{
    audio_capture_cancel();
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

static void refresh_firmware_page(void)
{
    if (fw_page.version == NULL) {
        return;   /* page not built yet */
    }

    ota_status_t o;
    ota_service_get_status(&o);
    const ui_firmware_view_t view = {
        .version = firmware_version(),
        .built_at = firmware_built_at(),
        .backend_url = device_config_backend_base_url(),
        .backend_fallback = device_config_backend_on_fallback(),
        .slot = firmware_running_partition_label(),
        .pending_verify = firmware_is_pending_verify(),
        .ota_state = (ui_fw_state_t)o.state,   /* ui_fw_state_t mirrors ota_state_t */
        .ota_message = o.message,
        .desired_version = o.desired_version,
        .ota_progress_pct = o.progress_pct,
        .rolled_back = o.rolled_back,
    };
    ui_firmware_page_apply(&fw_page, &view);
}

/* DIAGNOSTICS: one snapshot of what the subsystems already track, read on the
 * LVGL task (telemetry_timer_cb while the page is visible, and on entry).
 * Nothing new is polled: wifi_mgr / backend_health snapshots are safe from
 * any task, the sensor timestamps are copied under their existing muxes, and
 * freshness is derived from those timestamps with the existing thresholds
 * (ui_diag_freshness) rather than from the logging state machine. The one
 * new call is esp_wifi_sta_get_ap_info() for RSSI, only while ONLINE - the
 * same call report_client.c makes. */
static void refresh_diag_page(void)
{
    if (diag_page.q_link == NULL) {
        return;   /* page not built yet */
    }

    wifi_mgr_status_t ws;
    wifi_mgr_get_status(&ws);
    backend_health_status_t bh;
    backend_health_get_status(&bh);
    backend_health_stats_t bs;
    backend_health_get_stats(&bs);

    portENTER_CRITICAL(&sensor_mux);
    int64_t accel_last = sensor_last_good_us;
    portEXIT_CRITICAL(&sensor_mux);
    portENTER_CRITICAL(&humiture_mux);
    int64_t env_last = humiture_last_good_us;
    portEXIT_CRITICAL(&humiture_mux);
    int64_t now = esp_timer_get_time();

    ui_diag_view_t v = {
        .wifi = ws.state == WIFI_MGR_ONLINE       ? UI_DIAG_WIFI_ONLINE
              : ws.state == WIFI_MGR_CONNECTING   ? UI_DIAG_WIFI_JOINING
              : ws.state == WIFI_MGR_RETRY_WAIT   ? UI_DIAG_WIFI_RETRY
                                                  : UI_DIAG_WIFI_OFF,
        .wifi_reason = ws.reason_text,
        .wifi_attempts = ws.attempt_count,
        .wifi_retry_s = ws.retry_remaining_s,
        .api = bh.state == BACKEND_HEALTH_OK         ? UI_DIAG_API_OK
             : bh.state == BACKEND_HEALTH_DOWN       ? UI_DIAG_API_DOWN
             : bh.state == BACKEND_HEALTH_NO_NETWORK ? UI_DIAG_API_NO_NET
                                                     : UI_DIAG_API_UNKNOWN,
        .api_ever_ok = bs.ever_ok,
        .api_secs_since_ok = bs.secs_since_ok,
        .uptime_s = (uint64_t)now / 1000000ULL,
        .heap_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        .heap_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        .heap_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
    };
    if (ws.state == WIFI_MGR_ONLINE) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            v.have_rssi = true;
            v.rssi_dbm = ap.rssi;
        }
    }
    v.accel = ui_diag_freshness(sensor_probed, sensor_present, accel_last, now, SENSOR_STALE_THRESHOLD_US,
                                &v.accel_age_ms);
    v.env = ui_diag_freshness(humiture_probed, humiture_present, env_last, now, HUMITURE_STALE_THRESHOLD_US,
                              &v.env_age_ms);
    ui_diagnostics_apply(&diag_page, &v);
}

/* MESSAGES card stack: notification_client's snapshot (count, front id and
 * its text) plus this boot's outcomes. */
static void refresh_notify_page(void)
{
    if (ui == NULL || ui->notify.card == NULL) {
        return;
    }
    ui_notify_done_t *done = ui->notify_done;   /* PSRAM; borrowed by the page until the next apply */
    notification_status_t nst;
    notification_client_get_status(&nst);
    if (notify_front_text) {
        notification_client_get_text(notify_front_text, NOTIFICATION_TEXT_LEN);
    }
    backend_health_status_t bh;
    backend_health_get_status(&bh);
    for (int i = 0; i < notify_history_used && notify_history; i++) {
        done[i].outcome = notify_history[i].outcome;
        done[i].uptime_s = notify_history[i].uptime_s;
        done[i].text = notify_history[i].has_text ? notify_history[i].text : NULL;
        done[i].reason = notify_history[i].reason[0] ? notify_history[i].reason : NULL;
    }
    ui_notify_view_t v = {
        .link = !nst.polled ? UI_NOTIFY_LINK_UNKNOWN
              : bh.state == BACKEND_HEALTH_OK ? UI_NOTIFY_LINK_OK
              : bh.state == BACKEND_HEALTH_NO_NETWORK ? UI_NOTIFY_LINK_NO_NET
              : bh.state == BACKEND_HEALTH_DOWN ? UI_NOTIFY_LINK_DOWN : UI_NOTIFY_LINK_UNKNOWN,
        .pending = nst.pending ? nst.count : 0,
        .front_text = nst.text_state == 1 && notify_front_text && notify_front_text[0] ? notify_front_text : NULL,
        .front_text_failed = nst.text_state == 2,
        .done = done,
        .done_count = notify_history_used,
        .now_s = (uint32_t)(esp_timer_get_time() / 1000000ULL),
    };
    ui_notify_apply(&ui->notify, &v);
}

/* ---- Weather (1.3.0): weather_client snapshot -> HOME ----------------
 * Checked once a second from telemetry_timer_cb, redrawn only when something
 * visible changes: a new answer, the data crossing the "old" line, or the
 * reason there is no data. Thirty minutes is three missed polls - one late
 * answer is normal, three in a row means the numbers on screen deserve a
 * timestamp. */
#define WEATHER_STALE_S (30 * 60)

static void home_intro_cb(lv_event_t *e)
{
    (void)e;
    ui_home_intro_audio_result(voice_control_request_mouse_intro());
}
static void home_tour_return(void *ctx)
{
    (void)ctx;
    set_active_page(APP_PAGE_HOME);
}
static void home_start_tour_cb(lv_event_t *e)
{
    (void)e;
    if (ui_adventure_is_open()) return;
    ui_home_set_active(false);
    ui_adventure_open(app_screen, home_tour_return, NULL);
    ui_adventure_seek(ADVENTURE_EGYPT);
}

static void render_weather(void)
{
    static uint32_t shown_seq = UINT32_MAX;
    static uint32_t manual_ok, manual_fail;
    static int shown_mode = -1;   /* 0 loading, 1 no weather, 2 fresh, 3 stale */

    weather_status_t w;
    weather_client_get(&w);
    bool manual_changed = w.manual_success != manual_ok;
    if (manual_changed) manual_ok=w.manual_success;
    if (w.manual_failure != manual_fail) { manual_fail=w.manual_failure; ui_mouse_home_refresh_result(false); }
    int mode;
    if (w.valid) {
        mode = w.age_s > WEATHER_STALE_S ? 3 : 2;
    } else {
        backend_health_status_t bh;
        backend_health_get_status(&bh);
        mode = (bh.state == BACKEND_HEALTH_DOWN || bh.state == BACKEND_HEALTH_NO_NETWORK) ? 1 : 0;
    }
    if (mode == shown_mode && w.seq == shown_seq && !weather_force_update) {
        return;
    }
    /* A freshness-only change is not another weather snapshot. Revisit it
     * after the active sequence completes instead of queuing a duplicate. */
    if (ui_home_weather_stage_running() && w.seq == shown_seq) return;
    shown_mode = mode;
    shown_seq = w.seq;

    ui_home_weather_t v = {
        .valid = w.valid,
        .status = mode == 1 ? "No weather" : "Loading weather",
        .location = w.valid ? w.location : NULL,
        .stale_as_of = mode == 3 ? w.as_of : NULL,
        .now_temp = w.now_temp,
        .now_icon = w.now_icon,
        .now_text = w.now_text,
        .now_rain = w.now_rain,
        .tmrw_hi = w.tmrw_hi,
        .tmrw_lo = w.tmrw_lo,
        .tmrw_icon = w.tmrw_icon,
        .tmrw_rain = w.tmrw_rain,
    };
    for (int i = 0; i < UI_HOME_SLOTS && i < WEATHER_SLOTS; i++) {
        v.slot[i].label = w.slot[i].label;
        v.slot[i].temp = w.slot[i].temp;
        v.slot[i].icon = w.slot[i].icon;
        v.slot[i].rain = w.slot[i].rain;
    }
    weather_force_update=false;
    ui_weather_page_apply_weather(&weather_ui, &v);
    /* A reply received after leaving HOME must not start a paused animation
     * and strand the displayed data until the user returns. */
    bool home_visible = page_home && lv_obj_is_visible(page_home);
    if (home_visible && w.valid && (manual_changed || ui_home_weather_stage_running())) {
        bool started = ui_home_weather_stage_offer(&w);
        if (started) ui_mouse_home_refresh_result(true);
        return;
    }
    if (ui_home_weather_stage_running()) ui_home_weather_stage_cancel();
    ui_home_apply_weather(&home_ui, &v);
}

static void telemetry_timer_cb(lv_timer_t *t)
{
    render_weather();

    /* Only while it is on screen - this runs every second regardless of page,
     * and redrawing five hidden labels is work nobody sees. */
    if (page_firmware != NULL && !lv_obj_has_flag(page_firmware, LV_OBJ_FLAG_HIDDEN)) {
        refresh_firmware_page();
    }
    if (page_diag != NULL && !lv_obj_has_flag(page_diag, LV_OBJ_FLAG_HIDDEN)) {
        refresh_diag_page();
    }

    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    if (page_notify != NULL && !lv_obj_has_flag(page_notify, LV_OBJ_FLAG_HIDDEN)) {
        refresh_notify_page();   /* "4 min ago" and the queue move on their own */
    }

    diag_tick();
    diag_report_schedule(uptime_s);
}

/* ---- SENS (UI suite): dials from the same freshness rules as DIAGNOSTICS -
 * Read on the LVGL task: the sensor timestamps are copied under their
 * existing muxes, values were fixed-pointed when they arrived. */
static int32_t sens_accel_x100, sens_temp_x10, sens_humi_pct;
static bool sens_have_accel, sens_have_env;

static void refresh_sens_page(void)
{
    if (ui == NULL || ui->sens.chart[0] == NULL) {
        return;
    }
    portENTER_CRITICAL(&sensor_mux);
    int64_t accel_last = sensor_last_good_us;
    portEXIT_CRITICAL(&sensor_mux);
    portENTER_CRITICAL(&humiture_mux);
    int64_t env_last = humiture_last_good_us;
    portEXIT_CRITICAL(&humiture_mux);
    int64_t now = esp_timer_get_time();
    ui_sensors_view_t v = { 0 };
    v.r[UI_SENS_ACCEL].src = ui_diag_freshness(sensor_probed, sensor_present, accel_last, now,
                                               SENSOR_STALE_THRESHOLD_US, &v.r[UI_SENS_ACCEL].age_ms);
    v.r[UI_SENS_ACCEL].have_value = sens_have_accel;
    v.r[UI_SENS_ACCEL].value = sens_accel_x100;
    ui_diag_src_t env = ui_diag_freshness(humiture_probed, humiture_present, env_last, now,
                                          HUMITURE_STALE_THRESHOLD_US, &v.r[UI_SENS_TEMP].age_ms);
    v.r[UI_SENS_TEMP].src = v.r[UI_SENS_HUMI].src = env;
    v.r[UI_SENS_HUMI].age_ms = v.r[UI_SENS_TEMP].age_ms;
    v.r[UI_SENS_TEMP].have_value = v.r[UI_SENS_HUMI].have_value = sens_have_env;
    v.r[UI_SENS_TEMP].value = sens_temp_x10;
    v.r[UI_SENS_HUMI].value = sens_humi_pct;
    ui_sensors_apply(&ui->sens, &v);
}

/* ---- Sensor: fast timer, matched to SENSOR_ACQUIRE_PERIOD_MS ---------- */

static void sensor_timer_cb(lv_timer_t *t)
{
    /* The dials redraw at 5 Hz while SENS is on screen: fast enough to see
     * a shake and to turn STALE within its 300 ms threshold's next tick. */
    static int ticks;
    if (page_sens != NULL && !lv_obj_has_flag(page_sens, LV_OBJ_FLAG_HIDDEN) && (++ticks & 1) == 0) {
        refresh_sens_page();
    }
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
        sens_accel_x100 = ui_sens_fixed(UI_SENS_ACCEL, accel_snapshot);
        sens_have_accel = true;
        if (ui) {
            ui_sensors_push(&ui->sens, UI_SENS_ACCEL, sens_accel_x100);
        }
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
}

/* Boot-time hardware probe: bsp_sensor_init/iot_sensor_create fail
 * synchronously if the ICM42670 does not ACK on the onboard I2C bus,
 * which is the honest "not detected" signal - not something inferred
 * from later staleness. There is no runtime retry if this fails; that is
 * a known limitation for this mission, not a hidden assumption. */
static void live_data_deck_init(void)
{
    sensor_probed = true;   /* from here on, !sensor_present means the probe failed */
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
         * converted to Fahrenheit here, once, for the dial and its trend. */
        float temp_f = temp_snapshot * 9.0f / 5.0f + 32.0f;
        sens_temp_x10 = ui_sens_fixed(UI_SENS_TEMP, temp_f);
        if (ui) {
            ui_sensors_push(&ui->sens, UI_SENS_TEMP, sens_temp_x10);
        }
    }
    if (got_humi) {
        sens_humi_pct = ui_sens_fixed(UI_SENS_HUMI, humi_snapshot);
        if (ui) {
            ui_sensors_push(&ui->sens, UI_SENS_HUMI, sens_humi_pct);
        }
    }
    if (got_temp || got_humi) {
        sens_have_env = true;
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
    humiture_probed = true;
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
    event_history_alloc();
    ui = heap_caps_calloc(1, sizeof(*ui), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ui == NULL) {
        ui = heap_caps_calloc(1, sizeof(*ui), MALLOC_CAP_8BIT);   /* no PSRAM: keep working */
    }
    blackbox_boot_update();

    /* Widgets don't exist yet: record only, render once they do. */
    char boot_msg[EVENT_MSG_LEN];
    snprintf(boot_msg, sizeof(boot_msg), "BOOT #%lu", (unsigned long)blackbox.boot_count);
    event_push_only(EVT_BOOT, boot_msg);
    char reset_msg[EVENT_MSG_LEN];
    snprintf(reset_msg, sizeof(reset_msg), "RST %s", reset_reason_str(esp_reset_reason()));
    event_push_only(EVT_RESET, reset_msg);
    if (blackbox.prev_was_self_restart) {
        /* The last boot ended because the backend had been silent for ten
         * minutes. Say so on the LOG page, and send the evidence as soon as
         * the backend answers. */
        event_push_only(EVT_BACKEND, "PREV RST: API WATCHDOG");
        report_pending_trigger = "auto-watchdog";
    }
    const diag_crash_t *crash = diag_crash();
    if (crash->happened) {
        /* 1.2.1: say so on the LOG page; diag_report_schedule() sends the
         * after-crash report (with the breadcrumb) once the backend answers. */
        char crash_msg[EVENT_MSG_LEN];
        snprintf(crash_msg, sizeof(crash_msg), "PREV: %s %s", crash->kind,
                 crash->breadcrumb_valid ? crash->page : "(no crumb)");
        event_push_only(EVT_RESET, crash_msg);
    }
    if (diag_auto_reports_off()) {
        /* 1.2.2 loop guard: the last boot reset while a report was being
         * sent. Automatic reports are off until the next boot; RPT works.
         * (EVENT_MSG_LEN is 32; diag logs the full sentence.) */
        event_push_only(EVT_REPORT, "AUTO RPT OFF: CRASH IN REPORT");
    }
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
        /* Full screen. The corner buttons float on top; each page is
         * responsible for keeping its own content clear of them. */
        pages_init[i] = ui_page_create(scr);
    }
    page_home = pages_init[APP_PAGE_HOME];
    page_sens = pages_init[APP_PAGE_SENS];
    page_log = pages_init[APP_PAGE_LOG];
    page_projects = pages_init[APP_PAGE_PROJECTS];
    page_firmware = pages_init[APP_PAGE_FIRMWARE];
    page_set = pages_init[APP_PAGE_SET];
    page_menu = pages_init[APP_PAGE_MENU];
    page_diag = pages_init[APP_PAGE_DIAG];
    page_more = pages_init[APP_PAGE_MORE];
    page_notify = pages_init[APP_PAGE_NOTIFY];
    page_more3 = pages_init[APP_PAGE_MORE3];
    page_weather = pages_init[APP_PAGE_WEATHER];
    app_screen = scr;
    notify_history = heap_caps_calloc(NOTIFY_HISTORY_LEN, sizeof(notify_done_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    notify_front_text = heap_caps_calloc(1, NOTIFICATION_TEXT_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    notify_playing_text = heap_caps_calloc(1, NOTIFICATION_TEXT_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    /* 1.3.0 dark look: black behind everything (ui_theme.h). */
    lv_obj_set_style_bg_color(scr, UI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ---- HOME: weather, the microphone, MENU (1.3.0) ------------------ */

    /* The microphone still shows whether the workstation is listening and
     * reachable, and is still the TALK button - now a quarter of the screen
     * beside Orlando's weather. See ui_home.c. */
    /* House is Home; mouse owns the existing introduction audio request. */
    ui_home_build(&home_ui, page_home, talk_button_cb, menu_button_cb);
    ui_home_set_actions(home_nav_button_cb, home_start_tour_cb, home_intro_cb);
    voice_widget = home_ui.mic;

    /* ---- MENU: every other page, one tap from HOME -------------------- */

    static const ui_menu_item_t menu_items[] = {
        { "SENSORS", sens_nav_button_cb },
        { "LOG", log_nav_button_cb },
        { "PROJECTS", projects_nav_button_cb },
        { "FIRMWARE", firmware_nav_button_cb },
        { "SETTINGS", set_nav_button_cb },
        /* "DIAGNOSTICS" at 20 px fills a 151 px button edge to edge; the
         * page itself carries the full title. */
        { "DIAG", diag_nav_button_cb },
    };
    static const ui_menu_item_t more_key = { "MORE " LV_SYMBOL_RIGHT, more_nav_button_cb };
    ui_menu_build(page_menu, "MENU", menu_items, (int)(sizeof(menu_items) / sizeof(menu_items[0])), &more_key);

    /* UI suite: the second page - the card stack and three experiments. */
    static const ui_menu_item_t more_items[] = {
        { "AUDIO TEST", audio_test_open_cb },
        { "SURVEY", survey_open_cb },
        { "ADVENTURE", adventure_open_cb },
    };
    static const ui_menu_item_t menu_key = { "NEXT " LV_SYMBOL_RIGHT, more3_nav_button_cb };
    ui_menu_build(page_more, "MORE", more_items, (int)(sizeof(more_items) / sizeof(more_items[0])), &menu_key);
    const ui_menu_picture_item_t pictures[] = {
        { "Weather", weather_icon("partly",true), 320, weather_nav_button_cb },
        { "Adventure", ui_adventure_original_asset(TOUR_ASSET_VAN,0), 320, adventure_open_cb },
    };
    ui_menu_build_pictures(page_more3,"EXPLORE",pictures,(int)(sizeof(pictures)/sizeof(pictures[0])),NULL);
    ui_weather_page_build(&weather_ui,page_weather,talk_button_cb,more3_nav_button_cb);


    static const ui_notify_actions_t notify_actions = { notify_play_cb };
    (void)notify_actions; /* Messages screen retired; notification playback stays. */

    /* ---- SETTINGS (UI suite, ui_settings.c): Hazard Industrial blocks ---- */

    static const ui_settings_actions_t settings_actions = {
        volume_down_button_cb, volume_up_button_cb, recording_repo_prev_cb, recording_repo_next_cb,
        projects_nav_button_cb, firmware_nav_button_cb,
    };
    ui_settings_build(&ui->settings, page_set, &settings_actions);

    /* ---- PROJECTS: browse the AI-OS list, and read the cues ----------- */

    /* This page exists because the cue had nowhere honest to live. It was a
     * toast on the recording overlay, which meant it could only be read while
     * a capture was running, and it covered the counter to do it. Here it is
     * just shown - no tap, no timer, no overlap.
     *
     * The selection is the same one NOTE uses, so choosing here is choosing
     * for the next NOTE. That is the point: pick the project while you are
     * thinking about it, not while you are trying to talk.
     *
     *    y  58..126   the selector: < LABEL >
     *    y 132..206   the cue for whatever is selected
     */
    ui_page_title(page_projects, "PROJECTS");

    lv_obj_t *proj_ctrl = ui_page_panel(page_projects, 58, 68);

    lv_obj_t *proj_prev = lv_btn_create(proj_ctrl);
    lv_obj_set_size(proj_prev, 56, 56);
    lv_obj_align(proj_prev, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_add_event_cb(proj_prev, project_down_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *proj_prev_label = lv_label_create(proj_prev);
    lv_label_set_text(proj_prev_label, "<");
    lv_obj_set_style_text_font(proj_prev_label, &lv_font_montserrat_32, 0);
    lv_obj_center(proj_prev_label);

    projects_page_label = lv_label_create(proj_ctrl);
    lv_obj_set_style_text_font(projects_page_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(projects_page_label, lv_color_white(), 0);
    lv_obj_align(projects_page_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(projects_page_label, "...");

    lv_obj_t *proj_next = lv_btn_create(proj_ctrl);
    lv_obj_set_size(proj_next, 56, 56);
    lv_obj_align(proj_next, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_event_cb(proj_next, project_up_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *proj_next_label = lv_label_create(proj_next);
    lv_label_set_text(proj_next_label, ">");
    lv_obj_set_style_text_font(proj_next_label, &lv_font_montserrat_32, 0);
    lv_obj_center(proj_next_label);

    lv_obj_t *cue_panel = ui_page_panel(page_projects, 132, 74);
    projects_page_cue = lv_label_create(cue_panel);
    lv_label_set_long_mode(projects_page_cue, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(projects_page_cue, 284);
    lv_obj_set_style_text_align(projects_page_cue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(projects_page_cue, &lv_font_montserrat_14, 0);
    lv_obj_center(projects_page_cue);
    lv_label_set_text(projects_page_cue, "");

    /* ---- FIRMWARE: what is running, and what the updater is doing ------ */

    ui_firmware_page_build(&fw_page, page_firmware, firmware_check_button_cb);

    /* ---- DIAGNOSTICS: link, sensor freshness, uptime, internal heap ---- */

    ui_diagnostics_build(&diag_page, page_diag);

    refresh_firmware_page();

    /* ---- SENS (UI suite, ui_sensors.c): three dials with their trends ---- */

    ui_sensors_build(&ui->sens, page_sens);

    /* ---- LOG: Black Box panel (as tall as the page allows) + NET/SND/CLR */

    /* ---- LOG (UI suite, ui_log.c): the whole Black Box, a page at a time -- */

    static const ui_log_actions_t log_actions = { net_button_cb, report_button_cb, clear_log_button_cb };
    ui_log_build(&ui->log, page_log, &log_actions);
#if DIAG_TEST_CRASH
    /* commissioning hook: RPT held 5 s (see report_button_hold_cb) */
    for (uint32_t i = 0; i < lv_obj_get_child_count(page_log); i++) {
        lv_obj_t *c = lv_obj_get_child(page_log, (int32_t)i);
        if (lv_obj_get_event_count(c) && lv_obj_get_child_count(c) == 1 &&
            strcmp(lv_label_get_text(lv_obj_get_child(c, 0)), "RPT") == 0) {
            lv_obj_add_event_cb(c, report_button_hold_cb, LV_EVENT_PRESSED, NULL);
            lv_obj_add_event_cb(c, report_button_hold_cb, LV_EVENT_PRESSING, NULL);
        }
    }
#endif

    /* Mission 19's FPS/CPU readout, now parked in the LOG header's slot
     * (ui_log.c) instead of the old button row. Same reasoning and the same
     * private-struct access as before - see the lv_display_private.h include. */
    lv_display_t *disp = lv_display_get_default();
    if (disp != NULL && disp->perf_label != NULL) {
        lv_obj_set_parent(disp->perf_label, ui->log.perf_slot);
        lv_obj_align(disp->perf_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    }

    /* ---- Nav bar: HOME/SENS/LOG/TALK, always on screen, own row on `scr`
     * (not inside any page) - same size/position the old NET/SND/REC/CLR
     * row used, so the overlays below (already sized to cover it) still
     * cover exactly the same area. */
    /* 1.3.0: one "< HOME" button where the HOME corner always was - see
     * ui_back_button_create(). MENU on HOME replaced the other three. */
    back_button = ui_back_button_create(scr, back_nav_cb);

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

    /* 2 columns x 2 rows (SEND/NOTE, GO/YES) via flex wrap. Two 140px
     * buttons plus an 8px gap is 288px, inside the 304px usable width.
     *
     * Grown for issue #13 - 140x74 buttons in a 296x158 grid, command word
     * at montserrat_20, up from 136x56 in 288x120 at the default 14. Two
     * rows of 74 plus the 8px gap is 156, so the grid still clears
     * cmd_status_label along the bottom of the 240px overlay.
     *
     * Not clickable: clearing LV_OBJ_FLAG_CLICKABLE means a stray tap
     * cannot be confused with a real MultiNet recognition highlight. */
    lv_obj_t *cmd_grid = lv_obj_create(cmd_overlay);
    lv_obj_set_size(cmd_grid, 296, 158);
    lv_obj_align(cmd_grid, LV_ALIGN_TOP_MID, 0, 30);
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
        lv_obj_set_size(btn, 140, 74);
        /* Tappable now. These were deliberately non-clickable when the
         * command window was new, so a stray touch could not be mistaken
         * for a MultiNet hit - the right call then, but it left this as the
         * one screen that ignores touch entirely, which reads as broken.
         *
         * The tap does not run anything here: it raises a flag that
         * detect_task consumes and dispatches exactly as it would a spoken
         * word, because the mic handoff must not happen on the LVGL task. */
        lv_obj_add_event_cb(btn, cmd_tile_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)cmd_defs[i].id);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(btn, CMD_BTN_INACTIVE_BG, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_pad_all(btn, 4, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, cmd_defs[i].label);
        /* Issue #13, in Mike's own words captured through GO: "they should
         * be bigger so that I can see them better." 20 was still too small
         * on the device - at montserrat_32 a four-letter word nearly fills
         * the 140px tile, which is the intent. The hint under it stays at
         * 12: it is a reminder, read once, not from across the room. */
        lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_32, 0);

        lv_obj_t *btn_hint = lv_label_create(btn);
        lv_label_set_text(btn_hint, cmd_defs[i].hint);
        lv_obj_set_style_text_font(btn_hint, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(btn_hint, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);

        cmd_buttons[i] = btn;
    }

    /* Countdown while COMMAND_WINDOW is open ("Listening... 10" ... "...1"),
     * then briefly replaced with the recognized word / TIMEOUT /
     * UNRECOGNIZED - see render_command_overlay(). */
    cmd_status_label = lv_label_create(cmd_overlay);
    lv_label_set_text(cmd_status_label, "");
    lv_obj_align(cmd_status_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* SEND/NOTE/GO recording overlay (UI suite, ui_recording.c): the timer,
     * a health rail, CANCEL/SEND and the project/repository chooser. */
    static const ui_rec_actions_t rec_actions = {
        recording_cancel_button_cb, recording_stop_button_cb, rec_sel_prev_cb, rec_sel_next_cb,
    };
    ui_recording_build(&ui->rec, scr, &rec_actions);

    /* Notification playback overlay (UI suite, ui_notify.c). */
    ui_notify_overlay_build(&ui->notif, scr, notification_stop_button_cb);

    render_event_log();

    live_data_deck_init();
    humiture_deck_init();

    wifi_mgr_init(wifi_status_changed_cb, NULL);

    /* 1.2.0: the report worker replaces the handshake worker here - the
     * handshake is dormant (its SND button is now RPT), and its 6KB of
     * internal stack pays for the report task's 5KB (6KB since 1.2.2). */
    report_client_init();

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
    /* After wifi_mgr_init above - the poll task reads wifi_mgr_get_status()
     * to avoid calling a backend it has no route to. */
    backend_health_init(backend_event_cb, NULL);
    /* Also after wifi_mgr_init - it waits for ONLINE before its first fetch,
     * and stops once the catalog loads. */
    backend_catalog_init();
    /* 1.3.0: PSRAM-stack task; waits for the first OTA check before it
     * fetches anything (weather_client.h). */
    weather_client_init();

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
    /* Internal RAM is the scarce one on this board - PSRAM is 16MB, but
     * Wi-Fi, task stacks, DMA and esp_timer all need DRAM. The UI grew
     * enough in one go (a fourth page, four corner buttons, a larger
     * widget) to push Wi-Fi's phy_track_pll_init over the edge:
     *
     *   ESP_ERROR_CHECK failed: ESP_ERR_NO_MEM at phy_common.c:118
     *   func: phy_track_pll_init
     *
     * It rebooted and came up fine, which is worse than a hard failure -
     * a marginal boot fails intermittently. Logged at the one point where
     * the whole UI exists but Wi-Fi has not started, so the margin is
     * visible rather than inferred after the next crash. */
    /* 1.2.1: LVGL now allocates from PSRAM (lvgl_psram_alloc.c). Compare
     * this line with 1.2.0's on hardware: internal free should be up by the
     * size of the widget tree, and fallbacks should be 0. */
    ESP_LOGI(TAG, "UI built: internal heap %u free (%u largest block, %u min), PSRAM %u free, "
             "LVGL internal fallbacks %lu",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned long)lvgl_psram_alloc_fallbacks());

    set_active_page(APP_PAGE_HOME);
}
