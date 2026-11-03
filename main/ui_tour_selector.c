#include "ui_tour_selector.h"
#include "ui_kit.h"
#include "ui_menu.h"

lv_obj_t *ui_tour_selector_create(lv_obj_t *screen, lv_event_cb_t tour_a,
                                  lv_event_cb_t tour_b, lv_event_cb_t back,
                                  lv_event_cb_t deleted)
{
    lv_obj_t *page = ui_page_create(screen);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x111821), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(page, deleted, LV_EVENT_DELETE, NULL);
    const ui_menu_item_t choices[] = { { "TOUR A", tour_a }, { "TOUR B", tour_b } };
    const ui_menu_item_t back_key = { "BACK", back };
    ui_menu_build(page, "TOURS", choices, 2, &back_key);
    return page;
}
