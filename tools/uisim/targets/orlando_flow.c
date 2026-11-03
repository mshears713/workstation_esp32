/* Event test uses the real HOME and MENU builders, not a mock drawing.
 * Firmware keeps its original menu_button_cb -> set_active_page(APP_PAGE_MENU).
 * Here the same show/hide contract is exercised without ESP subsystem imports. */
#include "ws_sim.h"
#include "ui_home.h"
#include "ui_menu.h"
#include "ui_kit.h"
#include <assert.h>
#include <stdio.h>
static lv_obj_t *home_page, *menu_page;
static int hits;
static void open_menu(lv_event_t *e) {
    (void)e; hits++;
    lv_obj_add_flag(home_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(menu_page, LV_OBJ_FLAG_HIDDEN);
}
static void check(lv_timer_t *t) {
    assert(hits == 1);
    assert(lv_obj_has_flag(home_page, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(menu_page, LV_OBJ_FLAG_HIDDEN));
    puts("ORLANDO_MENU_TAP_PASS");
    lv_timer_delete(t);
}
void ws_render(lv_obj_t *scr) {
    static ui_home_t home;
    home_page=ui_page_create(scr);
    ui_home_build(&home,home_page,ws_noop_event_cb,open_menu);
    assert(home.now_temp == NULL && home.now_icon == NULL);
    menu_page=ui_page_create(scr);
    static const ui_menu_item_t items[]={
        {"SENSORS",ws_noop_event_cb},{"LOG",ws_noop_event_cb},
        {"PROJECTS",ws_noop_event_cb},{"FIRMWARE",ws_noop_event_cb},
        {"SETTINGS",ws_noop_event_cb},{"DIAG",ws_noop_event_cb}};
    static const ui_menu_item_t more={"MORE " LV_SYMBOL_RIGHT,ws_noop_event_cb};
    ui_menu_build(menu_page,"MENU",items,6,&more);
    lv_obj_add_flag(menu_page,LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(scr);
    lv_point_t pt={280,210};
    lv_obj_t *target=lv_indev_search_obj(scr,&pt);
    assert(target != NULL);
    lv_obj_send_event(target,LV_EVENT_CLICKED,NULL);
    lv_timer_create(check,100,NULL);
}
