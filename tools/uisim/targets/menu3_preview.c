#include "ws_sim.h"
#include "ui_menu.h"
#include "ui_kit.h"
#include "ui_adventure.h"
#include "weather_icons.h"
void ws_render(lv_obj_t *screen) {
 lv_obj_t *page=ui_page_create(screen);
 const ui_menu_picture_item_t cards[]={{"Weather",weather_icon("partly",true),320,ws_noop_event_cb},{"Mouse",ui_adventure_original_asset(TOUR_ASSET_MOUSE,0),768,ws_noop_event_cb},{"Adventure",ui_adventure_original_asset(TOUR_ASSET_VAN,0),320,ws_noop_event_cb}};
 ui_menu_build_pictures(page,"EXPLORE",cards,3,NULL);
 ui_back_button_create(screen,ws_noop_event_cb);
}
