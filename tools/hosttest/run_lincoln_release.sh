#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT INT TERM
flags='-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
mkdir -p "$out/sd" "$out/session/dottie/audio/lincoln/lincoln-scottw-two-20261030"
cc $flags -Wno-deprecated-declarations -DHOST_LINCOLN_MEDIA -DBSP_SD_MOUNT_POINT="\"$out/sd\"" \
  -I"$repo/tools/hosttest/audio_stubs" -I"$repo/main" "$repo/main/lincoln_media.c" "$repo/tools/hosttest/lincoln_media_test.c" -lcrypto -o "$out/media"
"$out/media" "$1"
python3 - "$out/session/dottie/audio/lincoln/lincoln-scottw-two-20261030" <<'PY'
import pathlib,struct,sys
p=pathlib.Path(sys.argv[1])
for name in ['music_loop','cabin_door','paper','lincoln_1','lincoln_2']:(p/(name+'.pcm')).write_bytes(struct.pack('<h',1000)*1000)
PY
cc $flags -DCONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=1 -DCONFIG_FATFS_LFN_HEAP=1 -DBSP_SD_MOUNT_POINT="\"$out/session\"" \
  -I"$repo/tools/hosttest/audio_stubs" -I"$repo/main" "$repo/main/audio_test_session.c" "$repo/main/audio_streams.c" "$repo/main/audio_mix.c" "$repo/tools/hosttest/lincoln_session_test.c" -o "$out/session-test"
"$out/session-test"
