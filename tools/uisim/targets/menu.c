/* Simulator adapter: the exhibit menus exactly as status_deck_ui() builds them.
 * page=explore (default): EXPLORE picture cards (Tour, Newton, Lincoln) with
 * TOOLS > in the title band and the < HOME back key; page=tools: the
 * maintenance pages with < MENU. */
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_adventure.h"
#include "newton_art.h"
#include "lincoln_art.h"

void ws_render(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *page = ui_page_create(scr);
    lv_obj_t *back = ui_back_button_create(scr, ws_noop_event_cb);
    if (ws_fx_is("page", "tools")) {
        static const ui_menu_item_t items[] = {
            { "SENSORS", ws_noop_event_cb },
            { "LOG", ws_noop_event_cb },
            { "FIRMWARE", ws_noop_event_cb },
            { "SETTINGS", ws_noop_event_cb },
            { "DIAG", ws_noop_event_cb },
        };
        ui_menu_build(page, "TOOLS", items, (int)(sizeof(items) / sizeof(items[0])), NULL);
        lv_label_set_text(lv_obj_get_child(back, 0), LV_SYMBOL_LEFT " MENU");
    } else {
        const ui_menu_picture_item_t pictures[] = {
            { "Tour", ui_adventure_original_asset(TOUR_ASSET_VAN, 0), 320, ws_noop_event_cb },
            { "Newton", newton_art_get()->person[0].images[0], 384, ws_noop_event_cb },
            { "Lincoln", lincoln_art_get()->actor[0], 384, ws_noop_event_cb },
        };
        static const ui_menu_item_t tools = { "TOOLS " LV_SYMBOL_RIGHT, ws_noop_event_cb };
        ui_menu_build_pictures(page, "EXPLORE", pictures, 3, &tools);
        lv_label_set_text(lv_obj_get_child(back, 0), LV_SYMBOL_LEFT " HOME");
    }
    lv_obj_move_foreground(back);
}
