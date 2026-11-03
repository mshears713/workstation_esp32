#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT INT TERM
flags='-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
cc $flags -I"$repo/main" "$repo/main/newton_scene.c" "$repo/tools/hosttest/newton_scene_test.c" -o "$out/newton"
"$out/newton"
cc $flags -I"$repo/tools/hosttest/newton_stubs" -I"$repo/tools/hosttest/audio_stubs" -I"$repo/main" "$repo/main/newton_store.c" "$repo/tools/hosttest/newton_store_test.c" -o "$out/newton-store"
"$out/newton-store"
cc $flags -I"$repo/main" "$repo/main/audio_mix.c" "$repo/tools/hosttest/audio_mix_test.c" -o "$out/mix"
"$out/mix"
cc $flags -I"$repo/main" "$repo/main/audio_mix.c" "$repo/main/audio_streams.c" "$repo/tools/hosttest/audio_streams_test.c" -o "$out/streams"
mkdir "$out/fixtures"
"$out/streams" "$out/fixtures"
# Real session implementation, fake hardware transport; no board behavior claim.
mkdir -p "$out/session/dottie/audio"
python3 - "$out/session/dottie/audio" <<'PY'
import pathlib,struct,sys
root=pathlib.Path(sys.argv[1])
for name,value in [('music_loop',1000),('wind_loop',100),('newton_1',2000)]:
    (root/(name+'.pcm')).write_bytes(struct.pack('<h',value)*1000)
PY
cc $flags -DCONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=1 -DCONFIG_FATFS_LFN_HEAP=1 \
    -DBSP_SD_MOUNT_POINT="\"$out/session\"" \
    -I"$repo/tools/hosttest/audio_stubs" -I"$repo/main" \
    "$repo/main/audio_test_session.c" "$repo/main/audio_streams.c" "$repo/main/audio_mix.c" \
    "$repo/tools/hosttest/audio_session_test.c" -o "$out/session-test"
"$out/session-test"
cc $flags -I"$repo/tools/hosttest/audio_stubs" -I"$repo/main" \
    "$repo/main/audio_playback.c" "$repo/tools/hosttest/audio_playback_test.c" -o "$out/player-test"
"$out/player-test"
