/* Simulator adapter: HOME (1.3.0), built the way status_deck_ui() builds it -
 * page container, ui_home_build() - with the weather view render_weather()
 * fills from weather_client, and the link state voice_ui_timer_cb sets from
 * backend_health. HOME has no back button, so none is drawn. */
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_home.h"
#include "ui_mouse.h"

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
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *page = ui_page_create(scr);
    static ui_home_t home;
    ui_home_build(&home, page, ws_noop_event_cb, ws_noop_event_cb);

    const char *stale = ws_fx_str("stale_as_of", "");
    const ui_home_weather_t w = {
        .valid = ws_fx_bool("weather_valid", true),
        .status = ws_fx_str("status", "Loading weather"),
        .location = ws_fx_str("location", "Orlando"),
        .stale_as_of = stale[0] ? stale : NULL,
        .now_temp = ws_fx_int("now_temp", 84),
        .now_icon = ws_fx_str("now_icon", "partly"),
        .now_text = ws_fx_str("now_text", "Partly cloudy"),
        .now_rain = ws_fx_int("now_rain", 10),
        .slot = {
            { ws_fx_str("slot1_label", "3 PM"), ws_fx_int("slot1_temp", 87), ws_fx_str("slot1_icon", "storm"), ws_fx_int("slot1_rain", 60) },
            { ws_fx_str("slot2_label", "7 PM"), ws_fx_int("slot2_temp", 79), ws_fx_str("slot2_icon", "partly"), ws_fx_int("slot2_rain", 20) },
        },
        .tmrw_hi = ws_fx_int("tmrw_hi", 88),
        .tmrw_lo = ws_fx_int("tmrw_lo", 73),
        .tmrw_icon = ws_fx_str("tmrw_icon", "sun"),
        .tmrw_rain = ws_fx_int("tmrw_rain", 20),
    };
    ui_home_apply_weather(&home, &w);

    /* Deterministic real-UI animation fixture, distinct from initial values. */
    if (ws_fx_bool("choreography", false)) {
        const weather_status_t next = {
            .valid=true, .seq=2, .location="Orlando", .now_temp=72,
            .now_icon="rain", .now_text="Rain", .now_rain=85,
            .slot={{.label="3 PM",.temp=74,.icon="cloud",.rain=70},
                   {.label="7 PM",.temp=69,.icon="storm",.rain=60}},
            .tmrw_hi=80, .tmrw_lo=65, .tmrw_icon="sun", .tmrw_rain=15,
        };
        if (ui_home_weather_stage_offer(&next)) ui_mouse_home_refresh_result(true);
    }

    ui_home_set_link(&home, link_from_fixture());
    voice_listening_widget_set_notification(home.mic, ws_fx_bool("notification_pending", false));
}
