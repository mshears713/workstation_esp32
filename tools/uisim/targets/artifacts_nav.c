/* Artifacts & Instruments navigation for review (native renderer and browser).
 *
 * Real firmware builders, wired the way status_deck_ui() wires them:
 *   HOME     ui_home_build(page, talk=NULL, menu=house) + ui_home_set_actions(home, tour, intro)
 *   EXPLORE  ui_menu_build_pictures("EXPLORE", Tour/Newton/Lincoln cards, "TOOLS >" key)
 *   TOOLS    ui_menu_build("TOOLS", SENSORS/LOG/FIRMWARE/SETTINGS/DIAG)
 *   back     ui_back_button_create, labelled and shown per page like set_active_page()
 *   van      when the van has left HOME: ui_adventure_open() + ui_adventure_seek(ADVENTURE_EGYPT),
 *            as home_start_tour_cb() does; Tour card: ui_adventure_open() from the start;
 *            the tour's Exit returns to HOME like tour_closed().
 *
 * Stand-ins (NOT firmware behaviour; each prints "stand-in: ..."):
 *   - page switching is a copy of set_active_page()'s rule for HOME/EXPLORE/TOOLS only;
 *   - Newton, Lincoln and the five maintenance pages are not linked: their keys only print;
 *   - speech: the tour's per-region clip request is printed, exhibit_audio is not linked;
 *   - radar/presence, diagnostics, sysmon and the crash breadcrumb are not linked.
 *
 * Fixtures: page=explore|tools (start there), disable=menu|tools (that callback is NULL, for
 * negative controls), test=true (scripted taps by hit test + assertions, prints
 * ARTIFACTS_NAV_PASS), capture_dir (50 ms frames). Every page change prints "nav: <PAGE>". */
#include "ws_sim.h"
#include "ui_home.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_adventure.h"
#include "newton_art.h"
#include "lincoln_art.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern int headless_display_write_rgb(const char *path);

typedef enum { NAV_HOME, NAV_EXPLORE, NAV_TOOLS } nav_page_t;

static lv_obj_t *screen, *page_home, *page_explore, *page_tools, *back;
static ui_home_t home;
static nav_page_t current;
static unsigned standins, phase;
static const char *capture_dir;
static uint32_t capture_start;

static void capture(lv_timer_t *t)
{
    (void)t;
    char path[512];
    unsigned ms = lv_tick_get() - capture_start;
    lv_obj_update_layout(lv_screen_active());
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    snprintf(path, sizeof(path), "%s/frame-%05u.rgb", capture_dir, ms);
    assert(headless_display_write_rgb(path) == 0);
}

/* Copy of set_active_page() for the three pages this target links. */
static void set_page(nav_page_t p)
{
    static const char *names[] = {"HOME", "EXPLORE", "TOOLS"};
    lv_obj_t *pages[] = {page_home, page_explore, page_tools};
    current = p;
    ui_home_set_active(p == NAV_HOME);
    for (int i = 0; i < 3; i++) {
        if (i == (int)p) lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (p == NAV_HOME) {
        lv_obj_add_flag(back, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(back, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(back, 0), p == NAV_EXPLORE ? LV_SYMBOL_LEFT " HOME" : LV_SYMBOL_LEFT " MENU");
        lv_obj_move_foreground(back);
    }
    printf("nav: %s\n", names[p]);
}

static void back_cb(lv_event_t *e) { (void)e; set_page(current == NAV_TOOLS ? NAV_EXPLORE : NAV_HOME); }
static void house_cb(lv_event_t *e) { (void)e; set_page(NAV_EXPLORE); }
static void tools_cb(lv_event_t *e) { (void)e; set_page(NAV_TOOLS); }
static void home_cb(lv_event_t *e) { (void)e; set_page(NAV_HOME); }
static void intro_cb(lv_event_t *e) { (void)e; standins++; puts("stand-in: guide clip request (exhibit_audio not linked)"); }
static void standin_cb(lv_event_t *e)
{
    lv_obj_t *t = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child_count(t) ? lv_obj_get_child(t, 0) : NULL;
    standins++;
    printf("stand-in: %s (page not linked in this target)\n",
           label && lv_obj_has_class(label, &lv_label_class) ? lv_label_get_text(label) : "card");
}

static void tour_scene(adventure_scene_t scene, void *ctx)
{
    (void)ctx;
    if (scene == ADVENTURE_EGYPT || scene == ADVENTURE_GREECE || scene == ADVENTURE_GOTHIC)
        printf("stand-in: tour clip request scene=%d (exhibit_audio not linked)\n", (int)scene);
    printf("tour: scene %d\n", (int)scene);
}
/* As the app's tour_closed(): back to HOME (its clip stop is a stand-in here). */
static void tour_closed(void *ctx) { (void)ctx; puts("tour: closed"); set_page(NAV_HOME); }

static void van_tour_cb(lv_event_t *e)
{
    (void)e;
    if (ui_adventure_is_open()) return;
    ui_home_set_active(false);
    ui_adventure_set_scene_cb(tour_scene, NULL);
    ui_adventure_open(screen, tour_closed, NULL);
    ui_adventure_seek(ADVENTURE_EGYPT);
    puts("tour: opened at Egypt from the van");
}
static void tour_card_cb(lv_event_t *e)
{
    (void)e;
    if (ui_adventure_is_open()) return;
    ui_home_set_active(false);
    ui_adventure_set_scene_cb(tour_scene, NULL);
    ui_adventure_open(screen, tour_closed, NULL);
    puts("tour: opened from the EXPLORE card");
}

/* ---- scripted test: taps go through LVGL hit testing like a finger would ---- */
static lv_obj_t *tap(int x, int y)
{
    lv_obj_update_layout(screen);
    lv_point_t p = {x, y};
    lv_obj_t *o = lv_indev_search_obj(screen, &p);
    assert(o);
    lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
    return o;
}
static lv_obj_t *find_text(lv_obj_t *o, const char *t)
{
    if (!lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_class(o, &lv_label_class) && !strcmp(lv_label_get_text(o), t))
        return o;
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return NULL;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_text(lv_obj_get_child(o, i), t);
        if (r) return r;
    }
    return NULL;
}
/* Tap the centre of a visible label's clickable parent, by coordinates. */
static void tap_label(const char *text)
{
    lv_obj_update_layout(screen);
    lv_obj_t *l = find_text(screen, text);
    assert(l);
    lv_area_t a;
    lv_obj_get_coords(lv_obj_get_parent(l), &a);
    tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
}
static bool visible(lv_obj_t *o) { return !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }

static void step(lv_timer_t *timer)
{
    switch (phase++) {
    case 0:
        assert(current == NAV_HOME && visible(page_home) && !visible(page_explore) && !visible(page_tools) && !visible(back));
        tap(160, 236);                        /* bare lawn: no page change */
        assert(current == NAV_HOME);
        tap(50, 170);                         /* house */
        assert(current == NAV_EXPLORE && visible(page_explore) && !visible(page_home) && visible(back));
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(back, 0)), LV_SYMBOL_LEFT " HOME"));
        tap_label("Newton");                  /* stand-in key: prints, stays on EXPLORE */
        assert(current == NAV_EXPLORE && standins == 1);
        tap_label("TOOLS " LV_SYMBOL_RIGHT);
        assert(current == NAV_TOOLS && visible(page_tools) && !visible(page_explore));
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(back, 0)), LV_SYMBOL_LEFT " MENU"));
        tap_label("DIAG");
        assert(current == NAV_TOOLS && standins == 2);
        tap_label(LV_SYMBOL_LEFT " MENU");
        assert(current == NAV_EXPLORE);
        tap_label(LV_SYMBOL_LEFT " HOME");
        assert(current == NAV_HOME && visible(page_home) && !visible(back) && ui_home_state().active);
        tap(250, 175);                        /* van */
        assert(ui_home_state().van_running && !ui_adventure_is_open());
        lv_timer_set_period(timer, 3600);
        break;
    case 1:                                   /* van gone: Tour A at Egypt */
        assert(ui_adventure_is_open() && ui_adventure_scene() == ADVENTURE_EGYPT && !ui_home_state().active);
        tap(4 + 2 * 106 + 50, 192 + 24);      /* the tour's own Exit key (real exit path) */
        lv_timer_set_period(timer, 300);
        break;
    default:
        assert(!ui_adventure_is_open());
        assert(ui_adventure_timer_count() == 0);
        assert(current == NAV_HOME && visible(page_home) && ui_home_state().active);
        printf("ARTIFACTS_NAV_PASS: lawn tap ignored; house -> EXPLORE (< HOME); Newton stand-in; TOOLS (< MENU); "
               "DIAG stand-in; back to EXPLORE and HOME; van -> Tour A at Egypt; tour Exit -> HOME active; %u stand-ins\n",
               standins);
        lv_timer_delete(timer);
    }
}

void ws_render(lv_obj_t *scr)
{
    screen = scr;
    capture_dir = ws_fx_str("capture_dir", "");
    capture_start = lv_tick_get();
    if (capture_dir[0]) lv_timer_create(capture, 50, NULL);
    const char *disable = ws_fx_str("disable", "");

    page_home = ui_page_create(scr);
    ui_home_build(&home, page_home, NULL, strcmp(disable, "menu") ? house_cb : NULL);
    ui_home_set_actions(home_cb, van_tour_cb, intro_cb);

    page_explore = ui_page_create(scr);
    const ui_menu_picture_item_t pictures[] = {
        {"Tour", ui_adventure_original_asset(TOUR_ASSET_VAN, 0), 320, tour_card_cb},
        {"Newton", newton_art_get()->person[0].images[0], 256, standin_cb},
        {"Lincoln", lincoln_art_get()->actor[0], 256, standin_cb},
    };
    static ui_menu_item_t tools_key = {"TOOLS " LV_SYMBOL_RIGHT, NULL};
    tools_key.cb = strcmp(disable, "tools") ? tools_cb : NULL;
    ui_menu_build_pictures(page_explore, "EXPLORE", pictures, 3, &tools_key);

    page_tools = ui_page_create(scr);
    static const ui_menu_item_t tools[] = {
        {"SENSORS", standin_cb}, {"LOG", standin_cb}, {"FIRMWARE", standin_cb}, {"SETTINGS", standin_cb}, {"DIAG", standin_cb},
    };
    ui_menu_build(page_tools, "TOOLS", tools, 5, NULL);

    back = ui_back_button_create(scr, back_cb);
    const char *start = ws_fx_str("page", "home");
    set_page(!strcmp(start, "explore") ? NAV_EXPLORE : !strcmp(start, "tools") ? NAV_TOOLS : NAV_HOME);
    if (ws_fx_bool("test", false)) lv_timer_create(step, 300, NULL);
}
