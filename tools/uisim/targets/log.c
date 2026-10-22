/* Render target: LOG (main/ui_log.c), built the way status_deck_ui.c builds
 * it. The Black Box ring is a fixture scenario of messages the firmware
 * really writes (status_deck_ui.c, wifi_manager.c, backend_health.c,
 * voice_control.c, audio_capture.c, report_client.c), newest first.
 * `count` trims the scenario. `seq_bump` holds back the newest N events when
 * the page first opens and records them at `bump_at_ms`, so a tap test can
 * show that paging keeps its place while events arrive. */
#include "lvgl.h"
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_log.h"
#include "ui_log_format.h"

#define E(t, s, m) { UI_LOG_##t, s, m }

/* An evening: boot after a watchdog, Wi-Fi, a backend outage, a NOTE that
 * failed to upload, a report. 32 lines = the recorder's whole ring. */
static const ui_log_entry_t evening[] = {
    E(REPORT, 8644, "REPORT SENT #14"),
    E(COMMAND, 8641, "CMD RPT"),
    E(BACKEND, 8460, "API BACK AFTER 412s"),
    E(AUDIO, 8102, "UPLOAD FAILED: UPLOAD TIMEOUT"),
    E(VOICE, 8101, "NOTE UPLOAD FAILED: TIMEOUT"),
    E(AUDIO, 8064, "UPLOADING"),
    E(AUDIO, 8020, "AUDIO START"),
    E(VOICE, 8019, "WAKE (MANUAL)"),
    E(BACKEND, 8048, "API LOST"),
    E(SENSOR, 7311, "HUMITURE RECOVERED"),
    E(SENSOR, 7302, "HUMITURE STALE"),
    E(VOICE, 6230, "LISTENING RESTORED"),
    E(VOICE, 6221, "NOTIFICATION DELIVERED"),
    E(VOICE, 6205, "NOTIFICATION PLAYING"),
    E(VOICE, 6203, "NOTIFICATION START"),
    E(NETWORK, 3907, "WIFI ONLINE"),
    E(NETWORK, 3901, "WIFI CONNECTING"),
    E(NETWORK, 3876, "WIFI LOST, RETRY 25s"),
    E(REPORT, 3602, "REPORT SENT #13"),
    E(NETWORK, 1211, "MANUAL RECONNECT"),
    E(COMMAND, 1210, "CMD NET"),
    E(SENSOR, 41, "HUMITURE FIRST DATA"),
    E(SENSOR, 40, "FIRST DATA"),
    E(BACKEND, 14, "API: OK"),
    E(NETWORK, 9, "WIFI ONLINE"),
    E(NETWORK, 4, "WIFI CONNECTING"),
    E(VOICE, 3, "VOICE LISTENING"),
    E(SENSOR, 2, "HUMITURE INIT"),
    E(SENSOR, 2, "SENSOR INIT"),
    E(RESET, 0, "PREV: TASK WDT LOG"),
    E(RESET, 0, "RST TASK WATCHDOG"),
    E(BOOT, 0, "BOOT #212"),
};

static const ui_log_entry_t fresh_boot[] = {
    E(SENSOR, 2, "HUMITURE INIT FAILED"),
    E(SENSOR, 2, "SENSOR INIT"),
    E(RESET, 0, "RST POWERON"),
    E(BOOT, 0, "BOOT #213"),
};

/* Longest realistic lines (31 chars) and the widest time stamps. */
static const ui_log_entry_t longest[] = {
    E(REPORT, 4294967295u, "AUTO RPT OFF: CRASH IN REPORT"),
    E(VOICE, 4294000000u, "GO UPLOAD FAILED: UPLOAD ERR 50"),
    E(NETWORK, 86399, "NO AP FOUND, RETRY 120s"),
    E(BACKEND, 3599, "WDT NO API 600s"),
    E(RESET, 59, "PREV: PANIC (no crumb)"),
    E(AUDIO, 7, "UPLOAD FAILED: UPLOAD TIMEOUT"),
};

static ui_log_page_t page;
static const ui_log_entry_t *ring;
static int ring_count;

static void bump_cb(lv_timer_t *t)
{
    (void)t;
    ui_log_show(&page, ring, ring_count, 212 + (uint32_t)ws_fx_int("seq_bump", 0));
}

void ws_render(lv_obj_t *scr)
{
    lv_obj_t *p = ui_page_create(scr);
    static const ui_log_actions_t actions = { ws_noop_event_cb, ws_noop_event_cb, ws_noop_event_cb };
    ui_log_build(&page, p, &actions);
    ui_back_button_create(scr, ws_noop_event_cb);

    int n = (int)(sizeof(evening) / sizeof(evening[0]));
    ring = evening;
    if (ws_fx_is("scenario", "fresh_boot")) {
        ring = fresh_boot;
        n = 4;
    } else if (ws_fx_is("scenario", "longest")) {
        ring = longest;
        n = 6;
    } else if (ws_fx_is("scenario", "cleared")) {
        n = 0;
    }
    ring_count = ws_fx_int("count", n);
    ring_count = ring_count < n ? ring_count : n;

    int bump = ws_fx_int("seq_bump", 0);
    ui_log_show(&page, ring + bump, ring_count - bump, 212);
    if (bump > 0) {
        lv_timer_t *t = lv_timer_create(bump_cb, (uint32_t)ws_fx_int("bump_at_ms", 1000), NULL);
        lv_timer_set_repeat_count(t, 1);
    }
}
