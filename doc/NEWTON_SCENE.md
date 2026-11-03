# Newton garden scene

Baseline: installed 1.4.3+38d0d08, physically confirmed SD music/wind/voice on
2026-10-30 at 05:09 UTC. Keep its published binary and normal OTA recovery.

## Controls and behavior

MORE > AUDIO TEST retains the working test. Its NEWTON button enters the garden
using that same active SD session; it does not create another player or reopen the
microphone. Music continues. Tap Newton or SPEAK to cycle six clips. One additional
tap can wait during speech; further taps coalesce into that one request. Quote
advancement and pose start follow the audio worker's successful file-open receipt,
not a touch or predicted duration. Music ducking and codec gain are unchanged.

Tap the tree: Newton walks continuously to the authored crown-contact anchor,
the apple falls, he reacts, and the apple remains absent. The next tree tap plays
the native regrowth sequence. Repeat tree taps during the routine are ignored.
Tree taps trigger a bounded wind gust; WIND independently toggles sustained wind.
BACK stops the session, closes all files/speaker, deletes the scene and timer, and
returns to MORE. The existing owner restores the microphone before releasing it.

Fruit state is stored in the new dedicated NVS namespace `newton_scene`, key
`fruit_v1`; no partition erase, card writes, device-settings changes or format.
Writes occur at state transitions and exit, with unchanged-value suppression.
Absent fruit persists across reopening/reboot. A partially completed fall reopens
as absent. Interrupted regrowth resumes from the saved remaining scene time;
there is no wall-clock/offline growth. Storage failure is logged and visible in
the scene; in-memory state remains usable.

## Native assets and framing

Source private Library pack: libfile_a75d1d601f28819191f7fc09ebbc8408.
Archive SHA256: bb8ccd9b8b4d285305f94db88a9a2325f09090c1e889f194e17d72eab98dd54a.
The separate native-art owner supplied 11 editable PXOs and fresh-process export
verification. Firmware integration verified all 171 archive hashes and consumed
98 PNG exports: eight garden tiles, 40 Newton poses, 25 tree and 25 fruit frames.
Runtime uses the explicit action ranges in scene-manifest.json, not the broader
native tag ranges or default 167ms frame metadata.

`tools/convert_newton_art.py` converts the unchanged exports to ARGB8888 in flash.
It shares identical images and removes only the fruit canvas's empty margins,
proving byte-identical reconstruction of every original RGBA frame. The crop
offset is added to the sprite origin. Result: 60 unique payloads, 964,320 bytes;
see `doc/newton-art-receipt.json` for source/export/conversion hashes.

All content remains at native 1:1 scale. A common y=-17 translation places the
authored foot anchor at y=188, above 48px touch controls at y=192..239. The top sky
and bottom background margins are intentionally clipped. The LVGL checks report
those four background-tile clips; they are framing, not clipped actors/controls.
Newton origin moves x=121 to192, y=128. Crown contact is (218,137), matching the
translated native fruit contact exactly. Native tree/fruit layers retain their
authored relative positions. Actor is foreground; apple is above actor at contact.

The Cambridge-inspired garden is theatrical staging. The head-bonk is a playful
legend, not an established historical event. The six spoken excerpts and their
source/context remain in the preserved audio manifest; speech is synthetic.

## Verification and limits

`tools/hosttest/run_audio.sh` covers model queue/failure/timing, persistent fruit,
NVS round-trip/corrupt-data/failures, mixer clipping/ducking, stream overlap,
session failures and cleanup, player failures and volume restoration with
ASan/UBSan. Hardware calls are stubbed; this is not physical codec/mic proof.

`tools/validate_newton_audio.py MANIFEST AUDIO_DIRECTORY` verifies the eight actual
PCM hashes, fixed allowlisted names, durations, format and action map. Negative
checks reject traversal, duplicates, changed hashes/format/actions/durations.
The device's existing stream loader uses fixed basenames under
`/sdcard/dottie/audio`, read-only, and checks missing/empty/odd/oversized PCM. It
does not claim to rehash all eight files or dynamically interpret manifest paths
on every scene entry. No audio masters, file gains or codec settings were changed.

Actual LVGL targets: `newton`, `newton_ui_test`, `newton_motion`. The latter uses
the real 50ms LVGL timer to drive approach/fall/bonk/absent/regrow. Input tests use
real lv_indev pointer events and assert 500 page lifecycles with no extra objects
or timers. The Audio Test pointer test remains separate (200 cycles).
`capture_newton_motion.py` documents the existing isolated Clawbox capture route;
91 frames at 50ms intervals cover 0..4500ms. Audio readiness is injected in the
simulator. Renderer PNGs and derived GIFs do not prove actual speaker timing,
touchscreen feel, device FPS, playback heap or post-playback microphone recovery.

Full device build uses the existing Windows ESP-IDF5.5.3 environment. Do not
upgrade dependencies or use the older dirty Clawbox firmware checkout as source.
Release identity, binary hash, boot validation and fresh check-ins belong in the
deployment receipt after guarded OTA, not inferred from a compile or simulator.
