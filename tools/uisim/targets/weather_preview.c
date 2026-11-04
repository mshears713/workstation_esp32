#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_weather_page.h"
void ws_render(lv_obj_t *screen) {
 static ui_home_t w;
 lv_obj_t *page=ui_page_create(screen);
 ui_weather_page_build(&w,page,ws_noop_event_cb,ws_noop_event_cb);
 const ui_home_weather_t data={.valid=true,.location="Orlando",.now_temp=84,.now_icon="partly",.now_text="Partly cloudy",.now_rain=10,
  .slot={{"3 PM",87,"storm",60},{"7 PM",79,"partly",20}},.tmrw_hi=88,.tmrw_lo=73,.tmrw_icon="sun",.tmrw_rain=20};
 ui_weather_page_apply_weather(&w,&data);
}
