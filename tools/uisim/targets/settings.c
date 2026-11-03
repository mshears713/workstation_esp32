/* Render target: SETTINGS (main/ui_settings.c), built and filled the way
 * status_deck_ui.c does: wifi_mgr / backend_health / audio_playback /
 * backend_catalog values from fixture state. */
#include "lvgl.h"
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_settings.h"

void ws_render(lv_obj_t *scr)
{
    static ui_settings_page_t page;
    static const ui_settings_actions_t acts = {
        ws_noop_event_cb, ws_noop_event_cb, ws_noop_event_cb, ws_noop_event_cb, ws_noop_event_cb, ws_noop_event_cb,
    };
    lv_obj_t *p = ui_page_create(scr);
    ui_settings_build(&page, p, &acts);

    static const char *const wifi[] = { "off", "joining", "online", "retry" };
    static const char *const api[] = { "unknown", "ok", "down", "no_net" };
    ui_settings_view_t v = {
        .uptime_s = (uint32_t)ws_fx_i64("uptime_s", 2537),
        .ip_octet = ws_fx_str("ip_octet", ".57"),
        .wifi_attempts = (uint32_t)ws_fx_int("wifi_attempts", 1),
        .wifi_reason = ws_fx_str("wifi_reason", ""),
        .wifi_retry_s = (uint32_t)ws_fx_int("wifi_retry_s", 0),
        .api_latency_ms = (uint32_t)ws_fx_int("api_ms", 38),
        .api_ever_ok = ws_fx_bool("api_ever_ok", true),
        .api_secs_since_ok = (uint32_t)ws_fx_int("api_secs_since_ok", 0),
        .volume = ws_fx_int("volume", 70),
        .repo = ws_fx_str("repo", "workstation"),
        .repo_count = ws_fx_int("repo_count", 4),
    };
    for (int i = 0; i < 4; i++) {
        if (ws_fx_is("wifi", wifi[i])) {
            v.wifi = (ui_diag_wifi_t)i;
        }
        if (ws_fx_is("api", api[i])) {
            v.api = (ui_diag_api_t)i;
        }
    }
    ui_settings_apply(&page, &v);
    ui_back_button_create(scr, ws_noop_event_cb);
}
