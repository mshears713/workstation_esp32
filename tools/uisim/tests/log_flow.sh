#!/usr/bin/env bash
# Interaction tests for LOG paging (ui_log.c) through the real handlers.
#   bash tools/uisim/tests/log_flow.sh [ws-ui] [evidence-dir]
# Asserts on the position text ("A-B of N") and the NEW key's label.
set -uo pipefail
WS=${1:-/opt/workstation/lvgl-sim/bin/ws-ui}
OUT=${2:-}
REPO=$(pwd)
fail=0
NEW=40,214; OLD=116,214

case_() {
    local name=$1 want=$2 never=$3; shift 3
    local res; res=$("$WS" --repo "$REPO" render log --json ${OUT:+--out "$OUT/$name"} "$@")
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

case_ first_page_newest "1-5 of 32, since boot|REPORT SENT #14" ""
case_ newer_does_nothing_on_top "1-5 of 32" "" --tap $NEW@300
case_ older_pages_down "of 32|SENSOR INIT" "REPORT SENT #14" --tap $OLD@300 --tap $OLD@700 --tap $OLD@1100 --tap $OLD@1500 --tap $OLD@1900
case_ every_line_reachable "BOOT #212|32 of 32" "" --tap $OLD@300 --tap $OLD@700 --tap $OLD@1100 --tap $OLD@1500 \
    --tap $OLD@1900 --tap $OLD@2300 --tap $OLD@2700
case_ newer_comes_back "1-5 of 32|REPORT SENT #14" "" --tap $OLD@300 --tap $OLD@700 --tap $NEW@1100 --tap $NEW@1500
# Paged back when 3 events arrive: the same lines stay on screen (offset moves by 3)
# and the NEW key counts them; going back to the top shows them.
case_ paged_back_keeps_place "3 NEW|9-15 of 32|! API LOST" "REPORT SENT #14" --advance-ms 1500 --set seq_bump=3 \
    --set bump_at_ms=900 --tap $OLD@300
case_ top_shows_arrivals "1-5 of 32|REPORT SENT #14" "NEW 3" --set seq_bump=3 --set bump_at_ms=900 \
    --tap $OLD@300 --tap $NEW@1300
case_ on_top_arrivals_just_appear "1-5 of 32|REPORT SENT #14" "3 NEW" --advance-ms 1200 --set seq_bump=3 \
    --set bump_at_ms=500
exit $fail
