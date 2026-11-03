# Lincoln preparation — not a release

Baseline: Newton 1.5.0+c6451e0, full commit c6451e0a737df728c4b8c6e658bf31195ae36cf3.
Isolated branch: lincoln-integration-20261030. Newton original checkout remains unchanged.
Firmware deployment and SD writes are not authorized for this stage.

## Implemented groundwork

- `lincoln_scene.*`: pure no-allocation state protocol, configurable native-art timing, hat/look/paper phases before an audio request, four-clip cycle, actual audio-start/completion acknowledgements, second-tap stop barrier, stale request rejection, timeout/failure recovery and bounded cabin events.
- `audio_streams.*`: existing Newton API retains its default six-clip behavior. Explicit Lincoln profile adds four named clips and a single bounded, non-looping SFX in the existing wind slot. Maximum simultaneously open files remains three. Voice limit stays exactly 30 seconds; SFX maximum is five seconds. No new player or microphone owner.
- Scene timing in `lincoln_scene_test.c` is an explicit test fixture, NOT the final art contract.

## Tested 2026-10-30

Isolated Linux test root: `/tmp/lincoln-host.Gvz81y` on the existing Clawbox route.
Sanitized source archive SHA256: 454641a8195fc2373c0210e553535955af3e2dad8fe379a83a4215433cb6a064.

`sh tools/hosttest/run_lincoln.sh`: PASS under AddressSanitizer and UndefinedBehaviorSanitizer; 500 scene lifecycles and 500 streaming cleanup cycles, four-clip cycle, callback-held reading, retap cancellation, stop acknowledgement, stale event rejection, failed starts, timeouts, elapsed overflow, cabin bounds, one-shot tails, exact 30-second speech and five-second SFX limits.

`sh tools/hosttest/run_audio.sh`: PASS all existing Newton state/persistence, mixer, six-voice streams, session and player tests. The first attempt stopped before running because the Windows-exported shell file had CRLF; only the isolated temporary Linux runner was normalized to LF, then tests passed.

No actual LVGL Lincoln renderer, full IDF build, board timing, speaker, microphone recovery or final-art visual test has been completed. The standalone scene module is not yet registered in firmware CMake or connected to UI/audio session. The general diagnostic `run.sh` suite awaits the complete candidate stage; it is not claimed run here.

## Required integration seams

- Connect profile selection through the existing exclusive `voice_detect` audio owner; do not bypass its microphone handoff or create another worker.
- Controller must wait for the old session to close before opening another scene. Request IDs are per scene session; clear/capture worker acknowledgement baselines at session entry so stale events from a previous session cannot match a new request.
- A second character tap requests voice/paper cancellation and waits for actual worker stop acknowledgement before returning/restarting. Music continues until page exit. No queued second voice.
- Wire normal completion only from worker EOF acknowledgement. The 32-second state watchdog is a failure guard, not an animation-duration timer; 30-second media acceptance is unchanged.
- Cabin one-shots and paper share one effect slot. A busy effect rejects a new one; UI must show/handle the rejection honestly, and final art event admission must align with accepted audio.
- Final UI native assets, anchors, frame ranges and phase durations come from the separate Pixelorama owner. No fixture art is final.

## Media staging and acceptance

Windows: `C:/Users/PC/Documents/Codex/2026-10-27/task-2/lincoln-audio-20261030`.
Clawbox: `/home/mshears/lincoln-audio-20261030`.
Ten original MP3s preserved: three initial speeches, think-anew A1 and A2, four SFX and music. All original hashes are in `download-receipt.json`; decode metadata, exact samples, PCM hashes and numerical peaks are in `conversion-receipt.json`. Windows PCM readback matched all ten hashes.

Candidate conversions: signed16 LE mono24kHz, voice -3dB, music -12dB, SFX -15dB. No trim, pad, stretch, normalization or crossfade. These gains are provisional references; quiet SFX have low measured RMS and need actual listening/mix acceptance. Both think-anew takes explicitly NOT_SELECTED. No final manifest names or card files have been created.

Decoded speech seconds: 23.92 / 27.20 / 25.04 / 19.92 A1 / 20.80 A2. Music32.00; door2.00, fire3.00, puff1.00, paper1.48. Speech fits the unchanged limit. Zero full-scale clipped samples in these attenuated PCM derivatives does not prove source or speaker quality.

Music boundary: first sample0, last4274 after -12dB conversion; first100ms silent, last100ms nonzero. Raw loop is NOT certified seamless; a reviewed crossfade derivative may be needed. No subjective listening capability was available in this task. ASR is not a listening pass, and bracketed delivery prompts are not captions.

Initial private Library audition batch did not begin transfer: current supported upload helper reported `library upload failed: Library prepare_uploads is not available`. A subsequent coordinator-authorized all-new direct batch succeeded for the five speech candidates and music. Exact IDs and version0 receipts: local `lincoln-audio-20261030/library-direct-receipt.json`. Windows xattr metadata application failed after successful upload (`os.setxattr` unavailable); identities were preserved in that receipt. No duplicate upload was attempted. Upload ownership then transferred to artifact owner01a18dbe-fba0-77c8-a638-20d7b1f7e003; this firmware owner stopped all further uploads. No new audio generated.

## Draft native simulator milestone, 20:20 UTC

Coordinator authorized partial art for DRAFT-only timing/clipping/state inspection. Native source ZIP SHA256 `2e30db0c6b499e186c4553ae08cf93231bdc09b48033e49c9a29a566e383a8db`; all3 PXO hashes and50 export PNG hashes matched. Lossless I8 packaging preserves exact RGBA values, verified by reconstructing every pixel from the palette; no quantization, resizing or creative pixel changes.42 unique payloads total377,344bytes. Artist acceptance remains false.

`main/ui_lincoln.*`, `main/lincoln_art.*`, and the standalone state are now connected in actual C/LVGL simulator adapters. They are NOT connected to the production menu or physical audio session, and are not registered in the firmware CMake yet. `tools/uisim/render_lincoln_draft.py` builds against a configuration/compiler/LVGL-commit-matched existing library cache; no LVGL rebuild or installs.

Actual pointer test PASS: native descriptors, disabled offline actor, person and button taps, preserved preparation pose while stopping, cabin door returning to idle0, all four reading-loop frame ranges,500 object/timer cleanup cycles. State/stream sanitizer tests rerun after adding idle animation and preserved cancellation pose: PASS.

Isolated root `/tmp/lincoln-sim.N3wr5D`. Captures:178 full-cycle frames through29,000ms,45 stop-during-reading frames,41 stop-during-preparation frames. Actual20msLVGL timer and native C source; audio start/end events injected from decoded candidate durations. Captures bypass the older20s renderer wrapper clamp by calling the same compiled simulator executable with explicit advancement. This is not real speaker/mic/SD evidence or final audio selection.

Visual inspection: preparation/closing/cancellation contact sheets and native-size idle/reading/chimney frames inspected. Actor feet stay at188, controls start192, no control overlap in reviewed frames. Source hat/hand jumps, stiff two-frame reading loops and chimney clipping remain visible; no procedural art repair. Door explicitly returns to source idle0 after its event. Artist refinements remain pending.

Local delivery: `lincoln-draft-delivery/Lincoln-DRAFT-Motion-and-Audio-Audition.html`,8,395,396bytes, SHA256 `2d72e7dcc45e6622fd080e425a4ff27fdebe51e7eeeb8385bf816b3fe7e78ced`. Embedded264 actualLVGL frames (52unique images),10 preserved originalMP3s, clean intended-text captions, visible draft/QA caveats, one-audio-at-a-time controls, no autoplay/network/device operations. This is interactive replay/scrubbing of captured frames, not a live simulator or a firmware rebuild. Embedded master hashes and JavaScript syntax checked; browser/audio listening not performed here. Native-size GIFs/contact sheets and exact source/capture hashes accompany the HTML. Separate artifact owner owns upload/delivery.
