# tools/uisim - what the host LVGL simulator can render

The renderer lives outside this repo at `/opt/workstation/lvgl-sim` (its own git repo, a fork of
jaklys/Lvgl-mcp-esp32). It reaches Claude Code through this repo's `.mcp.json` as the
`workstation-ui` MCP server, and reaches shells as `/opt/workstation/lvgl-sim/bin/ws-ui`. This
directory tells it **what** it can render from this firmware:

| File | What |
|---|---|
| `targets.json` | registered components and pages: firmware sources, adapter, state fields, named fixtures |
| `targets/*.c` | adapters: turn fixture state into the same calls `status_deck_ui.c` makes |
| `experiments/*.c` | design experiments rendered with `render_snippet`. **Not firmware.** Delete or promote them. |

The simulator takes its LVGL configuration from **this checkout's `sdkconfig`**, so fonts, widgets,
theme and colour depth follow the firmware automatically. It renders whichever checkout the session
was started in (or `--repo` / `$WS_REPO`), so a branch renders its own edits.

## The seam rule

A file can be rendered only if it is **pure LVGL**: it includes `lvgl.h` and other pure-LVGL
headers, never ESP-IDF, BSP, FreeRTOS or subsystem headers, and it never reads app state. The
pattern:

```
real subsystems ── status_deck_ui.c ──► view struct / setter calls ──► ui_*.c builder ──► LVGL
fixture JSON   ── tools/uisim adapter ─► view struct / setter calls ──► ui_*.c builder ──► LVGL
```

The builder is the same file on both paths, so the simulator can't drift from the device.
Seams so far:

| File | What | Seam |
|---|---|---|
| `main/ui_kit.c` | page container, panel style, page title, `< HOME` back button | plain arguments |
| `main/ui_home.c` | HOME 1.3.0: weather column, 104 px mic (TALK), link caption, MENU | `ui_home_weather_t` + `ui_home_apply_weather()`, `ui_home_set_link()` |
| `main/ui_menu.c` | MENU (1.3.0) | `ui_menu_item_t[]` |
| `main/weather_icons.c` | the eight weather images (generated) | `weather_icon(key, large)` |
| `main/voice_listening_widget.c` | the microphone ring | `_set_link`, `_set_notification` |
| `main/ui_firmware_page.c` | FIRMWARE page | `ui_firmware_view_t` + `ui_firmware_page_apply()` |

Still inside `status_deck_ui.c` (not renderable yet): SENS, LOG, SETTINGS, PROJECTS, and the
command, recording and notification overlays. Extract one **when you're about to work on it**. Don't
extract them all in advance.

## Adding a target

1. Move the page or component's LVGL construction into `main/ui_<name>.c/.h`: build function(s) that take
   the parent plus callbacks, and a view struct or setters for what changes at runtime. Keep the
   original comments with the code they explain. Add the file to `main/CMakeLists.txt`.
2. In `status_deck_ui.c`, call it, and fill the view from the real subsystems.
3. Write `tools/uisim/targets/<name>.c`: `void ws_render(lv_obj_t *scr)` that builds it the way
   `status_deck_ui()` does and fills the view from `ws_fx_str/int/bool/is()`. Attach
   `ws_noop_event_cb` wherever the firmware attaches a real handler, so touch-target checks see
   the same interactive objects.
4. Register it in `targets.json`: `sources`, `headers`, `adapter`, `fields` (type, default, doc) and
   **realistic** `fixtures`. That means values the device really produces: real version strings,
   real OTA messages, the longest project name, a stale state, an error state. Placeholder
   text hides the layout problems that matter.
5. `ws-ui selftest` must pass. Then **build the firmware** (`idf.py build` in a throwaway clone, or
   `workstation build` when the canonical tree is free): a render is not a build.
