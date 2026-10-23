/* Render target: the SEND / NOTE / GO recording overlay (main/ui_recording.c),
 * built on the screen the way status_deck_ui.c builds it, over a plain page.
 * Fixture state is what status_deck_ui.c reads from voice_control,
 * audio_capture, wifi_mgr and backend_health. Taps on CANCEL/SEND leave a
 * marker so a test can see that the handler ran. */
#include "lvgl.h"
#include "ws_sim.h"
#include "ui_recording.h"

static lv_obj_t *marker;

static void mark(const char *s)
{
    lv_label_set_text(marker, s);
}
static void cancel_cb(lv_event_t *e) { (void)e; mark("CANCEL PRESSED"); }
static void send_cb(lv_event_t *e) { (void)e; mark("SEND PRESSED"); }
static void prev_cb(lv_event_t *e) { (void)e; mark("PREV PRESSED"); }
static void next_cb(lv_event_t *e) { (void)e; mark("NEXT PRESSED"); }

void ws_render(lv_obj_t *scr)
{
    static ui_recording_t rec;
    static const ui_rec_actions_t acts = { cancel_cb, send_cb, prev_cb, next_cb };
    marker = lv_label_create(scr);   /* under the overlay; read from the tree */
    lv_label_set_text(marker, "no tap");
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);   /* tree-only: tests read its text */
    ui_recording_build(&rec, scr, &acts);

    static const char *const phases[] = { "starting", "arming", "recording", "uploading", "ready", "cancelled",
                                          "failed", "result" };
    static const char *const wifi[] = { "off", "joining", "online", "retry" };
    static const char *const api[] = { "unknown", "ok", "down", "no_net" };
    ui_rec_view_t v = {
        .command = ws_fx_str("command", "SEND"),
        .elapsed_ms = (uint32_t)ws_fx_i64("elapsed_ms", 7000),
        .target_ms = (uint32_t)ws_fx_i64("target_ms", 15000),
        .uploader_behind = ws_fx_bool("uploader_behind", false),
        .fail_reason = ws_fx_str("fail_reason", ""),
        .result = ws_fx_str("result", ""),
        .selector = ws_fx_str("selector", "")[0] ? ws_fx_str("selector", "") : NULL,
        .wifi = UI_DIAG_WIFI_ONLINE,
        .api = UI_DIAG_API_OK,
    };
    for (int i = 0; i < 8; i++) {
        if (ws_fx_is("phase", phases[i])) {
            v.phase = (ui_rec_phase_t)i;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (ws_fx_is("wifi", wifi[i])) {
            v.wifi = (ui_diag_wifi_t)i;
        }
        if (ws_fx_is("api", api[i])) {
            v.api = (ui_diag_api_t)i;
        }
    }
    ui_recording_apply(&rec, &v);
}
