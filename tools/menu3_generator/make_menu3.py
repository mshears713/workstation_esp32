from pathlib import Path
B=Path(__file__).resolve().parent
p=B/'ui_menu.h';s=p.read_text();s += '''\n/* Picture-card variant extends the existing checked-in LVGL menu builder. */
typedef struct { const char *label; const lv_image_dsc_t *image; int scale; lv_event_cb_t cb; } ui_menu_picture_item_t;
void ui_menu_build_pictures(lv_obj_t *,const char *,const ui_menu_picture_item_t *,int,const ui_menu_item_t *);
''';p.write_text(s)
p=B/'ui_menu.c';s=p.read_text();s += '''
void ui_menu_build_pictures(lv_obj_t *page,const char *title,const ui_menu_picture_item_t *items,int count,const ui_menu_item_t *corner)
{
    lv_obj_set_style_bg_color(page,lv_color_hex(0x254656),0);
    lv_obj_set_style_bg_grad_color(page,lv_color_hex(0x507a78),0);
    lv_obj_set_style_bg_grad_dir(page,LV_GRAD_DIR_VER,0);
    lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);
    ui_menu_build(page,title,NULL,0,corner);
    lv_obj_set_style_text_color(lv_obj_get_child(page,0),lv_color_hex(0xf6edda),0);
    for (int i=0;i<count && i<3;i++) {
        lv_obj_t *card=lv_button_create(page);
        lv_obj_set_pos(card,8+i*104,70);lv_obj_set_size(card,96,150);
        lv_obj_set_style_bg_color(card,lv_color_hex(0xe4d2ad),0);
        lv_obj_set_style_border_width(card,2,0);lv_obj_set_style_border_color(card,lv_color_hex(0x8a6d4b),0);
        lv_obj_set_style_radius(card,10,0);lv_obj_set_style_pad_all(card,0,0);
        lv_obj_set_style_shadow_color(card,lv_color_hex(0x132d31),0);
        lv_obj_set_style_shadow_width(card,5,0);lv_obj_set_style_shadow_offset_y(card,3,0);
        lv_obj_set_style_translate_y(card,2,LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(card,1,LV_STATE_PRESSED);
        if(items[i].cb)lv_obj_add_event_cb(card,items[i].cb,LV_EVENT_CLICKED,NULL);
        lv_obj_t *pic=lv_image_create(card);lv_image_set_src(pic,items[i].image);lv_image_set_scale(pic,items[i].scale);lv_image_set_antialias(pic,false);lv_obj_align(pic,LV_ALIGN_CENTER,0,-22);
        lv_obj_t *label=lv_label_create(card);lv_label_set_text(label,items[i].label);
        lv_obj_set_style_text_font(label,&lv_font_montserrat_14,0);lv_obj_set_style_text_color(label,lv_color_hex(0x172533),0);
        lv_obj_align(label,LV_ALIGN_BOTTOM_MID,0,-12);
    }
}
''';p.write_text(s)
p=B/'ui_weather_page.c';s=p.read_text().replace('lv_label_set_text(ml, "MENU");','lv_label_set_text(ml, "BACK");');p.write_text(s)
p=B/'status_deck_ui.c';s=p.read_text();s=s.replace('#include "weather_client.h"','#include "weather_client.h"\n#include "weather_icons.h"\n#include "ui_weather_page.h"')
s=s.replace('    APP_PAGE_NOTIFY,\n    APP_PAGE_COUNT,','    APP_PAGE_NOTIFY,\n    APP_PAGE_MORE3,\n    APP_PAGE_WEATHER,\n    APP_PAGE_COUNT,')
s=s.replace('static lv_obj_t *page_notify;', 'static lv_obj_t *page_more3, *page_weather;\nstatic ui_home_t weather_ui;\nstatic bool weather_force_update;\nstatic app_page_t current_app_page;\nstatic lv_obj_t *page_notify;')
s=s.replace('        page_more, page_notify,\n','        page_more, page_notify, page_more3, page_weather,\n')
s=s.replace('"MORE", "NOTIFY",\n','"MORE", "NOTIFY", "MORE3", "WEATHER",\n')
s=s.replace('    diag_set_page(page_names[page]);','    current_app_page=page;\n    diag_set_page(page_names[page]);')
s=s.replace('    if (page == APP_PAGE_HOME) {\n        lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);','    if (page == APP_PAGE_HOME || page == APP_PAGE_WEATHER) {\n        lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);')
s=s.replace('        lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);\n    }\n\n    /* Mission 15:', '        lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);\n        lv_label_set_text(lv_obj_get_child(back_button,0),page==APP_PAGE_MORE?LV_SYMBOL_LEFT " MENU":page==APP_PAGE_MORE3?LV_SYMBOL_LEFT " MORE":LV_SYMBOL_LEFT " HOME");\n    }\n\n    /* Mission 15:')
s=s.replace('static void menu_button_cb(lv_event_t *e)','static void back_nav_cb(lv_event_t *e)\n{\n    (void)e;\n    set_active_page(current_app_page==APP_PAGE_MORE?APP_PAGE_MENU:current_app_page==APP_PAGE_MORE3?APP_PAGE_MORE:APP_PAGE_HOME);\n}\n\nstatic void menu_button_cb(lv_event_t *e)')
s=s.replace('static void notify_nav_button_cb(lv_event_t *e)','static void render_weather(void);\nstatic void more3_nav_button_cb(lv_event_t *e) { (void)e; set_active_page(APP_PAGE_MORE3); }\nstatic void weather_nav_button_cb(lv_event_t *e) { (void)e; weather_force_update=true; set_active_page(APP_PAGE_WEATHER); render_weather(); }\n\nstatic void notify_nav_button_cb(lv_event_t *e)')
s=s.replace('    if (mode == shown_mode && w.seq == shown_seq) {','    if (mode == shown_mode && w.seq == shown_seq && !weather_force_update) {')
s=s.replace('    /* A reply received after leaving HOME must not start','    weather_force_update=false;\n    ui_weather_page_apply_weather(&weather_ui, &v);\n    /* A reply received after leaving HOME must not start')
s=s.replace('    ui_home_set_link(&home_ui, link);','    if (weather_ui.mic) { ui_weather_page_set_link(&weather_ui,link); voice_listening_widget_set_notification(weather_ui.mic,nst.pending); }\n    ui_home_set_link(&home_ui, link);')
s=s.replace('    page_notify = pages_init[APP_PAGE_NOTIFY];','    page_notify = pages_init[APP_PAGE_NOTIFY];\n    page_more3 = pages_init[APP_PAGE_MORE3];\n    page_weather = pages_init[APP_PAGE_WEATHER];')
s=s.replace('    static const ui_menu_item_t menu_key = { LV_SYMBOL_LEFT " MENU", menu_button_cb };','    static const ui_menu_item_t menu_key = { "NEXT " LV_SYMBOL_RIGHT, more3_nav_button_cb };')
anchor='    ui_menu_build(page_more, "MORE", more_items, (int)(sizeof(more_items) / sizeof(more_items[0])), &menu_key);'
assert anchor in s
s=s.replace(anchor,anchor+'''
    const ui_menu_picture_item_t pictures[] = {
        { "Weather", weather_icon("partly",true), 320, weather_nav_button_cb },
        { "Mouse", ui_adventure_original_asset(TOUR_ASSET_MOUSE,0), 768, mouse_open_cb },
        { "Adventure", ui_adventure_original_asset(TOUR_ASSET_VAN,0), 320, adventure_open_cb },
    };
    ui_menu_build_pictures(page_more3,"EXPLORE",pictures,3,NULL);
    ui_weather_page_build(&weather_ui,page_weather,talk_button_cb,more3_nav_button_cb);
''')
s=s.replace('ui_back_button_create(scr, home_nav_button_cb)','ui_back_button_create(scr, back_nav_cb)')
p.write_text(s)
# Apply the reviewed postcard correction reproducibly to the generated builder.
p=B/'ui_menu.c';s=p.read_text();s=s.replace('''        lv_obj_t *pic=lv_image_create(card);lv_image_set_src(pic,items[i].image);lv_image_set_scale(pic,items[i].scale);lv_image_set_antialias(pic,false);lv_obj_align(pic,LV_ALIGN_CENTER,0,-22);''','''        /* Each picture is inset like a postcard; the whole card is tappable. */
        lv_obj_t *scene=lv_obj_create(card);lv_obj_remove_style_all(scene);
        lv_obj_set_pos(scene,5,5);lv_obj_set_size(scene,86,104);
        lv_obj_set_style_bg_color(scene,lv_color_hex(i==0?0x789cb3:0x7594a7),0);
        lv_obj_set_style_bg_opa(scene,LV_OPA_COVER,0);lv_obj_set_style_radius(scene,6,0);
        lv_obj_remove_flag(scene,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *ground=lv_obj_create(scene);lv_obj_remove_style_all(ground);
        lv_obj_set_pos(ground,0,77);lv_obj_set_size(ground,86,27);
        lv_obj_set_style_bg_color(ground,lv_color_hex(0x8ca77b),0);lv_obj_set_style_bg_opa(ground,LV_OPA_COVER,0);
        lv_obj_remove_flag(ground,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *pic=lv_image_create(scene);lv_image_set_src(pic,items[i].image);lv_image_set_scale(pic,items[i].scale);lv_image_set_antialias(pic,false);lv_obj_align(pic,LV_ALIGN_CENTER,0,0);''');p.write_text(s)
p=B/'CMakeLists.txt';s=p.read_text().replace('"ui_home.c" "ui_weather_stage.c"','"ui_home.c" "ui_weather_page.c" "ui_weather_stage.c"');p.write_text(s)
