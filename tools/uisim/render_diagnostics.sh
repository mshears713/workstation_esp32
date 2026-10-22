#!/usr/bin/env bash
# Render MENU + every Diagnostics fixture, serially, with durable evidence.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
WS=("${WS_UI_BIN:-/opt/workstation/lvgl-sim/bin/ws-ui}" --repo "$REPO")
OUT="${1:-$REPO/diagnostics-review/renders}"
CACHE="${HOME}/.cache/workstation-ui/renders"
mkdir -p "$OUT"
: > "$OUT/SUMMARY.txt"
IDS=()
grab() {
    local name=$1 output id
    shift
    if ! output=$("${WS[@]}" "$@" 2>&1); then
        printf 'Render failed: %s\n%s\n' "$name" "$output" >&2
        return 1
    fi
    id=$(printf '%s\n' "$output" | sed -n 's/^render \([^: ]*\):.*/\1/p')
    if [[ -z "$id" || "$id" == *$'\n'* || ! -f "$CACHE/$id/view.png" ]]; then
        printf 'No unique render artifact for %s:\n%s\n' "$name" "$output" >&2
        return 1
    fi
    printf '%s  %s\n' "$name" "$output" >> "$OUT/SUMMARY.txt"
    cp "$CACHE/$id/view.png" "$OUT/$name.png"
    cp "$CACHE/$id/screen.png" "$OUT/$name.native.png"
    "${WS[@]}" check "$id" > "$OUT/$name.checks.txt" 2>&1
    IDS+=("$id")
}
grab menu render menu
fixtures=$(python3 - "$REPO/tools/uisim/targets.json" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    print('\n'.join(json.load(f)['targets']['diagnostics']['fixtures']))
PY
)
while IFS= read -r fixture; do
    [[ -n "$fixture" ]] && grab "diagnostics.$fixture" render diagnostics --fixture "$fixture"
done <<< "$fixtures"
for ((i=0; i<${#IDS[@]}; i+=4)); do
    batch=("${IDS[@]:i:4}")
    if ((${#batch[@]} >= 2)); then
        "${WS[@]}" compare "${batch[@]}" --out "$OUT/sheet-$((i/4+1)).png" > /dev/null
    fi
done
printf 'Rendered %s states; inspect images and checks in %s\n' "${#IDS[@]}" "$OUT"
