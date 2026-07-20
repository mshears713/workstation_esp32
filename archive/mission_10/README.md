# Operation Homebound — Workstation Console

## Lineage

This project is updated in place, mission by mission - the working tree
always reflects the newest mission, not a growing pile of sibling folders.
Each mission's pre-next-mission source snapshot is kept under `archive/`
instead:

- `archive/mission_09/` — the Earthside Handshake source as it stood right
  before Mission 10 (Capture the Transmission) was added. (Wi-Fi credentials
  are never archived - see `.gitignore`.)

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
