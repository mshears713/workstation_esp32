/* Simulator adapter: DIAGNOSTICS page. The fixture plays the part of
 * refresh_diag_page() in status_deck_ui.c - it fills the same ui_diag_view_t
 * from fixture values instead of wifi_manager / backend_health / the sensor
 * timestamps / heap_caps. Then the global "< HOME" button goes on top, the
 * way status_deck_ui() creates it once for every page.
 *
 * uptime_s is a string field: ws_fx_int is int32 and uptime is uint32
 * seconds (up to 4294967295) on the device. */
#include <stdlib.h>
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_diagnostics.h"

static ui_diag_src_t src(const char *key)
{
    return ws_fx_is(key, "failed")  ? UI_DIAG_SRC_FAILED
         : ws_fx_is(key, "waiting") ? UI_DIAG_SRC_WAITING
         : ws_fx_is(key, "live")    ? UI_DIAG_SRC_LIVE
         : ws_fx_is(key, "stale")   ? UI_DIAG_SRC_STALE
                                    : UI_DIAG_SRC_UNKNOWN;
}

void ws_render(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *page = ui_page_create(scr);
    static ui_diagnostics_page_t dp;
    ui_diagnostics_build(&dp, page);

    const ui_diag_view_t v = {
        .wifi = ws_fx_is("wifi", "online")    ? UI_DIAG_WIFI_ONLINE
              : ws_fx_is("wifi", "joining")   ? UI_DIAG_WIFI_JOINING
              : ws_fx_is("wifi", "retry")     ? UI_DIAG_WIFI_RETRY
                                              : UI_DIAG_WIFI_OFF,
        .have_rssi = ws_fx_bool("have_rssi", true),
        .rssi_dbm = (int)ws_fx_int("rssi_dbm", -61),
        .wifi_reason = ws_fx_str("wifi_reason", ""),
        .wifi_attempts = (uint32_t)ws_fx_int("wifi_attempts", 1),
        .wifi_retry_s = (uint32_t)ws_fx_int("wifi_retry_s", 0),
        .api = ws_fx_is("api", "ok")       ? UI_DIAG_API_OK
             : ws_fx_is("api", "down")     ? UI_DIAG_API_DOWN
             : ws_fx_is("api", "no_net")   ? UI_DIAG_API_NO_NET
                                           : UI_DIAG_API_UNKNOWN,
        .api_ever_ok = ws_fx_bool("api_ever_ok", true),
        .api_secs_since_ok = (uint32_t)ws_fx_int("api_secs_since_ok", 0),
        .accel = src("accel"),
        .accel_age_ms = (uint64_t)ws_fx_int("accel_age_ms", 100),
        .env = src("env"),
        .env_age_ms = (uint64_t)ws_fx_int("env_age_ms", 800),
        .uptime_s = strtoull(ws_fx_str("uptime_s", "371553"), NULL, 10),
        .heap_free = (uint32_t)ws_fx_int("heap_free", 42190),
        .heap_largest = (uint32_t)ws_fx_int("heap_largest", 18432),
        .heap_min = (uint32_t)ws_fx_int("heap_min", 9830),
    };
    ui_diagnostics_apply(&dp, &v);

    ui_back_button_create(scr, ws_noop_event_cb);
}
