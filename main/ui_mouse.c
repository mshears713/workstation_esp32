#define CARETAKER_NATIVE_ASSETS
#include "ui_mouse.h"
#include <stdint.h>
#include "mouse_pixels.h"
#ifdef CARETAKER_NATIVE_ASSETS
#include "caretaker_pixels.h"
#include "climb_pixels.h"
#include "ui_home.h"
#include "weather_icons.h"
void ui_home_caretaker_frame(int slot, unsigned frame);
void ui_home_caretaker_end(void);
#endif
#include "house_pixels.h"
#include "smoke_pixels.h"
#include "house_mask.h"
#define HOME_DOOR_CLIP_RIGHT 295
#define HOME_APPROACH_X 264
#define HOME_ROUTE_Y 138

static void draw_pixels(lv_event_t *e, int current_frame, int facing, int scale, int clip_right)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t pos;
    lv_obj_get_coords(lv_event_get_target(e), &pos);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_COVER;
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16;) {
            int src = facing > 0 ? col : 15 - col;
            uint32_t color = mouse_pixels[current_frame][row * 16 + src];
            int end = col + 1;
            while (end < 16 && mouse_pixels[current_frame][row * 16 + (facing > 0 ? end : 15 - end)] == color) end++;
            if (color) {
                d.bg_color = lv_color_hex(color & 0xffffff);
                lv_area_t a = {pos.x1 + col * scale, pos.y1 + row * scale,
                               pos.x1 + end * scale - 1, pos.y1 + row * scale + scale - 1};
                if (a.x2 > clip_right) a.x2 = clip_right;
                if (a.x1 <= a.x2) lv_draw_rect(layer, &d, &a);
            }
            col = end;
        }
    }
}

/* HOME keeps its existing weather and controls. The free 106x32 strip
 * below the microphone is a clipping-free 2x pixel route, x=210..284.
 * Decoration never participates in hit testing. Hidden HOME pauses the
 * timer; deleting the HOME page destroys its timer with the child. */
static lv_obj_t *home_mouse, *house_obj, *home_smoke, *house_foreground;
static unsigned smoke_frame;
static lv_timer_t *home_timer;
static int home_frame, home_x, home_direction, home_pause;
/* 1: entering, -1: exiting, 0: outside/inside. */
static int door_phase;
static int home_y=HOME_ROUTE_Y, route_index=-1;
static lv_event_cb_t refresh_cb;
static lv_obj_t *refresh_message;
static int message_ticks;
#ifdef CARETAKER_NATIVE_ASSETS
/* Parent clips the 32px mouse to the text-free 18px ladder corridor. */
static lv_obj_t *lane_mouse;
static int work_slot = -1, work_tick, pending_route, lane_y, transit;
static const int work_y[] = { 74, 120, 164, 204 };
static lv_obj_t *carry_icon, *work_effect;
static void position_carry(void)
{
    const weather_status_t *w=ui_home_weather_stage_snapshot();
    unsigned card; weather_stage_field_t field;
    if (!carry_icon) return;
    if (!w || !ui_home_weather_stage_current(&card,&field)) { lv_obj_add_flag(carry_icon,LV_OBJ_FLAG_HIDDEN); return; }
    const char *key=card==0?w->now_icon:card==3?w->tmrw_icon:w->slot[card-1].icon;
    const lv_image_dsc_t *img=weather_icon(key,false);
    if (!img || transit==5) { lv_obj_add_flag(carry_icon,LV_OBJ_FLAG_HIDDEN); return; }
    lv_image_set_src(carry_icon,img);
    int x=transit==2 || transit==7 ? 192 : home_x;
    int y=transit==2 || transit==7 ? lane_y : home_y;
    lv_obj_set_pos(carry_icon,x+8,y-16 < 0 ? 0 : y-16);
    lv_obj_remove_flag(carry_icon,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(carry_icon);
}
static void lane_draw(lv_event_t *e) {
    lv_area_t a; lv_obj_get_coords(lv_event_get_target(e), &a);
    lv_draw_image_dsc_t d; lv_draw_image_dsc_init(&d);
    d.src = climb_frames[home_frame % 4]; lv_draw_image(lv_event_get_layer(e), &d, &a);
}
static void start_route(void)
{
    pending_route = 0; work_tick = 0; work_slot = -1; transit = 1;
    home_pause = 0; route_index = -1;
    if (lv_obj_has_flag(home_mouse, LV_OBJ_FLAG_HIDDEN)) {
        home_x = HOME_DOOR_CLIP_RIGHT + 1; home_y = HOME_ROUTE_Y;
        home_direction = -1; door_phase = -1;
        lv_obj_set_pos(home_mouse, home_x, home_y);
        lv_obj_remove_flag(home_mouse, LV_OBJ_FLAG_HIDDEN);
        if (house_foreground) lv_obj_remove_flag(house_foreground, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* An update during entry reverses the visible walk instead of
         * completing entry and then moving a hidden sprite. */
        door_phase = 0;
        if (house_foreground) lv_obj_remove_flag(house_foreground, LV_OBJ_FLAG_HIDDEN);
    }
    if (house_obj) lv_obj_invalidate(house_obj);
}
#endif

/* Only the final approach to the doorway is clipped. Elsewhere the mouse
 * is in front of the house, including the exterior overlap. */
static void home_draw(lv_event_t *e)
{
    int clip_right = (home_y == HOME_ROUTE_Y && home_x > HOME_DOOR_CLIP_RIGHT - 31) ? HOME_DOOR_CLIP_RIGHT : 32767;
    draw_pixels(e, home_frame, home_direction, 2, clip_right);
}
/* Pixelorama exports remain transparent; facade draws below the mouse. */
static void house_draw(lv_event_t *e)
{
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(e), &area);
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = home_pause > 0 ? &house_occupied : &house_unoccupied;
    lv_draw_image(lv_event_get_layer(e), &d, &area);
}
static void home_step(lv_timer_t *t)
{
    (void)t;
    if (!home_mouse || !lv_obj_is_visible(lv_obj_get_parent(home_mouse))) return;
    position_carry();
    if (home_smoke) { smoke_frame = (smoke_frame + 1) % 4; lv_image_set_src(home_smoke, smoke_frames[smoke_frame]); }
    if (message_ticks > 0 && --message_ticks == 0 && refresh_message) lv_label_set_text(refresh_message, "");
    if (door_phase != 0) {
        /* Move two pixels per frame through the doorway clip: the visible
         * tail clears gradually; exit follows exactly the reverse path. */
        home_x += door_phase * 2;
        lv_obj_set_x(home_mouse, home_x);
        lv_obj_invalidate(home_mouse);
        if (door_phase > 0 && home_x >= HOME_DOOR_CLIP_RIGHT + 1) {
            door_phase=0; home_pause=24;
            lv_obj_add_flag(home_mouse, LV_OBJ_FLAG_HIDDEN);
            if (house_foreground) lv_obj_add_flag(house_foreground, LV_OBJ_FLAG_HIDDEN);
            if (house_obj) lv_obj_invalidate(house_obj);
        } else if (door_phase < 0 && home_x <= HOME_APPROACH_X) {
            door_phase=0;
        }
        position_carry();
        return;
    }
#ifdef CARETAKER_NATIVE_ASSETS
    if (transit) {
        unsigned card=0; weather_stage_field_t field=WEATHER_STAGE_ICON;
        bool have=ui_home_weather_stage_current(&card,&field);
        if ((transit>=2 && transit<=5) && !have) transit=6;
        if (transit==1 || transit==6 || transit==8) {
            int target=transit==8 ? HOME_APPROACH_X : 185;
            int dx=target-home_x; home_direction=dx<0?-1:1;
            home_x += dx>4?4:dx< -4?-4:dx;
            home_frame=(home_frame+1)%6; lv_obj_set_pos(home_mouse,home_x,home_y); lv_obj_invalidate(home_mouse);
            if (home_x==target) {
                if (transit==8) {
                    transit=0;
                    if (ui_home_weather_stage_running()) start_route();
                    else { home_direction=1; door_phase=1; }
                }
                else { lane_y=home_y; lv_obj_set_y(lane_mouse,lane_y); lv_obj_add_flag(home_mouse,LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(lane_mouse,LV_OBJ_FLAG_HIDDEN); transit=have?2:7; }
            }
            position_carry();
            return;
        }
        if (transit==2 || transit==7) {
            int target=transit==7 ? HOME_ROUTE_Y : work_y[card];
            int dy=target-lane_y; lane_y+=dy>4?4:dy< -4?-4:dy;
            lv_obj_set_y(lane_mouse,lane_y);home_frame=(home_frame+1)%4;lv_obj_invalidate(lane_mouse);
            if (lane_y==target) {
                lv_obj_add_flag(lane_mouse,LV_OBJ_FLAG_HIDDEN);home_x=185;home_y=lane_y;
                lv_obj_set_pos(home_mouse,home_x,home_y);lv_obj_remove_flag(home_mouse,LV_OBJ_FLAG_HIDDEN);
                transit=transit==7?8:3;
            }
            position_carry();
            return;
        }
        if (transit==3) {
            lv_obj_t *obj=ui_home_weather_field_object(card,field);
            if (!obj) { ui_home_weather_stage_cancel(); transit=6; return; }
            lv_obj_update_layout(obj);lv_area_t a;lv_obj_get_coords(obj,&a);
            int target=(a.x1+a.x2)/2-16;if(target<6)target=6;if(target>167)target=167;
            int dx=target-home_x;home_direction=dx<0?-1:1;home_x+=dx>4?4:dx< -4?-4:dx;
            home_frame=(home_frame+1)%6;lv_obj_set_x(home_mouse,home_x);lv_obj_invalidate(home_mouse);
            if(home_x==target){transit=4;work_tick=0;}
            position_carry();
            return;
        }
        if (transit==4) { if(++work_tick>=4){work_tick=0;transit=5;} position_carry(); return; }
        if (transit==5) {
            lv_obj_t *obj=ui_home_weather_field_object(card,field);if(!obj){ui_home_weather_stage_cancel();transit=6;return;}
            lv_area_t a;lv_obj_get_coords(obj,&a);lv_obj_set_pos(work_effect,a.x1,a.y1);
            lv_image_set_src(work_effect,caretaker_work30[work_tick % CARETAKER_WORK_FRAMES]);
            lv_obj_remove_flag(work_effect,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(work_effect);
            lv_obj_add_flag(home_mouse,LV_OBJ_FLAG_HIDDEN);
            if(++work_tick>=CARETAKER_WORK_FRAMES){
                lv_obj_add_flag(work_effect,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(home_mouse,LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(home_mouse);work_tick=0;unsigned old=card;ui_home_weather_stage_reveal();
                if(!ui_home_weather_stage_current(&card,&field) || card!=old)transit=6;else transit=3;
            }
            position_carry();
            return;
        }
    }
#else
    if (route_index >= 0) {
        static const int targets[][2]={{220,124},{155,70},{155,116},{155,160},{155,204},{HOME_APPROACH_X,HOME_ROUTE_Y}};
        int tx=targets[route_index][0], ty=targets[route_index][1];
        int dx=tx-home_x, dy=ty-home_y;
        home_direction = dx < 0 ? -1 : dx > 0 ? 1 : home_direction;
        home_x += dx > 12 ? 12 : dx < -12 ? -12 : dx;
        home_y += dy > 12 ? 12 : dy < -12 ? -12 : dy;
        home_frame=(home_frame+1)%6;
        lv_obj_set_pos(home_mouse,home_x,home_y); lv_obj_invalidate(home_mouse);
        if (home_x==tx && home_y==ty && ++route_index==6) { route_index=-1; home_direction=1; door_phase=1; }
        return;
    }
#endif
    if (home_pause > 0) { home_pause--; if (!home_pause && house_obj) lv_obj_invalidate(house_obj); return; }
    if (lv_obj_has_flag(home_mouse, LV_OBJ_FLAG_HIDDEN)) {
        home_x=HOME_DOOR_CLIP_RIGHT + 1; home_direction=-1; door_phase=-1;
        lv_obj_set_x(home_mouse, home_x);
        lv_obj_remove_flag(home_mouse, LV_OBJ_FLAG_HIDDEN);
        if (house_foreground) lv_obj_remove_flag(house_foreground, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(home_mouse);
        return;
    }
    home_frame = (home_frame + 1) % 6;
    home_x += home_direction * 4;
    if (home_x <= 210) { home_x=210; home_direction=1; }
    if (home_x >= HOME_APPROACH_X) {
        home_x=HOME_APPROACH_X; home_direction=1; door_phase=1;
    }
    lv_obj_set_x(home_mouse, home_x);
    lv_obj_invalidate(home_mouse);
}
static void home_deleted(lv_event_t *e)
{
    (void)e;
    if (home_timer) { lv_timer_delete(home_timer); home_timer = NULL; }
#ifdef CARETAKER_NATIVE_ASSETS
    ui_home_caretaker_end(); lane_mouse = NULL; carry_icon = NULL; work_effect = NULL; work_slot = -1; pending_route = 0; transit = 0;
#endif
    home_mouse = NULL; house_obj = NULL; home_smoke = NULL; house_foreground = NULL; refresh_message = NULL; route_index=-1; door_phase=0;
}
void ui_mouse_home_set_active(bool active)
{
    if (!home_timer) return;
    if (active) lv_timer_resume(home_timer);
    else {
        ui_home_weather_stage_cancel();
        transit=0; work_slot=-1; work_tick=0; pending_route=0; door_phase=0; home_pause=0;
        if (lane_mouse) lv_obj_add_flag(lane_mouse,LV_OBJ_FLAG_HIDDEN);
        if (carry_icon) lv_obj_add_flag(carry_icon,LV_OBJ_FLAG_HIDDEN);
        if (work_effect) lv_obj_add_flag(work_effect,LV_OBJ_FLAG_HIDDEN);
        if (house_foreground) lv_obj_remove_flag(house_foreground, LV_OBJ_FLAG_HIDDEN);
        if (house_obj) lv_obj_invalidate(house_obj);
        if (home_mouse) { home_x=210; home_y=HOME_ROUTE_Y; home_direction=1;
            lv_obj_set_pos(home_mouse,home_x,home_y); lv_obj_remove_flag(home_mouse,LV_OBJ_FLAG_HIDDEN); }
        lv_timer_pause(home_timer);
    }
}
void ui_mouse_home_set_refresh_cb(lv_event_cb_t cb) { refresh_cb=cb; }
void ui_mouse_home_refresh_notice(const char *message)
{
    if (refresh_message) lv_label_set_text(refresh_message, message);
    message_ticks=18;
}
void ui_mouse_home_refresh_result(bool success)
{
    if (!home_mouse) return;
    if (refresh_message) lv_label_set_text(refresh_message, success ? "UPDATED" : "NO UPDATE");
    message_ticks=18;
#ifdef CARETAKER_NATIVE_ASSETS
    if (success && lane_mouse) { if (transit) pending_route=1; else start_route(); }
#else
if (success) { home_pause=0; door_phase=0; if (house_obj) lv_obj_invalidate(house_obj); route_index=0; lv_obj_remove_flag(home_mouse,LV_OBJ_FLAG_HIDDEN); if (house_foreground) lv_obj_remove_flag(house_foreground, LV_OBJ_FLAG_HIDDEN); }
#endif
}
void ui_mouse_home_create(lv_obj_t *page)
{
    if (home_mouse) return;
    home_frame = 0; home_x = 210; home_y=HOME_ROUTE_Y; home_direction = 1; home_pause = 0; route_index=-1; door_phase=0;
    home_mouse = lv_obj_create(page);
    lv_obj_remove_style_all(home_mouse);
    lv_obj_set_pos(home_mouse, home_x, HOME_ROUTE_Y);
    lv_obj_set_size(home_mouse, 32, 32);
    lv_obj_remove_flag(home_mouse, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(home_mouse, home_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(home_mouse, home_deleted, LV_EVENT_DELETE, NULL);
    smoke_frame = 0;
    home_smoke = lv_image_create(page);
    lv_image_set_src(home_smoke, smoke_frames[0]);
    lv_obj_set_pos(home_smoke, 292, 102);
    lv_obj_move_background(home_smoke);
    lv_obj_remove_flag(home_smoke, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *house = lv_obj_create(page);
    house_obj=house;
    lv_obj_remove_style_all(house);
    lv_obj_set_pos(house, 268, 126);
    lv_obj_set_size(house, 48, 48);
    lv_obj_remove_flag(house, LV_OBJ_FLAG_SCROLLABLE);
    if (refresh_cb) lv_obj_add_event_cb(house, refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(house, house_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_move_foreground(home_mouse);
    /* Foreground native facade occludes the sprite at entry/exit. A transparent
     * opening lets it appear inside the doorway; no hit target is added. */
    house_foreground = lv_image_create(page);
    lv_image_set_src(house_foreground, &house_mask);
    lv_obj_set_pos(house_foreground, 268, 126);
    lv_obj_remove_flag(house_foreground, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
#ifdef CARETAKER_NATIVE_ASSETS
    lv_obj_t *lane = lv_obj_create(page);
    lv_obj_remove_style_all(lane);
    lv_obj_set_pos(lane, 192, 0); lv_obj_set_size(lane, 18, 240);
    lv_obj_remove_flag(lane, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ladder = lv_image_create(lane);
    lv_image_set_src(ladder, &caretaker_ladder);
    lv_obj_set_pos(ladder, 0, 6);
    lv_obj_remove_flag(ladder, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lane_mouse = lv_obj_create(lane);
    lv_obj_remove_style_all(lane_mouse);
    lv_obj_set_size(lane_mouse, 18, 32); lv_obj_set_pos(lane_mouse, 0, work_y[0]);
    lv_obj_remove_flag(lane_mouse, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(lane_mouse, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(lane_mouse, lane_draw, LV_EVENT_DRAW_MAIN, NULL);
    work_slot = -1; pending_route = 0; transit = 0;
    lv_obj_move_foreground(home_mouse);
    carry_icon=lv_image_create(page);
    lv_image_set_scale(carry_icon,128);
    lv_obj_remove_flag(carry_icon,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(carry_icon,LV_OBJ_FLAG_HIDDEN);
    work_effect=lv_image_create(page);
    lv_obj_remove_flag(work_effect,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(work_effect,LV_OBJ_FLAG_HIDDEN);
#endif
    refresh_message=lv_label_create(page);
    lv_label_set_text(refresh_message, "");
    lv_obj_set_style_text_font(refresh_message, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(refresh_message, lv_color_white(), 0);
    lv_obj_set_pos(refresh_message, 210, 160);
    home_timer = lv_timer_create(home_step, 167, NULL);
}
