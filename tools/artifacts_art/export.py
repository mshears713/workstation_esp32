"""PNG -> LVGL 9 image descriptors (one header). Sprites are cropped to their
opaque bounds; the crop offset is emitted so callers keep the painted position."""
import sys, os
from PIL import Image
OUT = os.environ.get('ART_OUT', '/tmp/artifacts_art/out')
def bbox(im):
    b = im.getchannel('A').getbbox(); return b or (0, 0, 1, 1)
def arr(name, data):
    lines = []
    for i in range(0, len(data), 32): lines.append(','.join(str(v) for v in data[i:i+32]))
    return f'static const uint8_t {name}_data[]={{\n' + ',\n'.join(lines) + '};\n'
def argb(name, path, crop=True):
    im = Image.open(path).convert('RGBA'); ox = oy = 0
    if crop:
        b = bbox(im); ox, oy = b[0], b[1]; im = im.crop(b)
    w, h = im.size; data = []
    for r, g, b_, a in im.getdata(): data += [b_, g, r, a] if a else [0, 0, 0, 0]
    s = arr(name, data)
    s += (f'static const lv_image_dsc_t {name}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,'
          f'.w={w},.h={h},.stride={w*4}}},.data_size=sizeof({name}_data),.data={name}_data}};\n')
    s += f'#define {name.upper()}_OX {ox}\n#define {name.upper()}_OY {oy}\n'
    return s, (w, h, ox, oy)
def rgb565(name, path):
    im = Image.open(path).convert('RGB'); w, h = im.size; data = []
    for r, g, b in im.getdata():
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); data += [v & 255, v >> 8]
    s = arr(name, data)
    s += (f'static const lv_image_dsc_t {name}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,'
          f'.w={w},.h={h},.stride={w*2}}},.data_size=sizeof({name}_data),.data={name}_data}};\n')
    return s
hdr = ['/* Artifacts & Instruments HOME art (native 320x240 pixel art, 1:1).',
       ' * Generated from the reviewed still by tools/artifacts_art/export.py; the',
       ' * castle, house, van, mouse, Eye and coaster are the accepted earlier pixels,',
       ' * re-lit / enlarged / whisker-corrected - see tools/artifacts_art/README.md. */',
       '#pragma once', '#include "lvgl.h"', '']
body = [rgb565('ai_background', f'{OUT}/background_final.png')]
meta = {}
for n, f in [('ai_castle', 'castle_lit'), ('ai_house', 'house_big'), ('ai_palm_a', 'palm_a'), ('ai_palm_b', 'palm_b'),
             ('ai_palm_c', 'palm_c'), ('ai_bush_l', 'bush_l'), ('ai_bush_r', 'bush_r'), ('ai_van_shadow', 'van_shadow'),
             ('ai_mouse_shadow', 'mouse_shadow')]:
    s, m = argb(n, f'{OUT}/{f}.png', crop=n.startswith('ai_palm') or n.startswith('ai_bush')); body.append(s); meta[n] = m
for i in range(4):
    s, m = argb(f'ai_bridge_{i}', f'{OUT}/drawbridge_{i}.png', crop=False); body.append(s)
for i in range(6):
    s, m = argb(f'ai_mouse_{i}', f'{OUT}/mouse_fixed_{i}.png', crop=False); body.append(s)
body.append('static const lv_image_dsc_t *const ai_bridge_frames[4]={&ai_bridge_0,&ai_bridge_1,&ai_bridge_2,&ai_bridge_3};\n')
body.append('static const lv_image_dsc_t *const ai_mouse_frames[6]={&ai_mouse_0,&ai_mouse_1,&ai_mouse_2,&ai_mouse_3,&ai_mouse_4,&ai_mouse_5};\n')
open(sys.argv[1], 'w', newline='\r\n').write('\n'.join(hdr) + '\n'.join(body))
for k, v in meta.items(): print(k, v)
