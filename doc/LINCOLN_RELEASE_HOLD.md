# Lincoln release hold — 2026-10-30

## Superseding selection at21:08:59UTC

Mike approved the ScottW audition and requested only two speeches. The coordinator supplied the exact approved Gettysburg generation `A1dG4Ltvk5NV1QhRub55` and sole new With Malice generation `1uu8fHS8WeWjuvsJ3o9w`, both ScottW `6Xb9dRSSm1t1OLWvzT1T`, eleven_v4. Both originals were preserved and independently decoded to25.92seconds,24kHz mono signed16LE with -3dB headroom, zero clipped samples. No quotation trimming, padding or time stretching. Assistant listening is not claimed; the user's Gettysburg audition approval is the subjective evidence.

Release hold on Brian remains permanent for this candidate. The new selected ScottW-only revision `lincoln-scottw-two-20261030` has exactly seven files,4,383,360bytes. Source hashes and conversion details are in `lincoln-selected-media.json`. The active two-speech protocol rejects indices2and above, cycles Gettysburg/With Malice, preserves callback completion and the30second cap. Native reading frames14/15 and18/19 preserve the corresponding original Gettysburg/Malice art groups. Music and four SFX retain their existing masters/derivatives. All Brian files remain separate rejected archives.

The20:49 guarded deployment authorization applies after final tests/build/hash and recovery checks. No deployment had occurred at this selection checkpoint. Historical hold/build evidence below describes the superseded candidate; never publish its cached binary.

User authorized the draft scene/menu release at20:49:10UTC, then explicitly rejected the Brian voice at20:56:42UTC. The later instruction holds publication/OTA until a replacement voice audition and selection. Do not deploy a cached candidate or treat an earlier media selection as approval.

## Actual device/publication status

- Read-only check at20:58UTC: installed1.5.0+c6451e0, ota_1 VALID; latest check-in20:58:01UTC.
- No Lincoln OTA, publication, SD writes or audio playback triggered by this task.
- `/opt/workstation/artifacts/bin/lincoln-draft-a2-20261030` does not exist.
- Published1.5.0 recovery SHA256730105fd1114789455468cf2168d4d67ce9353ce828a2bc51df043b3fe61ee70 and1.4.3 SHA25618ed1cea1ecd7985538195a9d8db79d33597fea91f25877ff101e766caae93ad were read and verified before any intended write. Both remain unchanged.

## Independent code completed

- Audio Test is a two-scene selector: NEWTON, LINCOLN and BACK. No Wind/SFX or numbered-voice buttons. Choosing a scene admits its existing exclusive audio owner; Back cancels that owner and returns to the selector. Rapid scene changes receive a visible busy response until cleanup/microphone restoration completes.
- Lincoln UI now wired into `status_deck_ui.c` with a single50ms timer and existing `voice_control` audio owner. Hat/look/paper preparation precedes voice. Four exact clip IDs, start/error/EOF/stop sequence acknowledgements; one voice, second-tap cancellation, no voice queue. Actual completion is reported on the producer call after the final audio block was written, not a fixed scene timer.
- Three streams maximum: music, one effect and voice. Paper can replace a cabin effect; other busy effects reject rather than overlap. Thirty-second speech cap unchanged, five-second one-shot cap. Existing Newton audio/state/storage behavior retained.
- Additive provisioning code uses the existing certificate-pinned HTTPS artifact route and fixed hashes, nine files6,549,120bytes in a new Lincoln revision subfolder. Existing files must match or remain untouched with an error; temporary files use exclusive creation, hash-before-rename, failure cleanup. No card format, unrelated scan or replacement.
- Rejected Brian manifest explicitly marked TEST FIXTURES ONLY. Generated `media_voice_approved=false` prevents device-side downloads/playback of this selection. Host integrity tests explicitly bypass acceptance only to exercise transactional code against preserved fixture bytes.
- Version under development1.5.1; no clean release commit or approved release artifact yet. Windows compilation is a code check only. The private existing device header was copied locally into its git-ignored path for the authorized build; it was excluded from every host-test archive. No credentials changed or exposed.

## Completed checks

Host root `/tmp/lincoln-release-tests.jOGLPG`:

- `run_lincoln.sh`: PASS state and streaming sanitizers,500 lifecycles.
- `run_lincoln_release.sh <fixture-media>`: PASS real selected-byte integrity, missing/failing/corrupt download cleanup, idempotent cache, conflicting existing-file preservation; real audio session cancellation-before-start, cancellation-during-play, exact request IDs, completion after final write, effect priority, missing voice and200 clean exits. Hardware transport/mount/player mocked; no board proof claimed.
- Existing `run_audio.sh`: PASS Newton state/persistence, mix, streams, session and player regressions.
- Required diagnostic `run.sh`: ALL PASS.
- Actual C/LVGL: new two-scene selector pointer tests200 cycles; Lincoln pointer/lifecycle500 cycles; retained Newton pointer/lifecycle500 cycles. Passed and menu screenshot visually inspected at320x240. Readable labels, separate48px-or-larger targets, no obsolete controls.

## Local build check completed at21:07:20UTC

- Existing Windows ESP-IDF5.5.3 build completed successfully, exit0; all1783 build steps and partition-size check passed. No toolchain installation or upgrade.
- Draft binary `build/status_deck.bin`:5,079,424bytes; SHA256 `41b3ea79e175a92cda507311bea0ff1c8e00ed6168af8fbb1b5bd2562b1afbd3`.
- Smallest application partition5,636,096bytes; remaining556,672bytes (9.88%). Binary is384,016bytes larger than installed Newton1.5.0. These are file/partition measurements, not board heap or playback measurements.
- This is an uncommitted1.5.1 draft based on `c6451e0a737df728c4b8c6e658bf31195ae36cf3`, with the rejected-voice approval gate still FALSE. It is NOT an approved release image. Do not publish this cached build after selecting replacement audio; rebuild and verify the selected media identity first.
- Original Newton checkout tracked source remains clean. No OTA or media publication was initiated; no deployment command is pending.

The accepted-voice media revision, final release commit/build, OTA promotion and physical screen/hearing checks remain pending. No automatic22:10artist polish promotion is authorized; it is a separate future candidate.
