/* Simulator adapter: FIRMWARE page. The fixture plays the part of
 * refresh_firmware_page() in status_deck_ui.c - it fills the same
 * ui_firmware_view_t from fixture values instead of from firmware_identity /
 * ota_service / device_config. */
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_firmware_page.h"

void ws_render(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *page = ui_page_create(scr);
    static ui_firmware_page_t fw;
    ui_firmware_page_build(&fw, page, ws_noop_event_cb);

    const ui_firmware_view_t view = {
        .version = ws_fx_str("version", "?"),
        .built_at = ws_fx_str("built_at", "?"),
        .backend_url = ws_fx_str("backend_url", "?"),
        .backend_fallback = ws_fx_bool("backend_fallback", false),
        .slot = ws_fx_str("slot", "?"),
        .pending_verify = ws_fx_bool("pending_verify", false),
        .ota_state = ws_fx_str("ota_state", "idle"),
        .ota_message = ws_fx_str("ota_message", ""),
        .ota_downloading = ws_fx_is("ota_state", "downloading"),
        .ota_progress_pct = ws_fx_int("ota_progress_pct", 0),
    };
    ui_firmware_page_apply(&fw, &view);

    ui_back_button_create(scr, ws_noop_event_cb);
}
