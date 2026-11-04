"""Preview the HOME still exactly as ui_home.c will layer it (nearest-neighbour
LVGL scales reproduced), so the review image is the screen, not a mock-up."""
import sys, os, math
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paint import load, Canvas, SHADOW

OUT = sys.argv[1] if len(sys.argv) > 1 else '/tmp/artifacts_art/out'
def o(n): return Image.open(os.path.join(OUT, n + '.png')).convert('RGBA')
def scaled(img, scale256):
    if scale256 == 256: return img
    w = max(1, img.width * scale256 // 256); h = max(1, img.height * scale256 // 256)
    return img.resize((w, h), Image.NEAREST)

# Layout (also the numbers ui_home.c uses)
L = dict(house=(5, 117), van=(196, 150), mouse=(134, 170), plaque=(52, 164),
         castle=(112, 0), bridge=(147, 92), palm_a=(26, 196), palm_b=(306, 212), palm_c=(214, 126),
         bush_l=(-6, 206), bush_r=(94, 206))

def shadow(c, cx, cy, rx, ry):
    for y in range(int(cy - ry), int(cy + ry) + 1):
        for x in range(int(cx - rx), int(cx + rx) + 1):
            d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
            if d <= 1 and (x + y) % 2 == 0 or d <= .45: c.dot(x, y, SHADOW)

def palm_at(c, name, base):
    im = o(name); bx = im.width // 2; by = im.height - 2
    c.blit(im, base[0] - bx, base[1] - by)

def render(bridge_frame=0, princess_y=None, mouse_frame=0):
    c = Canvas(320, 240); c.blit(o('background'), 0, 0)
    c.blit(scaled(load('hulk_launch'), 192), 6, 64)
    c.blit(load('orlando_crest'), 77, 65)
    c.blit(load('eye_base'), 242, 99)
    c.blit(load('eye_rim'), 230, 59)
    for i in range(12):
        a = (i * 30) * math.pi / 180
        c.blit(load('eye_cabin'), 266 + round(35 * math.cos(a)), 95 + round(35 * math.sin(a)))
    # castle shadow falls right, onto the mound
    shadow(c, 200, 118, 18, 4)
    c.blit(o('castle_lit'), *L['castle'])
    c.blit(load('launch_bridge'), 78, 54)
    br = o(f'drawbridge_{bridge_frame}')
    c.blit(br, *L['bridge'])
    if princess_y is not None:
        c.blit(load('princess_walk_0'), 151, princess_y)
    palm_at(c, 'palm_c', L['palm_c'])
    shadow(c, 66, 231, 58, 4)
    c.blit(o('house_big'), *L['house'])
    c.blit(scaled(load('home_plaque'), 304), *L['plaque'])
    shadow(c, 246, 199, 50, 5)
    c.blit(scaled(load('tour_van_0'), 384), *L['van'])
    palm_at(c, 'palm_b', L['palm_b'])
    palm_at(c, 'palm_a', L['palm_a'])
    c.blit(o('bush_l'), *L['bush_l']); c.blit(o('bush_r'), *L['bush_r'])
    shadow(c, L['mouse'][0] + 24, L['mouse'][1] + 46, 13, 2.5)
    c.blit(o(f'mouse_fixed_{mouse_frame}'), *L['mouse'])
    return c.im

if __name__ == '__main__':
    still = render(); still.save(os.path.join(OUT, 'home_still.png'))
    down = render(3, 118); down.save(os.path.join(OUT, 'home_bridge_down.png'))
    print('composed')
