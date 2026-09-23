# The UI as built (1.2.2, 23 Sep 2026). Re-read the source before relying on this.

Code: `main/status_deck_ui.c` (the app: state, timers, callbacks, most pages), `main/ui_kit.c`
(furniture), `main/ui_home.c`, `main/ui_firmware_page.c`, `main/voice_listening_widget.c`.
The file header of `status_deck_ui.c` is the design history (Missions 05–19). Read it for why
things are the way they are.

## Structure

- Six full-screen (320x240) transparent page containers, one visible at a time: **HOME, SENS, LOG,
  SET, PROJECTS, FIRMWARE** (`app_page_t`). Adding a page touches the enum, `set_active_page()`'s
  `pages[]` and the `pages_init[]` block.
- **Corner nav** (`ui_nav_create`): four 96x52 buttons (HOME top-left, SENS top-right, LOG
  bottom-left, SET bottom-right), Montserrat 20. All four on HOME; only HOME elsewhere (HOME is the
  hub). Active page blue, others grey. PROJECTS and FIRMWARE open from buttons on SETTINGS.
- **Sub-page convention**: title in Montserrat 32 blue at x=104, y=12 (beside the HOME button),
  content from y=58, backing panels 304 px wide (`ui_page_panel`): dark blue-grey, 2 px blue border,
  radius 10.
- **Overlays** on the screen, above everything: Command Window (voice command grid), recording
  (NOTE/GO capture, project/repo selectors, counter, SEND), notification (read-aloud with STOP).
  Small, time-critical, always built.

## Pages

| Page | Content | Renderable |
|---|---|---|
| HOME | only the listening widget (172 px ring, centred), tapped to TALK; ring and mic colour show backend link and pending notification | `render_page home` |
| SENS | ACCEL / TEMP (°F 60–90) / HUMIDITY charts with live values, captions instead of axis ticks | not yet |
| LOG | black-box event list, NET / RPT / CLR buttons, FPS/CPU readout | not yet |
| SET | uptime, API, Wi-Fi; volume -/+; GO repository < >; PROJECTS >, FIRMWARE > | not yet |
| PROJECTS | AI-OS project selector < name > and its cue text | not yet |
| FIRMWARE | version, build, backend URL, slot + probation, OTA status, CHECK CLAWBOX NOW | `render_page firmware` |

## Listening widget semantics (keep them unless deliberately redesigning)

Motion carries the alarm, colour carries the reason: spinning + white mic = OK; **stopped + red mic
= backend down**; stopped + grey = no Wi-Fi; spinning + grey = not established yet. The outer ring
turns orange while a low-confidence notification waits.

## Known issues the simulator shows (not yet hardware-confirmed)

- On sub-page panels, labels use the light theme's near-black default text on the dark blue-grey
  panel: measured ~1.6:1 contrast on FIRMWARE. See `tools/uisim/experiments/firmware_readable.c` for
  one fix.
- White on the grey inactive nav buttons: ~2.7:1. The blue page titles on the light background: ~2.9:1.
- FIRMWARE's CHECK CLAWBOX NOW button is 28 px tall.

## Where it's heading (draft, needs Mike's approval: `doc/SPEC-ui-weather-diagnostics.md` §1.3.0)

One home screen: Orlando weather down the left (now, +2 h, +6 h, tomorrow), a smaller mic top-right
still showing link status and tapping to record, a MENU button bottom-right opening the existing
pages, a dark theme from one header of named colours, ~8 small weather icons in flash, possibly
Montserrat 48 for the current temperature, pages built on open. Use the simulator to explore that
before it's coded. It is still a draft.
