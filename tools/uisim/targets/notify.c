/* Render target: NOTIFY card stack and the playback overlay (main/ui_notify.c),
 * built the way status_deck_ui.c builds them. `view` picks the page or the
 * overlay; the rest is what notification_client / voice_control report.
 * Texts are realistic spoken_message values (the backend's TTS sentences). */
#include "lvgl.h"
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_notify.h"

static lv_obj_t *marker;
static void play_cb(lv_event_t *e) { (void)e; lv_label_set_text(marker, "PLAY PRESSED"); }
static void stop_cb(lv_event_t *e) { (void)e; lv_label_set_text(marker, "STOP PRESSED"); }

static const ui_notify_done_t history[] = {
    { UI_NOTIFY_ACK_FAILED, 6100, "Reminder: the filament dryer has been on for six hours.", NULL },
    { UI_NOTIFY_DELIVERED, 5400, "The print finished. Bed is cooling.", NULL },
    { UI_NOTIFY_STOPPED, 3000, NULL, NULL },
    { UI_NOTIFY_FETCH_FAILED, 2200, NULL, "FETCH FAILED: HTTP 404" },
};

void ws_render(lv_obj_t *scr)
{
    marker = lv_label_create(scr);
    lv_label_set_text(marker, "no tap");
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);   /* tree-only: tests read it */

    static const char *const links[] = { "unknown", "ok", "down", "no_net" };
    int link = 1;
    for (int i = 0; i < 4; i++) {
        if (ws_fx_is("link", links[i])) {
            link = i;
        }
    }
    const char *text = ws_fx_str("text", "Your van build note was filed under Van Build 2. Two items need a decision.");

    if (ws_fx_is("view", "overlay")) {
        static ui_notify_overlay_t o;
        ui_notify_overlay_build(&o, scr, stop_cb);
        static const char *const phases[] = { "fetching", "playing", "acking", "done" };
        static const char *const outs[] = { "delivered", "ack_failed", "stopped", "fetch_failed" };
        ui_notify_pb_view_t v = { .text = text[0] ? text : NULL, .remaining = ws_fx_int("pending", 1) - 1,
                                  .reason = ws_fx_str("reason", "")[0] ? ws_fx_str("reason", "") : NULL };
        for (int i = 0; i < 4; i++) {
            if (ws_fx_is("phase", phases[i])) {
                v.phase = (ui_notify_pb_phase_t)i;
            }
            if (ws_fx_is("outcome", outs[i])) {
                v.outcome = (ui_notify_outcome_t)i;
            }
        }
        ui_notify_overlay_apply(&o, &v);
        return;
    }

    static ui_notify_page_t page;
    static const ui_notify_actions_t acts = { play_cb };
    lv_obj_t *p = ui_page_create(scr);
    ui_notify_build(&page, p, &acts);
    int hist = ws_fx_int("history", 2);
    ui_notify_view_t v = {
        .link = (ui_notify_link_t)link,
        .pending = ws_fx_int("pending", 1),
        .front_text = ws_fx_bool("text_loaded", true) && text[0] ? text : NULL,
        .front_text_failed = ws_fx_bool("text_failed", false),
        .front_age_s = (uint32_t)ws_fx_int("age_s", 360),
        .front_age_known = true,
        .done = history,
        .done_count = hist < 4 ? hist : 4,
        .now_s = 6400,
    };
    ui_notify_apply(&page, &v);
    ui_back_button_create(scr, ws_noop_event_cb);
}
