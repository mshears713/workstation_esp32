/* Render target: SENSORS dial cluster (main/ui_sensors.c), built and filled
 * the way status_deck_ui.c does. Readings are fixed-point like the model:
 * accel centi-g, temp deci-degrees F, humidity whole percent. Temperature and
 * humidity share one freshness (env), as they do in the firmware.
 *
 * The trend is synthesized (chart history is not fixture state): `trace`
 * picks a shape between the fixture's *_lo and *_hi, ending on the current
 * value - flat-ish jitter for a resting board, spikes for shaking. */
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

static void reading(ui_sens_reading_t *r, ui_diag_src_t s, const char *age, const char *val)
{
    r->src = s;
    r->age_ms = (uint64_t)ws_fx_int(age, 0);
    r->have_value = s == UI_DIAG_SRC_LIVE || s == UI_DIAG_SRC_STALE;
    r->value = ws_fx_int(val, 0);
}

/* Deterministic pseudo-trace: `n` samples between lo and hi, last = value.
 * Only `fill` per mille of the history exists (fewer points shortly after
 * boot or after a sensor stopped). */
static void trace(ui_sensors_page_t *p, ui_sens_id_t id, const ui_sens_reading_t *r, const char *lo_key,
                  const char *hi_key)
{
    if (!r->have_value) {
        return;
    }
    int n = ui_sens_info(id)->history * ws_fx_int("history_permille", 1000) / 1000;
    int32_t lo = ws_fx_int(lo_key, r->value), hi = ws_fx_int(hi_key, r->value);
    uint32_t seed = 2463534242u + (uint32_t)id;
    for (int k = 0; k < n; k++) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        int32_t v = lo + (int32_t)(seed % (uint32_t)(hi - lo + 1));
        /* mostly near the middle, occasional excursions to the ends */
        if (seed % 7u) {
            v = (v + (lo + hi) / 2) / 2;
        }
        ui_sensors_push(p, id, k == n - 1 ? r->value : v);
    }
}

void ws_render(lv_obj_t *scr)
{
    static ui_sensors_page_t page;
    lv_obj_t *pg = ui_page_create(scr);
    ui_sensors_build(&page, pg);

    ui_sensors_view_t v;
    reading(&v.r[UI_SENS_ACCEL], src("accel"), "accel_age_ms", "accel_x100");
    reading(&v.r[UI_SENS_TEMP], src("env"), "env_age_ms", "temp_x10");
    reading(&v.r[UI_SENS_HUMI], src("env"), "env_age_ms", "humi_pct");
    trace(&page, UI_SENS_ACCEL, &v.r[UI_SENS_ACCEL], "accel_lo", "accel_hi");
    trace(&page, UI_SENS_TEMP, &v.r[UI_SENS_TEMP], "temp_lo", "temp_hi");
    trace(&page, UI_SENS_HUMI, &v.r[UI_SENS_HUMI], "humi_lo", "humi_hi");
    ui_sensors_apply(&page, &v);

    ui_back_button_create(scr, ws_noop_event_cb);
}
