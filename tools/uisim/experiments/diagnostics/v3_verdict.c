/* EXPERIMENT (not firmware). DIAGNOSTICS - variant 3: VERDICT FIRST.
 *
 * Hierarchy: the page answers before it explains. A full-width band says the single worst
 * thing in words ("BACKEND DOWN 2m 14s"), the rest of the problems underneath; when nothing
 * is wrong it says ALL NOMINAL and when sources have not reported yet it says so instead of
 * claiming health. Below it the detail is split: signal words on the left (no numbers unless
 * the signal is in trouble - the band carries those), internal heap and uptime on the right.
 *
 *   y   4.. 52  HOME key | DIAGNOSTICS                       (rule at 55)
 *   y  59..101  VERDICT band: worst item at 20, the others at 14, stripe in its colour
 *   y 106..236  WI-FI / API / IMU / ENV words | INT HEAP free/block/low + word, UPTIME
 *
 * Render: ws-ui --repo <repo> snippet tools/uisim/experiments/diagnostics/v3_verdict.c --set state=degraded
 */
#include "../tools/uisim/experiments/diagnostics/diag_common.h"

#define LCOL_W 160
#define RCOL_X 170

static struct {
    lv_obj_t *band, *stripe, *head, *sub;
    lv_obj_t *word[4];
    lv_obj_t *heap_word, *mem_val[3], *uptime;
} ui;

static void diag_v3_build(lv_obj_t *scr)
{
    diag_header(scr, "DIAGNOSTICS");

    ui.band = dbox(scr, 4, 59, 312, 43, C_TILE);
    lv_obj_set_style_border_width(ui.band, 1, 0);
    ui.stripe = dbox(scr, 4, 59, 6, 43, C_NOM); /* sibling, so the band's border doesn't clip it */
    ui.head = dtxt(ui.band, "", &lv_font_montserrat_20, C_TXT);
    lv_obj_set_pos(ui.head, 14, 2);
    lv_obj_set_size(ui.head, 290, 22);
    lv_label_set_long_mode(ui.head, LV_LABEL_LONG_MODE_DOTS);
    ui.sub = dtxt(ui.band, "", &lv_font_montserrat_14, C_SIL);
    lv_obj_set_pos(ui.sub, 14, 24);
    lv_obj_set_size(ui.sub, 290, 16);
    lv_label_set_long_mode(ui.sub, LV_LABEL_LONG_MODE_DOTS);

    /* left: four signal words, 32 px rows, hairlines between */
    static const char *const names[4] = { "WI-FI", "API", "IMU", "ENV" };
    for (int i = 0; i < 4; i++) {
        int y = 107 + i * 33;
        lv_obj_set_pos(dtxt(scr, names[i], &lv_font_montserrat_14, C_SIL), 8, y + 9);
        ui.word[i] = dtxt(scr, "", &lv_font_montserrat_20, C_TXT);
        lv_obj_align(ui.word[i], LV_ALIGN_TOP_RIGHT, -(320 - LCOL_W), y + 5); /* 4 px clear of the divider */
        if (i < 3) dbox(scr, 4, y + 32, LCOL_W, 1, C_RULE);
    }
    dbox(scr, RCOL_X - 4, 107, 1, 129, C_RULE);

    /* right: heap block, then uptime */
    lv_obj_set_pos(dtxt(scr, "INT HEAP", &lv_font_montserrat_14, C_HEAD), RCOL_X + 2, 107);
    ui.heap_word = dtxt(scr, "", &lv_font_montserrat_14, C_SIL);
    lv_obj_align(ui.heap_word, LV_ALIGN_TOP_RIGHT, -8, 107);
    static const char *const mem[3] = { "FREE", "BLOCK", "LOW" };
    for (int i = 0; i < 3; i++) {
        lv_obj_set_pos(dtxt(scr, mem[i], &lv_font_montserrat_14, C_SIL), RCOL_X + 2, 128 + i * 22);
        ui.mem_val[i] = dtxt(scr, "", &lv_font_montserrat_20, C_TXT);
        lv_obj_align(ui.mem_val[i], LV_ALIGN_TOP_RIGHT, -8, 124 + i * 22);
    }
    dbox(scr, RCOL_X, 192, 316 - RCOL_X, 1, C_RULE);
    lv_obj_set_pos(dtxt(scr, "UPTIME", &lv_font_montserrat_14, C_HEAD), RCOL_X + 2, 196);
    ui.uptime = dtxt(scr, "", &lv_font_montserrat_20, C_TXT);
    lv_obj_set_pos(ui.uptime, RCOL_X + 2, 213);
}

typedef struct {
    const char *name;
    diag_line_t l;
} item_t;

static void diag_v3_apply(const ui_diag_view_t *v)
{
    item_t it[5] = {
        { "WI-FI", diag_eval_wifi(v) },
        { "BACKEND", diag_eval_api(v) },
        { "IMU", diag_eval_src(v->accel, v->accel_age_ms, false) },
        { "TEMP/RH", diag_eval_src(v->env, v->env_age_ms, true) },
        { "HEAP", diag_eval_heap(v) },
    };

    /* left column words */
    for (int i = 0; i < 4; i++) {
        lv_label_set_text(ui.word[i], it[i].l.word);
        lv_obj_set_style_text_color(ui.word[i], lv_color_hex(sev_color(it[i].l.sev)), 0);
    }

    /* verdict: worst first (ALM before CAU, then list order); the rest at 14 */
    int order[5], n = 0;
    for (int s = SEV_ALM; s >= SEV_CAU; s--)
        for (int i = 0; i < 5; i++)
            if ((int)it[i].l.sev == s) order[n++] = i;

    /* sources that have not reported yet; NO NET (explained by Wi-Fi) and NO DOCK (a fact) don't count */
    char waiting[64] = "";
    int nwait = 0;
    for (int i = 0; i < 4; i++) {
        bool explained = (i == 1 && v->api == DV_API_NO_NET) || (i == 3 && v->env == DV_SRC_ABSENT);
        if (it[i].l.sev == SEV_NONE && !explained) {
            size_t len = strlen(waiting);
            snprintf(waiting + len, sizeof waiting - len, "%s%s", nwait++ ? ", " : "waiting on ", it[i].name);
        }
    }

    char head[48], sub[96] = "";
    sev_t worst = SEV_NOM;
    if (n > 0) {
        const item_t *w = &it[order[0]];
        worst = w->l.sev;
        /* the heap's detail is its numbers, shown on the right; elsewhere the band adds the age/reason */
        snprintf(head, sizeof head, "%s %s  %s", w->name, w->l.word, order[0] == 4 ? "" : w->l.detail);
        for (int k = 1; k < n; k++) {
            size_t len = strlen(sub);
            snprintf(sub + len, sizeof sub - len, "%s%s %s", k > 1 ? ",  " : "+ ", it[order[k]].name,
                     it[order[k]].l.word);
        }
        if (n == 1) snprintf(sub, sizeof sub, "%s", nwait ? waiting : "nothing else needs a look");
    } else {
        /* no problems - but only "nominal" if every source has actually reported */
        if (nwait) {
            snprintf(sub, sizeof sub, "%s", waiting);
            snprintf(head, sizeof head, "STARTING UP");
            worst = SEV_NONE;
        } else {
            snprintf(head, sizeof head, "ALL NOMINAL");
            snprintf(sub, sizeof sub, v->env == DV_SRC_ABSENT ? "link, IMU, heap reporting; no dock"
                                                              : "link, sensors, heap all reporting");
        }
    }
    uint32_t wc = sev_color(worst);
    lv_label_set_text(ui.head, head);
    lv_obj_set_style_text_color(ui.head, lv_color_hex(worst == SEV_NONE ? C_TXT : wc), 0);
    lv_label_set_text(ui.sub, sub);
    lv_obj_set_style_bg_color(ui.stripe, lv_color_hex(worst == SEV_NONE ? C_RULE : wc), 0);
    lv_obj_set_style_border_color(ui.band, lv_color_hex(worst >= SEV_CAU ? wc : C_RULE), 0);

    char b[24];
    lv_label_set_text(ui.heap_word, it[4].l.word);
    lv_obj_set_style_text_color(ui.heap_word, lv_color_hex(sev_color(it[4].l.sev)), 0);
    uint32_t vals[3] = { v->heap_free, v->heap_largest, v->heap_min };
    sev_t sev[3] = { SEV_NONE, diag_eval_heap_block(v), diag_eval_heap_min(v) };
    for (int i = 0; i < 3; i++) {
        diag_fmt_kb(b, sizeof b, vals[i]);
        lv_label_set_text(ui.mem_val[i], b);
        lv_obj_set_style_text_color(ui.mem_val[i], lv_color_hex(sev[i] >= SEV_CAU ? sev_color(sev[i]) : C_TXT), 0);
    }
    diag_fmt_uptime(b, sizeof b, v->uptime_s);
    lv_label_set_text(ui.uptime, b);
}

void ws_render(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_diag_view_t v;
    diag_fixture(&v);
    diag_v3_build(scr);
    diag_v3_apply(&v);
}
