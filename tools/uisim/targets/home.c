/* Simulator adapter: HOME, built in the order status_deck_ui() builds it -
 * page container, HOME content, then the corner nav on top - with the widget
 * state voice_ui_timer_cb would set from backend_health / notification_client. */
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_home.h"
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
    lv_obj_t *page = ui_page_create(scr);
    lv_obj_t *widget = ui_home_build(page, ws_noop_event_cb);

    static const lv_event_cb_t nav_cbs[UI_NAV_COUNT] = {
        ws_noop_event_cb, ws_noop_event_cb, ws_noop_event_cb, ws_noop_event_cb,
    };
    lv_obj_t *nav[UI_NAV_COUNT];
    ui_nav_create(scr, nav, nav_cbs);
    ui_nav_set_active(nav, UI_NAV_HOME);

    voice_listening_widget_set_link(widget, link_from_fixture());
    voice_listening_widget_set_notification(widget, ws_fx_bool("notification_pending", false));
}
