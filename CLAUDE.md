# Workstation firmware: agent notes

ESP32-S3-BOX-3 console firmware (ESP-IDF 5.5.3, esp-box-3 BSP, LVGL 9.5.0). The operational source
of truth is the AI-OS page **"WORKSTATION on CLAWBOX — how agents operate the device"**. This file
holds only the rules every session here needs. `README.md` has the product and build history.

## Where

- On CLAWBOX, **`/opt/workstation/repo` is the only clone that builds and deploys** (the `workstation`
  CLI hard-codes it; it holds the git-ignored `main/wifi_credentials.h`). It is owned by `agent`, so
  run git and `workstation` there as `sudo -n -u agent …`. `/home/agent/.openclaw/workspace/workstation_esp32`
  is **stale. Do not use it.**
- Someone else may be mid-change in `/opt/workstation/repo`. Check `git status` / branch first. For
  experiments, work in your own clone or branch.

## Build → publish → deploy are separate verbs

`workstation build`, `workstation publish`, `workstation deploy <version>`, `workstation status`,
`workstation doctor` (read-only). A commit or build never reaches the hardware; only `deploy` does,
and only when Mike asks. The device polls every 60 s and must pass a 90 s health gate or it rolls back.

## Evidence

1. **A render is not a build, a build is not the device.** Working means the boot shows the expected
   `fw_id`, `workstation status` shows the version/slot/valid, and the feature was exercised on the BOX-3.
2. Say which level you actually reached: rendered / built / deployed and seen on hardware.

## Load-bearing constraints (see the AI-OS page for why)

- **Internal RAM is the ceiling, not flash.** Min-free has hit 1.2 KB (PANIC). OTA needs ~7 KB
  contiguous. Watch the `status_deck: UI built: internal heap … PSRAM …` line. LVGL allocates from
  PSRAM (`lvgl_psram_alloc.c`); the 32 KB draw buffer is internal. Do not grow it.
- `ota_service_start()` stays **before** `bsp_display_start()` in `main.c`.
- Keep `MBEDTLS_EXTERNAL_MEM_ALLOC=y` and hardware AES off; `BACKEND_BASE_URL` is a function call;
  the version string `<semver>+<sha>[-dirty]` is a contract; don't casually add health-gate conditions.
- Automatic work that runs "once the backend answers" must not be able to crash-loop ahead of the
  OTA check (the 1.2.1 lesson).

## UI work

Use the **`workstation-ui` skill** (`.claude/skills/workstation-ui/`) and the **`workstation-ui`
MCP renderer** (`.mcp.json` → `/opt/workstation/lvgl-sim`, on demand, no daemon). UI builders in
`main/ui_*.c` are pure LVGL so the simulator can build the same code: `tools/uisim/README.md`.
Mike's evolving taste lives in the AI-OS page **"Workstation UI — Design Preferences & Experiments"**.

## Host tests

`tools/hosttest/run.sh` must print ALL PASS (report/diag code under ASan/UBSan).
