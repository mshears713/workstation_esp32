#!/bin/sh
# Convert an approved ElevenLabs MP3 (or any audio) into the exhibit clip format:
# 24000 Hz, signed 16-bit little-endian, mono, raw PCM, at most 30 s.
#   tools/exhibit_audio/make_pcm.sh welcome.mp3 welcome
# writes welcome.pcm and prints its size, duration and SHA-256 for the receipt.
# Copy the .pcm to the SD card (SENSOR dock) at /dottie/exhibit/<name>.pcm.
set -e
[ $# -eq 2 ] || { echo "usage: $0 <input audio> <name: welcome|guide|egypt|greece|gothic>"; exit 2; }
ffmpeg -hide_banner -loglevel error -y -i "$1" -ac 1 -ar 24000 -f s16le -acodec pcm_s16le "$2.pcm"
bytes=$(wc -c < "$2.pcm")
max=1440000
echo "$2.pcm: $bytes bytes, $(awk "BEGIN{printf \"%.2f\", $bytes/48000}") s, sha256 $(sha256sum "$2.pcm" | cut -d' ' -f1)"
[ "$bytes" -le "$max" ] || { echo "TOO LONG: the exhibit player refuses clips over 30 s ($max bytes)"; exit 1; }
