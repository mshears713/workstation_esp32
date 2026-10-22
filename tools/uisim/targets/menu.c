/* Simulator adapter: MENU (1.3.0) with the same five entries, in the same
 * order, that status_deck_ui() passes, and the back button on top. */
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_menu.h"

void ws_render(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *page = ui_page_create(scr);
    static const ui_menu_item_t items[] = {
        { "SENSORS", ws_noop_event_cb },
        { "LOG", ws_noop_event_cb },
        { "PROJECTS", ws_noop_event_cb },
        { "FIRMWARE", ws_noop_event_cb },
        { "SETTINGS", ws_noop_event_cb },
    };
    ui_menu_build(page, items, (int)(sizeof(items) / sizeof(items[0])));
    ui_back_button_create(scr, ws_noop_event_cb);
}
