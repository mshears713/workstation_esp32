/* EXPERIMENT (not firmware): a more readable FIRMWARE page.
 *
 * Render:  ws-ui snippet tools/uisim/experiments/firmware_readable.c --use firmware
 *   or MCP render_snippet(code=<this file>, use=["firmware"], state={...})
 *
 * Why: the simulator's check_ui measured the current page's labels at 1.6:1
 * contrast - the light theme's near-black default text on ui_kit's dark
 * blue-grey panels - and its CHECK CLAWBOX NOW button is 28 px tall (~4 mm).
 * This keeps ui_kit's furniture and the same information, and changes three
 * things: light text on the dark panels, the version as the one big number,
 * and the confirmed/probation state as a coloured word instead of prose.
 * Fixture keys are the firmware target's (describe_target firmware).
 */
#include <stdio.h>
#include "ws_sim.h"
#include "ui_kit.h"

#define TEXT_MAIN lv_color_white()
#define TEXT_DIM  lv_color_hex(0xB0BEC5)   /* blue-grey 200: ~6:1 on the panel */

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

void ws_render(lv_obj_t *scr)
{
    lv_obj_t *page = ui_page_create(scr);
    ui_page_title(page, "FIRMWARE");

    /* Identity: the version is what you came here to read. */
    lv_obj_t *id = ui_page_panel(page, 58, 74);
    lv_obj_t *ver = text(id, ws_fx_str("version", "?"), &lv_font_montserrat_32, TEXT_MAIN);
    lv_obj_align(ver, LV_ALIGN_TOP_LEFT, 10, 2);
    char buf[112];
    snprintf(buf, sizeof(buf), "built %s", ws_fx_str("built_at", "?"));
    lv_obj_align(text(id, buf, &lv_font_montserrat_14, TEXT_DIM), LV_ALIGN_TOP_LEFT, 10, 38);
    snprintf(buf, sizeof(buf), "api %s%s", ws_fx_str("backend_url", "?"),
             ws_fx_bool("backend_fallback", false) ? " (fallback)" : "");
    lv_obj_t *api = text(id, buf, &lv_font_montserrat_14,
                         ws_fx_bool("backend_fallback", false) ? lv_palette_lighten(LV_PALETTE_AMBER, 1) : TEXT_DIM);
    lv_obj_align(api, LV_ALIGN_TOP_LEFT, 10, 54);

    /* State: slot + a coloured verdict, then what the updater is doing. */
    lv_obj_t *st = ui_page_panel(page, 136, 50);
    snprintf(buf, sizeof(buf), "SLOT %s", ws_fx_str("slot", "?"));
    lv_obj_align(text(st, buf, &lv_font_montserrat_20, TEXT_MAIN), LV_ALIGN_TOP_LEFT, 10, 4);
    bool probation = ws_fx_bool("pending_verify", false);
    lv_obj_t *verdict = text(st, probation ? "ON PROBATION" : "CONFIRMED", &lv_font_montserrat_20,
                             probation ? lv_palette_lighten(LV_PALETTE_AMBER, 1) : lv_palette_lighten(LV_PALETTE_GREEN, 2));
    lv_obj_align(verdict, LV_ALIGN_TOP_RIGHT, -10, 4);

    const char *state = ws_fx_str("ota_state", "idle");
    const char *msg = ws_fx_str("ota_message", "");
    if (ws_fx_is("ota_state", "downloading")) {
        snprintf(buf, sizeof(buf), "OTA downloading %d%%", (int)ws_fx_int("ota_progress_pct", 0));
    } else {
        snprintf(buf, sizeof(buf), "OTA %s", msg[0] ? msg : state);
    }
    lv_color_t ota_color = ws_fx_is("ota_state", "failed") ? lv_palette_lighten(LV_PALETTE_RED, 2) : TEXT_DIM;
    lv_obj_t *ota = text(st, buf, &lv_font_montserrat_14, ota_color);
    lv_label_set_long_mode(ota, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(ota, 284, 16);   /* one line: DOTS with auto height wraps instead */
    lv_obj_align(ota, LV_ALIGN_TOP_LEFT, 10, 29);

    /* A real finger target: 46 px tall (~7 mm), full width, bottom band. */
    lv_obj_t *btn = lv_button_create(page);
    lv_obj_set_size(btn, 304, 46);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_event_cb(btn, ws_noop_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = text(btn, "CHECK CLAWBOX NOW", &lv_font_montserrat_20, TEXT_MAIN);
    lv_obj_center(bl);

    static const lv_event_cb_t cbs[UI_NAV_COUNT] = {
        ws_noop_event_cb, ws_noop_event_cb, ws_noop_event_cb, ws_noop_event_cb,
    };
    lv_obj_t *nav[UI_NAV_COUNT];
    ui_nav_create(scr, nav, cbs);
    ui_nav_set_active(nav, UI_NAV_COUNT + 1);
}
