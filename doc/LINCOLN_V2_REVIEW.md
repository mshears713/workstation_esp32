# Lincoln v2 isolated review candidate

This branch is local implementation/testing only. Do not publish, change desired.json, copy into hosted artifacts or operate the device. Frozen1.5.1+fcacf89 and its pending approval remain separate; its binary SHA256 is7d6ee27bd30c8f591d683343d16f382b220fd3ebfe92ccc4d53db99a32f29a73.

Native source bundle SHA256:76229262dc437e888b30afd15ee4b6ad9e439568a6c6598fb7880a99a30109dc. All78 used frame exports and3PXO hashes were independently checked against producer receipts, with exact RGBA reconstruction through LVGL I8 packaging. There are66 unique payloads,549376bytes,172032bytes above v1. The ZIP hash list contains101 available files plus76 omitted producer-side copies; no claim that omitted files were locally verified. Corrected external review-motion-v2.html, not the ZIP's coupled-clock preview, supplied the independent-clock guidance.

Runtime uses the native manifest's per-frame durations, separate from the167ms PXO defaults. Preparation4..20 lasts4480ms; speech starts at anchor21. All four five-frame reading gesture blocks cycle with anchor21 between them until the actual audio completion callback. Exactly two approved ScottW speeches remain active, both25.92seconds, without changing media hashes, streaming or the30second cap.

Completion/cancellation saves the actual pose and remaining frame time. After the stop acknowledgement, the bounded return plan finishes that pose, follows the manifest's block-end/anchor route, then42..53,4,0. Preparation cancellation continues to anchor21 and closes without starting speech. No queued speech. Cabin door/hearth/puff have independent2800/1760/1740ms clocks, native closed/idle terminal frames and effects requested after their400ms lead-in. No creative pixel edits.

Completed tests: ASan/UBSan500 scene lifecycles;1024 cancellation times covering all preparation and20reading poses; two-speech cycle, stale acknowledgements, timeout/overflow, cabin bounds. Real selected-media transaction tests,200audio-session cleanups and retained Newton/mixer/stream/player regressions pass. Actual LVGL selector200, Lincoln500 and Newton500 pointer/object/timer lifecycle checks pass. Host transport is mocked; no physical sound, SD download, microphone recovery, heap or touch proof is implied.

Final motion capture, local firmware build/size and independent visual review receipts are separate artifacts. This candidate is not authorized for promotion, and does not replace the frozen pending1.5.1 release.
