/* Render target: FIRMWARE page (main/ui_firmware_page.c + ui_fw_timeline.c),
 * built and filled the way status_deck_ui.c's refresh_firmware_page() does.
 * Messages in fixtures are ota_service.c's own strings. */
#include "lvgl.h"
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_firmware_page.h"

void ws_render(lv_obj_t *scr)
{
    static ui_firmware_page_t page;
    lv_obj_t *p = ui_page_create(scr);
    ui_firmware_page_build(&page, p, ws_noop_event_cb);

    static const char *const states[] = { "idle", "validating", "checking", "update ready", "downloading",
                                          "verifying", "rebooting", "failed" };
    ui_fw_state_t st = UI_FW_IDLE;
    for (int i = 0; i < 8; i++) {
        if (ws_fx_is("ota_state", states[i])) {
            st = (ui_fw_state_t)i;
        }
    }
    const ui_firmware_view_t view = {
        .version = ws_fx_str("version", "1.2.2+2d189af"),
        .built_at = ws_fx_str("built_at", "Sep 23 2026 15:06:22"),
        .backend_url = ws_fx_str("backend_url", "http://192.168.1.71:8000"),
        .backend_fallback = ws_fx_bool("backend_fallback", false),
        .slot = ws_fx_str("slot", "ota_1"),
        .pending_verify = ws_fx_bool("pending_verify", false),
        .ota_state = st,
        .ota_message = ws_fx_str("ota_message", ""),
        .desired_version = ws_fx_str("desired_version", ""),
        .ota_progress_pct = ws_fx_int("ota_progress_pct", 0),
        .rolled_back = ws_fx_bool("rolled_back", false),
    };
    ui_firmware_page_apply(&page, &view);

    ui_back_button_create(scr, ws_noop_event_cb);
}
