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

### 1.2.1 field failure → 1.2.2 (23 Sep 2026)
**What happened.** 1.2.1 ran fine for ~6 min after its 14:03 deploy. At ~14:10 Mike tapped RPT (a normal tap)
on the LOG page; the report never reached the backend, and from then on the device restarted every ~25 s and
never reached its first OTA check. Stopping the backend on :8000 kept it up long enough for OTA to pull 1.2.0.

**Cause (certain).** `diag_stack_hwm()` looked tasks up with `xTaskGetHandle(name)`. ESP-IDF 5.5's FreeRTOS
starts that function with `configASSERT(strlen(pcNameToQuery) < configMAX_TASK_NAME_LEN)`, and
`configMAX_TASK_NAME_LEN` is 16. The list contained `"handshake_worker"`: 16 characters. So **every** report
(`stack_free_min` is in all of them) hit `assert()` → `abort()` inside `build_body()`, before the POST. The RPT
tap crashed the first time. Each boot after that queued an `after-crash` report "once the backend answers",
which crashed the same way, ~25 s after boot. The first OTA check comes ~60 s after boot (the OTA task's first
pass runs before Wi-Fi is up, so its first real check waits one poll interval), so OTA never got a turn. With
the backend down the report never started, so the device stayed up. 1.2.0 had no stack marks, so its RPT worked.
Reproduced off-device: `tools/hosttest/run.sh 1.2.1` aborts on exactly that assert. The report task's stack
was measured and was not the problem (see the 1.2.2 commit message for the numbers).

**Fix.** Names are cut to 15 characters before the lookup (how FreeRTOS stores them); the report key keeps the
full name.

**Loop guard (1.2.2, for any future crash in the report path).**
- Every report attempt (automatic or RPT) sets an RTC_NOINIT marker (magic + complement, and the trigger)
  before building the body and clears it when the attempt ends.
- If a boot finds the marker set after a PANIC/watchdog/brownout reset, it counts it (NVS `diag/rptcrash`,
  reported as `counters.report_crashes`), logs "AUTO REPORT OFF: crashed while reporting" (Black Box line
  `AUTO RPT OFF: CRASH IN REPORT`), and sends **no automatic report that boot** (after-crash, heartbeat,
  auto-reconnect, auto-watchdog). RPT still works; its report says `auto_reports_off: true`. The next
  boot starts fresh. A report path that always crashes therefore costs one reset, then the device stays up.
- No automatic report is sent before **120 s of uptime and one finished OTA check**, so an update can
  always land before any report work runs.
- The RPT 5 s hold test crash (`DIAG_TEST_CRASH`) stays for commissioning.
- Host test: `tools/hosttest/run.sh` compiles the real `report_client.c` + `diag.c` with gcc under
  ASan/UBSan, with a worst-case breadcrumb and event ring (15.6 KB body; buffer now 20 KB), and checks the
  guard logic.

## 1.3.0 — home + weather + menu + dark theme (Class 3 rework; built 21 Oct 2026)
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

### 1.3.0 as built (21 Oct 2026, branch `v1.3.0-home-weather`)
Mike approved the look from the `home130-v2` mockup. Decisions taken while building, against the draft above:
- **°F, and rain % on every slot** (now, +2 h, +6 h, tomorrow) - the two open questions, answered in the mockup.
- **Pages stay built at boot.** The draft's build-on-open was a memory measure written before 1.2.1 moved LVGL
  to PSRAM (`lvgl_psram_alloc.c`). With widgets in PSRAM it would save almost no internal RAM, and it would
  add use-after-free risk on every timer and lose the SENS chart history each visit. Revisit only if the
  heartbeat shows internal RAM worse than 1.2.2.
- **MENU is a 2-column grid of five buttons, not a scrolling list**: everything fits on one screen at
  151x54. A sixth page fits; beyond that it becomes a list.
- **Back control**: the four corner buttons became one `< HOME` button (top-left, where HOME always was),
  hidden on HOME. `ui_nav_*` was replaced by `ui_back_button_create()`.
- **Mic**: `voice_listening_widget_create_sized()` builds the widget at 104 px - geometry scaled, no
  transform, so no extra render layer and a touch area that matches the drawing. A caption under it
  spells out the link (LINK OK / NO BACKEND / NO WI-FI / CONNECTING).
- **Dark theme**: `CONFIG_LV_THEME_DEFAULT_DARK=y`, black screen, charcoal panels, `ui_theme.h` names the
  deliberate colours. The ~70 existing palette calls were left alone: they are status colours
  (green/orange/red) that read fine on black, and replacing them is churn with no visible gain.
- **Weather poller** (`weather_client.c`): PSRAM-stack task, first fetch only after the updater's first
  finished manifest check (or 180 s if the updater never started), then every 10 min; 60 s retry. The
  snapshot is replaced only by a complete answer. Stale (> 30 min) shows "as of HH:MM" in orange where
  the location sits. Never writes NVS.
- **Backend**: `GET /api/v1/weather` (backend branch `weather`), Open-Meteo, location in
  `app/config.py` (`weather_*`, overridable in `.env`), cached 10 min, last good answer served with
  `age_s` when Open-Meteo fails, 503 only before the first answer.
- **Icons**: `main/weather_icons.c`, generated by `tools/weather_icons/gen_icons.py` (Pillow, 4x
  supersampled, ARGB8888, 48 px and 30 px; ~100 KB flash). **Font**: Montserrat 48 for the current temp.
