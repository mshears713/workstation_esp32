#!/usr/bin/env bash
# Interaction tests for the music remote (ui_music.c + ui_music_mock.c),
# driven through the real LVGL event handlers with simulated taps. Each case
# renders, then asserts on the widget tree (the texts on screen at the end).
# trace=true makes the adapter list every command the callback received
# ("cmds:next,vol_up"), so a case can prove which handler fired - or that
# none did. Run from the repo root:
#   bash tools/uisim/tests/music_flow.sh [ws-ui] [evidence-dir]
# Coordinates are native 320x240 px, taken from ui_music.c's layout.
set -uo pipefail
WS=${1:-/opt/workstation/lvgl-sim/bin/ws-ui}
OUT=${2:-}
REPO=$(pwd)
fail=0

HOME_KEY=58,26; PREV=38,186; PLAY=118,186; NEXT=198,186; VOL_UP=277,88; VOL_DOWN=277,210

# case NAME "expected text|another" "forbidden text" ws-ui-args...
case_() {
    local name=$1 want=$2 never=$3; shift 3
    local res; res=$("$WS" --repo "$REPO" render music --json --set trace=true ${OUT:+--out "$OUT/$name"} "$@")
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
    if [ $ok = 1 ]; then echo "PASS $name"; else echo "FAIL $name"; echo "$texts" | sed 's/^/   | /'; fail=1; fi
}

# 1. Play/pause reaches the handler; the mock answers after 400 ms; paused -> playing.
case_ play_toggles "cmds:play_pause|Playing|Play/pause (simulated)" "Paused" \
    --tap $PLAY@300 --frames 200,500,900 --advance-ms 1200

# 2. Pending shows while the answer is out (slow mock): status line + the command name.
case_ pending_shows "cmds:next|Next (simulated)..." "" \
    --set ack_ms=5000 --tap $NEXT@300 --advance-ms 1000

# 3. Taps while a command is out are dropped: one Next, no second Next, no play/pause.
case_ no_queue_while_pending "cmds:next" "next,next|play_pause" \
    --set ack_ms=5000 --tap $NEXT@300 --tap $NEXT@500 --tap $PLAY@700 --advance-ms 1000

# 4. Prev and Next each reach their own handler.
case_ prev_then_next "cmds:prev,next|Next (simulated)" "" \
    --tap $PREV@300 --tap $NEXT@1000 --advance-ms 1800

# 5. Volume down steps 40 -> 35 and says which volume it is.
case_ volume_down_step "cmds:vol_down|35%|Laptop volume|Volume down (simulated)" "40%" \
    --tap $VOL_DOWN@300 --advance-ms 1000

# 6. Upper limit: 95 + 5 = 100, then + is refused (no second vol_up, never 105).
case_ volume_upper_limit "cmds:vol_up|100%" "vol_up,vol_up|105%" \
    --set volume=95 --tap $VOL_UP@300 --tap $VOL_UP@1200 --advance-ms 1800

# 7. Lower limit: at 0, - does nothing at all.
case_ volume_lower_limit "0%" "vol_down|-5%" --set volume=0 --tap $VOL_DOWN@300 --advance-ms 1000

# 8. A failure is shown as a failure, and the state does not change.
case_ failure_honest "cmds:next|Next failed" "Next (simulated)" \
    --set outcome=fail --set play=playing --tap $NEXT@300 --advance-ms 1000

# 9. No answer: after timeout_ms the screen says so, and play state stays Paused.
case_ timeout_honest "cmds:play_pause|Play/pause: no answer|Paused" "Playing" \
    --set outcome=silent --set timeout_ms=1000 --tap $PLAY@300 --advance-ms 2000

# 10. Mock mode never says CONNECTED, even when seeded with link=connected and after a command.
case_ mock_never_connected "SIMULATED|cmds:next" "CONNECTED" \
    --set link=connected --tap $NEXT@300 --advance-ms 1000

# 11. Disconnected: keys are off - no command reaches the callback.
case_ disconnected_keys_off "NOT CONNECTED|Laptop not reachable|cmds:" "prev|play_pause|next|vol_" \
    --set mock=false --set link=disconnected --set play=unknown --set volume=-1 \
    --tap $PREV@300 --tap $PLAY@500 --tap $NEXT@700 --tap $VOL_UP@900 --tap $VOL_DOWN@1100

# 12. Connecting (unknown): same, keys off.
case_ connecting_keys_off "CONNECTING|Looking for the laptop...|cmds:" "play_pause|next|vol_" \
    --set mock=false --set link=unknown --tap $PLAY@300 --tap $NEXT@500 --tap $VOL_UP@700

# 13. Real-bridge mode: the tap reaches the callback, but with nothing answering
#     the screen does not invent a result.
case_ connected_no_fake_result "CONNECTED|cmds:next" "Next sent|SIMULATED" \
    --set mock=false --set link=connected --tap $NEXT@300 --advance-ms 1000

# 14. "< HOME" leaves (exit callback closes the remote).
case_ home_exits "MUSIC CLOSED" "SIMULATED" --tap $HOME_KEY@300 --advance-ms 600

# 15. A near-miss below "< HOME" (on the rule / track text) does not leave and sends nothing.
case_ near_miss_stays "SIMULATED|cmds:" "MUSIC CLOSED|play_pause|next|prev" --tap 58,60@300 --tap 120,100@600

exit $fail
