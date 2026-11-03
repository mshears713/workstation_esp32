/* Simulator adapter: main/voice_listening_widget.c alone, centred. */
#include "ws_sim.h"
#include "voice_listening_widget.h"

static voice_widget_link_t link_from_fixture(void)
{
    if (ws_fx_is("link", "down")) {
        return VOICE_WIDGET_LINK_DOWN;
    }
    if (ws_fx_is("link", "no_net")) {
        return VOICE_WIDGET_LINK_NO_NET;
    }
    if (ws_fx_is("link", "unknown")) {
        return VOICE_WIDGET_LINK_UNKNOWN;
    }
    return VOICE_WIDGET_LINK_OK;
}

void ws_render(lv_obj_t *scr)
{
    lv_obj_t *w = voice_listening_widget_create(scr);
    lv_obj_center(w);
    voice_listening_widget_set_link(w, link_from_fixture());
    voice_listening_widget_set_notification(w, ws_fx_bool("notification_pending", false));
}
