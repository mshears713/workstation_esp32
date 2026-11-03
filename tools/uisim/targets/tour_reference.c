#include "ws_sim.h"
#include "ui_adventure.h"
#include "ui_adventure_b.h"
void ws_render(lv_obj_t *scr) {
 if(ws_fx_int("variant",0)) {
  ui_adventure_b_open(scr,NULL,NULL);
  ui_adventure_b_seek(ws_fx_int("scene",0),ws_fx_int("elapsed",0));
 } else {
  ui_adventure_open(scr,NULL,NULL);
  ui_adventure_seek((adventure_scene_t)ws_fx_int("scene",0));
 }
}
