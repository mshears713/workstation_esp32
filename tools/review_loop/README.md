# tools/review_loop: art or UI change → real simulator → review bundle for Dottie

One command renders a registered firmware UI with the real LVGL simulator, runs the checks a
program can do, captures native 320x240 stills and animation frames, and packages it all with
provenance and a review request that Dottie (or Mike) can open on a phone.

```bash
# checkpoint validation: only the exact locked Workshop package may supply art
python3 tools/review_loop/review.py --plan artifacts_home --sim /opt/workstation/lvgl-sim --art-lock LOCK.json --art DIR
# labelled diagnostic of the art already committed in firmware (never checkpoint validation)
python3 tools/review_loop/review.py --plan artifacts_home --sim /opt/workstation/lvgl-sim --committed-art-diagnostic
python3 -m unittest tools/review_loop/test_review.py                           # the tool's own tests (mocked simulator)
python3 tools/review_loop/e2e_controls.py --sim /opt/workstation/lvgl-sim      # controls with the real simulator
```

**Without `--art-lock` and without a diagnostic flag (`--committed-art-diagnostic`, `--art-loose`)
nothing is rendered**: the run reports INCOMPLETE and produces no stills. `orlando_home` and
`weather_page` are plans for the pre-exhibit Workstation baseline; on the exhibit branch their targets
are not registered and they FAIL at the target stage by design.

Output: `~/workstation-reviews/<UTC time>-<plan>/` and a `.zip` of it (or `--out DIR`).
Exit code 0 = deterministic checks passed (warnings allowed), 1 = something failed (including a
malformed plan: a required stage that lists nothing, an unknown stage name, an entry missing a key),
2 = a required stage could not run. Every outcome writes `results.json`; prerequisites that are
missing are reported as structured results, not tracebacks. Needs Python 3 + Pillow, and a
`workstation-lvgl-sim` checkout with its LVGL submodule initialised (`--sim` or `$WS_SIM`; default
`/opt/workstation/lvgl-sim`).

## Three separate art claims (readiness)

| axis | established by | never established by |
|---|---|---|
| `art_package_integrity` | manifest + every file SHA-256 equal to the art lock, at the source **and again in the frozen copy the renders read**, and unchanged at the end of the run | a newer/older package, a loose export |
| `art_native_editable` | at best `CLAIMED`: the package's Workshop `native_check` receipt (fresh-process reopen) is present and consistent with the packaged atlas. This tool cannot reopen a `.pxo`, so it never says "verified" | a `.pxo` filename, a matching hash, a fixture |
| `visual_review` | Dottie / Mike | any program |

## What it does, and what it never does

| Stage | Deterministic? | What |
|---|---|---|
| provenance | yes | firmware/simulator/LVGL commits, branches, uncommitted files, SHA-256 of every file the target compiles or includes; simulator **and LVGL source** hashed at start and end (drift fails the run) |
| target | yes | the plan's target is registered and its files exist; with `--build-dir`, each source is a compile unit of that configured build by its project-relative path (not its basename) |
| assets | yes | every reachable LVGL image descriptor: format, width, height, stride and array length agree |
| art | yes | the `--art-lock` package is selected exactly, verified, frozen into the bundle, re-verified there, and its frames compared byte-for-byte with the firmware arrays they should have produced (same lossless conversion as `tools/convert_*.py`); differing art is integrated into the run snapshot only |
| stills | render: yes | native PNGs + the simulator's mechanical checks (clipping, overflow, contrast, touch size...) |
| motion | frames: yes; hints: heuristic | 50 ms adapter captures (`capture_dir`) or sampled `advance_ms`; frame-to-frame change counts; hints at one-frame appear/vanish/jump moments |
| tests | yes | adapter interaction tests: the run must not abort AND must print the plan's PASS marker |
| references | no | a scene rendered beside the one under review as the quality bar |
| visual review | **no: Dottie/Mike** | grounding, silhouette, texture, depth, composition, motion quality. `DOTTIE.md` lists the plan's questions |

It never edits the firmware checkout (locked art is integrated into the run's snapshot of `main/`,
`tools/uisim/`, `sdkconfig`; the changed files are kept in `integrated/` and the snapshot is deleted),
never commits, never promotes art into firmware, never runs an ESP-IDF build, never flashes or
deploys. Missing inputs are reported as UNAVAILABLE, not replaced by mocks.

## Bundle layout

```
DOTTIE.md            review request: provenance, every check result, the visual questions, answer format
review-sheet.png     all stills + REFERENCE scene(s), native size, one image
focus-crops.png      enlarged areas the plan asks about (nearest-neighbour)
stills/<name>.png    native 320x240 (also @2x, checks.json, tree.json, log, state)
motion/<name>/       contact-sheet.png, animation.gif, frames/<ms>.png, motion.json
tests/  references/  integrated/ (only when locked art differed from the committed art)
inputs/              firmware/simulator file hashes, art-lock.json, art/<project>-r<rev>/ (the frozen package)
results.json  manifest.json  commands.json   (manifest hashes every file in the bundle)
```

## Manual steps (not automatic yet)

1. **Getting Workshop art here.** Delivery packages land in Google Drive
   (`MCP Workshop Results/pixelorama/<project>/r<rev>-<hash>/`). Download the folder(s) and pass the
   parent as `--art`. A worker volume (`pixelorama/work/projects/`) also works. Nothing here reads Drive.
2. **Sending the bundle to Dottie.** Attach `review-sheet.png`, `focus-crops.png`, the
   `motion/*/contact-sheet.png` you care about and paste `DOTTIE.md` into the Dottie chat (or attach
   the zip). Nothing here sends it.
3. **Acting on the review.** Art corrections go back to an Artwork Workshop session; firmware
   layout/motion corrections are ordinary code changes on a branch. Then rerun the same plan.
4. **Promoting art.** Converting accepted art into the committed headers stays a deliberate step
   with the existing converters (`tools/orlando_overnight/convert_*.py`, `tools/convert_*.py`).
5. **Device.** Build, deploy and on-device checks are separate (`CLAUDE.md`): a passing bundle is
   rendered-level evidence only.

## Plans

`plans/<name>.json`: `target` (from `tools/uisim/targets.json`), `stills`, `motion`, `tests`
(`require` = the line the adapter prints on success), `references`, `focus` crops,
`visual_review` questions, and `art` bindings (`symbol` = the `lv_image_dsc_t`, `file`, and
`source` = Workshop `project` + `frame`; no `source` means "not available", with a `note`).
A still may `accept` a mechanical finding with a reason (e.g. the van leaving the screen).

Known limits: tiled backgrounds (`orlando_v5_background`) cannot be bound yet; motion hints are
heuristics; the simulator does not model the panel, touch, audio, frame rate or heap.
