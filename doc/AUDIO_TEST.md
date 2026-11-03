# Audio Test candidate (2026-10-30)

Base: 1.4.2+9251718, branch audio-test-20261030. This is a tested release candidate,
not a deployed release. Existing 1.4.2 remains installed, ota_1 VALID.

More > Audio Test replaces the entry to the recoverable Music mock. The existing
voice_detect task owns SD mount, files and speaker for the entire session. It
admits no competing mouse speech, pauses microphone reads before closing the
mic codec, and reopens the mic before releasing ownership on exit. The LVGL
page polls plain status at 100ms; buttons only coalesce control requests. Back
cancels and closes the page immediately, with worker cleanup following.

PCM: 24kHz signed 16-bit little-endian mono. Music and optional wind loop;
Newton voice cycles 1..6, with one voice at a time, 30-second voice-file ceiling.
Music ducks 9dB with 100ms attack and 250ms release. Per-file stdio caches and
mixing buffers are in one PSRAM allocation. Output chunks are 256 samples.
Speaker uses codec volume 100 for this requested test, restores normal volume
on exit. Saturation protects integer output; acoustic cleanliness is UNTESTED.

## Hardware / recovery

Official installed esp-box-3 BSP maps SENSOR SDMMC D0/D1/D2/D3=9/13/42/12,
CMD=14, CLK=11, active-low power=43. GPIO43 conflicts with UART0 TX. Candidate
selects USB Serial/JTAG as primary console (GPIO19/20), not USB OTG CDC.
Windows presently enumerates USB VID303A:PID1001 COM7 plus JTAG interface.
This establishes physical interface presence, not an executed recovery test.
No GPIO19/20, BOOT, partition layout, IDF version, eFuse or OTA gate changes.
Existing 1.4.2 published artifact and inactive-slot OTA recovery remain available.
OTA updates the application only; do not flash the generated whole-device args.

FATFS LFN_HEAP enabled, max filename 255. Mount explicitly sets
format_if_mount_failed=false and compile fails if BSP format-on-failure is set.
Only /sdcard/dottie/audio is opened, all files with rb. No card writes/format.
E: remains the previously verified 7,939,817,472-byte FAT32 volume on Windows.
It must be safely ejected and placed in SENSOR before actual SD playback.

## Tests and acceptance

Windows: run ./build_audio_test.ps1 in PowerShell using installed IDF 5.5.3 and
Python3.11. PYTHONUTF8/PYTHONIOENCODING avoid ESP-SR packaging output failure.
Native executable startup needs the permitted execution context; no install.

Linux host (existing authorized simulator environment):
    sh tools/hosttest/run_audio.sh
Compiles actual mixer, streams and session under ASan/UBSan. Session hardware
calls are stubbed; this does not prove mic recovery, codec/I2S, SD pins or sound.
Tests cover duck/saturation, short-file loops, concurrent wind+voice, six voice
cycling, duplicate rejection, malformed/missing inputs, cancellation, mount,
allocation and speaker failures, 200 cleanup cycles and no-format mount config.

Render actual UI (requires existing ws-ui and LVGL9.5):
    ws-ui --repo . render audio_test --fixture default
    ws-ui --repo . render audio_test --fixture mixing
    ws-ui --repo . render audio_test --fixture unavailable
    bash tools/uisim/tests/audio_test_flow.sh /path/to/ws-ui
Inspect pixel output; the fixture supplies status only and does not play audio.

Passed on Clawbox: mixer, streams, session and player under ASan/UBSan; actual LVGL pointer tests and 200 UI lifecycle cycles. Default, mixing and missing-card pixels reviewed. Pending: release identity/OTA and hardware checks. Run future changes through the established
simulator environment, then release identity/commit and controlled application
OTA. Do not call these tests passed merely because IDF compilation succeeded.
Physical acceptance: music starts; Wind toggles while voice plays; duplicate
voice taps ignored; six voices cycle; Back stops, normal microphone resumes;
repeat entry/exit, missing card, and network/OTA check-ins remain healthy.
Before/after SD logs capture internal free/largest/minimum and PSRAM; these are
instrumentation only until actual device logs are collected.
