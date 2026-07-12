# Operation Homebound — Mission 05: The Status Deck

## Overview

Integrated command-and-telemetry deck for the ESP32-S3-BOX-3, built on the
BSP display/touch foundation from Mission 02 (`display`) and the LVGL touch
console from Mission 04 (`first_command`). A top status bar shows uptime,
FreeRTOS tick count, free heap and PSRAM. A state panel shows current mode,
last command, link status and diag status. Five touch buttons (ARM, PING,
DIAG, LINK, RESET) drive a single `app_state_t`, and a compact on-screen
event log shows the last few command events.

## Build and Flash

```
idf.py -D SDKCONFIG_DEFAULTS=sdkconfig.bsp.esp-box-3 -p {port} flash monitor
```

Make sure the correct board name is set in the `main/idf_component.yml` file
under the `dependencies` section (defaults to `esp-box-3`).
