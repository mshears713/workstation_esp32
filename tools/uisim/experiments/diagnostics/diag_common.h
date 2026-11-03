/* EXPERIMENT (not firmware): shared model + furniture for the DIAGNOSTICS page variants.
 *
 * Included by v1_quad.c, v2_ledger.c, v3_verdict.c as
 *   #include "../tools/uisim/experiments/diagnostics/diag_common.h"
 * (resolved against the simulator's -I main/), so all three variants read the same view,
 * the same formatting and the same severity rules, and differ only in layout and hierarchy.
 *
 * Three layers, in the shape the firmware seam would take (main/ui_diagnostics.{c,h}):
 *   1. ui_diag_view_t  - RAW values, exactly what the subsystems expose (field comments name
 *                        the source). status_deck_ui.c would fill it once a second; the
 *                        simulator fills it from fixtures (diag_fixture below).
 *   2. diag_fmt_* / diag_eval_* - pure C (snprintf only): text + severity per signal. No LVGL,
 *                        so tools/hosttest can test them under ASan/UBSan.
 *   3. per-variant builder + apply - pure LVGL.
 *
 * Truthfulness rules baked into layer 2:
 *   - nothing is "OK" until the source has said so this boot (UNKNOWN reads WAITING/PROBING);
 *   - every non-nominal state carries a WORD, colour only reinforces it;
 *   - stale data shows its age; absent optional hardware (the AHT30 dock) is neutral, not red;
 *   - internal heap only. PSRAM is deliberately absent: it is never the constraint.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ws_sim.h"

/* ---------------------------------------------------------------- 1. view */

/* mirrors wifi_mgr_state_t (wifi_manager.h) */
typedef enum { DV_WIFI_OFF = 0, DV_WIFI_CONNECTING, DV_WIFI_ONLINE, DV_WIFI_RETRY } dv_wifi_t;
/* mirrors backend_health_state_t (backend_health.h) */
typedef enum { DV_API_UNKNOWN = 0, DV_API_OK, DV_API_DOWN, DV_API_NO_NET } dv_api_t;
/* mirrors source_status_t (status_deck_ui.c) + the boot probe's *_present flag */
typedef enum { DV_SRC_ABSENT = 0, DV_SRC_WAITING, DV_SRC_LIVE, DV_SRC_STALE } dv_src_t;

typedef struct {
    /* wifi_mgr_get_status() */
    dv_wifi_t wifi;
    char ip_tail[8];            /* ".42" - last octet of ip_addr, "" unless ONLINE */
    char wifi_reason[20];       /* reason_text, e.g. "AUTH FAILED" */
    uint32_t wifi_attempts;     /* attempt_count */
    uint32_t wifi_retry_s;      /* retry_remaining_s */
    /* esp_wifi_sta_get_ap_info() - NEW read on the UI path (report_client.c already does it) */
    bool have_rssi;
    int rssi_dbm;

    /* backend_health_get_status() / _get_stats() */
    dv_api_t api;
    uint32_t api_latency_ms;    /* status.latency_ms of the last good probe */
    uint32_t api_secs_since_ok; /* stats.secs_since_ok */
    bool api_ever_ok;           /* stats.ever_ok */
    bool api_fallback;          /* device_config: running on the fallback base URL */

    /* status_deck_ui.c: sensor_present / sensor_status / sensor_last_good_us / last_accel_g */
    dv_src_t accel;
    uint32_t accel_age_ms;      /* now - sensor_last_good_us */
    float accel_g;
    /* humiture_present / humiture_status / humiture_last_good_us / last_temperature_c, _humidity_pct */
    dv_src_t env;
    uint32_t env_age_ms;
    float env_temp_f;
    float env_rh;

    /* esp_timer_get_time() / 1e6 ; diag_counters()->boot */
    uint32_t uptime_s;
    uint32_t boot;

    /* heap_caps_*(MALLOC_CAP_INTERNAL) - the same three calls diag_tick() already makes */
    uint32_t heap_free;
    uint32_t heap_largest;
    uint32_t heap_min;
} ui_diag_view_t;

/* ------------------------------------------------------ 2. text + severity */

typedef enum { SEV_NONE = 0, SEV_NOM, SEV_CAU, SEV_ALM } sev_t;

/* Proposed thresholds (need Mike's OK; sources in diagnostics-review/STATE.md):
 *   min-free < 2 KB  = ALARM   (1.2.0 PANIC at 1,207 B; workstation-watch alerts under 2 KB)
 *   largest  < 7 KB  = CAUTION (CLAUDE.md: OTA needs ~7 KB contiguous; 1.2.0 had 6,144 B) */
#define DIAG_HEAP_MIN_ALM   2048u
#define DIAG_HEAP_BLOCK_CAU 7168u

typedef struct {
    char word[16];   /* status word, 20 px: ONLINE / DOWN / LIVE / STALE ... */
    char detail[32]; /* supporting value, 20 or 14 px: "-61 dBm", "38 ms", "42 s" */
    sev_t sev;
} diag_line_t;

static inline void diag_fmt_age(char *b, size_t n, uint32_t ms)
{
    uint32_t s = ms / 1000;
    if (ms < 1000) snprintf(b, n, "<1 s");
    else if (s < 60) snprintf(b, n, "%lu s", (unsigned long)s);
    else if (s < 3600) snprintf(b, n, "%lum %02lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
    else snprintf(b, n, "%luh %02lum", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
}

/* Days / hours / minutes, always all three so the field never changes shape. */
static inline void diag_fmt_uptime(char *b, size_t n, uint32_t s)
{
    snprintf(b, n, "%lud %02luh %02lum", (unsigned long)(s / 86400), (unsigned long)(s / 3600 % 24),
             (unsigned long)(s / 60 % 60));
}

/* "41.2 K" under 100 KB, else "212 K". K = 1024 B, as heap_caps reports bytes. */
static inline void diag_fmt_kb(char *b, size_t n, uint32_t bytes)
{
    uint32_t t = (bytes * 10u + 512u) / 1024u; /* tenths of a KB */
    if (t < 1000) snprintf(b, n, "%lu.%lu K", (unsigned long)(t / 10), (unsigned long)(t % 10));
    else snprintf(b, n, "%lu K", (unsigned long)((bytes + 512u) / 1024u));
}

static inline diag_line_t diag_eval_wifi(const ui_diag_view_t *v)
{
    diag_line_t l = { "", "", SEV_NONE };
    switch (v->wifi) {
    case DV_WIFI_ONLINE:
        snprintf(l.word, sizeof l.word, "ONLINE");
        if (v->have_rssi) snprintf(l.detail, sizeof l.detail, "%d dBm", v->rssi_dbm);
        else snprintf(l.detail, sizeof l.detail, "%s", v->ip_tail);
        l.sev = SEV_NOM;
        break;
    case DV_WIFI_CONNECTING:
        snprintf(l.word, sizeof l.word, "JOINING");
        snprintf(l.detail, sizeof l.detail, "try %lu", (unsigned long)v->wifi_attempts);
        l.sev = SEV_CAU;
        break;
    case DV_WIFI_RETRY:
        snprintf(l.word, sizeof l.word, "RETRY");
        /* wifi_manager.c's four reason_text values, shortened to fit beside a 14 px label */
        {
            const char *r = strcmp(v->wifi_reason, "AUTH FAILED") == 0 ? "auth fail"
                          : strcmp(v->wifi_reason, "NO AP FOUND") == 0 ? "no AP"
                          : strcmp(v->wifi_reason, "WIFI LOST") == 0   ? "lost"
                                                                       : "retry";
            snprintf(l.detail, sizeof l.detail, "%s %lus", r, (unsigned long)v->wifi_retry_s);
        }
        l.sev = SEV_ALM;
        break;
    default:
        snprintf(l.word, sizeof l.word, "OFF");
        l.sev = SEV_ALM;
        break;
    }
    return l;
}

static inline diag_line_t diag_eval_api(const ui_diag_view_t *v)
{
    diag_line_t l = { "", "", SEV_NONE };
    char age[16];
    switch (v->api) {
    case DV_API_OK:
        snprintf(l.word, sizeof l.word, v->api_fallback ? "FALLBACK" : "OK");
        snprintf(l.detail, sizeof l.detail, "%lu ms", (unsigned long)v->api_latency_ms);
        l.sev = v->api_fallback ? SEV_CAU : SEV_NOM;
        break;
    case DV_API_DOWN:
        snprintf(l.word, sizeof l.word, "DOWN");
        diag_fmt_age(age, sizeof age, v->api_secs_since_ok * 1000u);
        snprintf(l.detail, sizeof l.detail, "%s", v->api_ever_ok ? age : "never up");
        l.sev = SEV_ALM;
        break;
    case DV_API_NO_NET:
        /* the cause is Wi-Fi, which already carries the alarm: say so, don't double-count */
        snprintf(l.word, sizeof l.word, "NO NET");
        l.sev = SEV_NONE;
        break;
    default:
        snprintf(l.word, sizeof l.word, "PROBING");
        l.sev = SEV_NONE;
        break;
    }
    return l;
}

/* `optional`: the AHT30 lives on the SENSOR dock; no dock is a fact, not a fault. */
static inline diag_line_t diag_eval_src(dv_src_t s, uint32_t age_ms, bool optional)
{
    diag_line_t l = { "", "", SEV_NONE };
    switch (s) {
    case DV_SRC_LIVE:
        snprintf(l.word, sizeof l.word, "LIVE");
        diag_fmt_age(l.detail, sizeof l.detail, age_ms);
        l.sev = SEV_NOM;
        break;
    case DV_SRC_STALE:
        snprintf(l.word, sizeof l.word, "STALE");
        diag_fmt_age(l.detail, sizeof l.detail, age_ms);
        l.sev = SEV_CAU;
        break;
    case DV_SRC_WAITING:
        snprintf(l.word, sizeof l.word, "WAITING");
        l.sev = SEV_NONE;
        break;
    default:
        snprintf(l.word, sizeof l.word, optional ? "NO DOCK" : "MISSING");
        l.sev = optional ? SEV_NONE : SEV_ALM;
        break;
    }
    return l;
}

static inline sev_t diag_eval_heap_min(const ui_diag_view_t *v)
{
    return v->heap_min < DIAG_HEAP_MIN_ALM ? SEV_ALM : SEV_NOM;
}
static inline sev_t diag_eval_heap_block(const ui_diag_view_t *v)
{
    return v->heap_largest < DIAG_HEAP_BLOCK_CAU ? SEV_CAU : SEV_NOM;
}
static inline sev_t diag_sev_max(sev_t a, sev_t b) { return a > b ? a : b; }

/* The block's word, so the heap never signals by colour alone. */
static inline diag_line_t diag_eval_heap(const ui_diag_view_t *v)
{
    diag_line_t l = { "", "", SEV_NOM };
    l.sev = diag_sev_max(diag_eval_heap_min(v), diag_eval_heap_block(v));
    snprintf(l.word, sizeof l.word, "%s", l.sev == SEV_ALM ? "CRITICAL" : l.sev == SEV_CAU ? "TIGHT" : "OK");
    return l;
}

/* ------------------------------------------------------- 3. look + furniture */

/* Mission Control Dense, adapted to the 1.3.0 rules: no grey text on dark (secondary is
 * the ui_theme.h silver), no 12 px text, 20 px for anything read at a glance. */
#define C_BG    0x070B10
#define C_TILE  0x0E151D
#define C_RULE  0x24323F
#define C_HEAD  0x6FA8C8   /* block captions, chrome */
#define C_TXT   0xE6EDF2
#define C_SIL   0xC0C4CA   /* = UI_TEXT_DIM: supporting values */
#define C_NOM   0x3DD68C
#define C_CAU   0xFFA726
#define C_ALM   0xFF4D4D
#define C_PRS   0x1B2A38

static inline uint32_t sev_color(sev_t s)
{
    return s == SEV_ALM ? C_ALM : s == SEV_CAU ? C_CAU : s == SEV_NOM ? C_NOM : C_SIL;
}

static inline lv_obj_t *dbox(lv_obj_t *p, int x, int y, int w, int h, uint32_t bg)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static inline lv_obj_t *dtxt(lv_obj_t *p, const char *s, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}

/* Header shared by all variants: square 96x48 HOME key at (4,4) - the corner and size the
 * hand already knows from ui_back_button_create - title at 20 beside it, rule at y=55.
 * Returns nothing; variants place their own roll-up (if any) at the right of the band. */
static inline void diag_header(lv_obj_t *scr, const char *title)
{
    lv_obj_t *home = lv_button_create(scr);
    lv_obj_remove_style_all(home);
    lv_obj_set_pos(home, 4, 4);
    lv_obj_set_size(home, 96, 48);
    lv_obj_set_style_bg_color(home, lv_color_hex(C_TILE), 0);
    lv_obj_set_style_bg_color(home, lv_color_hex(C_PRS), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(home, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(home, lv_color_hex(C_HEAD), 0);
    lv_obj_set_style_border_width(home, 1, 0);
    lv_obj_add_event_cb(home, ws_noop_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(dtxt(home, LV_SYMBOL_LEFT " HOME", &lv_font_montserrat_20, C_TXT));

    lv_obj_set_pos(dtxt(scr, title, &lv_font_montserrat_20, C_TXT), 110, 17);
    dbox(scr, 4, 55, 312, 1, C_HEAD);
}

/* A status word in a square outline; filled when it needs a look. 14 px, fixed height 20. */
static inline lv_obj_t *diag_flag(lv_obj_t *p, const char *w, sev_t s)
{
    uint32_t c = sev_color(s);
    bool fill = s == SEV_ALM || s == SEV_CAU;
    lv_obj_t *f = dbox(p, 0, 0, 0, 20, fill ? c : C_BG);
    lv_obj_set_width(f, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(f, 5, 0);
    lv_obj_set_style_border_color(f, lv_color_hex(s == SEV_NONE ? C_RULE : c), 0);
    lv_obj_set_style_border_width(f, 1, 0);
    lv_obj_t *l = dtxt(f, w, &lv_font_montserrat_14, fill ? C_BG : c);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    return f;
}

/* ------------------------------------------------------------ fixtures */

/* state = nominal | degraded | offline | booting; any view field can then be overridden
 * with --set (names as below). Heap numbers in `degraded` are the real 1.2.0 report
 * (min 1,207 B, largest 6,144 B). `nominal` heap numbers are ILLUSTRATIVE - no post-1.2.1
 * heartbeat value is recorded in this repo; read one from a real report before trusting. */
static inline void diag_fixture(ui_diag_view_t *v)
{
    memset(v, 0, sizeof *v);
    const char *st = ws_fx_str("state", "nominal");

    /* nominal */
    v->wifi = DV_WIFI_ONLINE; snprintf(v->ip_tail, sizeof v->ip_tail, ".42");
    v->have_rssi = true; v->rssi_dbm = -61; v->wifi_attempts = 1;
    v->api = DV_API_OK; v->api_latency_ms = 38; v->api_ever_ok = true;
    v->accel = DV_SRC_LIVE; v->accel_age_ms = 100; v->accel_g = 1.00f;
    v->env = DV_SRC_LIVE; v->env_age_ms = 1000; v->env_temp_f = 76.3f; v->env_rh = 52;
    v->uptime_s = 4 * 86400 + 7 * 3600 + 12 * 60 + 33; v->boot = 611;
    v->heap_free = 42190; v->heap_largest = 18432; v->heap_min = 9830;

    if (strcmp(st, "degraded") == 0) {
        /* Wi-Fi fine, backend down 2m14s, dock humiture stale 42 s, heap at the 1.2.0 numbers */
        v->rssi_dbm = -78;
        v->api = DV_API_DOWN; v->api_secs_since_ok = 134;
        v->env = DV_SRC_STALE; v->env_age_ms = 42000;
        v->uptime_s = 7 * 60 + 5; v->boot = 612;
        v->heap_free = 9420; v->heap_largest = 6144; v->heap_min = 1207;
    } else if (strcmp(st, "offline") == 0) {
        /* Wi-Fi auth failure in retry back-off, so the API was never tried; no SENSOR dock */
        v->wifi = DV_WIFI_RETRY; v->ip_tail[0] = 0; v->have_rssi = false; v->wifi_attempts = 6;
        snprintf(v->wifi_reason, sizeof v->wifi_reason, "AUTH FAILED"); v->wifi_retry_s = 24;
        v->api = DV_API_NO_NET; v->api_ever_ok = false;
        v->env = DV_SRC_ABSENT;
        v->uptime_s = 3 * 3600 + 41 * 60;
    } else if (strcmp(st, "booting") == 0) {
        /* first seconds: nothing has reported yet, so nothing may claim health */
        v->wifi = DV_WIFI_CONNECTING; v->ip_tail[0] = 0; v->have_rssi = false;
        v->api = DV_API_UNKNOWN; v->api_ever_ok = false;
        v->accel = DV_SRC_WAITING; v->env = DV_SRC_WAITING;
        v->uptime_s = 9; v->heap_free = 31744; v->heap_largest = 14336; v->heap_min = 28672;
    }

    v->rssi_dbm = ws_fx_int("rssi_dbm", v->rssi_dbm);
    v->api_secs_since_ok = (uint32_t)ws_fx_int("api_secs_since_ok", (int)v->api_secs_since_ok);
    v->env_age_ms = (uint32_t)ws_fx_int("env_age_ms", (int)v->env_age_ms);
    v->accel_age_ms = (uint32_t)ws_fx_int("accel_age_ms", (int)v->accel_age_ms);
    v->uptime_s = (uint32_t)ws_fx_int("uptime_s", (int)v->uptime_s);
    v->heap_free = (uint32_t)ws_fx_int("heap_free", (int)v->heap_free);
    v->heap_largest = (uint32_t)ws_fx_int("heap_largest", (int)v->heap_largest);
    v->heap_min = (uint32_t)ws_fx_int("heap_min", (int)v->heap_min);
}
