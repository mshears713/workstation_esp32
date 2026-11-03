---
name: workstation-ui
description: Design, render, inspect and refine Workstation's on-device UI (the ESP32-S3-BOX-3 console, 320x240 LVGL 9.5) with the workstation-ui renderer on CLAWBOX. Use whenever Mike talks about how the Workstation screen looks or behaves - a page, the microphone/listening widget, HOME, weather, menu, status, fonts, colours, layout, readability, "make it look more X", "give me a different direction", "I don't know what I want", "readable at arm's length" - or before changing any LVGL code in main/ui_*.c, voice_listening_widget.c or status_deck_ui.c. Not for backend, OTA or audio work unless it changes what the screen shows.
---

# Workstation UI

Workstation is a **physical engineering console**: a 2.4" 320x240 touchscreen on a desk, read at
arm's length across a workshop. It is not a phone app and not a web dashboard. You design it by
rendering real code on CLAWBOX, looking at the render, and refining it before Mike ever sees it.

## Your role: make the design decisions

Unless Mike says otherwise, this is **personal, experimental, low-consequence hardware**. You have
broad visual autonomy, and you are expected to use it.

- **Decide.** Choose the colours, sizes, spacing, radii, composition and widget types yourself. Don't
  ask Mike to pick a hex value, a pixel offset or a font size. He wants to react to a picture, not
  fill in a form.
- Treat existing layouts as **precedent, not constraint**. You may rethink a screen outright, use an
  unfamiliar LVGL pattern, try an unconventional composition, or propose a different interaction. Git
  makes it reversible.
- **Show him things he wouldn't have known to ask for.** When there are genuinely different good
  answers (e.g. "industrial" could mean instrument-panel, blueprint, or rugged high-contrast),
  render two or three distinct directions and put them side by side (`compare_renders`). When the
  request is clear, make one strong version. Alternatives serve the problem; they're not a quota.
- **Firm vs exploratory.** Hardware and functional constraints are firm (below and in
  `references/device.md`). Aesthetics are exploratory.
- Fix what you can see is wrong **before** showing Mike: clipped text, cramped margins, weak hierarchy,
  unreadable contrast. His attention is for taste, not for bugs you could have caught.

## Before substantial work

1. Read **"Workstation UI — Design Preferences & Experiments"** in Notion AI-OS (Knowledge
   database; search that title). It holds Mike's observed taste and what earlier experiments taught.
   Treat it as **evidence of preference, not a spec**. A single comment there is weaker than a
   principle that recurs.
2. `list_targets` to see what renders from real code, then render the current state of what you're
   changing (`render_page` / `render_component`) so you start from what the device shows today.
3. Skim `references/current-ui.md` for how the UI is organised, and `references/device.md` for the
   physical and memory limits. Read `references/lvgl95.md` before writing LVGL; it lists the
   gotchas that have already bitten.

## The loop

```
render current ─► decide a direction ─► render ─► LOOK ─► check_ui ─► refine ─► ... ─► show Mike
```

- **Look at every render yourself** before anything else. Ask whether the hierarchy is right at arm's
  length, what the eye lands on first, whether the state (ok / stale / error / unknown) is honest and
  obvious, and whether it feels like an instrument or like a phone.
- `check_ui` gives mechanical evidence: overflow, clipping, truncation, measured contrast, touch-target
  size, overlaps. It is not a judge. A flagged overlap may be intentional (HOME's corner buttons sit over
  the widget's transparent corners), and a clean check can still be ugly.
- `inspect_ui` only when the picture raises a question you can't answer by looking, such as an exact
  position, which object is clipping, or a font. Don't dump trees by default.
- Render **realistic states**, not just the happy path: the longest real string, a failure, stale or
  unknown data, an empty list. Use named fixtures (`describe_target`), and add better ones when they
  are missing.
- Try ideas cheaply with `render_snippet`. Use `use=[...]` to pull in real components and
  `fixture="page.name"` to borrow a real page's state. Move an idea into firmware only once it's worth
  keeping.

Tool details, the snippet contract and CLI equivalents: `references/renderer.md`.

## Design judgement for this screen

- **Glanceable first.** One primary thing per screen, readable from ~1 m. Montserrat 20 is the
  practical floor for things read at a glance; 14 is for secondary detail; 12 only for incidental
  hints. Big numbers beat labelled small numbers.
- **Finger-sized controls.** ~0.15 mm/px: 48 px ≈ 7 mm is comfortable, under 40 px (6 mm) misses.
  Keep targets apart, and keep them out of the rounded corners of the bezel.
- **Hierarchy through size, weight and contrast**, not through boxes. Don't turn everything into cards.
  Panels are one tool among several: alignment, whitespace, rules, a single accent colour, and scale all
  group things too.
- **Empty space is allowed.** A sparse screen with one strong element is often right here. Don't fill
  every pixel, and don't add chrome that says nothing.
- **Honest state.** Show current, stale (with age), error and unknown differently. Motion and colour can
  carry status (the stopped ring / red mic on HOME is the precedent), but never colour alone for
  something critical. Pair it with shape, motion or a word.
- **Don't import desktop/phone/web habits without a reason**: no hamburger for three items, no tiny
  top-bar status icons, no hover states, no scrolling pages when the content can fit.
- **Animation** when it carries status, confirms a touch, or adds personality worth its cost. Not
  because LVGL can. Continuous animation costs redraw time on an SPI panel.
- **Use what exists**: fonts Montserrat 12/14/20/32 are compiled in; LVGL symbols (`LV_SYMBOL_*`)
  are free; native primitives (arcs, rounded rects, lines) are cheap. A new font or image costs flash
  and needs a firmware change (see `references/device.md`).
- Dark or light is open. 1.3.0's draft spec proposes a dark theme with white/silver text and "no grey
  text on dark backgrounds". Check the preferences page for where that stands.

## Integrating into the firmware

- For mouse speech or a HOME house-tap voice change, read the observed
  connected ElevenLabs-to-device workflow in `doc/OTA.md` under “Mouse voice:
  ElevenLabs to HOME”; keep audio, weather, touch and device evidence separate.
- Keep the **seam rule**: UI builders (`main/ui_*.c`) are pure LVGL and take a view struct, setters
  or plain arguments. `status_deck_ui.c` reads subsystems and fills them. The simulator's adapter
  (`tools/uisim/targets/`) fills the same struct from fixtures. Don't write a second mock UI that can
  drift. Add seams incrementally, where you're working (`tools/uisim/README.md`).
- Refactoring UI-only code for renderability is in scope. Application-wide rewrites are not.
- Firmware rules that stay load-bearing: `CLAUDE.md` in this repo (internal RAM budget,
  `ota_service_start()` before `bsp_display_start()`, version-string contract, no casual health-gate
  changes).
- A render is not a build and a build is not the device. Before calling UI work done: `ws-ui selftest`
  passes, the ESP-IDF build passes, and (only when Mike asks for it) it is deployed and **seen on the
  BOX-3**. Never deploy an experiment just to look at it. Say clearly which of those three you have.

## Showing Mike

He is usually on his phone. Send the picture, not a description: `SendUserFile` with a render's
`view.png`, or a `compare_renders` image. Say in a sentence or two what you changed and why, and
what you'd try next. If you made a real judgement call (e.g. dropped a label), say so.

## Recording what you learn

After Mike reacts, update the Notion preferences page:

`experiment → Mike's feedback → observed preference → repeated evidence → stable principle`

- Log the experiment and his reaction, with the render or a short description.
- Promote to "observed preference" only what he actually said. Promote to "stable principle" only
  after it holds across more than one experiment.
- Don't edit this skill for a one-off comment. Stable principles may eventually move here.
