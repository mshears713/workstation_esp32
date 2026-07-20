# Operation Homebound — Workstation Console

## Overview

Integrated command-and-telemetry deck for the ESP32-S3-BOX-3, built on the
BSP display/touch foundation from Mission 02 (`display`) and the LVGL touch
console from Mission 04 (`first_command`). A top status bar shows uptime,
FreeRTOS tick count, free heap and PSRAM. A state panel shows current mode,
last command, link status and diag status. Seven touch buttons (ARM, PING,
DIAG, LINK, NET, SND, CLR) drive a single `app_state_t`, and a compact
on-screen event log shows the last few command events.

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

## Run the backend (required before Mission 09's SND button will work)

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
networks - allow it, or the ESP32's requests will silently fail to connect.

Verify it's up: `curl http://127.0.0.1:8000/health` should return
`{"status":"ok"}`.

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
