/* EXPERIMENT (not firmware). DIAGNOSTICS - variant 1: QUADRANTS.
 *
 * Hierarchy: four equal blocks, one per question - "am I connected?", "are the sensors
 * fresh?", "how long up?", "how much internal RAM?". The eye goes to whichever block has a
 * thick coloured border (worst severity inside it); inside each block the status WORD is the
 * 20 px line, the label and the supporting value sit above it at 14.
 *
 *   y   4.. 52  HOME key | DIAGNOSTICS                          (rule at 55)
 *   y  60..148  LINK (Wi-Fi, API)        | SENSORS (accel, env)
 *   y 150..238  UPTIME (d h m, boot #)    | INTERNAL HEAP (free / largest / low)
 *
 * Render: ws-ui --repo <repo> snippet tools/uisim/experiments/diagnostics/v1_quad.c --set state=degraded
 * state = nominal | degraded | offline | booting (diag_common.h).
 */
#include "../tools/uisim/experiments/diagnostics/diag_common.h"

#define QW 155
#define QH 88

typedef struct {
    lv_obj_t *box;
    lv_obj_t *name, *detail, *word; /* per signal */
} sig_t;

static struct {
    lv_obj_t *q_link, *q_sens, *q_up, *q_mem;
    sig_t wifi, api, accel, env;
    lv_obj_t *uptime, *boot;
    lv_obj_t *mem_val[3], *mem_word;
} ui;

static lv_obj_t *quad(lv_obj_t *scr, int x, int y)
{
    lv_obj_t *q = dbox(scr, x, y, QW, QH, C_TILE);
    lv_obj_set_style_border_color(q, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_border_width(q, 1, 0);
    return q;
}

/* one signal = 42 px: 14 px name (left) + 14 px detail (right), then the 20 px word */
static void sig_build(sig_t *s, lv_obj_t *q, int y, const char *name)
{
    s->name = dtxt(q, name, &lv_font_montserrat_14, C_HEAD);
    lv_obj_set_pos(s->name, 7, y);
    s->detail = dtxt(q, "", &lv_font_montserrat_14, C_SIL);
    lv_obj_align(s->detail, LV_ALIGN_TOP_RIGHT, -7, y);
    s->word = dtxt(q, "", &lv_font_montserrat_20, C_TXT);
    lv_obj_set_pos(s->word, 7, y + 16);
}

static void sig_apply(sig_t *s, diag_line_t l)
{
    lv_label_set_text(s->word, l.word);
    lv_obj_set_style_text_color(s->word, lv_color_hex(sev_color(l.sev)), 0);
    lv_label_set_text(s->detail, l.detail);
    if (l.detail[0]) lv_obj_remove_flag(s->detail, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s->detail, LV_OBJ_FLAG_HIDDEN);
}

static void quad_sev(lv_obj_t *q, sev_t s)
{
    bool hot = s == SEV_ALM || s == SEV_CAU;
    lv_obj_set_style_border_color(q, lv_color_hex(hot ? sev_color(s) : C_RULE), 0);
    lv_obj_set_style_border_width(q, hot ? 2 : 1, 0);
}

static void diag_v1_build(lv_obj_t *scr)
{
    diag_header(scr, "DIAGNOSTICS");

    ui.q_link = quad(scr, 4, 60);
    ui.q_sens = quad(scr, 161, 60);
    ui.q_up = quad(scr, 4, 150);
    ui.q_mem = quad(scr, 161, 150);

    sig_build(&ui.wifi, ui.q_link, 3, "WI-FI");
    sig_build(&ui.api, ui.q_link, 45, "BACKEND");
    sig_build(&ui.accel, ui.q_sens, 3, "ACCEL");
    sig_build(&ui.env, ui.q_sens, 45, "TEMP / RH");

    lv_obj_set_pos(dtxt(ui.q_up, "UPTIME", &lv_font_montserrat_14, C_HEAD), 7, 3);
    ui.uptime = dtxt(ui.q_up, "", &lv_font_montserrat_20, C_TXT);
    lv_obj_set_pos(ui.uptime, 7, 24);
    ui.boot = dtxt(ui.q_up, "", &lv_font_montserrat_14, C_SIL);
    lv_obj_set_pos(ui.boot, 7, 61);

    lv_obj_set_pos(dtxt(ui.q_mem, "INT HEAP", &lv_font_montserrat_14, C_HEAD), 7, 3);
    ui.mem_word = dtxt(ui.q_mem, "", &lv_font_montserrat_14, C_SIL);
    lv_obj_align(ui.mem_word, LV_ALIGN_TOP_RIGHT, -7, 3);
    static const char *const names[3] = { "FREE", "BLOCK", "LOW" };
    for (int i = 0; i < 3; i++) {
        /* 22 px pitch = the 20 px line height, so values never touch */
        lv_obj_set_pos(dtxt(ui.q_mem, names[i], &lv_font_montserrat_14, C_SIL), 7, 24 + i * 22);
        ui.mem_val[i] = dtxt(ui.q_mem, "", &lv_font_montserrat_20, C_TXT);
        lv_obj_align(ui.mem_val[i], LV_ALIGN_TOP_RIGHT, -7, 20 + i * 22);
    }
}

static void diag_v1_apply(const ui_diag_view_t *v)
{
    diag_line_t w = diag_eval_wifi(v), a = diag_eval_api(v);
    diag_line_t ac = diag_eval_src(v->accel, v->accel_age_ms, false);
    diag_line_t en = diag_eval_src(v->env, v->env_age_ms, true);
    sig_apply(&ui.wifi, w);
    sig_apply(&ui.api, a);
    sig_apply(&ui.accel, ac);
    sig_apply(&ui.env, en);
    quad_sev(ui.q_link, diag_sev_max(w.sev, a.sev));
    quad_sev(ui.q_sens, diag_sev_max(ac.sev, en.sev));

    char b[24];
    diag_fmt_uptime(b, sizeof b, v->uptime_s);
    lv_label_set_text(ui.uptime, b);
    snprintf(b, sizeof b, "boot #%lu", (unsigned long)v->boot);
    lv_label_set_text(ui.boot, b);

    uint32_t vals[3] = { v->heap_free, v->heap_largest, v->heap_min };
    sev_t sev[3] = { SEV_NONE, diag_eval_heap_block(v), diag_eval_heap_min(v) };
    for (int i = 0; i < 3; i++) {
        diag_fmt_kb(b, sizeof b, vals[i]);
        lv_label_set_text(ui.mem_val[i], b);
        lv_obj_set_style_text_color(ui.mem_val[i], lv_color_hex(sev[i] >= SEV_CAU ? sev_color(sev[i]) : C_TXT), 0);
    }
    diag_line_t h = diag_eval_heap(v);
    lv_label_set_text(ui.mem_word, h.word);
    lv_obj_set_style_text_color(ui.mem_word, lv_color_hex(sev_color(h.sev)), 0);
    quad_sev(ui.q_mem, h.sev);
}

void ws_render(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_diag_view_t v;
    diag_fixture(&v);
    diag_v1_build(scr);
    diag_v1_apply(&v);
}
