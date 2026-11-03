#!/usr/bin/env python3
"""Generate main/weather_icons.c: the eight weather icons HOME shows (1.3.0).

The icons are drawn here with Pillow rather than kept as PNGs so that they can
be changed by editing shapes, not pixels. Each is drawn at 4x and downsampled,
which is what gives them anti-aliased edges on the 320x240 panel. Two sizes are
emitted: 48 px for the "now" panel and 30 px for the forecast rows - LVGL can
scale an image at draw time, but that costs a transform per frame for something
that never changes.

Output is LV_COLOR_FORMAT_ARGB8888 (B,G,R,A bytes), ~100 KB of flash in all.
Flash is not the constraint on this device; internal RAM is, and C-array images
are read straight from flash.

The icon keys must match what the backend sends (app/api/weather.py ICONS):
    sun moon partly cloud fog rain storm snow

Run:  python tools/weather_icons/gen_icons.py   (needs Pillow; writes main/)
"""

from pathlib import Path

from PIL import Image, ImageDraw

SS = 4                     # supersampling factor
BASE = 48 * SS             # drawing canvas, in 4x pixels
SIZES = (48, 30)

SUN = (255, 200, 61, 255)
MOON = (236, 230, 206, 255)
CLOUD = (215, 220, 226, 255)
CLOUD_DARK = (142, 150, 159, 255)
RAIN = (79, 163, 255, 255)
BOLT = (255, 213, 79, 255)
SNOW = (255, 255, 255, 255)
FOG = (176, 182, 190, 255)

ICONS = ("sun", "moon", "partly", "cloud", "fog", "rain", "storm", "snow")


def u(v: float) -> int:
    """Fraction of the icon size -> canvas pixels."""
    return int(round(v * BASE))


def circle(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float, fill) -> None:
    d.ellipse((u(cx - r), u(cy - r), u(cx + r), u(cy + r)), fill=fill)


def cloud(d: ImageDraw.ImageDraw, cx: float, cy: float, s: float, fill) -> None:
    """A cloud centred on (cx, cy), s = its width as a fraction of the icon."""
    circle(d, cx - 0.16 * s / 0.8, cy, 0.17 * s / 0.8, fill)
    circle(d, cx + 0.05 * s / 0.8, cy - 0.08 * s / 0.8, 0.23 * s / 0.8, fill)
    circle(d, cx + 0.22 * s / 0.8, cy + 0.03 * s / 0.8, 0.15 * s / 0.8, fill)
    h = 0.17 * s / 0.8
    d.rounded_rectangle((u(cx - s / 2), u(cy), u(cx + s / 2), u(cy + h)), radius=u(h / 2), fill=fill)


def sun(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float) -> None:
    import math
    for i in range(8):
        a = i * math.pi / 4
        x0, y0 = cx + math.cos(a) * r * 1.35, cy + math.sin(a) * r * 1.35
        x1, y1 = cx + math.cos(a) * r * 1.75, cy + math.sin(a) * r * 1.75
        d.line((u(x0), u(y0), u(x1), u(y1)), fill=SUN, width=max(1, u(r * 0.22)))
    circle(d, cx, cy, r, SUN)


def draw(name: str) -> Image.Image:
    img = Image.new("RGBA", (BASE, BASE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if name == "sun":
        sun(d, 0.5, 0.5, 0.24)
    elif name == "moon":
        circle(d, 0.5, 0.5, 0.32, MOON)
        circle(d, 0.66, 0.38, 0.27, (0, 0, 0, 0))
    elif name == "partly":
        sun(d, 0.36, 0.36, 0.17)
        cloud(d, 0.56, 0.6, 0.72, CLOUD)
    elif name == "cloud":
        cloud(d, 0.5, 0.52, 0.9, CLOUD)
    elif name == "fog":
        cloud(d, 0.5, 0.36, 0.7, CLOUD_DARK)
        for i, (x0, x1) in enumerate(((0.12, 0.82), (0.2, 0.9), (0.12, 0.74))):
            y = 0.66 + i * 0.12
            d.rounded_rectangle((u(x0), u(y), u(x1), u(y + 0.06)), radius=u(0.03), fill=FOG)
    elif name == "rain":
        cloud(d, 0.5, 0.38, 0.86, CLOUD_DARK)
        for x in (0.3, 0.5, 0.7):
            d.line((u(x + 0.04), u(0.68), u(x - 0.04), u(0.9)), fill=RAIN, width=u(0.07))
    elif name == "storm":
        cloud(d, 0.5, 0.36, 0.86, CLOUD_DARK)
        bolt = [(0.54, 0.52), (0.36, 0.76), (0.5, 0.76), (0.42, 0.97), (0.66, 0.68), (0.52, 0.68), (0.62, 0.52)]
        d.polygon([(u(x), u(y)) for x, y in bolt], fill=BOLT)
    elif name == "snow":
        cloud(d, 0.5, 0.36, 0.86, CLOUD)
        for x, y in ((0.3, 0.74), (0.5, 0.86), (0.7, 0.74)):
            circle(d, x, y, 0.06, SNOW)
    else:
        raise ValueError(name)
    return img


def c_array(name: str, size: int, img: Image.Image) -> str:
    small = img.resize((size, size), Image.LANCZOS)
    px = small.load()
    data = []
    for y in range(size):
        for x in range(size):
            r, g, b, a = px[x, y]
            data += [b, g, r, a]        # LVGL ARGB8888 is B,G,R,A in memory
    rows = [", ".join(f"0x{v:02x}" for v in data[i:i + 24]) for i in range(0, len(data), 24)]
    sym = f"weather_icon_{name}_{size}"
    return (
        f"static const uint8_t {sym}_map[] = {{\n    " + ",\n    ".join(rows) + "\n};\n\n"
        f"const lv_image_dsc_t {sym} = {{\n"
        f"    .header = {{\n"
        f"        .magic = LV_IMAGE_HEADER_MAGIC,\n"
        f"        .cf = LV_COLOR_FORMAT_ARGB8888,\n"
        f"        .w = {size},\n"
        f"        .h = {size},\n"
        f"        .stride = {size * 4},\n"
        f"    }},\n"
        f"    .data_size = sizeof({sym}_map),\n"
        f"    .data = {sym}_map,\n"
        f"}};\n\n"
    )


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    out = [
        "/* GENERATED by tools/weather_icons/gen_icons.py - edit that, not this. */\n",
        '#include "weather_icons.h"\n\n',
    ]
    for name in ICONS:
        img = draw(name)
        for size in SIZES:
            out.append(c_array(name, size, img))
    lookup = ["const lv_image_dsc_t *weather_icon(const char *key, bool large)\n{\n"]
    for name in ICONS:
        lookup.append(
            f'    if (strcmp(key, "{name}") == 0) {{\n'
            f"        return large ? &weather_icon_{name}_48 : &weather_icon_{name}_30;\n"
            f"    }}\n"
        )
    lookup.append("    return NULL;\n}\n")
    out += lookup
    (root / "main" / "weather_icons.c").write_text("".join(out).replace('#include "weather_icons.h"\n',
                                                                         '#include "weather_icons.h"\n#include <string.h>\n'),
                                                 encoding="utf-8", newline="\n")
    # A contact sheet for humans, on a black background like the device.
    sheet = Image.new("RGBA", (len(ICONS) * 56, 56 + 38), (0, 0, 0, 255))
    for i, name in enumerate(ICONS):
        img = draw(name)
        sheet.alpha_composite(img.resize((48, 48), Image.LANCZOS), (i * 56 + 4, 4))
        sheet.alpha_composite(img.resize((30, 30), Image.LANCZOS), (i * 56 + 13, 60))
    sheet.save(Path(__file__).with_name("contact_sheet.png"))


if __name__ == "__main__":
    main()
