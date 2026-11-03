# Mouse icon stations, 2026-11-03

Base: deployed 1.5.2, commit 858d54e75301dde7665a0988d442ac7be647b724.
Only the standalone More > Mouse overlay changes. HOME caretaker implementation is byte-identical.

Three accepted native Pixelorama 32x32 RGBA PNGs are preserved in assets/mouse-stations.
provenance.json records exact canonical Drive identities and PNG/decoded hashes. The Library
Windows materializer failed on os.setxattr before installing any file. The supported Drive
read supplied the canonical exports; no Library metadata persistence is claimed.
tools/pack_mouse_station_icons.py packages the original pixels losslessly as 12,288 bytes
of flash-resident BGRA; runtime nearest-neighbor 2x rendering. No artwork was redrawn.

HOME stays at (8,8), 104x48. Station centers are x=56/160/264; labels y=60,
icon containers y=76, 72x68. The 48x48 mouse walks at y=148, x=8..264,
167ms per tick. It dwells 835ms below each station in each direction and highlights
that station. Background taps, including through the icons, freeze/resume both walk
and dwell. Close deletes its single timer and asynchronously deletes its overlay.
Reopening resets the walk. No audio, networking, storage, HOME or other page behavior changes.

Validation used the previously installed Clawbox /opt/workstation/lvgl-sim and cached LVGL9.5,
in isolated /tmp/mouse-stations-20261103. No Codespace or Artwork Workshop operation.
- ASan/UBSan standalone state: 10,000 ticks, all stations both directions, turns and pause.
- Actual LVGL: 300 repeated open/double-close cycles, timer baseline, pointer taps through
  all icons, pause/resume, HOME callback, fresh reopen.
- Existing Newton/Lincoln 500 lifecycles each; audio selector 200; repository host suite ALL PASS.
- HOME RGB captures match base at 0/1000/5000ms.
- 211 actual C/LVGL frames from 0 through 21,000ms at 100ms spacing, not a mock animation.

Runner: run_mouse_sim.py in the sanitized integration handoff; requires the existing cached
LVGL configuration and stops if it is missing. Registered targets: mouse and mouse_ui_test.
The simulator uses libc allocation; it cannot prove physical board heap, touch or speaker behavior.
