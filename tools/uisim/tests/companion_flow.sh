#!/usr/bin/env bash
# Interaction tests for the Friendly Companion eyes (ui_companion.c), driven
# through the real LVGL event handlers with simulated touch. The eyes are
# drawn, not widgets, so each case turns on the adapter's test-only state line
# ("mood=... react=... key=...") and asserts on it plus the texts on screen.
# Run from the repo root:
#   bash tools/uisim/tests/companion_flow.sh [ws-ui] [evidence-dir]
# Coordinates are native 320x240 px, from ui_companion.c (corner zone 76x68,
# HOME key at 8,8 104x48).
set -uo pipefail
WS=${1:-/opt/workstation/lvgl-sim/bin/ws-ui}
OUT=${2:-}
REPO=$(pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0

FACE=200,110; CORNER=30,30; KEY=60,32

# case NAME "expected text|another" "forbidden text" ws-ui-args...
case_() {
    local name=$1 want=$2 never=$3; shift 3
    local res; res=$("$WS" --repo "$REPO" render companion --json --set show_state=true \
        ${OUT:+--out "$OUT/$name"} "$@")
    local texts; texts=$(python3 -c '
import json, sys
r = json.loads(sys.argv[1])
if not r["ok"]:
    print("RENDER FAILED", r["summary"]); sys.exit(0)
tree = json.load(open(r["dir"] + "/tree.json"))
print("\n".join(n["text"] for n in tree if n.get("text") and not n.get("hidden")))' "$res")
    local ok=1
    IFS='|' read -ra W <<< "$want"
    for w in "${W[@]}"; do grep -qF -- "$w" <<< "$texts" || { ok=0; echo "   missing: $w"; }; done
    if [ -n "$never" ]; then
        IFS='|' read -ra N <<< "$never"
        for n in "${N[@]}"; do grep -qF -- "$n" <<< "$texts" && { ok=0; echo "   unexpected: $n"; }; done
    fi
    if [ $ok = 1 ]; then echo "PASS $name"; else echo "FAIL $name"; echo "$texts" | sed 's/^/   saw: /'; fail=1; fi
}

# 0. Left alone, nothing is playing and no key is showing.
case_ idle_untouched "mood=neutral react=none key=hidden" "HOME" --advance-ms 1500

# 1. A tap on the face plays BOOP (it reacts on touch, still playing just after release).
case_ tap_boops "react=boop" "EXITED" --tap $FACE@800

# 2. ... and the face returns to its mood afterwards.
case_ tap_then_settles "mood=neutral react=none" "" --tap $FACE@500 --advance-ms 3500

# 3. Holding still on the face (> 0.4 s) laughs.
printf '500 down 160 110\n1300 up\n' > "$TMP/hold.txt"
case_ hold_laughs "react=laugh" "" --input "$TMP/hold.txt"

# 4. A drag is not a hold: the eyes follow, no laugh.
printf '500 down 160 110\n560 move 200 110\n620 move 250 120\n700 move 300 130\n1300 move 300 130\n1400 up\n' > "$TMP/drag.txt"
case_ drag_no_laugh "mood=neutral" "react=laugh" --input "$TMP/drag.txt"

# 5. Four quick taps: "too much!" -> CONFUSED.
case_ four_taps_confused "react=confused" "" --tap $FACE@300 --tap 120,120@600 --tap $FACE@900 --tap 140,100@1200

# 6. Three taps are still just boops.
case_ three_taps_boop "react=boop" "react=confused" --tap $FACE@300 --tap 120,120@600 --tap $FACE@900

# 7. One tap in the top-left corner only reveals the HOME key; it does not leave.
case_ corner_reveals_key "HOME|key=shown" "EXITED" --tap $CORNER@500

# 8. A stray tap exactly where the key would be (key hidden) does not leave either.
case_ key_spot_tap_stays "key=shown" "EXITED" --tap $KEY@500

# 9. Revealed key, then tap it: the exit callback fires and the face is gone.
case_ key_exits "COMPANION EXITED|closed" "mood=" --tap $CORNER@500 --tap $KEY@1500

# 10. The key hides itself after 4 s.
case_ key_hides "key=hidden" "HOME" --tap $CORNER@500 --advance-ms 5000

# 11. No touch for doze_s: falls asleep. A tap wakes it (SURPRISED, back to its mood).
case_ dozes "mood=asleep" "" --set doze_s=2 --advance-ms 2600
case_ tap_wakes "mood=neutral react=surprised" "mood=asleep" --set doze_s=2 --tap $FACE@2800

# 12. Holding to wake does not also laugh.
printf '2800 down 160 110\n3600 up\n' > "$TMP/wakehold.txt"
case_ wake_hold_no_laugh "mood=neutral" "react=laugh" --set doze_s=2 --input "$TMP/wakehold.txt"

# 13. Status hook: the app sets WORRIED with a sentence mid-run.
case_ status_hook "mood=worried|Backend is down." "" --set mood2=worried --set mood2_at_ms=500 \
    --set "say2=Backend is down." --advance-ms 1000

exit $fail
