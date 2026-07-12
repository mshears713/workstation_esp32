/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 05 — The Status Deck
 * @details Command-and-telemetry deck: five touch controls drive a single
 *          app_state_t, a state panel and event log render from that state,
 *          and a 1 Hz timer refreshes system telemetry (uptime/tick/heap).
 *          Built on the Mission 04 first_command BSP/LVGL foundation.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "status_deck";

/* ---- App state ---------------------------------------------------- */

typedef enum {
    MODE_SAFE = 0,
    MODE_ARMED,
} app_mode_t;

typedef struct {
    app_mode_t mode;
    bool link_up;
    bool diag_active;
    char last_command[16];
    uint32_t command_count;
} app_state_t;

static app_state_t app_state = {
    .mode = MODE_SAFE,
    .link_up = true,
    .diag_active = false,
    .last_command = "NONE",
    .command_count = 0,
};

/* ---- Event log ------------------------------------------------------ */

#define EVENT_LOG_LINES 4
#define EVENT_LOG_TEXT_LEN 40

static char event_log[EVENT_LOG_LINES][EVENT_LOG_TEXT_LEN];
static int event_log_used = 0;

static void event_log_push(const char *text)
{
    int keep = (event_log_used < EVENT_LOG_LINES - 1) ? event_log_used : EVENT_LOG_LINES - 1;
    for (int i = keep; i > 0; i--) {
        strncpy(event_log[i], event_log[i - 1], EVENT_LOG_TEXT_LEN - 1);
        event_log[i][EVENT_LOG_TEXT_LEN - 1] = '\0';
    }
    strncpy(event_log[0], text, EVENT_LOG_TEXT_LEN - 1);
    event_log[0][EVENT_LOG_TEXT_LEN - 1] = '\0';
    if (event_log_used < EVENT_LOG_LINES) {
        event_log_used++;
    }
}

/* ---- NVS persistence: event log + app_state survive reboot ----------- */

#define NVS_NAMESPACE "deck"

static void nvs_save_state(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "mode", (uint8_t)app_state.mode);
    nvs_set_u8(h, "link", (uint8_t)app_state.link_up);
    nvs_set_u8(h, "diag", (uint8_t)app_state.diag_active);
    nvs_set_str(h, "lastcmd", app_state.last_command);
    nvs_set_u32(h, "cmdcount", app_state.command_count);
    nvs_set_u8(h, "logn", (uint8_t)event_log_used);
    char key[8];
    for (int i = 0; i < EVENT_LOG_LINES; i++) {
        snprintf(key, sizeof(key), "log%d", i);
        nvs_set_str(h, key, event_log[i]);
    }
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_load_state(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return; /* first boot: no saved state yet, keep defaults */
    }
    uint8_t u8val;
    if (nvs_get_u8(h, "mode", &u8val) == ESP_OK) {
        app_state.mode = (app_mode_t)u8val;
    }
    if (nvs_get_u8(h, "link", &u8val) == ESP_OK) {
        app_state.link_up = u8val;
    }
    if (nvs_get_u8(h, "diag", &u8val) == ESP_OK) {
        app_state.diag_active = u8val;
    }
    size_t len = sizeof(app_state.last_command);
    nvs_get_str(h, "lastcmd", app_state.last_command, &len);
    nvs_get_u32(h, "cmdcount", &app_state.command_count);
    if (nvs_get_u8(h, "logn", &u8val) == ESP_OK) {
        event_log_used = (u8val > EVENT_LOG_LINES) ? EVENT_LOG_LINES : u8val;
    }
    char key[8];
    for (int i = 0; i < EVENT_LOG_LINES; i++) {
        snprintf(key, sizeof(key), "log%d", i);
        len = EVENT_LOG_TEXT_LEN;
        nvs_get_str(h, key, event_log[i], &len);
    }
    nvs_close(h);
}

/* ---- UI widgets (set once in status_deck_ui) ------------------------- */

static lv_obj_t *telemetry_label;
static lv_obj_t *mode_label;
static lv_obj_t *link_label;
static lv_obj_t *diag_label;
static lv_obj_t *last_cmd_label;
static lv_obj_t *log_label;

/* ---- Rendering: UI reads app_state / event_log, never the reverse --- */

static void render_state_panel(void)
{
    if (app_state.mode == MODE_ARMED) {
        lv_label_set_text(mode_label, "MODE: ARMED");
        lv_obj_set_style_text_color(mode_label, lv_palette_main(LV_PALETTE_RED), 0);
    } else {
        lv_label_set_text(mode_label, "MODE: SAFE");
        lv_obj_set_style_text_color(mode_label, lv_palette_main(LV_PALETTE_GREEN), 0);
    }

    if (app_state.link_up) {
        lv_label_set_text(link_label, "LINK: UP");
        lv_obj_set_style_text_color(link_label, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        lv_label_set_text(link_label, "LINK: DOWN");
        lv_obj_set_style_text_color(link_label, lv_palette_main(LV_PALETTE_RED), 0);
    }

    lv_label_set_text(diag_label, app_state.diag_active ? "DIAG: ON" : "DIAG: OFF");
    lv_obj_set_style_text_color(diag_label,
                                 app_state.diag_active ? lv_palette_main(LV_PALETTE_ORANGE)
                                                        : lv_palette_main(LV_PALETTE_GREY),
                                 0);

    char buf[32];
    snprintf(buf, sizeof(buf), "LAST CMD: %s", app_state.last_command);
    lv_label_set_text(last_cmd_label, buf);
}

static void render_event_log(void)
{
    char buf[EVENT_LOG_LINES * EVENT_LOG_TEXT_LEN];
    buf[0] = '\0';
    for (int i = 0; i < event_log_used; i++) {
        strcat(buf, event_log[i]);
        if (i != event_log_used - 1) {
            strcat(buf, "\n");
        }
    }
    lv_label_set_text(log_label, event_log_used ? buf : "(no events yet)");
}

/* ---- Button handlers: touch -> app_state -> render + serial log ----- */

/* Updates state bookkeeping + event log only; no widgets, no NVS, no
 * serial log. Used both by button handlers (which do all three after)
 * and at boot (widgets don't exist yet, so render/log happen later). */
static void record_event(const char *name)
{
    app_state.command_count++;
    strncpy(app_state.last_command, name, sizeof(app_state.last_command) - 1);
    app_state.last_command[sizeof(app_state.last_command) - 1] = '\0';

    char entry[EVENT_LOG_TEXT_LEN];
    snprintf(entry, sizeof(entry), "#%03u %s", (unsigned)app_state.command_count, name);
    event_log_push(entry);
}

static void log_command(const char *name)
{
    record_event(name);

    render_state_panel();
    render_event_log();
    ESP_LOGI(TAG, "command=%s mode=%s link=%s diag=%s", name,
             app_state.mode == MODE_ARMED ? "ARMED" : "SAFE",
             app_state.link_up ? "UP" : "DOWN",
             app_state.diag_active ? "ON" : "OFF");
    nvs_save_state();
}

static void arm_button_cb(lv_event_t *e)
{
    app_state.mode = (app_state.mode == MODE_SAFE) ? MODE_ARMED : MODE_SAFE;
    log_command(app_state.mode == MODE_ARMED ? "ARM" : "SAFE");
}

static void ping_button_cb(lv_event_t *e)
{
    log_command("PING");
}

static void diag_button_cb(lv_event_t *e)
{
    app_state.diag_active = !app_state.diag_active;
    log_command("DIAG");
}

static void link_button_cb(lv_event_t *e)
{
    app_state.link_up = !app_state.link_up;
    log_command("LINK");
}

static void reset_log_button_cb(lv_event_t *e)
{
    event_log_used = 0;
    log_command("RESET");
}

/* ---- Telemetry: 1 Hz timer, UI-only, no serial log spam -------------- */

static void telemetry_timer_cb(lv_timer_t *t)
{
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t ticks = (uint32_t)xTaskGetTickCount();
    size_t heap_kb = esp_get_free_heap_size() / 1024;
    size_t psram_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;

    char buf[64];
    snprintf(buf, sizeof(buf), "UP %02u:%02u  TICK %lu  HEAP %uK  PSRAM %uK",
             (unsigned)(uptime_s / 60), (unsigned)(uptime_s % 60),
             (unsigned long)ticks, (unsigned)heap_kb, (unsigned)psram_kb);
    lv_label_set_text(telemetry_label, buf);
}

/* ---- Layout ----------------------------------------------------------- */

void status_deck_ui(lv_obj_t *scr)
{
    nvs_load_state();
    record_event("BOOT"); /* widgets don't exist yet: no render/log/save here */

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

    telemetry_label = lv_label_create(scr);
    lv_obj_align(telemetry_label, LV_ALIGN_TOP_MID, 0, 26);

    mode_label = lv_label_create(scr);
    lv_obj_align(mode_label, LV_ALIGN_TOP_LEFT, 8, 44);

    link_label = lv_label_create(scr);
    lv_obj_align(link_label, LV_ALIGN_TOP_RIGHT, -8, 44);

    last_cmd_label = lv_label_create(scr);
    lv_obj_align(last_cmd_label, LV_ALIGN_TOP_LEFT, 8, 62);

    diag_label = lv_label_create(scr);
    lv_obj_align(diag_label, LV_ALIGN_TOP_RIGHT, -8, 62);

    lv_obj_t *log_panel = lv_obj_create(scr);
    lv_obj_set_size(log_panel, 304, 82);
    lv_obj_align(log_panel, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_pad_all(log_panel, 4, 0);
    lv_obj_clear_flag(log_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *log_title = lv_label_create(log_panel);
    lv_label_set_text(log_title, "EVENT LOG");
    lv_obj_align(log_title, LV_ALIGN_TOP_LEFT, 0, 0);

    log_label = lv_label_create(log_panel);
    lv_obj_align(log_label, LV_ALIGN_TOP_LEFT, 0, 18);

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
        { "ARM",   arm_button_cb },
        { "PING",  ping_button_cb },
        { "DIAG",  diag_button_cb },
        { "LINK",  link_button_cb },
        { "RESET", reset_log_button_cb },
    };

    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        lv_obj_t *btn = lv_btn_create(btn_row);
        lv_obj_set_size(btn, 56, 48);
        lv_obj_add_event_cb(btn, buttons[i].cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, buttons[i].label);
        lv_obj_center(btn_label);
    }

    render_state_panel();
    render_event_log();
    ESP_LOGI(TAG, "boot event recorded, restored command_count=%u",
             (unsigned)app_state.command_count);
    nvs_save_state();

    lv_timer_create(telemetry_timer_cb, 1000, NULL);
}
