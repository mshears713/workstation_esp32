# Operation Homebound — Workstation Console

## Lineage

This project is updated in place, mission by mission - the working tree
always reflects the newest mission, not a growing pile of sibling folders.
Each mission's pre-next-mission source snapshot is kept under `archive/`
instead:

- `archive/mission_09/` — the Earthside Handshake source as it stood right
  before Mission 10 (Capture the Transmission) was added. (Wi-Fi credentials
  are never archived - see `.gitignore`.)
- `archive/mission_10/` — the Capture the Transmission source (including
  `backend/`) as it stood right before Mission 11 (Computer Is Listening)
  was added.

## Overview

Integrated command-and-telemetry deck for the ESP32-S3-BOX-3, built on the
BSP display/touch foundation from Mission 02 (`display`) and the LVGL touch
console from Mission 04 (`first_command`). A top status bar shows uptime,
FreeRTOS tick count, free heap and PSRAM. A state panel shows current mode,
last command, link status and diag status. Touch buttons drive a single
`app_state_t`, and a compact on-screen event log shows the last few command
events.

- **Mission 06 — Live Data Deck:** real acceleration magnitude from the
  onboard ICM42670 IMU, shown with an explicit current/stale/error status
  and a trend chart.
- **Mission 07 — Black Box Recorder:** bounded structured event history,
  reset-reason and boot-count capture, and a small set of NVS-persisted
  evidence that survives a reboot.
- **Mission 08 — Connection Deck:** a Wi-Fi station lifecycle
  (`main/wifi_manager.c`) with explicit DISCONNECTED / CONNECTING / ONLINE /
  RETRY_WAIT states, bounded retry backoff, a manual NET reconnect button,
  and network transitions fed into the Black Box.
- **Mission 09 — Earthside Handshake:** an operator-triggered backend
  request (`main/handshake_client.c` + `backend/main.py`). The SND button
  sends a small JSON event to a local FastAPI service, which returns a
  server-generated `event_id`; request state (SENDING / ACCEPTED / TIMEOUT /
  NETWORK_ERROR / SERVER_ERROR / BAD_RESPONSE) is tracked separately from
  Wi-Fi state and fed into the Black Box.
- **Mission 10 — Capture the Transmission:** bounded microphone capture
  (`main/audio_capture.c`) against the verified esp-box-3 BSP mic path
  (ES7210 ADC over I2S, `bsp_audio_codec_microphone_init()` +
  `esp_codec_dev`). The REC button starts one 4.0s mono 16-bit/16kHz
  capture into a fixed-size buffer (no growth, no per-capture allocation);
  state is IDLE / ARMING / RECORDING / COMPLETE / UPLOADING / READY /
  FAILED, shown on its own AUD status row with live elapsed time and peak
  level while RECORDING. Completed captures upload straight to the backend
  over the same Wi-Fi link the Handshake uses (see "Inspecting a capture"
  below) - this mission does **not** add wake-word, transcription, or
  OpenAI calls, just microphone bring-up and getting the audio off the
  device as a playable WAV.
- **Mission 11 — Computer Is Listening:** local, offline wake word and
  command recognition (`main/voice_control.c`) using the official Espressif
  ESP-SR AFE/WakeNet9/MultiNet7 pipeline. Saying **"computer"** opens a short
  command window; saying **"capture," "go," "listen," or "okay"** within it
  triggers the exact same Mission 10 capture-and-upload path the REC button
  does. Manual REC
  keeps working unconditionally as a fallback. See "Voice control (Mission
  11)" below for the wake word, command phrase, and the mic-ownership design
  that lets one ES7210 codec serve both continuous listening and bounded
  recording.

## Inspecting a capture (Mission 10)

Press **REC** on the device. After ~4s the AUD row reads `DONE` (peak/RMS
shown), then `UPLOADING...` while the capture POSTs to the backend over
Wi-Fi (typically well under a second for 128,000 bytes on a healthy LAN),
then briefly `RDY capture_NNN` before returning to idle.

The backend needs to be running first (see "Run the backend" below) -
`main/audio_capture.c`'s `upload_capture()` POSTs the raw PCM straight to
`POST /api/v1/audio` on `BACKEND_BASE_URL` (`main/backend_config.h`), which
wraps it in a real WAV file under `captures/`. If the backend isn't
reachable, the AUD row shows `FAIL UPLOAD UNREACHABLE` (or `UPLOAD
TIMEOUT`) and returns to idle after a moment - the recording itself is not
lost, only the upload needs retrying, so press REC again once the backend
is up.

To get the file:

1. Browse `http://127.0.0.1:8000/captures` for the list (JSON), or
2. Open `http://127.0.0.1:8000/captures/capture_NNN.wav` directly in a
   browser tab to play it - browsers play WAV inline.

`captures/*.wav` is git-ignored - these are local inspection artifacts, not
project source.

**PSRAM:** the first build attempt hit `AUD: FAIL BUFFER ALLOC FAILED` -
`CONFIG_SPIRAM` was off, so the 128,000-byte capture buffer (4s of 16kHz
16-bit mono) had to come from internal SRAM, the same pool as LVGL's
framebuffers and every task stack, and there wasn't a big enough free block.
`CONFIG_SPIRAM=y` (Octal mode, matching this board's 8MB Octal-PSRAM
module) is now set in both `sdkconfig` and `sdkconfig.bsp.esp-box-3`, so
`audio_capture_init()`'s PSRAM-first allocation should succeed. This
requires a fresh build (the PSRAM/flash config changed, not just source),
so expect the next build to take longer than an incremental one.

## Voice control (Mission 11)

Say **"computer"**, wait for the VOICE row to read `COMMAND WINDOW`, then
say **"capture"**, **"go"**, **"listen"**, or **"okay"** - all four trigger
the identical capture-and-upload sequence REC does (they're registered as
synonyms of the same command, not four different actions) - watch the AUD
row for the actual capture/upload progress, same as a manual press. If the
command window closes without a recognized phrase (silence, unrelated
speech, or the wrong words), VOICE reads `TIMEOUT/UNRECOGNIZED` and returns
to `LISTENING` on its own - nothing is captured. Manual REC keeps working at
any time, including while VOICE is in any state.

**Wake word and command:** stock WakeNet9 `wn9_computer_tts` ("computer",
`CONFIG_SR_WN_WN9_COMPUTER_TTS`) and MultiNet7 general English recognition
(`CONFIG_SR_MN_EN_MULTINET7_QUANT`). The accepted command phrases
(`"capture"`, `"go"`, `"listen"`, `"okay"` - see `voice_control.c`'s
`COMMAND_PHRASES`) are registered at runtime via `esp_mn_commands_add()`,
all under the same command ID (MultiNet supports multiple phrases per
command by design), not through menuconfig, so they stay visible in source.
Getting here took two real, hardware-confirmed dead ends, not guesswork:

1. `"start recording"` - MultiNet7's runtime grapheme-to-phoneme conversion
   (no precomputed phoneme column supplied) produced too inaccurate a
   phoneme sequence for a two-word phrase to ever match (printed as
   `STnRT RcKeRDgl` at boot, never once reached `ESP_MN_STATE_DETECTED`).
2. `"record"` - a single word, but still never matched even at a very
   permissive detection threshold (0.1). A diagnostic build that logged
   MultiNet's live raw-decoded phonemes during the command window (via
   `multinet->get_results()`, polled every ~500ms while
   `ESP_MN_STATE_DETECTING`) showed real speech consistently decoding as
   `RgKeR`/`RgKeRD` - close to, but not a structural match for, the
   registered `RfKkD`. MultiNet7's FST beam search
   (`ESP_MN_BEAM_SEARCH_WITH_FST`) requires the decoded path to align with
   the registered grammar, not just sound similar, so no threshold could
   have fixed this.

That same diagnostic build registered five unrelated control words
alongside "record" specifically to tell "this phrase is bad" apart from
"the whole pipeline is broken." Four of them (`"capture"`, `"go"`,
`"listen"`, `"okay"` - "yes" never showed up as recognized) produced real
`ESP_MN_STATE_DETECTED` hits in that session - proving the pipeline itself
works, and handing over four already-hardware-verified words instead of
another guess. Rather than pick just one, all four are kept as accepted
synonyms - useful in practice since Mike didn't reliably remember any
single command word on its own. `multinet->set_det_threshold(model_data,
0.1)` stays at the permissive value that session confirmed works; if any
of these trigger on unrelated speech during quiet-room validation, raise it
in `voice_control.c`. The on-screen/Black Box display name for the action
stays the single word `"capture"` regardless of which synonym was actually
spoken - see `voice_control.c`'s `COMMAND_TEXT_RECORD` and
`COMMAND_PHRASES`. The wake word is similarly a single config line,
`sdkconfig.bsp.esp-box-3`'s `CONFIG_SR_WN_WN9_COMPUTER_TTS`.

**Mic ownership:** the ES7210 codec is one physical device, and Mission 10's
bounded 4s capture and this mission's continuous WakeNet listening cannot
both read it at once. Exactly one `esp_codec_dev_handle_t` is created (in
`status_deck_ui.c`, once, via `bsp_audio_codec_microphone_init()`) and
shared between `audio_capture_init()` and `voice_control_init()`. Normally
`voice_control.c`'s feed task holds the codec open and reads continuously
for WakeNet/MultiNet; when any accepted command phrase is recognized, it closes its own
session, calls the same `audio_capture_start()` the REC button calls, waits
for that capture-and-upload to return to idle, then reopens the codec and
resumes listening. See the Mission 11 comment at the top of
`voice_control.c` for the full handoff sequence.

**Flash size:** Missions 01-10 had `CONFIG_ESPTOOLPY_FLASHSIZE_4MB` set,
which is too small for the ESP-SR model partition (the WakeNet + MultiNet
models need ~5MB in flash). This mission corrects it to 16MB - Espressif's
documented flash size for the ESP32-S3-BOX-3, paired with the 8MB
Octal-PSRAM module Mission 10 already confirmed. **Verify this against the
real board** (`esptool.py flash_id`, or the module part number on the
board) the first time you flash this mission - if this specific unit
genuinely has less flash, `partitions.csv` needs to shrink to match, not
the other way around.

## Run the backend (required before Mission 09's SND button or Mission 10's REC upload will work)

The backend uses this machine's existing global Python 3.13 install, which
already has `fastapi`/`uvicorn`/`pydantic` (see `backend/requirements.txt`
for the exact versions - no venv needed):

```
python backend/main.py
```

It binds `0.0.0.0:8000` (reachable from the ESP32 over the LAN, not just
this machine) and logs the LAN IP it's reachable at on startup - confirm
that matches `BACKEND_BASE_URL` in `main/backend_config.h`. Plain HTTP, no
TLS - a local learning setup only, not for anything beyond the LAN.

The first time it starts and accepts a connection from another device (the
ESP32), Windows Firewall may prompt to allow `python.exe` on **private**
networks - allow it, or the ESP32's requests (handshake or audio upload)
will silently fail to connect.

Verify it's up: `curl http://127.0.0.1:8000/health` should return
`{"status":"ok"}`. `curl http://127.0.0.1:8000/captures` lists any saved
audio captures as JSON.

## Wi-Fi credentials (required before building Mission 08+)

Copy `main/wifi_credentials.h.example` to `main/wifi_credentials.h` and fill
in your real Wi-Fi SSID and password:

```
cp main/wifi_credentials.h.example main/wifi_credentials.h
```

`main/wifi_credentials.h` is listed in `.gitignore` and is never committed -
unlike `sdkconfig`, which this project does track in git. The build fails
with a clear `#error` if this file is missing.

## Backend address (Mission 09+)

`main/backend_config.h` holds `BACKEND_BASE_URL`, the one place the ESP32's
backend address is configured. Unlike `wifi_credentials.h` it's a normal
tracked file (a LAN IP isn't a credential) - it's currently set to this
machine's Wi-Fi adapter IP as of Mission 09
(`ipconfig` -> "Wireless LAN adapter Wi-Fi" -> IPv4 Address). Update it
if that address ever changes.

## Build and Flash

```
idf.py -D SDKCONFIG_DEFAULTS=sdkconfig.bsp.esp-box-3 -p {port} flash monitor
```

Make sure the correct board name is set in the `main/idf_component.yml` file
under the `dependencies` section (defaults to `esp-box-3`).
