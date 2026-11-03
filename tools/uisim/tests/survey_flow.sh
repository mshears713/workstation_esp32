#!/usr/bin/env bash
# Interaction tests for the survey (ui_survey.c), driven through the real LVGL
# event handlers with simulated taps. Each case renders, then asserts on the
# widget tree (the texts on screen at the end). Run from the repo root:
#   bash tools/uisim/tests/survey_flow.sh [ws-ui] [evidence-dir]
# Coordinates are native 320x240 px, taken from ui_survey.c's layout.
set -uo pipefail
WS=${1:-/opt/workstation/lvgl-sim/bin/ws-ui}
OUT=${2:-}
REPO=$(pwd)
fail=0

START=160,186; TILE4=219,130; NEXT=224,208; BACK=72,208
PLUS=278,158; MINUS=42,158; PHONE=160,140; SUBMIT=224,208; NEXT_VISITOR=94,208

# case NAME "expected text|another" "forbidden text" ws-ui-args...
case_() {
    local name=$1 want=$2 never=$3; shift 3
    local res; res=$("$WS" --repo "$REPO" render survey --json ${OUT:+--out "$OUT/$name"} "$@")
    local texts; texts=$(python3 -c '
import json, sys
r = json.loads(sys.argv[1])
if not r["ok"]:
    print("RENDER FAILED", r["summary"]); sys.exit(0)
tree = json.load(open(r["dir"] + "/tree.json"))
print("\n".join(n["text"] for n in tree if n.get("text")))' "$res")
    local ok=1
    IFS='|' read -ra W <<< "$want"
    for w in "${W[@]}"; do grep -qF -- "$w" <<< "$texts" || { ok=0; echo "   missing: $w"; }; done
    if [ -n "$never" ]; then
        IFS='|' read -ra N <<< "$never"
        for n in "${N[@]}"; do grep -qF -- "$n" <<< "$texts" && { ok=0; echo "   unexpected: $n"; }; done
    fi
    if [ $ok = 1 ]; then echo "PASS $name"; else echo "FAIL $name"; fail=1; fi
}

# 1. The whole visitor flow: Start, rate 4, slider + twice (lands mid at 5, then 6),
#    pick "Phone app", review, submit -> thank-you with response #12.
case_ full_flow "Thanks!|Response #12." "Check your answers" \
    --tap $START@200 --tap $TILE4@600 --tap $NEXT@1000 \
    --tap $PLUS@1400 --tap $PLUS@1800 --tap $NEXT@2200 \
    --tap $PHONE@2600 --tap $NEXT@3000 --tap $SUBMIT@3400 --frames 100,500,900,1300,1700,2100,2500,2900,3300,3800

# 2. Next does nothing until the question is answered.
case_ next_blocked_until_answered "Question 1 of 3" "Question 2 of 3" --set step=q1 --tap $NEXT@300

# 3. Back keeps answers: from Q2 (answered 6) back to Q1 (4 stays selected), then forward again.
case_ back_keeps_answers "Question 2 of 3|6" "" --set step=q2 --set a1=4 --set a2=6 \
    --tap $BACK@300 --tap $NEXT@800

# 4. The review shows what was answered, and the row edits that question.
case_ review_edit_row "Question 2 of 3|7" "" --set step=review --set a1=4 --set a2=7 --set a3=1 --tap 160,116@300

# 5. Double tap on Submit stores ONE response (#12, not #13).
case_ no_double_submit "Response #12." "Response #13." --set step=review --set a1=4 --set a2=7 --set a3=1 \
    --tap $SUBMIT@300 --tap $SUBMIT@450 --tap $SUBMIT@700

# 6. A refused save stays on review with the notice, and nothing claims success.
case_ save_failure_honest "Not saved. Try again.|Submit" "Thanks!" --set step=review --set a1=4 --set a2=7 \
    --set a3=1 --set save_fails=true --tap $SUBMIT@300

# 7. Slider limits: at 10, + does nothing; - steps down.
case_ slider_limits "9" "10|11" --set step=q2 --set a1=4 --set a2=10 --tap $PLUS@300 --tap $MINUS@700

# 8. Thank-you resets itself and clears answers (back to welcome; Q1 shows nothing chosen).
case_ thanks_auto_reset "Got 30 seconds?" "Thanks!" --set step=thanks --set a1=4 --set a2=7 --set a3=1 \
    --set thanks_reset_s=2 --advance-ms 3500

# 9. Next visitor clears the answers: Start again, Next is still blocked at Q1.
case_ next_visitor_clears "Question 1 of 3" "Question 2 of 3" --set step=thanks --set a1=4 --set a2=7 --set a3=1 \
    --tap $NEXT_VISITOR@300 --tap $START@800 --tap $NEXT@1300

# 10. Walk-away: mid-survey with no touch for idle_reset_s -> welcome.
case_ idle_reset "Got 30 seconds?" "Question 2 of 3" --set step=q2 --set a1=4 --set idle_reset_s=2 --advance-ms 3500

# 11. Organiser exit: a 3.2 s hold on the top bar leaves; a quick tap there does not.
printf '300 down 60 20\n3500 up\n' > /tmp/survey_hold.txt
case_ hold_to_exit "SURVEY EXITED" "Got 30 seconds?" --input /tmp/survey_hold.txt
case_ tap_top_bar_stays "Got 30 seconds?" "SURVEY EXITED" --tap 60,20@300

exit $fail
