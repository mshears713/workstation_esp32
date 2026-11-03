/* Simulator adapter: MENU with the same entries, in the same order, that
 * status_deck_ui() passes - the first page with MORE >, or (page=more) the
 * second page of experiments with < MENU - and the back button on top. */
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_menu.h"

void ws_render(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *page = ui_page_create(scr);
    if (ws_fx_is("page", "more")) {
        static const ui_menu_item_t items[] = {
            { "MESSAGES", ws_noop_event_cb },
            { "MUSIC", ws_noop_event_cb },
            { "FACE", ws_noop_event_cb },
            { "SURVEY", ws_noop_event_cb },
        };
        static const ui_menu_item_t back = { LV_SYMBOL_LEFT " MENU", ws_noop_event_cb };
        ui_menu_build(page, "MORE", items, (int)(sizeof(items) / sizeof(items[0])), &back);
    } else {
        static const ui_menu_item_t items[] = {
            { "SENSORS", ws_noop_event_cb },
            { "LOG", ws_noop_event_cb },
            { "PROJECTS", ws_noop_event_cb },
            { "FIRMWARE", ws_noop_event_cb },
            { "SETTINGS", ws_noop_event_cb },
            { "DIAG", ws_noop_event_cb },
        };
        static const ui_menu_item_t more = { "MORE " LV_SYMBOL_RIGHT, ws_noop_event_cb };
        ui_menu_build(page, "MENU", items, (int)(sizeof(items) / sizeof(items[0])), &more);
    }
    ui_back_button_create(scr, ws_noop_event_cb);
}
