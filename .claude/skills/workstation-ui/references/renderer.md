# The renderer: tools, contracts, CLI

`workstation-ui` MCP server = `/opt/workstation/lvgl-sim` (Mike's fork of jaklys/Lvgl-mcp-esp32),
registered in this repo's `.mcp.json`. It starts with a Claude Code session opened in this repo and
exits with it. No daemon, no port. Every render is a child process that exits (1–5 s, the first
render after an sdkconfig LVGL change ~1 min).

## Tools

| Tool | Arguments | Returns |
|---|---|---|
| `render_snippet` | `code`, `use[]`, `fixture`, `state{}`, `advance_ms`, `scale`, `label` | image + one line |
| `render_component` | `name`, `fixture`, `state{}`, `advance_ms`, `scale` | image + one line |
| `render_page` | `name`, `fixture`, `state{}`, `advance_ms`, `scale` | image + one line |
| `inspect_ui` | `render_id` (default last), `node`, `only` = visible/text/touch/all | compact tree or one node's JSON |
| `check_ui` | `render_id`, `include_info` | findings with numbers |
| `compare_renders` | `render_ids[2..4]`, `scale` | one side-by-side image |
| `list_targets`, `describe_target` | `name` | registry, fields, fixtures |

The summary line names the render id (e.g. `0923-155009-firmware_readable`), which the other tools
take. Files: `~/.cache/workstation-ui/renders/<id>/` has `view.png` (upscaled; send this to Mike),
`screen.png` (native 320x240), `tree.json`, `checks.json`, `source/`, `log.txt`. The last 60
renders are kept.

## Snippet contract

`code` is the **body** of `void ws_render(lv_obj_t *scr)`, where `scr` is the active 320x240 screen,
or a whole file that defines `ws_render` (then include what you need, e.g. `<stdio.h>` for
`snprintf`). Available: all of LVGL as configured for the device; `ws_fx_str(key, def)`,
`ws_fx_int`, `ws_fx_bool`, `ws_fx_is(key, value)` for state; `ws_noop_event_cb` as a stand-in
handler (so the checks count the object as a touch target). `use: ["voice_listening_widget"]` links
that component's firmware sources and includes its headers (in body mode). Firmware `main/` is on the
include path.

`fixture: "firmware.failed"` borrows a registered target's fixture state (all its fields, defaults
filled in), and `state` then overrides keys. Use it for variants of a real page, so the variant sees
the same realistic data.

Errors come back as compiler output with snippet line numbers. A crash reports its exit signal.
Nothing is left running.

## Time and animation

`advance_ms` (default 400) runs simulated time in 5 ms steps before the frame is captured. That's enough
for default-theme transitions. For an animation, render a few values (e.g. 0 / 1750 / 3500 for a 7 s
rotation) and `compare_renders` them. The render is deterministic, so the same inputs give the same frame.

## CLI (sessions not started in this repo, or shell)

```bash
ws=/opt/workstation/lvgl-sim/bin/ws-ui          # add --repo PATH to pick the checkout
$ws list | describe NAME
$ws render NAME [--fixture F] [--set key=value ...] [--advance-ms N]
$ws snippet FILE [--use NAME] [--fixture NAME.F] [--set key=value] [--label L]
$ws check [ID] | inspect [ID] [--node N] [--only text|touch|all] | compare ID ID [...] [--out file.png]
$ws selftest
```

Then use `Read` on the printed `view.png` to look at it.

## Mechanical checks: what the numbers mean

- `contrast`: text colour against the most common *measured* background pixel under the label. Under
  3:1 is warn, under 4.5:1 is info. Coloured text on saturated buttons scores low without looking bad.
  Judge it.
- `touch-target`: interactive widgets or objects with handlers. Under 40 px is warn, under 48 px is info.
- `text-overflow` / `text-truncated` / `clipped`: text needs more room than its box, or a parent cuts it.
  Common cause: `LV_LABEL_LONG_MODE_DOTS` or `CLIP` without a fixed one-line height (the label wraps
  and grows instead).
- `overlap`: two touch targets or two texts intersect. It may be intentional; say why if so.
- `small-text`: line height under 16 px (Montserrat 12).
- `viewport` / `off-screen` / `zero-size`: geometry sanity.

## Registry

Targets live in `tools/uisim/targets.json` + `tools/uisim/targets/*.c` in this repo. How to add one
(and the pure-LVGL seam rule): `tools/uisim/README.md`.
