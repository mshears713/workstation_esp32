"""Lossless native PNG -> LVGL ARGB8888 packaging, never creative authoring.

Deduplicates identical native frames; preserves all frame indices and explicit
runtime timings. No drawing, resizing, recolouring, or flattened PXO imports.
"""
import hashlib
import json
import pathlib
from PIL import Image

REPO = pathlib.Path(__file__).resolve().parents[1]
ROOT = REPO / 'newton-native-art'
scene = json.loads((ROOT/'scene-manifest.json').read_text())
assert scene['schema'] == 'newton-native-scene/v1'
assert scene['newton']['neutral_origin'] == [121,145]
assert scene['newton']['bonk_origin'] == [192,145]
assert scene['newton']['foot_anchor'] == [24,60]
assert scene['tree']['origin'] == [190,81]
assert scene['tree']['apple_bottom_impact_scene'] == [218,154]
assert scene['newton']['crown_anchor'] == [26,9]
assert scene['newton']['actions'][3]['frame_ms'] == 120
text = ['/* Lossless conversion of reviewed native Pixelorama exports. See newton-art-receipt.json. */',
        '#include "newton_art.h"']
seen = {}
receipt = []
raw_total = 0

def image(path, crop=None):
    global raw_total
    im = Image.open(path).convert('RGBA')
    original_rgba_sha=hashlib.sha256(im.tobytes()).hexdigest()
    if crop:
        trimmed=im.crop(crop)
        restored=Image.new('RGBA',im.size);restored.paste(trimmed,crop[:2])
        assert restored.tobytes()==im.tobytes(), 'Cropping would lose native pixels'
        im=trimmed
    w,h = im.size
    # BGRA bytes correspond to LVGL ARGB8888 little-endian memory.
    raw = im.tobytes('raw','BGRA')
    key = (w,h,hashlib.sha256(raw).hexdigest())
    if key not in seen:
        name = f'newton_native_{len(seen)}'
        seen[key] = name
        raw_total += len(raw)
        text.append(f'static const uint8_t {name}_data[] __attribute__((aligned(4))) = {{')
        for pos in range(0,len(raw),32):
            text.append(','.join(f'0x{b:02x}' for b in raw[pos:pos+32])+',')
        text.append('};')
        text.append(f'static const lv_image_dsc_t {name} = {{.header = {{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w={w},.h={h},.stride={w*4}}},.data_size=sizeof({name}_data),.data={name}_data}};')
    name = seen[key]
    receipt.append({'source':path.relative_to(ROOT).as_posix(), 'png_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                    'rgba_sha256':original_rgba_sha,'crop':crop,'descriptor':name,'width':w,'height':h})
    return name

def frames(group,project,count,crop=None):
    manifest = json.loads((ROOT/group/f'{project}-manifest.json').read_text())
    assert len(manifest['frames']) == count
    names=[]
    for i in range(count):
        path=ROOT/group/'projects'/project/f'frame_{i:03}.png'
        assert hashlib.sha256(path.read_bytes()).hexdigest() == manifest['frames'][i]['sha256']
        names.append(image(path,crop))
    return names

background=[]
for tile in scene['background']['tiles']:
    name=image(ROOT/'background'/'projects'/tile['project']/'frame_000.png')
    x,y=tile['origin'];background.append(f'{{&{name},{x},{y-17}}}')
persons=frames('newton','newton-sage',40)
trees=frames('tree','apple-tree',25)
fruits=frames('tree','apple-fruit',25,(24,32,33,74))

def array(name,names):
    text.append(f'static const lv_image_dsc_t *const {name}[] = {{'+','.join('&'+n for n in names)+'};')
array('tree_frames',trees);array('fruit_frames',fruits)
actions={a['name']:a for a in scene['newton']['actions']}
order=('idle','look_up','think','walk_toward','walk_back','prism','inspect_delight','bonk')
for name in (*order,'caution'):
    a=actions[name];array('person_'+name,persons[a['start']:a['end']+1])
text.append('static const newton_tile_t garden_tiles[] = {'+','.join(background)+'};')
def f(name):
    a=actions[name];return f'{{person_{name},{a["end"]-a["start"]+1},{a["frame_ms"]}}}'
text.append('static const ui_newton_art_t art = {')
text.append('.background=garden_tiles,.background_count=8,.tree={tree_frames,25,180},.fruit={fruit_frames,25,85},')
text.append('.person={'+','.join(f(n) for n in order)+'},.caution='+f('caution')+',')
text.append('.tree_x=190,.ground_y=188,.person_foot_x=145,.tree_origin={190,64},.fruit_origin={214,96},.person_foot_anchor={24,60},.native_verified=true};')
text.append('const ui_newton_art_t *newton_art_get(void) { return &art; }')
out=REPO/'main/newton_art.c'
out.write_text('\n'.join(text)+'\n',encoding='utf-8')
info={'source_pack_sha256':'bb8ccd9b8b4d285305f94db88a9a2325f09090c1e889f194e17d72eab98dd54a',
      'conversion':'lossless RGBA to BGRA/ARGB8888; duplicate exact frames shared; fruit empty margins trimmed with byte-identical reconstruction proof and anchor offsets; no art mutation',
      'framing':'native 1:1 pixels, all scene coordinates translated y=-17, stage clips at y=192; 48px controls below',
      'unique_payloads':len(seen),'flash_payload_bytes':raw_total,'exports':receipt,
      'c_sha256':hashlib.sha256(out.read_bytes()).hexdigest()}
(REPO/'newton-art-receipt.json').write_text(json.dumps(info,indent=2)+'\n')
print(json.dumps({k:v for k,v in info.items() if k!='exports'},indent=2))
