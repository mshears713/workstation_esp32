/* Interactive HOME for the browser preview (workstation-lvgl-sim web/) and the
 * native renderer. Same builders and wiring order as status_deck_ui.c:
 * ui_page_create -> ui_home_build(talk, menu) -> ui_home_set_actions(home, tour, intro),
 * and the house opens MENU with ui_menu_build.
 * Stand-ins (not firmware behaviour): MENU's pages are not part of this build, so
 * every MENU button returns to HOME; TALK and the tour callback do nothing.
 * Navigation is logged ("nav: ...") so browser tests can assert it. */
#include "ws_sim.h"
#include "ui_home.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include <stdio.h>

static lv_obj_t *screen, *home_page, *menu_page;
static ui_home_t home;

static void back_home(lv_event_t *e)
{
    (void)e;
    if (menu_page) {
        lv_obj_delete_async(menu_page);
        menu_page = NULL;
    }
    lv_obj_remove_flag(home_page, LV_OBJ_FLAG_HIDDEN);
    ui_home_set_active(true);
    puts("nav: HOME");
}

static void open_menu(lv_event_t *e)
{
    (void)e;
    if (menu_page)
        return;
    ui_home_set_active(false);
    lv_obj_add_flag(home_page, LV_OBJ_FLAG_HIDDEN);
    menu_page = ui_page_create(screen);
    static const ui_menu_item_t items[] = {
        {"SENSORS", back_home}, {"LOG", back_home}, {"PROJECTS", back_home},
        {"FIRMWARE", back_home}, {"SETTINGS", back_home}, {"DIAG", back_home}};
    static const ui_menu_item_t corner = {"< HOME", back_home};
    ui_menu_build(menu_page, "MENU", items, 6, &corner);
    puts("nav: MENU");
}

static void intro(lv_event_t *e) { (void)e; puts("action: intro"); }
static void tour(lv_event_t *e) { (void)e; puts("action: tour"); }

void ws_render(lv_obj_t *scr)
{
    screen = scr;
    home_page = ui_page_create(scr);
    ui_home_build(&home, home_page, ws_noop_event_cb, open_menu);
    ui_home_set_actions(ws_noop_event_cb, tour, intro);
}
