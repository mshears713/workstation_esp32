"""Reproduce main/orlando_artifacts.h from the accepted sprites in main/*.h.

    python3 tools/artifacts_art/build_art.py            # writes main/orlando_artifacts.h
    python3 tools/artifacts_art/build_art.py --check    # rebuild and compare, no write

Steps: decode the existing sprites (decode.py) -> paint the new layers
(paint.py, seeded, deterministic) -> preview the still (compose.py) -> export
LVGL descriptors (export.py). Needs Pillow.
"""
import os, subprocess, sys, glob, tempfile, filecmp
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
work = tempfile.mkdtemp(prefix='artifacts_art_')
src, out = os.path.join(work, 'src'), os.path.join(work, 'out')
os.makedirs(src); os.makedirs(out)
os.environ['ART_SRC'] = src; os.environ['ART_OUT'] = out
heads = sorted(glob.glob(os.path.join(REPO, 'main', 'orlando_*.h')))
heads = [h for h in heads if not h.endswith('orlando_artifacts.h')]
subprocess.run([sys.executable, os.path.join(HERE, 'decode.py'), src] + heads +
               [os.path.join(REPO, 'main', n) for n in ('tour_pixels.h', 'house_pixels.h')], check=True, stdout=subprocess.DEVNULL)
sys.path.insert(0, HERE)
import re
from PIL import Image
# Background and the two uint32 sprites use a different array style; decode here.
t = open(os.path.join(REPO, 'main', 'orlando_v5_background.h')).read()
b = bytes(int(x) for x in re.search(r'orlando_v5_background_data\[\]=\{([^}]*)\}', t).group(1).split(','))
px = [(((b[i] | b[i+1] << 8) >> 11 & 31) * 255 // 31, ((b[i] | b[i+1] << 8) >> 5 & 63) * 255 // 63, ((b[i] | b[i+1] << 8) & 31) * 255 // 31) for i in range(0, 320*240*2, 2)]
im = Image.new('RGB', (320, 240)); im.putdata(px); im.save(os.path.join(src, 'orlando_v5_background.png'))
for name, f, w, h in [('orlando_crest', 'orlando_v5_crest.h', 42, 70), ('orlando_end', 'orlando_v5_end.h', 26, 30)]:
    t = open(os.path.join(REPO, 'main', f)).read()
    vals = [int(x, 16) for x in re.search(name + r'_data\[\]=\{([^}]*)\}', t).group(1).replace('\n', '').split(',') if x.strip()]
    im = Image.new('RGBA', (w, h)); im.putdata([((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255) for v in vals[:w*h]]); im.save(os.path.join(src, name + '.png'))

import paint
from paint import *
bg = background(); mound_moat(bg); bg.im.save(f'{out}/background.png')
for i, f in enumerate(drawbridge_frames()): f.save(f'{out}/drawbridge_{i}.png')
palm(108, .55)[0].save(f'{out}/palm_a.png')
palm(116, 1.0, flip=True)[0].save(f'{out}/palm_b.png')
palm(42, -.4)[0].save(f'{out}/palm_c.png')
bush(42, 28, 1).save(f'{out}/bush_l.png'); bush(40, 27, 2).save(f'{out}/bush_r.png')
house_scaled(116).save(f'{out}/house_big.png')
for i in range(6): fix_whiskers(load('mouse%d' % i)).save(f'{out}/mouse_fixed_{i}.png')
light_castle(load('castle_overnight')).save(f'{out}/castle_lit.png')
def shadow_sprite(w, h):
    c = Canvas(w, h); cx = (w - 1) / 2; cy = (h - 1) / 2
    for y in range(h):
        for x in range(w):
            d = ((x - cx) / (w / 2)) ** 2 + ((y - cy) / (h / 2)) ** 2
            if d <= 1 and ((x + y) % 2 == 0 or d <= .45): c.dot(x, y, SHADOW)
    return c.im
shadow_sprite(100, 10).save(f'{out}/van_shadow.png'); shadow_sprite(28, 5).save(f'{out}/mouse_shadow.png')
c = Canvas(320, 240); c.blit(Image.open(f'{out}/background.png').convert('RGBA'), 0, 0)
def sh(cx, cy, rx, ry):
    for y in range(int(cy - ry), int(cy + ry) + 1):
        for x in range(int(cx - rx), int(cx + rx) + 1):
            d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
            if d <= 1 and (x + y) % 2 == 0 or d <= .45: c.dot(x, y, SHADOW)
sh(200, 118, 18, 4); sh(66, 231, 58, 4)
c.im.save(f'{out}/background_final.png')
subprocess.run([sys.executable, os.path.join(HERE, 'compose.py'), out], check=True, stdout=subprocess.DEVNULL)
target = os.path.join(REPO, 'main', 'orlando_artifacts.h')
dest = os.path.join(work, 'orlando_artifacts.h') if '--check' in sys.argv else target
subprocess.run([sys.executable, os.path.join(HERE, 'export.py'), dest], check=True, stdout=subprocess.DEVNULL)
if '--check' in sys.argv:
    same = filecmp.cmp(dest, target, shallow=False)
    print('ART_REPRODUCIBLE' if same else 'ART_DIFFERS', work); sys.exit(0 if same else 1)
print('wrote', target, '- preview:', os.path.join(out, 'home_still.png'))
