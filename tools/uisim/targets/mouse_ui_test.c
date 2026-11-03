#include "lvgl.h"
#include "ui_mouse.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned exits;
static lv_point_t point;
static lv_indev_state_t press;
static void exit_cb(void *ctx){assert(ctx==(void *)&exits);exits++;}
static void read_pointer(lv_indev_t *d,lv_indev_data_t *v){(void)d;v->point=point;v->state=press;}
static void tick(unsigned ms){for(unsigned t=0;t<ms;t+=5){lv_tick_inc(5);lv_timer_handler();}}
static unsigned timers(void){unsigned n=0;for(lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t))n++;return n;}
static void tap(lv_indev_t *d,int x,int y){point=(lv_point_t){x,y};press=LV_INDEV_STATE_PRESSED;lv_indev_read(d);lv_tick_inc(40);press=LV_INDEV_STATE_RELEASED;lv_indev_read(d);}
void ws_render(lv_obj_t *scr)
{
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input,lv_display_get_default());lv_indev_set_read_cb(input,read_pointer);lv_indev_set_mode(input,LV_INDEV_MODE_EVENT);
    tick(50);unsigned objects=lv_obj_get_child_count(scr),base=timers();
    for(unsigned i=0;i<300;i++) {
        ui_mouse_open(scr,exit_cb,&exits);ui_mouse_open(scr,exit_cb,&exits);
        assert(lv_obj_get_child_count(scr)==objects+1);
        assert(timers()==base+1);
        ui_mouse_close();ui_mouse_close();tick(10);
        assert(lv_obj_get_child_count(scr)==objects&&timers()==base);
    }
    ui_mouse_open(scr,exit_cb,&exits);lv_obj_t *root=lv_obj_get_child(scr,-1);
    lv_obj_t *sprite=lv_obj_get_child(root,-2),*hint=lv_obj_get_child(root,-1);
    lv_obj_update_layout(scr);assert(lv_obj_get_width(sprite)==48);
    tick(340);assert(lv_obj_get_x(sprite)>8);
    /* Every station, including the scaled artwork, passes the tap to root. */
    for(int i=0;i<3;i++) {
        lv_point_t p={56+i*104,105};assert(lv_indev_search_obj(scr,&p)==root);
        tap(input,p.x,p.y);assert(!strcmp(lv_label_get_text(hint),"Paused - tap to walk"));
        lv_obj_update_layout(scr);int x=lv_obj_get_x(sprite);tick(1300);assert(lv_obj_get_x(sprite)==x);
        tap(input,p.x,p.y);assert(!strcmp(lv_label_get_text(hint),"Tap to pause"));tick(1700);
    }
    tap(input,40,30);tick(10);assert(exits==1);
    assert(lv_obj_get_child_count(scr)==objects&&timers()==base);
    ui_mouse_open(scr,exit_cb,&exits);root=lv_obj_get_child(scr,-1);lv_obj_update_layout(scr);
    assert(lv_obj_get_x(lv_obj_get_child(root,-2))==8);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(root,-1)),"Tap to pause"));
    ui_mouse_close();tick(10);lv_indev_delete(input);
    puts("MOUSE UI PASS: 300 repeated open/double-close cycles, timer baseline, icon pass-through taps, pause/resume, HOME callback and fresh reopen");
}
