from pathlib import Path
import re
p=Path('/home/mshears/orlando-home-20261103/fw-overnight/main/status_deck_ui.c');s=p.read_text()
def section(start,end):return s[s.index(start):s.index(end,s.index(start))]
more=section('static const ui_menu_item_t more_items[]','static const ui_menu_item_t menu_key')
pics=section('const ui_menu_picture_item_t pictures[]','ui_menu_build_pictures(page_more3')
assert all(x not in more for x in ['"MESSAGES"','"FACE"','"MOUSE"'])
assert all(x in more for x in ['"AUDIO TEST"','"SURVEY"','"ADVENTURE"'])
assert '"Mouse"' not in pics and '"Weather"' in pics and '"Adventure"' in pics
assert 'ui_notify_build(&ui->notify' not in s and 'ui_notify_overlay_build' in s
assert 'ui_mouse_open(app_screen' not in s and 'ui_companion_open(app_screen' not in s
mouse=p.parent/'ui_mouse.c';shared=mouse.read_text();assert 'ui_mouse_home_create' in shared and 'draw_pixels' in shared
assert 'ui_mouse_open(' not in shared and not (p.parent/'mouse_station_assets.h').exists()
print('MENU_SCOPE_PASS: retired standalone entries and page builders; preserved Weather, Adventure, audio and notification overlay')
