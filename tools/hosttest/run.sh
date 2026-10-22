#!/bin/sh
# Host test: compiles the REAL main/report_client.c, main/diag.c and
# main/ui_diag_format.c with Linux gcc against stubs (stubs/host_stubs.h, host_stubs.c) and runs them
# under AddressSanitizer + UBSan. xTaskGetHandle() is stubbed with the same
# configASSERT as ESP-IDF's FreeRTOS, so the 1.2.1 crash reproduces on the
# host:  ./run.sh 1.2.1   -> aborts with "strlen(name) < configMAX_TASK_NAME_LEN"
#        ./run.sh         -> the working tree: must print ALL PASS
# Needs gcc and the ESP-IDF tree (for cJSON) at $IDF_PATH or /opt/workstation/esp-idf.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
IDF=${IDF_PATH:-/opt/workstation/esp-idf}
OUT=${TMPDIR:-/tmp}/ws-hosttest.$$
mkdir -p "$OUT"
SRC="$REPO/main"
DEF=""
if [ "$1" = "1.2.1" ]; then
    git -C "$REPO" show ebdb282:main/diag.c > "$OUT/diag.c"
    git -C "$REPO" show ebdb282:main/diag.h > "$OUT/diag.h"
    git -C "$REPO" show ebdb282:main/report_client.c > "$OUT/report_client.c"
    for f in report_client.h backend_config.h backend_health.h device_config.h firmware_identity.h \
             lvgl_psram_alloc.h ota_service.h wifi_manager.h; do cp "$SRC/$f" "$OUT/"; done
    SRC="$OUT"; DEF="-DHOST_1_2_1"
fi
gcc -std=gnu11 -g -O1 -Wall -Wno-unused-function -fsanitize=address,undefined -fno-omit-frame-pointer $DEF \
   \
    -I"$HERE/stubs" -I"$SRC" -I"$IDF/components/json/cJSON" \
    "$HERE/test_main.c" "$HERE/host_stubs.c" "$IDF/components/json/cJSON/cJSON.c" \
    -x c "$HERE/diag_wrap.c" -x c "$HERE/rc_wrap.c" \
    -iquote "$SRC" -o "$OUT/hosttest"
# DIAGNOSTICS page model (main/ui_diag_format.c): plain C, same sanitizers,
# plus the firmware's -Werror=format-truncation at -O2.
gcc -std=gnu11 -g -O2 -Wall -Wextra -Werror=format-truncation -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -I"$REPO/main" "$HERE/test_diag_format.c" "$REPO/main/ui_diag_format.c" -o "$OUT/diagformat"
"$OUT/diagformat"
# SENSORS page model (main/ui_sensor_model.c): same flags.
gcc -std=gnu11 -g -O2 -Wall -Wextra -Werror=format-truncation -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -I"$REPO/main" "$HERE/test_sensor_model.c" "$REPO/main/ui_sensor_model.c" -o "$OUT/sensormodel"
"$OUT/sensormodel"
# diag_init() runs once per boot on the device; the test boots it 4 times,
# so its one PSRAM copy of last_words per boot "leaks" by design here.
ASAN_OPTIONS=detect_leaks=0 "$OUT/hosttest"
