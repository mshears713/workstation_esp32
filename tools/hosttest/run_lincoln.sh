#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT INT TERM
flags='-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
cc $flags -I"$repo/main" "$repo/main/lincoln_scene.c" "$repo/tools/hosttest/lincoln_v2_state_test.c" -o "$out/state"
"$out/state"
mkdir "$out/fixtures"
cc $flags -I"$repo/main" "$repo/main/audio_mix.c" "$repo/main/audio_streams.c" "$repo/tools/hosttest/lincoln_streams_test.c" -o "$out/streams"
"$out/streams" "$out/fixtures"
