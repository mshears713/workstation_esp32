/* MOCKUP, not firmware. SENSORS layout alternatives to the trio in
 * main/ui_sensors.c, built from the same dial (ui_dial.c) and the same
 * reading -> dial-state rules (ui_sensors_dial_state), so only the placement
 * differs. Render with:
 *   ws-ui snippet tools/uisim/experiments/sensors/variants.c --use sensors \
 *         --fixture sensors.stale --set layout=hero
 * layout=hero: temperature as the big instrument, accel + humidity small.
 * layout=trend: half dials across the top, a real trend line under each. */
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

static const char *const ends[UI_SENS_COUNT][2] = { { "0", "2" }, { "60", "90" }, { "0", "100" } };

static void dial_at(ui_dial_t *d, ui_dial_spec_t *spec, lv_obj_t *parent, ui_sens_id_t id, int x, int y,
                    int size, int w, const lv_font_t *font, bool name_under, const ui_sens_reading_t *r)
{
    const ui_sens_info_t *info = ui_sens_info(id);
    *spec = (ui_dial_spec_t){
        .x = x, .y = y, .size = size, .arc_w = w, .value_font = font,
        .name = name_under ? info->name : NULL,
        .lo_text = ends[id][0], .hi_text = ends[id][1],
        .tick_pos = info->rest == INT32_MIN ? -1 : ui_sens_dial_pos(id, info->rest, NULL),
        .color = DIAL_CYAN,
    };
    ui_dial_create(d, parent, spec);
    char value[16], word[24];
    ui_dial_state_t st;
    ui_sensors_dial_state(id, r, &st, value, sizeof(value), word, sizeof(word));
    ui_dial_apply(d, spec, &st);
}

static lv_obj_t *label(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

/* B: the room's temperature is what a glance across the bench wants; the
 * other two are supporting instruments with their names and words beside. */
static void hero(lv_obj_t *page, const ui_sensors_view_t *v)
{
    static ui_dial_t d[3];
    static ui_dial_spec_t s[3];
    ui_page_title(page, "SENSORS");
    dial_at(&d[1], &s[1], page, UI_SENS_TEMP, 6, 60, 150, 13, &lv_font_montserrat_48, true, &v->r[UI_SENS_TEMP]);
    dial_at(&d[2], &s[2], page, UI_SENS_HUMI, 166, 60, 82, 8, &lv_font_montserrat_20, false, &v->r[UI_SENS_HUMI]);
    dial_at(&d[0], &s[0], page, UI_SENS_ACCEL, 166, 148, 82, 8, &lv_font_montserrat_20, false, &v->r[UI_SENS_ACCEL]);
    /* names to the right of the small dials; their words moved there too */
    label(page, "HUMIDITY", &lv_font_montserrat_14, DIAL_DIM, 252, 80);
    label(page, "ACCEL", &lv_font_montserrat_14, DIAL_DIM, 252, 168);
    lv_obj_set_pos(d[2].word, 252, 98);
    lv_obj_set_pos(d[0].word, 252, 186);
}

/* C: half dials for the reading, and the trend as an actual line - the old
 * page's charts kept, made small, under the instrument they belong to. */
static void trend(lv_obj_t *page, const ui_sensors_view_t *v)
{
    static ui_dial_t d[3];
    static ui_dial_spec_t s[3];
    ui_page_title(page, "SENSORS");
    for (int i = 0; i < UI_SENS_COUNT; i++) {
        dial_at(&d[i], &s[i], page, (ui_sens_id_t)i, 8 + i * 106, 58, 92, 8, &lv_font_montserrat_32, true,
                &v->r[i]);
        const ui_sens_reading_t *r = &v->r[i];
        lv_obj_t *c = lv_chart_create(page);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 92, 26);
        lv_obj_set_pos(c, 8 + i * 106, 196);
        lv_obj_set_style_bg_color(c, DIAL_FACE, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);
        lv_chart_set_point_count(c, 40);
        const ui_sens_info_t *info = ui_sens_info((ui_sens_id_t)i);
        lv_chart_set_axis_range(c, LV_CHART_AXIS_PRIMARY_Y, info->scale_lo, info->scale_hi);
        lv_chart_series_t *ser = lv_chart_add_series(c, r->src == UI_DIAG_SRC_LIVE ? DIAL_CYAN : DIAL_TICK,
                                                     LV_CHART_AXIS_PRIMARY_Y);
        if (r->have_value) {
            /* a plausible trace between the fixture's low and high */
            for (int k = 0; k < 40; k++) {
                int32_t span = r->hi - r->lo;
                int32_t y = r->lo + (span * ((k * 37 + i * 11) % 23)) / 22;
                lv_chart_set_next_value(c, ser, k == 39 ? r->value : y);
            }
        }
    }
    /* trio's words sit under the name; push the charts' area clear of them */
}

void ws_render(lv_obj_t *scr)
{
    ui_sensors_view_t v;
    reading(&v.r[UI_SENS_ACCEL], src("accel"), "accel_age_ms", "accel_x100", "accel_lo", "accel_hi");
    reading(&v.r[UI_SENS_TEMP], src("env"), "env_age_ms", "temp_x10", "temp_lo", "temp_hi");
    reading(&v.r[UI_SENS_HUMI], src("env"), "env_age_ms", "humi_pct", "humi_lo", "humi_hi");
    lv_obj_t *p = ui_page_create(scr);
    if (ws_fx_is("layout", "trend")) {
        trend(p, &v);
    } else {
        hero(p, &v);
    }
    ui_back_button_create(scr, ws_noop_event_cb);
}
