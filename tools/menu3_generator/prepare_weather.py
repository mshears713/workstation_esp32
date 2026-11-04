"""Reproduce a namespaced copy of the archived HOME weather implementation.

Input must be `git show a5a9132:main/ui_home.c`, retained as weather-original.c.
The historical global mouse owner is omitted; existing Orlando owns its mouse.
This prepares files only and does not wire the application or change a checkout.
"""
from pathlib import Path
p=Path(__file__).resolve().parent
s=(p/'weather-original.c').read_text(encoding='utf-8-sig')
s=s.replace('#define CARETAKER_NATIVE_ASSETS\n','/* No duplicate caretaker frames: Orlando retains its own mouse owner. */\n',1)
s=s.replace('#include "ui_home.h"','#include "ui_weather_page.h"')
for name in ['weather_stage_offer','weather_stage_current','weather_stage_snapshot','weather_stage_running','weather_stage_reveal','weather_stage_cancel','weather_field_object','reveal_weather_field','caretaker_end','caretaker_frame','apply_weather','set_link','build']:
 s=s.replace('ui_home_'+name,'ui_weather_page_'+name)
s=s.replace('    ui_mouse_home_create(page);','    /* Orlando HOME remains the sole owner of the existing mouse integration. */')
(p/'ui_weather_page.c').write_text(s,encoding='utf-8')
(p/'ui_weather_page.h').write_text('''#pragma once
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
''',encoding='utf-8')
