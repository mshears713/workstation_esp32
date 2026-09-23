# SPEC — diagnostics (1.2.1), then home + weather + menu (1.3.0)

Status: **1.2.1 approved to build (Mike, 23 Sep 2026). 1.3.0 is a draft awaiting Mike's approval.**
Process: AIOS "Tool Evolution & Change Triage". This file is the plan delta, and the Change Log lives on the
Notion page "WORKSTATION on CLAWBOX — how agents operate the device".

## Why
- The report from 1.2.0 (23 Sep 13:16) showed that the boot before last ended in a **PANIC**. Internal heap
  **min-free was 1,207 B** and the largest block was 6,144 B; the OTA task needs 6 KB contiguous. Boot count is 518,
  up from 304 on 12 Sep.
- Cause, per the survey: `CONFIG_LV_USE_CLIB_MALLOC` + `SPIRAM_MALLOC_ALWAYSINTERNAL=4096` put every LVGL
  object/style/label (~110–130 objects, est. 25–45 KB) in internal RAM. All 6 pages and 3 overlays are built at boot
  and never freed.
- There is no core dump and no free flash for one, so a panic leaves no evidence beyond "PANIC".
- Mike can't read the screen: default LIGHT theme, grey text on blue-grey panels.
- Mike wants a single home screen with weather, and a menu instead of 4 corner buttons.

## 1.2.1 — diagnostics + memory (Class 1 patch: no product change)
1. **LVGL off internal RAM.** Use `LV_STDLIB_CUSTOM` (or equivalent) so LVGL's malloc/realloc/free prefer PSRAM
   (`heap_caps_*` with `MALLOC_CAP_SPIRAM`, internal as fallback). Leave the DMA draw buffer internal. Measure
   the "UI built" heap line before and after. Watch for slower rendering on SENS charts.
2. **Crash breadcrumb (RTC_NOINIT, survives a panic reset).** Keep a ~2 KB ring of the last log lines (wrap
   `esp_log_set_vprintf`), plus the active page, last command, and heap free/min at the last tick. Validate it
   with a magic number and CRC. After a PANIC/WDT reset, copy it into the next report as `last_words`.
3. **Persistent counters (NVS):** boots, panics, watchdog resets, self-restarts, brownouts. Totals, plus the
   timestamp of the last of each.
4. **Hourly heartbeat report** (`trigger: "heartbeat"`) and a report on every boot after an abnormal reset
   (`trigger: "after-crash"`). Include stack high-water marks for every app task and heap min-free.
5. **clawbox side:** `workstation-watch` alerts Mike once when a report shows a new crash, or when heap min-free
   drops under 2 KB. Same rule as now: an alert only when a problem starts.

Acceptance: builds clean. After deploy, the next heartbeat shows internal min-free meaningfully above 1.2 KB, and
the report carries the counters. A deliberate test panic (debug-only trigger, removed before merge, or gated by a
backend flag) produces `last_words` in the following report.

## 1.3.0 — home + weather + menu + dark theme (Class 3 rework; DRAFT)
**Home (the only screen at boot):**
| Area | Content |
|---|---|
| Left column, full height | Weather for Orlando: **now** (icon, temp °F, condition), then **+2 h**, **+6 h** (icon, time, temp), then **tomorrow** (icon, high/low, rain %) |
| Top-right, ~25% of the screen | The existing mic/record widget, scaled down, still showing link status (ring/red mic) and still tapping to record |
| Bottom-right | **MENU** button |

**Menu:** a scrollable list that opens the existing pages unchanged: Sensors, Log & report, Projects,
Firmware, Settings (Weather and Spotify later). Every page gets a "‹ Home" back control. No clock (Mike has one
above the device).

**Look:** LVGL dark theme. One header of named colours (BG black, PANEL charcoal, TEXT white, TEXT_DIM silver,
ACCENT, OK, WARN, ERR) replaces the ~70 ad-hoc palette calls. No grey text on dark backgrounds anywhere.

**Weather data:** the backend fetches Open-Meteo (free, no key; verified 23 Sep: Orlando 87.4 °F, code 3) every
10 min and serves a compact `GET /api/v1/weather` with the four slots already chosen and the icon keys already
mapped. The location (Orlando 28.5383, -81.3792) is set in backend config, and Mike changes it there if he moves.
The device polls every 10 min (reusing an existing poller or a PSRAM-stack task) and caches the last good
answer, showing "as of HH:MM" when the data is stale.

**Icons:** about 8 small C-array images in flash (sun, moon, partly cloudy, cloud, fog, rain, thunderstorm, snow),
mapped from WMO weather codes on the backend. Fonts: add Montserrat 48 for the current temperature (flash only).

**Memory:** pages other than HOME are built when opened and deleted when left, with NULL guards on every timer
and render path that touches page widgets (the survey's #2 risk: use-after-free). Overlays stay, since they are small
and time-critical.

Acceptance (commissioning with Mike): readable at arm's length; weather matches Open-Meteo; every existing
page opens from the menu and works as before; recording works from home; the heartbeat heap after an hour of
use is not worse than 1.2.1.

Open questions for Mike: °F confirmed? Rain % on the +2 h / +6 h slots, or only tomorrow?
