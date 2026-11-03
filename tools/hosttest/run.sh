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
# LOG page model (main/ui_log_format.c): same flags.
gcc -std=gnu11 -g -O2 -Wall -Wextra -Werror=format-truncation -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -I"$REPO/main" "$HERE/test_log_format.c" "$REPO/main/ui_log_format.c" -o "$OUT/logformat"
"$OUT/logformat"
# FIRMWARE timeline model (main/ui_fw_timeline.c): same flags, and the
# ota_service.c messages it places failures by must still exist.
gcc -std=gnu11 -g -O2 -Wall -Wextra -Werror=format-truncation -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -I"$REPO/main" "$HERE/test_fw_timeline.c" "$REPO/main/ui_fw_timeline.c" -o "$OUT/fwtimeline"
"$OUT/fwtimeline"
for m in '"image %lu > slot' '"cannot fetch image"' '"out of memory"' '"hash mismatch' '"unhealthy (' \
         '"image marked valid"' '"up to date (' '"no manifest from CLAWBOX"'; do
    grep -qF "$m" "$REPO/main/ota_service.c" || { echo "FW TIMELINE FAIL: ota_service.c no longer says $m"; exit 1; }
done
echo "FW TIMELINE: ota_service.c messages present"
# notification_client.c's json_string_field() is static: copy its exact text out.
sed -n '/^static bool json_string_field(/,/^}/p' "$REPO/main/notification_client.c" > "$OUT/json_string_field.inc"
gcc -std=gnu11 -g -O2 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -fno-omit-frame-pointer -I"$OUT" "$HERE/test_notify_text.c" -o "$OUT/notifytext"
"$OUT/notifytext"
# diag_init() runs once per boot on the device; the test boots it 4 times,
# so its one PSRAM copy of last_words per boot "leaks" by design here.
ASAN_OPTIONS=detect_leaks=0 "$OUT/hosttest"
