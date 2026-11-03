/* EXPERIMENT (not firmware). DIAGNOSTICS - variant 2: LEDGER.
 *
 * Hierarchy: one row per signal, the STATUS COLUMN on the right is what you read - a single
 * vertical scan down the flags answers "anything wrong?", and only then does the eye go left
 * to the number. Blocks are grouped by thin rules, not boxes. Memory gets the bottom strip,
 * three numbers side by side, because they are only meaningful together.
 *
 *   y   4.. 52  HOME key | DIAGNOSTICS                       (rule at 55)
 *   y  59..110  WI-FI    -61 dBm            [ ONLINE ]
 *               BACKEND  38 ms              [ OK     ]       (group rule)
 *   y 113..164  ACCEL    <1 s               [ LIVE   ]
 *               TEMP/RH  1 s                [ LIVE   ]       (group rule)
 *   y 167..189  UPTIME   4d 07h 12m           boot #611      (group rule)
 *   y 194..238  INT HEAP | FREE 41.2 K | BLOCK 18.0 K | LOW 9.6 K
 *
 * Render: ws-ui --repo <repo> snippet tools/uisim/experiments/diagnostics/v2_ledger.c --set state=degraded
 */
#include "../tools/uisim/experiments/diagnostics/diag_common.h"

#define ROW_H 26
#define FLAG_W 100
#define FLAG_X (316 - FLAG_W)

typedef struct {
    lv_obj_t *detail, *flag, *word;
} row_t;

static struct {
    row_t wifi, api, accel, env;
    lv_obj_t *uptime, *boot;
    lv_obj_t *heap_flag, *heap_word, *mem_val[3];
} ui;

static void row_build(row_t *r, lv_obj_t *scr, int y, const char *name)
{
    lv_obj_set_pos(dtxt(scr, name, &lv_font_montserrat_14, C_SIL), 8, y + 5);
    r->detail = dtxt(scr, "", &lv_font_montserrat_20, C_TXT);
    lv_obj_set_pos(r->detail, 88, y + 1);
    lv_obj_set_size(r->detail, FLAG_X - 92, 22);
    lv_label_set_long_mode(r->detail, LV_LABEL_LONG_MODE_DOTS);
    /* the flag: a square outline with the 20 px word; filled when it needs a look */
    r->flag = dbox(scr, FLAG_X, y + 1, FLAG_W, 24, C_BG);
    lv_obj_set_style_border_width(r->flag, 1, 0);
    r->word = dtxt(r->flag, "", &lv_font_montserrat_20, C_TXT);
    lv_obj_center(r->word);
}

static void flag_apply(lv_obj_t *flag, lv_obj_t *word, const char *w, sev_t s)
{
    uint32_t c = sev_color(s);
    bool fill = s == SEV_ALM || s == SEV_CAU;
    lv_label_set_text(word, w);
    lv_obj_set_style_text_color(word, lv_color_hex(fill ? C_BG : c), 0);
    lv_obj_set_style_bg_color(flag, lv_color_hex(fill ? c : C_BG), 0);
    lv_obj_set_style_border_color(flag, lv_color_hex(s == SEV_NONE ? C_RULE : c), 0);
}

static void row_apply(row_t *r, diag_line_t l)
{
    lv_label_set_text(r->detail, l.detail);
    flag_apply(r->flag, r->word, l.word, l.sev);
}

static void diag_v2_build(lv_obj_t *scr)
{
    diag_header(scr, "DIAGNOSTICS");

    row_build(&ui.wifi, scr, 59, "WI-FI");
    row_build(&ui.api, scr, 59 + ROW_H, "BACKEND");
    dbox(scr, 4, 111, 312, 1, C_RULE);
    row_build(&ui.accel, scr, 114, "ACCEL");
    row_build(&ui.env, scr, 114 + ROW_H, "TEMP/RH");
    dbox(scr, 4, 166, 312, 1, C_RULE);

    lv_obj_set_pos(dtxt(scr, "UPTIME", &lv_font_montserrat_14, C_SIL), 8, 173);
    ui.uptime = dtxt(scr, "", &lv_font_montserrat_20, C_TXT);
    lv_obj_set_pos(ui.uptime, 88, 169);
    ui.boot = dtxt(scr, "", &lv_font_montserrat_14, C_SIL);
    lv_obj_align(ui.boot, LV_ALIGN_TOP_RIGHT, -8, 173);
    dbox(scr, 4, 194, 312, 1, C_RULE);

    /* heap strip: label + its word on the left, the three numbers in equal cells */
    lv_obj_set_pos(dtxt(scr, "INT HEAP", &lv_font_montserrat_14, C_SIL), 8, 198);
    ui.heap_flag = dbox(scr, 8, 216, 74, 20, C_BG);
    lv_obj_set_style_border_width(ui.heap_flag, 1, 0);
    ui.heap_word = dtxt(ui.heap_flag, "", &lv_font_montserrat_14, C_TXT);
    lv_obj_center(ui.heap_word);
    static const char *const names[3] = { "FREE", "BLOCK", "LOW" };
    for (int i = 0; i < 3; i++) {
        int x = 88 + i * 76;
        dbox(scr, x - 4, 198, 1, 38, C_RULE);
        lv_obj_set_pos(dtxt(scr, names[i], &lv_font_montserrat_14, C_SIL), x + 2, 197);
        ui.mem_val[i] = dtxt(scr, "", &lv_font_montserrat_20, C_TXT);
        lv_obj_set_pos(ui.mem_val[i], x + 2, 214);
    }
}

static void diag_v2_apply(const ui_diag_view_t *v)
{
    row_apply(&ui.wifi, diag_eval_wifi(v));
    row_apply(&ui.api, diag_eval_api(v));
    row_apply(&ui.accel, diag_eval_src(v->accel, v->accel_age_ms, false));
    row_apply(&ui.env, diag_eval_src(v->env, v->env_age_ms, true));

    char b[24];
    diag_fmt_uptime(b, sizeof b, v->uptime_s);
    lv_label_set_text(ui.uptime, b);
    snprintf(b, sizeof b, "boot #%lu", (unsigned long)v->boot);
    lv_label_set_text(ui.boot, b);

    diag_line_t h = diag_eval_heap(v);
    flag_apply(ui.heap_flag, ui.heap_word, h.word, h.sev);
    uint32_t vals[3] = { v->heap_free, v->heap_largest, v->heap_min };
    sev_t sev[3] = { SEV_NONE, diag_eval_heap_block(v), diag_eval_heap_min(v) };
    for (int i = 0; i < 3; i++) {
        diag_fmt_kb(b, sizeof b, vals[i]);
        lv_label_set_text(ui.mem_val[i], b);
        lv_obj_set_style_text_color(ui.mem_val[i], lv_color_hex(sev[i] >= SEV_CAU ? sev_color(sev[i]) : C_TXT), 0);
    }
}

void ws_render(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_diag_view_t v;
    diag_fixture(&v);
    diag_v2_build(scr);
    diag_v2_apply(&v);
}
