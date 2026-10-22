/* Render target: SENSORS dial cluster (main/ui_sensors.c), built and filled
 * the way status_deck_ui.c does. Readings are fixed-point like the model:
 * accel centi-g, temp deci-degrees F, humidity whole percent. Temperature and
 * humidity share one freshness (env), as they do in the firmware. */
#include "lvgl.h"
#include "ws_sim.h"
#include "ui_kit.h"
#include "ui_sensors.h"

static ui_diag_src_t src(const char *key)
{
    static const char *const names[] = { "unknown", "failed", "waiting", "live", "stale" };
    for (int i = 0; i < 5; i++) {
        if (ws_fx_is(key, names[i])) {
            return (ui_diag_src_t)i;
        }
    }
    return UI_DIAG_SRC_UNKNOWN;
}

static void reading(ui_sens_reading_t *r, ui_diag_src_t s, const char *age, const char *val, const char *lo,
                    const char *hi)
{
    r->src = s;
    r->age_ms = (uint64_t)ws_fx_i64(age, 0);
    r->have_value = s == UI_DIAG_SRC_LIVE || s == UI_DIAG_SRC_STALE;
    r->value = ws_fx_int(val, 0);
    r->have_range = r->have_value && ws_fx_bool("have_range", true);
    r->lo = ws_fx_int(lo, r->value);
    r->hi = ws_fx_int(hi, r->value);
}

void ws_render(lv_obj_t *scr)
{
    static ui_sensors_page_t page;
    lv_obj_t *p = ui_page_create(scr);
    ui_sensors_build(&page, p);

    ui_sensors_view_t v;
    reading(&v.r[UI_SENS_ACCEL], src("accel"), "accel_age_ms", "accel_x100", "accel_lo", "accel_hi");
    reading(&v.r[UI_SENS_TEMP], src("env"), "env_age_ms", "temp_x10", "temp_lo", "temp_hi");
    reading(&v.r[UI_SENS_HUMI], src("env"), "env_age_ms", "humi_pct", "humi_lo", "humi_hi");
    ui_sensors_apply(&page, &v);

    ui_back_button_create(scr, ws_noop_event_cb);
}
