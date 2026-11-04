"""Artifacts & Instruments HOME repaint - native 320x240 pixel art.

Everything is placed pixel by pixel at 1:1 (no scaling filters). The existing
accepted sprites (castle, house, van, mouse, Eye, coaster, princess) are reused
as their own pixels; the background, mound/moat/drawbridge, palms, bushes,
shadows and the enlarged house extension are new pixels.

Light: late-afternoon sun, upper left. Shadows fall to the lower right.
"""
import math, random, sys, os
from PIL import Image

SRC = os.environ.get('ART_SRC', '/tmp/artifacts_art/src')
OUT = sys.argv[1] if len(sys.argv) > 1 else '/tmp/artifacts_art/out'
os.makedirs(OUT, exist_ok=True)
W, H = 320, 240
rng = random.Random(1104)

def hx(c):
    c = c.lstrip('#'); return (int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16), 255)

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

class Canvas:
    def __init__(self, w, h, fill=(0, 0, 0, 0)):
        self.im = Image.new('RGBA', (w, h), fill); self.px = self.im.load(); self.w = w; self.h = h
    def dot(self, x, y, c):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            if len(c) == 4 and c[3] < 255:
                b = self.px[x, y]; a = c[3] / 255
                self.px[x, y] = tuple(int(c[i] * a + b[i] * (1 - a)) for i in range(3)) + (max(b[3], c[3]),)
            else:
                self.px[x, y] = c if len(c) == 4 else c + (255,)
    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h: return self.px[x, y]
        return (0, 0, 0, 0)
    def rect(self, x, y, w, h, c):
        for yy in range(int(y), int(y + h)):
            for xx in range(int(x), int(x + w)): self.dot(xx, yy, c)
    def line(self, x0, y0, x1, y1, c, w=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0), 1))
        for i in range(n + 1):
            t = i / n; x = x0 + (x1 - x0) * t; y = y0 + (y1 - y0) * t
            for dy in range(w):
                for dx in range(w): self.dot(x + dx - w // 2, y + dy - w // 2, c)
    def ellipse(self, cx, cy, rx, ry, c, fn=None):
        for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
            for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
                if ((x - cx) / max(rx, .5)) ** 2 + ((y - cy) / max(ry, .5)) ** 2 <= 1:
                    self.dot(x, y, fn(x, y) if fn else c)
    def blit(self, img, x, y):
        self.im.alpha_composite(img, (int(x), int(y))); self.px = self.im.load()
    def dither(self, x, y, c0, c1, t):
        """t in 0..1 -> ordered-dither mix of two palette colours (no new colours)."""
        return c1 if t * 16 > BAYER[y % 4][x % 4] + 0.5 else c0

def load(name): return Image.open(os.path.join(SRC, name + '.png')).convert('RGBA')

def recolour(img, fn):
    im = img.copy(); p = im.load()
    for y in range(im.height):
        for x in range(im.width):
            c = p[x, y]
            if c[3]: p[x, y] = fn(x, y, c)
    return im

def shade(c, k):  # multiply rgb by k
    return tuple(max(0, min(255, int(v * k))) for v in c[:3]) + (c[3],)

def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3)) + (255,)

# ---------------------------------------------------------------- palette
SKY = [hx('#3f6e9e'), hx('#5687b4'), hx('#74a3c6'), hx('#9cc0d3'), hx('#c9d8d2'), hx('#ecdcbf')]
CLOUD_LIT, CLOUD_MID, CLOUD_SH = hx('#fbf6ea'), hx('#e4e4df'), hx('#b9c6d0')
FAR = [hx('#7f9fae'), hx('#6c8f9a'), hx('#5d7f86')]           # hazy far trees / skyline
LAKE, LAKE_HI = hx('#7aa9b8'), hx('#d6ecec')
GRASS = [hx('#9fbf6e'), hx('#8db15f'), hx('#7aa253'), hx('#678f48'), hx('#557a3d')]  # far(light) -> near(dark)
GRASS_TIP = hx('#c2d58a')
EARTH = [hx('#b59466'), hx('#9a7a52'), hx('#7c6143'), hx('#5d4834')]
STONE, STONE_SH, STONE_HI = hx('#bdb3a2'), hx('#8f8575'), hx('#ddd5c5')
WATER = [hx('#4d8fa3'), hx('#3e7a90'), hx('#336a80')]; WATER_HI = hx('#bfe3e6')
SAND = [hx('#f0dfb0'), hx('#e6d09a'), hx('#d8bd84'), hx('#c4a66f')]
PATH = [hx('#e2cfa0'), hx('#cdb685'), hx('#b39b6c')]
SHADOW = (40, 58, 36, 90)
TRUNK = [hx('#9c7f5a'), hx('#7d6446'), hx('#5e4a35')]
FROND = [hx('#2f5d3f'), hx('#3f7a4c'), hx('#5c9a58'), hx('#8cbf6a')]
BUSH = [hx('#2f5a3a'), hx('#3d7046'), hx('#548a52'), hx('#7aaa62')]
FLOWER = [hx('#f2d2dc'), hx('#f6e7a8'), hx('#e98c8c')]

HORIZON = 118

def background():
    c = Canvas(W, H)
    # Sky: dithered bands, warm toward the horizon.
    bands = [0, 30, 56, 78, 96, 110, HORIZON]   # sky band tops; tighter near the horizon
    for y in range(HORIZON):
        i = max(k for k in range(len(bands) - 1) if bands[k] <= y)
        i = min(i, len(SKY) - 1)
        top = bands[i]; col = SKY[i]
        for x in range(W):
            if i > 0 and y - top < 3:   # short ordered-dither seam, not a full gradient
                c.dot(x, y, c.dither(x, y, SKY[i - 1], col, (y - top + 1) / 4))
            else:
                c.dot(x, y, col)
    # Sun glow upper left (behind clouds), a soft dithered disc.
    c.ellipse(34, 26, 13, 13, hx('#a9c7d6')); c.ellipse(34, 26, 10, 10, hx('#d7e4dc'))
    c.ellipse(34, 26, 7, 7, hx('#fff6d8'))
    # Clouds: lumpy, lit from the upper left.
    def cloud(cx, cy, s):
        blobs = [(-1.6, .3, .9), (-.7, -.4, 1.2), (.4, -.6, 1.3), (1.4, .1, 1.0), (.2, .4, 1.1), (-.9, .5, .9)]
        mask = Canvas(int(8 * s) * 2 + 4, int(4 * s) * 2 + 4)
        for bx, by, r in blobs:
            mask.ellipse(mask.w / 2 + bx * 4 * s, mask.h / 2 + by * 3 * s, r * 3.4 * s, r * 2.6 * s, (255, 255, 255, 255))
        for y in range(mask.h):
            for x in range(mask.w):
                if mask.get(x, y)[3]:
                    top = not mask.get(x - 1, y - 1)[3]; bottom = not mask.get(x + 1, y + 2)[3]
                    v = y / mask.h
                    col = CLOUD_LIT if top or v < .35 else CLOUD_SH if bottom or v > .72 else CLOUD_MID
                    c.dot(cx - mask.w / 2 + x, cy - mask.h / 2 + y, c.dither(x, y, col, CLOUD_MID, .25) if col is CLOUD_MID else col)
    cloud(112, 22, 2.2); cloud(250, 14, 1.6); cloud(300, 44, 1.2); cloud(62, 58, 1.0)
    # Far skyline + tree line (atmospheric, cool, low contrast).
    xs = 0
    while xs < W:
        w = rng.randint(6, 16); h = rng.randint(3, 12)
        if 112 < xs < 206: h = rng.randint(2, 5)        # keep the castle's silhouette clean
        c.rect(xs, HORIZON - h, w, h, FAR[0]); c.rect(xs + w - 2, HORIZON - h, 2, h, FAR[1])
        xs += w + rng.randint(0, 3)
    for x in range(W):   # rounded tree tops in front of the skyline
        h = 4 + int(3 * math.sin(x * .21) + 2 * math.sin(x * .57 + 1))
        for y in range(HORIZON - h, HORIZON + 2): c.dot(x, y, FAR[1] if y < HORIZON - h + 2 else FAR[2])
    # Lake glint on the right horizon (Orlando's lakes), under the Eye.
    for y in range(HORIZON + 1, HORIZON + 7):
        for x in range(214, 320):
            c.dot(x, y, LAKE)
            if (x * 7 + y * 13) % 23 == 0: c.dot(x, y, LAKE_HI)
    # Ground: lighter far, darker near (depth), dithered between rows.
    gy0 = HORIZON + 1
    for y in range(gy0, H):
        t = (y - gy0) / (H - gy0) * (len(GRASS) - 1); i = min(int(t), len(GRASS) - 2); f = t - i
        for x in range(W):
            if 214 <= x and y < HORIZON + 7: continue
            c.dot(x, y, c.dither(x, y, GRASS[i], GRASS[i + 1], f))
    # Grass tufts: denser and bigger near the viewer.
    for _ in range(1400):
        x = rng.randrange(W); y = rng.randrange(gy0 + 2, H); near = (y - gy0) / (H - gy0)
        base = c.get(x, y)
        if near > .3 and rng.random() < .55:
            c.dot(x, y, shade(base, .82)); c.dot(x, y - 1, shade(base, .9))
            if near > .6: c.dot(x + 1, y - 2, GRASS_TIP if rng.random() < .3 else shade(base, 1.08))
        else:
            c.dot(x, y, GRASS_TIP if rng.random() < .25 else shade(base, 1.07))
    # Beach on the right: sand sweeping in from the lower right, wet edge and shells.
    def sand_left(y): return 214 + int((y - 136) * -0.55 + 6 * math.sin(y * .19)) if y >= 136 else 999
    for y in range(136, H):
        L = sand_left(y)
        for x in range(max(0, L), W):
            d = x - L
            if d < 2: col = shade(GRASS[2], .9) if (x + y) % 2 else SAND[3]       # broken grass edge
            else:
                t = min(1, (y - 136) / 100); col = c.dither(x, y, SAND[0], SAND[1], t)
                if d > 70 and (x + 2 * y) % 5 == 0: col = SAND[2]                  # wind ripples
            c.dot(x, y, col)
    for _ in range(70):
        y = rng.randrange(150, H); x = rng.randrange(max(0, sand_left(y) + 4), W)
        c.dot(x, y, SAND[2] if rng.random() < .6 else hx('#fbf1d6'))
    for x, y in [(268, 214), (300, 228), (236, 236), (310, 198), (283, 233)]:   # little shells
        c.dot(x, y, hx('#f7e9e9')); c.dot(x + 1, y, hx('#e6b9b6')); c.dot(x, y + 1, hx('#d9a8a3'))
    # Path from the house towards the drawbridge: converging = depth, like Newton's garden walk.
    for y in range(150, H):
        t = (y - 150) / (H - 150)
        cx = 160 - 95 * t * t - 10 * t; half = 4 + 22 * t
        for x in range(int(cx - half), int(cx + half) + 1):
            edge = abs(x - cx) > half - 1.2
            col = PATH[2] if edge else c.dither(x, y, PATH[0], PATH[1], .35 + .4 * t)
            if not edge and (x * 3 + y * 5) % 17 == 0: col = PATH[2]
            c.dot(x, y, col)
    return c

def mound_moat(c):
    """Raised earth under the castle: a rounded grassy hill whose front is cut
    by a stone retaining wall above the moat. Lit from the left."""
    cx = 160
    for y in range(104, 136):
        t = (y - 104) / 32
        half = 46 + 40 * math.sqrt(t)          # rounded shoulders, not a box
        for x in range(int(cx - half), int(cx + half) + 1):
            u = (x - (cx - half)) / (2 * half); side = abs(x - cx) / half
            if y < 118 or side > .78:          # grass on top and on the slopes
                col = GRASS[1] if u < .45 else GRASS[2] if u < .8 else GRASS[3]
                if side > .9: col = GRASS[3] if u > .5 else GRASS[2]
                if (x * 7 + y * 3) % 13 == 0: col = GRASS_TIP if u < .5 else shade(col, .85)
            elif y < 125:                      # exposed earth under the plateau lip
                col = EARTH[0] if u < .35 else EARTH[1] if u < .7 else EARTH[2]
                if y == 118: col = GRASS[3]
                if (x * 5 + y * 3) % 11 == 0: col = EARTH[3]
                if (x + y * 2) % 13 == 0 and u < .5: col = hx('#c9a978')
            else:                              # stone retaining wall
                row = (y - 125) // 3
                col = STONE_SH if (y - 125) % 3 == 2 or (x + row * 4) % 8 == 0 else STONE
                if (y - 125) % 3 == 0 and u < .5: col = STONE_HI
                if u > .7: col = shade(col, .88)
            c.dot(x, y, col)
    # Moat in front of the wall, darker in the wall's shadow.
    for y in range(135, 153):
        half = 88 - abs(y - 143) * 2.4
        for x in range(int(cx - half), int(cx + half) + 1):
            dy = y - 135; col = WATER[2] if dy < 3 else WATER[1] if dy < 8 else WATER[0]
            c.dot(x, y, col)
        c.dot(cx - half - 1, y, GRASS[3]); c.dot(cx + half + 1, y, GRASS[3])
    g = random.Random(7)
    for _ in range(26):   # short horizontal sun glints, not a repeating hatch
        y = g.randrange(139, 151); half = 88 - abs(y - 143) * 2.4
        x = g.randrange(int(cx - half) + 3, int(cx + half) - 5); n = g.randrange(2, 5)
        for k in range(n): c.dot(x + k, y, WATER_HI if k < n - 1 else WATER[0])
    for x in range(cx - 76, cx + 77):
        if (x // 3) % 2 == 0: c.dot(x, 136, shade(STONE_SH, .7))
    for x, y in [(98, 145), (220, 142), (116, 149)]:
        c.ellipse(x, y, 3, 1.4, hx('#5f9a5a')); c.dot(x + 1, y - 1, hx('#f2d2dc'))

DRAWBRIDGE_SIZE = (26, 60)  # x0..x1 stays inside 0..25 at full spread    # sprite placed at (147, 92): hinge row y=24 = screen y 116

def drawbridge_frames():
    """Raised (0) -> lowered (3), hinged at the gate sill (sprite row 24).
    Up it stands over the gate; down it lies across the moat toward the
    viewer, foreshortened, reaching the near bank (screen y ~150)."""
    HINGE = 24
    WOOD_L, WOOD_R, EDGE, IRON = hx('#a77c52'), hx('#8f6844'), hx('#5b4230'), hx('#3d3936')
    frames = []
    for k in range(4):
        f = Canvas(*DRAWBRIDGE_SIZE)
        if k == 0:   top, bot = 2, HINGE           # vertical, covering the gate
        elif k == 1: top, bot = 12, HINGE           # tilting toward us
        elif k == 2: top, bot = HINGE, HINGE + 14   # past horizontal-ish, reaching out
        else:        top, bot = HINGE, HINGE + 34   # down across the moat
        for y in range(top, bot):
            t = (y - top) / max(bot - top - 1, 1)
            # down: widens toward the viewer (perspective); up: constant width
            spread = int(4 * t) if k >= 2 else 0
            x0, x1 = 4 - spread, 22 + spread
            for x in range(max(0, x0), min(26, x1)):
                col = WOOD_L if x < 13 else WOOD_R
                if (y - top) % 4 == 3: col = shade(col, .82)       # plank seams
                if x in (max(0, x0), min(26, x1) - 1): col = EDGE
                f.dot(x, y, col)
            if (y - top) % 9 == 4:
                for x in range(max(0, x0) + 2, min(26, x1) - 2): f.dot(x, y, IRON)
        # chains from the wall above the gate to the bridge's free edge
        free = top if k < 2 else bot - 1
        f.line(2, 0, 3, free, IRON); f.line(23, 0, 22, free, IRON)
        frames.append(f.im)
    return frames

def palm(height, lean, flip=False):
    """A foreground palm: segmented, curving trunk and layered fronds."""
    w = 84; c = Canvas(w, height + 34)
    bx = w // 2; by = height + 32
    pts = []
    for i in range(height):
        t = i / height; x = bx + lean * t * t * height * .35; y = by - i
        pts.append((x, y))
        thick = 6 if t < .35 else 5 if t < .75 else 4
        for d in range(thick):
            col = TRUNK[0] if d == 0 else TRUNK[1] if d < thick - 1 else TRUNK[2]
            if i % 5 == 0: col = TRUNK[2]
            c.dot(x - thick // 2 + d, y, col)
    tx, ty = pts[-1]
    # coconuts
    for dx, dy in [(-2, 2), (1, 3), (0, 1)]: c.dot(tx + dx, ty + dy, hx('#6b4f2e')); c.dot(tx + dx + 1, ty + dy, hx('#8a6a3c'))
    fronds = [(-1.0, -.35), (-.75, -.75), (-.25, -1.0), (.3, -.95), (.8, -.6), (1.05, -.15), (-1.05, .15), (.95, .35)]
    for k, (dx, dy) in enumerate(fronds):
        L = 30 if abs(dy) < .5 else 24
        prev = None
        for i in range(L):
            t = i / L
            x = tx + dx * i; y = ty + dy * i + 9 * t * t   # droop
            lit = dx < 0 or dy < -.6
            mid = FROND[2] if lit else FROND[1]
            c.dot(x, y, FROND[0] if i % 6 == 0 else mid)
            if i > 3 and i % 2 == 0:   # leaflets hanging off the rib
                for j in range(1, 4 - int(t * 2)):
                    c.dot(x - dx * .4, y + j, FROND[3] if lit and j == 1 else mid)
                    c.dot(x + dx * .2, y + j + 1, FROND[0])
    im = c.im
    return im.transpose(Image.FLIP_LEFT_RIGHT) if flip else im, (bx, by)

def bush(w, h, seed):
    r = random.Random(seed); c = Canvas(w, h)
    blobs = [(w * (.2 + .6 * i / 4) + r.uniform(-2, 2), h * .6 + r.uniform(-2, 1), w * .22, h * .42) for i in range(5)]
    for bx, by, rx, ry in blobs: c.ellipse(bx, by, rx, ry, BUSH[1])
    for bx, by, rx, ry in blobs:
        c.ellipse(bx - rx * .25, by - ry * .3, rx * .65, ry * .55, BUSH[2])
        c.ellipse(bx - rx * .4, by - ry * .5, rx * .3, ry * .25, BUSH[3])
    for y in range(h):          # shaded base
        for x in range(w):
            if c.get(x, y)[3] and y > h * .78: c.dot(x, y, BUSH[0])
    for _ in range(int(w * h / 40)):   # leaf texture + a few flowers
        x = r.randrange(w); y = r.randrange(h)
        if c.get(x, y)[3]: c.dot(x, y, BUSH[0] if r.random() < .5 else BUSH[3])
    for _ in range(max(2, w // 8)):
        x = r.randrange(2, w - 2); y = r.randrange(2, int(h * .7))
        if c.get(x, y)[3]: c.dot(x, y, FLOWER[r.randrange(3)])
    return c.im

def big_house():
    """The original 48x48 house at its accepted 2x, then 'dragged' from the
    bottom-right corner: the right wall gains a wing with a window, and the
    front gains a porch step, keeping the top-left anchor and the style."""
    src = load('house_unoccupied')
    base = src.resize((96, 96), Image.NEAREST)
    c = Canvas(118, 112); c.blit(base, 0, 0)
    p = base.load()
    # sample the house's own colours
    wall = p[30, 60]; wall_sh = shade(wall, .86); outline = p[14, 92] if p[14, 92][3] else hx('#4a3426')
    roof = p[40, 22]; roof_sh = shade(roof, .82); trim = shade(wall, .7)
    # find the right wall edge of the original, extend the building right by 20 px
    for y in range(44, 96):
        for x in range(84, 106):
            col = wall if x < 96 else wall_sh
            if (y - 44) % 9 == 0: col = shade(col, .95)
            c.dot(x, y, col)
        c.dot(106, y, outline)
    for x in range(80, 108):     # wing roof: same tiles, stepping down
        for y in range(36, 46):
            if y - 36 >= (x - 80) * .25:
                col = roof if (x + y) % 4 else roof_sh
                if y == 45: col = trim
                c.dot(x, y, col)
    # wing window with shutters, lit glass
    c.rect(88, 56, 12, 14, outline); c.rect(89, 57, 10, 12, hx('#f3d27a'))
    c.rect(89, 57, 10, 1, hx('#fff0b0')); c.line(94, 57, 94, 68, outline); c.line(89, 63, 98, 63, outline)
    c.rect(85, 56, 3, 14, hx('#7c4b3a')); c.rect(100, 56, 3, 14, hx('#7c4b3a'))
    # porch and steps along the bottom (extends the house down)
    for y in range(94, 112):
        for x in range(4, 110):
            if y < 100: col = hx('#b8a58a') if x < 70 else hx('#a3917a')
            elif y < 106: col = hx('#a3917a') if (x // 10) % 2 else hx('#988670')
            else: col = hx('#8a7965')
            if y in (99, 105) or x in (4, 109): col = outline
            c.dot(x, y, col)
    for x in range(40, 62):       # step in front of the door
        c.dot(x, 105, hx('#cdbda4'))
    return c.im

def mouse_whiskers(fr):
    """Re-draw the whiskers: anchored at the muzzle just behind the nose and
    fanning forward (up, level, down) - not trailing back from the cheek."""
    im = fr.copy(); p = im.load()
    # remove the old whisker strokes: thin grey/brown 1-px lines right of the face
    old = []
    for y in range(im.height):
        for x in range(36, im.width):
            r, g, b, a = p[x, y]
            if a and abs(r - g) < 40 and r < 190 and not (r > 180 and g < 120):   # not the pink/red nose
                old.append((x, y))
    return im, old

if __name__ == '__main__':
    bg = background(); mound_moat(bg)
    bg.im.save(os.path.join(OUT, 'background.png'))
    for i, f in enumerate(drawbridge_frames()): f.save(os.path.join(OUT, f'drawbridge_{i}.png'))
    p1, a1 = palm(86, -1.0); p1.save(os.path.join(OUT, 'palm_a.png'))
    p2, a2 = palm(96, 1.0, flip=True); p2.save(os.path.join(OUT, 'palm_b.png'))
    p3, a3 = palm(58, -.6); p3.save(os.path.join(OUT, 'palm_c.png'))
    bush(30, 18, 1).save(os.path.join(OUT, 'bush_l.png')); bush(28, 17, 2).save(os.path.join(OUT, 'bush_r.png'))
    big_house().save(os.path.join(OUT, 'house_big.png'))
    print('painted', OUT)

def fix_whiskers(fr):
    """Whiskers rooted at the whisker pad just under the nose, fanning forward:
    up, level, down - each 1 px, light enough to read as whisker not mouth.
    The canvas grows 6 px to the right so they have room (48 -> 54 wide)."""
    src = fr.convert('RGBA'); sp = src.load()
    im = Image.new('RGBA', (src.width + 6, src.height), (0, 0, 0, 0)); im.alpha_composite(src, (0, 0)); p = im.load()
    NOSE = (212, 86, 106); OLD = (118, 83, 75)
    nose = [(x, y) for y in range(src.height) for x in range(src.width) if sp[x, y][:3] == NOSE and sp[x, y][3]]
    for y in range(src.height):
        for x in range(30, src.width):
            if sp[x, y][3] and sp[x, y][:3] == OLD: p[x, y] = (0, 0, 0, 0)
    if not nose: return im
    nx = max(x for x, _ in nose); ny = max(y for _, y in nose)   # front-bottom of the nose
    # Three roots stacked on the whisker pad just behind the nose, each
    # whisker fanning away from the next: up, level, down.
    WH = (240, 232, 218, 255)
    roots = [(nx - 2, ny - 1), (nx - 1, ny + 1), (nx - 2, ny + 3)]
    for (rx, ry), (dx, dy) in zip(roots, [(10, -5), (11, 0), (10, 4)]):
        n = max(abs(dx), abs(dy))
        for i in range(2, n + 1):
            x = rx + round(dx * i / n); y = ry + round(dy * i / n)
            if 0 <= x < im.width and 0 <= y < im.height and p[x, y][:3] != NOSE:
                p[x, y] = WH
    return im

def light_castle(img):
    """Same castle pixels, lit from the upper left: per horizontal run of a
    surface colour, the left third is raised, the right third lowered; stone
    courses every 6 rows; a warm rim on lit edges."""
    im = img.copy(); p = im.load(); w, h = im.size
    orig = img.load()
    for y in range(h):
        x = 0
        while x < w:
            c = orig[x, y]
            if not c[3]: x += 1; continue
            x1 = x
            while x1 < w and orig[x1, y][3] and orig[x1, y][:3] == c[:3]: x1 += 1
            n = x1 - x
            bright = sum(c[:3]) / 3
            for i in range(x, x1):
                u = (i - x) / max(n - 1, 1)
                k = 1.0
                if n >= 4 and bright > 60:
                    k = 1.08 if u < .3 else .84 if u > .7 else 1.0
                col = shade(c, k)
                if bright > 170 and n >= 4:   # pale stone walls only
                    if y % 6 == 0 and (i + (y // 6) * 3) % 9 != 0: col = shade(col, .9)
                    elif (i + (y // 6) * 4) % 9 == 0 and y % 6 < 3: col = shade(col, .93)
                p[i, y] = col
            x = x1
    return im


def scale2x(img):
    """EPX / Scale2x: doubles pixel art while keeping edges crisp."""
    w, h = img.size; p = img.load(); out = Image.new('RGBA', (w * 2, h * 2)); q = out.load()
    g = lambda x, y: p[min(max(x, 0), w - 1), min(max(y, 0), h - 1)]
    for y in range(h):
        for x in range(w):
            P = g(x, y); A = g(x, y - 1); B = g(x + 1, y); C = g(x - 1, y); D = g(x, y + 1)
            e0 = A if (C == A and C != D and A != B) else P
            e1 = B if (A == B and A != C and B != D) else P
            e2 = C if (D == C and D != B and C != A) else P
            e3 = D if (B == D and B != A and D != C) else P
            q[2*x, 2*y] = e0; q[2*x+1, 2*y] = e1; q[2*x, 2*y+1] = e2; q[2*x+1, 2*y+1] = e3
    return out

def house_scaled(size=116):
    """The accepted house, larger about its top-left corner (Mike: drag the
    bottom-right corner out). Scale2x keeps edges clean; the final step to
    `size` repeats every ~5th row/column once - far less visible than a
    straight 2.4x nearest-neighbour stretch of the 48 px original."""
    return scale2x(load('house_unoccupied')).resize((size, size), Image.NEAREST)
