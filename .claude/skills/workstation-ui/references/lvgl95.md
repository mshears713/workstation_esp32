# LVGL 9.5 notes that matter here

The official docs (docs.lvgl.io/9.5) and the source in the simulator
(`/opt/workstation/lvgl-sim/simulator/lib/lvgl`, tag v9.5.0) outrank anything below. Parts are
adapted from ezrover/ESP32-AI-Agent-Skill, with its v8-isms corrected.

## Names

- v9 names: `lv_button_create`, `lv_obj_remove_flag`, `lv_display_get_screen_active`,
  `lv_screen_active`. The firmware also uses v8 names (`lv_btn_create`, `lv_obj_clear_flag`,
  `lv_disp_get_scr_act`); they are compat macros in `lv_api_map_v8.h` and still build. Match the
  surrounding code.
- Label long modes: `LV_LABEL_LONG_MODE_WRAP | DOTS | SCROLL | SCROLL_CIRCULAR | CLIP`
  (`LV_LABEL_LONG_DOT` etc. are compat aliases). **DOTS and CLIP need a fixed height**. With
  auto height the label wraps and grows instead of truncating (seen in the simulator, 23 Sep).
- `lv_color_t` is RGB888 in v9 regardless of display depth. Use `lv_color_hex(0xRRGGBB)` and
  `lv_palette_main/lighten/darken(LV_PALETTE_*, n)`. Don't size buffers with `sizeof(lv_color_t)`;
  use `LV_DRAW_BUF_DEFINE` / `lv_color_format_get_size`.
- `lv_chart` has no axis tick labels in v9. Use `lv_scale` beside it, or a caption (current SENS).

## Defaults that surprise

- Plain `lv_obj_create` objects are **clickable and scrollable** by default. Clear `SCROLLABLE` on
  static containers (it avoids scrollbars and drag), and clear `CLICKABLE` on decoration so taps reach
  what's underneath.
- The default theme adds padding, radius, a border, shadows on buttons and a grow-on-press transition
  (`CONFIG_LV_THEME_DEFAULT_GROW`, 80 ms). Set styles explicitly when the exact look matters.
- `lv_arc` always draws a knob. Hide it with `lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB)`.
- Creation order is z-order. Later siblings draw on top and receive taps first.

## Styles and memory

- `lv_obj_set_style_*(obj, ..., selector)` stores a local style per object. That's fine for a few
  objects. For many objects sharing a look, one `static lv_style_t` + `lv_obj_add_style` is cheaper.
- `lv_label_set_text` copies the text. `lv_label_set_text_static` doesn't: use it for constant strings.
- Opaque backgrounds (`LV_OPA_COVER`) are cheaper than blended ones. Shadows, blur, `transform_*`
  and whole-object opacity cost render time (and a layer buffer).
- Flex (`lv_obj_set_flex_flow` / `lv_obj_set_flex_align`) and grid are available. For fixed
  320x240 pages absolute alignment is also fine and is the current style. Pick whichever reads better.

## ESP-IDF integration (already done: don't redo it)

`esp_lvgl_port` via the BSP runs the LVGL task. Don't call `lv_timer_handler()` yourself. Touch LVGL
from other tasks only inside `bsp_display_lock()` / `unlock()`. `status_deck_ui()` runs under the lock
from `main.c`. Subsystem state crosses into the UI through pending queues and LVGL timers (see
`status_deck_ui.c`).
