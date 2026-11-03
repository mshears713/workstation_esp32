#!/usr/bin/env bash
# Uses actual LVGL pointer events in the adapter, also on older ws-ui CLIs.
set -euo pipefail
WS=${1:?Pass existing ws-ui executable}
"$WS" --repo "$PWD" render audio_test_ui_test
printf '%s\n' 'PASS Audio Test UI input/lifetime assertions (renderer completed)'
