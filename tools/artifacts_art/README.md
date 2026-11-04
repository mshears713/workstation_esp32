# Artifacts & Instruments HOME art

`build_art.py` rebuilds `main/orlando_artifacts.h` deterministically (seeded):

1. `decode.py` - the accepted sprites back out of `main/orlando_*.h`,
   `tour_pixels.h`, `house_pixels.h` (castle, house, van, mouse, Eye, coaster,
   princess, sign...).
2. `paint.py` - the new layers, placed pixel by pixel at 1:1: sky bands with
   short dither seams, far skyline/tree line, lake glint, depth-shaded lawn
   with tufts, the path, the beach, the raised mound + stone wall + moat, the
   4-frame drawbridge, palms, bushes, the castle re-lit from the upper left,
   the house enlarged about its top-left (Scale2x then 116 px), the mouse
   whiskers re-rooted at the nose (frames grow 48 -> 54 px wide).
3. `compose.py` - the still exactly as `ui_home.c` layers it (review image).
4. `export.py` - LVGL 9 descriptors; palms/bushes are cropped and their offsets
   applied in `ui_home.c`.

Light comes from the upper left; shadows fall lower right. Layout numbers live
in `compose.py` (`L`) and the `#define`s at the top of `ui_home.c` - change both.

Status: reviewed as a still (2026-11-04). Not yet passed through the native
Pixelorama workshop as PXO projects; do that once the art is accepted, so the
layers are editable there too.
