#include "lvgl.h"
#include "ws_sim.h"
#include "ui_mouse.h"
static void closed(void *ctx) {
    lv_obj_t *l = lv_label_create(ctx);
    lv_label_set_text(l, "MOUSE CLOSED");
    lv_obj_center(l);
}
void ws_render(lv_obj_t *scr) {
    ui_mouse_open(scr, closed, scr);
    lv_obj_t *overlay = lv_obj_get_child(scr, -1);
    if (ws_fx_is("action", "pause")) lv_obj_send_event(overlay, LV_EVENT_CLICKED, NULL);
    if (ws_fx_is("action", "close")) lv_obj_send_event(lv_obj_get_child(overlay, 0), LV_EVENT_CLICKED, NULL);
}
