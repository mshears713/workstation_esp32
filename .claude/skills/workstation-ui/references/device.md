# The physical device: firm constraints

Verify against source before relying on a number here. The file that owns each number is named.

## Screen and touch

- ESP32-S3-BOX-3, **320x240 landscape**, 2.4" diagonal: about **166 ppi, 0.15 mm per pixel**. The
  BSP is `espressif/esp-box-3`.
- Colour: **RGB565** (`CONFIG_LV_COLOR_DEPTH=16`). Subtle gradients band. Near colours can collapse
  to the same 565 value. The simulator renders in 565 too, so what you see there is what the panel
  gets. The panel's own gamma, viewing angle and backlight are not modelled.
- Capacitive touch through the BSP. A finger covers ~40–60 px. Targets: **48 px** comfortable,
  **40 px** minimum, more space between adjacent targets than on a phone. The bezel corners are
  rounded, so keep targets and text a few px in from the edges.
- Refresh: `LV_DEF_REFR_PERIOD` 33 ms (at most ~30 fps) over SPI. Large redrawn areas cost frame time.
- Viewed from ~0.5–1.5 m, often while Mike is doing something else.

## LVGL build (from `sdkconfig`; the simulator derives its config from the same file)

- **LVGL 9.5.0** (`dependencies.lock`), default theme **light** (`CONFIG_LV_THEME_DEFAULT_DARK` unset),
  primary blue. Screen background renders `#F7F7F7`.
- Fonts compiled in: **Montserrat 12, 14 (default), 20, 32**. Line heights 15/16/22/35 px. Nothing
  else exists until a firmware change adds it: `CONFIG_LV_FONT_MONTSERRAT_<N>=y` in
  `sdkconfig.defaults`, which costs flash only. The app slot is 5.5 MB and ~47% used, so flash is
  not the constraint; internal RAM is. After a font change the simulator rebuilds LVGL on its next
  render (~1 min).
- `LV_SYMBOL_*` glyphs (in every Montserrat size): AUDIO, VIDEO, LIST, OK, CLOSE, POWER, SETTINGS,
  HOME, DOWNLOAD, REFRESH, MUTE, VOLUME_MID/MAX, PREV/PLAY/PAUSE/STOP/NEXT, LEFT/RIGHT/UP/DOWN,
  PLUS/MINUS, WARNING, BELL, WIFI, BATTERY_*, CHARGE, USB, BLUETOOTH, GPS, FILE, DIRECTORY, SAVE,
  EDIT, TRASH, EYE_OPEN/CLOSE, UPLOAD, CALL, ENVELOPE, SD_CARD, KEYBOARD... No microphone glyph (the
  mic on HOME is drawn from three rounded rects).
- `CONFIG_LV_GRADIENT_MAX_STOPS=2`: gradients have two stops, no more.
- `CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE=0`: shadows are recomputed on every redraw, so avoid them on
  anything that animates or updates often.
- Widgets enabled include arc, bar, button, canvas, chart, image, label, line, scale, span, table;
  flex and grid layouts; float (`CONFIG_LV_USE_FLOAT`); gesture recognition.

## Memory: internal RAM is the ceiling

(`sdkconfig.defaults` comments, `main/lvgl_psram_alloc.c`, the WORKSTATION on CLAWBOX page)

- The ESP32-S3 has ~512 KB internal RAM shared by Wi-Fi, TLS, the voice pipeline (WakeNet/MultiNet),
  the display DMA buffer and everything else. It has been down to **1.2 KB min-free (PANIC, 1.2.0)**.
  OTA needs a ~6–7 KB contiguous block or updates silently stop.
- Since 1.2.1 **LVGL's allocations go to PSRAM** (custom allocator, internal only as fallback). So more
  objects and styles mostly cost PSRAM, which is plentiful (16 MB), not internal RAM.
- The draw buffer is **320x50 RGB565 = 32 KB, internal DMA** (`CONFIG_BSP_LCD_DRAW_BUF_HEIGHT=50`).
  It was halved from 100 rows because MultiNet couldn't start. Do not grow it.
- Rendering from PSRAM is slower than from internal RAM, and CPU is shared with voice detection.
  Keep continuous animations small in area (HOME already runs two rotating arcs), avoid full-screen
  opacity fades and big transforms. `lv_obj_set_style_transform_*` and group opacity render through
  an intermediate layer (`CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE` 24 KB chunks).
- Today all six pages and three overlays are built at boot and never freed. The 1.3.0 draft proposes
  building pages on open. Until then every page's objects are resident.
- Images: C arrays in flash are fine. Decoded or cached images, canvases and snapshots need RAM:
  size them deliberately and say what they cost.

## What the simulator can't tell you

Glare, viewing angle, the panel's real brightness, touch accuracy, how an animation *feels*, frame
rate on the device, heap pressure, and whether the voice pipeline still starts. Those need the
BOX-3 and Mike's judgement.
