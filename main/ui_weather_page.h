#pragma once
#include "ui_home.h"
void ui_weather_page_build(ui_home_t *,lv_obj_t *,lv_event_cb_t,lv_event_cb_t);
void ui_weather_page_apply_weather(ui_home_t *,const ui_home_weather_t *);
void ui_weather_page_reveal_weather_field(ui_home_t *,const weather_status_t *,unsigned,weather_stage_field_t);
bool ui_weather_page_weather_stage_offer(const weather_status_t *);
bool ui_weather_page_weather_stage_current(unsigned *,weather_stage_field_t *);
const weather_status_t *ui_weather_page_weather_stage_snapshot(void);
bool ui_weather_page_weather_stage_running(void);
bool ui_weather_page_weather_stage_reveal(void);
void ui_weather_page_weather_stage_cancel(void);
lv_obj_t *ui_weather_page_weather_field_object(unsigned,weather_stage_field_t);
void ui_weather_page_caretaker_end(void);
void ui_weather_page_caretaker_frame(int,unsigned);
void ui_weather_page_set_link(ui_home_t *,voice_widget_link_t);
