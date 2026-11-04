# Artifacts & Instruments exhibit build (branch `artifacts-instruments`, 1.6.x)

A lean build of the Workstation firmware for the physical exhibit: an Orlando
HOME scene, a mouse guide, Tour A (Egypt / Greece / Gothic), Newton and Lincoln,
a radar welcome and spoken clips - plus the maintenance pages and the
OTA / health-gate / rollback machinery unchanged. Based on `ae8ff86` (overnight
HOME), which was based on the deployed `1.5.3+19381ab`. The November lineage is
archived on GitHub as `archive/*` branches.

## What the exhibit build links

| Kept | Notes |
|---|---|
| HOME (`ui_home.c`) | repainted scene (`orlando_artifacts.h`): house = EXPLORE, mouse = guide (+ `guide` clip), van = Tour A at Egypt, castle = drawbridge then princess, Eye, coaster, radar welcome card |
| EXPLORE (`ui_menu_build_pictures`) | the main menu: Tour, Newton, Lincoln cards; TOOLS > in the title band |
| TOOLS | SENSORS, LOG, FIRMWARE, SETTINGS, DIAG |
| Tour A (`ui_adventure.c`) | unchanged visuals; reports scenes to the app, which narrates per region |
| Newton, Lincoln | unchanged scenes and SD audio (`audio_test_session.c`) |
| `exhibit_audio.c` | the one task that owns the speaker + SD card (replaces the voice task's role) |
| `radar_presence.c`, `presence_logic.c` | SENSOR dock AT581X + when to welcome |
| OTA, health gate, rollback, reports, Black Box, diagnostics | unchanged, except below |

**Removed from the link** (source stays in the tree and in git history):
voice capture and wake-word commands (`voice_control`, `audio_capture`, ESP-SR,
the SEND/NOTE/GO upload clients, the command and recording overlays),
notifications, PROJECTS (`project_selector`, `backend_catalog`), survey, music,
Tour B and the tour selector, weather (page, client, icons), the IR/Roku
remote, the legacy standalone mouse (`ui_mouse.c`), the AUDIO TEST page and the
dormant handshake. Face and the Messages page were already out (verified).

**One behaviour change outside the removals:** `backend_health.c` no longer
self-restarts after 10 minutes of silence when the backend has *never* answered
this boot. Off the home LAN that rule rebooted the exhibit every ten minutes.

## Radar (SENSOR dock)

MoreSense MS58-3909S68U4 = Airtouch AT581X, 5.8 GHz Doppler. Identified from
Espressif's factory BSP for this dock (`BSP_RADAR_OUT_IO = GPIO21`, I2C 0x28 on
the dock bus GPIO40/41) and the dock spec ("human presence detection, around
2 meters"). **It outputs yes/no, not a distance.** SETTINGS > RADAR REACH 1-10
maps to the chip threshold 623..983 in steps of 40 (level 6 = 823, the vendor
default; 1023 is avoided - zero margin may trigger on noise); what a
level means in centimetres has to be measured at the exhibit. The SETTINGS row
shows the live output (SEEN / --) for walk tests.

Welcome rule (`presence_logic.c`, host-tested): detected for 6 s (gaps under
2.5 s forgiven - a still visitor can drop out of a Doppler sensor) on an idle
HOME -> one welcome; it re-arms only after 30 s of continuous quiet; a touch
anywhere counts as "already engaged". After 2 minutes without a touch (5 while
the tour plays on its own) the screen returns to HOME, so the welcome works
for the next visitor. NVS `radar`: `reach` (1-10), `dwell_ms`
(2000-20000), `rearm_ms` (5000-600000).

## Spoken clips (SD card, offline)

`/dottie/exhibit/<name>.pcm` on the SENSOR dock's microSD: 24000 Hz s16le mono,
at most 30 s (`tools/exhibit_audio/make_pcm.sh`). Names: `welcome` (radar),
`guide` (mouse tap), `egypt`, optional `greece`, `gothic` (Tour A, one talk per
region, started on arrival, stopped on leaving the region). A missing card or
file is logged (`CLIP egypt: FILE MISSING`) and the written text still shows.
Newton's files stay in `/dottie/audio/`; Lincoln caches its media from CLAWBOX
on first open - **open Lincoln once on the home LAN before taking the exhibit
out**.

## Before taking it to a venue

1. Deploy and confirm `valid` on the home LAN (the health gate needs CLAWBOX).
2. Clips on the SD card; open each scene once; Lincoln once (media cache).
3. Walk-test the radar at the venue distance; set RADAR REACH; note the level.
4. Off the LAN the device keeps running: no backend restarts, OTA polls fail
   harmlessly, reports wait.

## Rebuilding the art

`python3 tools/artifacts_art/build_art.py` regenerates `main/orlando_artifacts.h`
from the accepted sprites (`--check` proves the committed header is
reproducible). See `tools/artifacts_art/README.md`.
