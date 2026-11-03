#!/usr/bin/env bash
# Compile chosen main/*.c files with the firmware's own toolchain and flags -
# seconds, not a full build. Use it after a render looks right and before the
# full ESP-IDF build: the host simulator compiles with the PC's gcc, which does
# not report what ESP-IDF's xtensa gcc 14 does (e.g. -Werror=format-truncation
# on a snprintf buffer), so a page can render cleanly and still fail the build.
#
#   bash tools/uisim/fw_compile.sh main/ui_survey.c main/ui_sensors.c
#
# Needs a configured build directory (one full build, or `idf.py reconfigure`)
# in this checkout. Runs nice/ionice, one job.
set -uo pipefail
cd "$(dirname "$0")/../.."
[ -f build/build.ninja ] || { echo "fw_compile: no build/build.ninja - run one full build or idf.py reconfigure first" >&2; exit 2; }
export IDF_TOOLS_PATH=${IDF_TOOLS_PATH:-/opt/workstation/.espressif}
export IDF_PATH=${IDF_PATH:-/opt/workstation/esp-idf}
. "$IDF_PATH/export.sh" > /dev/null 2>&1
rc=0
for f in "$@"; do
    obj="esp-idf/main/CMakeFiles/__idf_main.dir/$(basename "$f").obj"
    rm -f "build/$obj"   # always recompile: the point is to see its warnings
    # (kconfgen's notices about sdkconfig.defaults are long-standing and not about $f)
    if out=$(nice -n 19 ionice -c3 ninja -C build -j1 "$obj" 2>&1 | grep -v -e kconfgen -e "unknown kconfig symbol"; exit "${PIPESTATUS[0]}"); then
        warn=$(grep -c "warning:" <<< "$out")
        echo "OK   $f${warn:+ ($warn warning line(s))}"
        [ "$warn" != "0" ] && grep "warning:" <<< "$out" | head -5
    else
        echo "FAIL $f"
        grep -E "error:|warning:" <<< "$out" | head -10
        rc=1
    fi
done
exit $rc
