#!/usr/bin/env bash
# Interaction tests for the recording overlay (ui_recording.c): which controls
# exist in which phase, and that taps reach their handlers. The adapter writes
# "CANCEL PRESSED" / "SEND PRESSED" / "PREV PRESSED" into a hidden label.
#   bash tools/uisim/tests/recording_flow.sh [ws-ui] [evidence-dir]
set -uo pipefail
WS=${1:-/opt/workstation/lvgl-sim/bin/ws-ui}
OUT=${2:-}
REPO=$(pwd)
fail=0
CANCEL=116,212; SEND=252,212; PREV=76,162

case_() {
    local name=$1 want=$2 never=$3; shift 3
    local res; res=$("$WS" --repo "$REPO" render recording --json ${OUT:+--out "$OUT/$name"} "$@")
    local texts; texts=$(python3 -c '
import json, sys
r = json.loads(sys.argv[1])
if not r["ok"]:
    print("RENDER FAILED", r["summary"]); sys.exit(0)
print("\n".join(n["text"] for n in json.load(open(r["dir"] + "/tree.json")) if n.get("text")))' "$res")
    local ok=1
    IFS='|' read -ra W <<< "$want"
    for w in "${W[@]}"; do grep -qF -- "$w" <<< "$texts" || { ok=0; echo "   missing: $w"; }; done
    if [ -n "$never" ]; then
        IFS='|' read -ra N <<< "$never"
        for n in "${N[@]}"; do grep -qF -- "$n" <<< "$texts" && { ok=0; echo "   unexpected: $n"; }; done
    fi
    if [ $ok = 1 ]; then echo "PASS $name"; else echo "FAIL $name"; fail=1; fi
}

case_ cancel_while_recording "CANCEL PRESSED" "" --tap $CANCEL@300
case_ cancel_while_arming "CANCEL PRESSED" "" --set phase=arming --tap $CANCEL@300
case_ no_cancel_before_start "no tap" "CANCEL PRESSED" --set phase=starting --tap $CANCEL@300
case_ no_cancel_while_uploading "no tap" "CANCEL PRESSED" --set phase=uploading --tap $CANCEL@300
case_ no_cancel_on_result "no tap" "CANCEL PRESSED" --set phase=result --set result=SENT --tap $CANCEL@300
case_ send_finishes_note "SEND PRESSED" "" --set command=NOTE --set target_ms=1200000 --tap $SEND@300
case_ no_send_on_bounded "no tap" "SEND PRESSED" --tap $SEND@300
case_ chooser_on_note "PREV PRESSED" "" --set command=NOTE --set target_ms=1200000 --set selector=GEN --tap $PREV@300
case_ chooser_gone_after "no tap" "PREV PRESSED" --set command=NOTE --set phase=uploading --set selector=GEN --tap $PREV@300
case_ rail_is_not_an_exit "no tap" "CANCEL PRESSED|SEND PRESSED" --tap 22,30@300 --tap 22,150@700
exit $fail
