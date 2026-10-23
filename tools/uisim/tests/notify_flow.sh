#!/usr/bin/env bash
# Interaction tests for NOTIFY (ui_notify.c): browsing the stack, PLAY only on
# the front card and only when CLAWBOX is reachable, STOP only while playing.
#   bash tools/uisim/tests/notify_flow.sh [ws-ui] [evidence-dir]
set -uo pipefail
WS=${1:-/opt/workstation/lvgl-sim/bin/ws-ui}
OUT=${2:-}
REPO=$(pwd)
fail=0
PREV=44,217; NEXT=276,217; PLAY=160,217; STOP=160,214

case_() {
    local name=$1 want=$2 never=$3; shift 3
    local res; res=$("$WS" --repo "$REPO" render notify --json ${OUT:+--out "$OUT/$name"} "$@")
    local texts; texts=$(python3 -c '
import json, sys
r = json.loads(sys.argv[1])
if not r["ok"]:
    print("RENDER FAILED", r["summary"]); sys.exit(0)
print("\n".join(n["text"] for n in json.load(open(r["dir"] + "/tree.json")) if n.get("text") and not n["hidden"] or n.get("text") in ("no tap","PLAY PRESSED","STOP PRESSED")))' "$res")
    local ok=1
    IFS='|' read -ra W <<< "$want"
    for w in "${W[@]}"; do grep -qF -- "$w" <<< "$texts" || { ok=0; echo "   missing: $w"; }; done
    if [ -n "$never" ]; then
        IFS='|' read -ra N <<< "$never"
        for n in "${N[@]}"; do grep -qF -- "$n" <<< "$texts" && { ok=0; echo "   unexpected: $n"; }; done
    fi
    if [ $ok = 1 ]; then echo "PASS $name"; else echo "FAIL $name"; fail=1; fi
}

case_ play_front "PLAY PRESSED" "" --tap $PLAY@300
case_ no_play_when_down "no tap|may be old" "PLAY PRESSED" --set link=down --tap $PLAY@300
case_ next_shows_queue "+2 more waiting|2 / 4" "New message" --set pending=3 --tap $NEXT@300
case_ queue_card_has_no_play "no tap" "PLAY PRESSED" --set pending=3 --tap $NEXT@300 --tap $PLAY@800
case_ earlier_outcomes "Played, not acknowledged|3 / 4" "" --set pending=3 --tap $NEXT@300 --tap $NEXT@700
case_ last_card_stops "Delivered|4 / 4" "" --set pending=3 --tap $NEXT@300 --tap $NEXT@700 --tap $NEXT@1100 --tap $NEXT@1500
case_ prev_returns "New message" "+2 more waiting" --set pending=3 --tap $NEXT@300 --tap $PREV@700
case_ stop_while_playing "STOP PRESSED" "" --set view=overlay --set phase=playing --tap $STOP@300
case_ no_stop_while_fetching "no tap" "STOP PRESSED" --set view=overlay --set phase=fetching --tap $STOP@300
case_ ack_failed_is_not_delivered "ack failed" "Delivered: played and acknowledged" --set view=overlay --set phase=done \
    --set outcome=ack_failed
exit $fail
