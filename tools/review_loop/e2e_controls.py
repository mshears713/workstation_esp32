#!/usr/bin/env python3
"""End-to-end negative and positive controls for review.py, with the real simulator.

    python3 tools/review_loop/e2e_controls.py --sim /path/to/workstation-lvgl-sim [--keep DIR]

Each case runs review.py as a subprocess against this checkout (or a throwaway git worktree of
it) and checks the exit code and the decisive stage results. Art packages used here are
FIXTURES built in a temp folder: the left-bush sprite decoded from the committed header,
optionally altered. They exercise the selection/integration machinery only and are not
native art. Prints one PASS/FAIL line per case; exit 1 if any case did not behave.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from PIL import Image, ImageOps

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import review  # noqa: E402

BASE = json.loads((HERE / "plans" / "artifacts_home.json").read_text())
SLOT = {"symbol": "ai_bush_l", "file": "main/orlando_artifacts.h"}


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def plan(tmp: Path, name: str, **over) -> Path:
    """A small FIXTURE plan from the exhibit plan: one still unless overridden."""
    p = {**BASE, "name": name, "stills": [{"name": "home", "advance_ms": 500, "accept": BASE["stills"][0]["accept"]}],
         "motion": [], "tests": [], "references": [], "focus": [], "required": ["stills", "art"], **over}
    path = tmp / f"{name}.json"
    path.write_text(json.dumps(p, indent=1))
    return path


def committed_bush() -> Image.Image:
    text = (REPO / SLOT["file"]).read_text()
    d = next(d for d in review.descriptors(text) if d["name"] == SLOT["symbol"])
    raw = review.array_bytes(text, d["data"])
    return Image.frombytes("RGBA", (d["w"], d["h"]), raw, "raw", "BGRA")


def package(root: Path, project: str, revision: int, frame1: Image.Image) -> dict:
    """FIXTURE delivery-v1 package: frame 0 blank, frame 1 = the given image."""
    pkg = root / project / f"r{revision:04d}"
    pkg.mkdir(parents=True)
    w, h = frame1.size
    atlas = Image.new("RGBA", (2 * w, h))
    atlas.paste(frame1, (w, 0))
    stem = f"{project}-r{revision:04d}"
    atlas.save(pkg / f"{stem}-atlas.png")
    (pkg / f"{stem}.pxo").write_bytes(b"FIXTURE: not a native Pixelorama project")
    files = [{"name": n, "role": "FIXTURE", "bytes": (pkg / n).stat().st_size, "sha256": sha(pkg / n)}
             for n in (f"{stem}.pxo", f"{stem}-atlas.png")]
    (pkg / "manifest.json").write_text(json.dumps({
        "schema": "pixelorama-workshop/delivery-v1", "project": project, "revision": revision,
        "checkpoint": f"r{revision:04d}-fixture", "files": files,
        "atlas": {"width": 2 * w, "height": h, "frames": [{"index": i, "x": w * i, "y": 0, "width": w, "height": h}
                                                          for i in (0, 1)]}}))
    return {"project": project, "revision": revision, "checkpoint": f"r{revision:04d}-fixture",
            "manifest_sha256": sha(pkg / "manifest.json"), "files": {f["name"]: f["sha256"] for f in files}, "_dir": pkg}


def lock(tmp: Path, name: str, *entries) -> Path:
    p = tmp / f"{name}-lock.json"
    p.write_text(json.dumps({"schema": "workstation-review/art-lock-v1",
                             "selection": [{k: v for k, v in e.items() if k != "_dir"} for e in entries]}))
    return p


def review_run(sim: Path, out: Path, plan_path: Path, *extra, repo: Path = REPO, during=None) -> tuple[int, dict]:
    cmd = [sys.executable, str(HERE / "review.py"), "--plan", str(plan_path), "--sim", str(sim), "--repo", str(repo),
           "--out", str(out), *map(str, extra)]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if during:
        threading.Thread(target=during, daemon=True).start()
    log, _ = proc.communicate(timeout=1200)
    (out.parent / f"{out.name}.log").write_text(log)
    results = json.loads((out / "results.json").read_text()) if (out / "results.json").exists() else {}
    return proc.returncode, results


def status(results: dict, stage: str, name: str | None = None) -> list[str]:
    return [i["status"] for i in results.get("results", []) if i["stage"] == stage and (name is None or i["name"] == name)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", type=Path, required=True)
    ap.add_argument("--keep", type=Path, help="keep run folders here (default: a temp folder)")
    a = ap.parse_args()
    tmp = Path(tempfile.mkdtemp(prefix="review-e2e-"))
    runs = a.keep or tmp / "runs"
    runs.mkdir(parents=True, exist_ok=True)
    art = tmp / "art"
    bush = committed_bush()
    cases = []

    def case(name, ok, detail):
        cases.append((name, ok))
        print(f"{'PASS' if ok else 'FAIL'} {name}: {detail}", flush=True)

    # 1. checkpoint validation requested implicitly (no lock, no diagnostic flag) -> INCOMPLETE
    rc, r = review_run(a.sim, runs / "no-lock", plan(tmp, "FIXTURE-no-lock"))
    case("no art lock blocks", rc == 2 and UNAV in status(r, "art", "checkpoint"), f"exit {rc}, art {status(r, 'art')}")

    # 2. the real exhibit bindings have no native source: a lock cannot satisfy them
    other = package(art / "unrelated", "FIXTURE-unrelated", 1, bush)
    rc, r = review_run(a.sim, runs / "unsupported", plan(tmp, "FIXTURE-real-bindings"),
                       "--art-lock", lock(tmp, "unrelated", other), "--art", art / "unrelated")
    case("real exhibit bindings are UNSUPPORTED under checkpoint validation",
         rc == 2 and "UNSUPPORTED" in status(r, "art") and not status(r, "still", "home"),
         f"exit {rc}, art {sorted(set(status(r, 'art')))}, renders {status(r, 'still')}")

    one = {"art": [{**SLOT, "source": {"project": "FIXTURE-bush", "frame": 1}}]}
    # 3. exact fixture package identical to the committed bytes -> art PASS, renders run
    same = package(art / "same", "FIXTURE-bush", 1, bush)
    rc, r = review_run(a.sim, runs / "identical", plan(tmp, "FIXTURE-identical", **one),
                       "--art-lock", lock(tmp, "same", same), "--art", art / "same")
    case("exact locked package (identical bytes) passes", rc == 0 and status(r, "art", SLOT["symbol"]) == ["PASS"]
         and status(r, "still", "home") == ["PASS"], f"exit {rc}, readiness art {r.get('readiness', {}).get('art_verified')}")

    # 4. changed pixels -> integrated into the snapshot, render differs only around the bush, checkout untouched
    before = sha(REPO / SLOT["file"])
    alt = package(art / "alt", "FIXTURE-bush", 2, ImageOps.mirror(bush))
    rc, r = review_run(a.sim, runs / "integrated", plan(tmp, "FIXTURE-integrated", **one),
                       "--art-lock", lock(tmp, "alt", alt), "--art", art / "alt")
    diffbox = None
    if rc == 0:
        a_img = Image.open(runs / "identical/stills/home.png").convert("RGB")
        b_img = Image.open(runs / "integrated/stills/home.png").convert("RGB")
        diffbox = review.changed_pixels(a_img, b_img)[1]
    case("different locked pixels are integrated (snapshot only)", rc == 0 and diffbox is not None
         and diffbox[0] >= 0 and diffbox[2] <= 36 and before == sha(REPO / SLOT["file"]),
         f"exit {rc}, changed bbox {diffbox}, checkout header unchanged {before == sha(REPO / SLOT['file'])}")

    # 5. corrupt selected package while a VALID older package of the same project is present
    old = package(art / "mixed", "FIXTURE-bush", 1, bush)
    new = package(art / "mixed", "FIXTURE-bush", 2, ImageOps.mirror(bush))
    lk = lock(tmp, "mixed", new)
    (new["_dir"] / "FIXTURE-bush-r0002-atlas.png").write_bytes(b"corrupted after the lock was written")
    rc, r = review_run(a.sim, runs / "corrupt-with-older", plan(tmp, "FIXTURE-corrupt", **one), "--art-lock", lk, "--art", art / "mixed")
    detail = next((i["detail"] for i in r.get("results", []) if i["name"] == "package:FIXTURE-bush"), "")
    case("corrupt selected package is blocked; valid older r1 not used", rc == 1 and "BLOCKED" in detail
         and not status(r, "still", "home") and "r0001" not in json.dumps(r.get("readiness")), f"exit {rc}: {detail[:160]}")
    del old

    # 6. missing file in the selected package
    miss = package(art / "missing", "FIXTURE-bush", 1, bush)
    lk = lock(tmp, "missing", miss)
    (miss["_dir"] / "FIXTURE-bush-r0001.pxo").unlink()
    rc, r = review_run(a.sim, runs / "missing-file", plan(tmp, "FIXTURE-missing", **one), "--art-lock", lk, "--art", art / "missing")
    case("missing selected file blocks", rc == 1 and "FAIL" in status(r, "art"), f"exit {rc}")

    # 7. wrong dimensions
    small = package(art / "small", "FIXTURE-bush", 1, bush.crop((0, 0, 40, 24)))
    rc, r = review_run(a.sim, runs / "wrong-size", plan(tmp, "FIXTURE-size", **one),
                       "--art-lock", lock(tmp, "small", small), "--art", art / "small")
    detail = next((i["detail"] for i in r.get("results", []) if i["name"] == SLOT["symbol"]), "")
    case("wrong export dimensions fail", rc == 1 and "40x24" in detail, f"exit {rc}: {detail}")

    # 8. wrong firmware/profile: the exhibit plan against the pre-exhibit Orlando lineage
    wt = tmp / "fw-orlando"
    subprocess.run(["git", "-C", str(REPO), "worktree", "add", "-q", "--detach", str(wt),
                    "ae8ff86d3449d2e34f87140cf698bda9b91a3a05"], check=True)
    try:
        rc, r = review_run(a.sim, runs / "wrong-profile", plan(tmp, "FIXTURE-wrong-profile"), "--committed-art-diagnostic", repo=wt)
        case("exhibit plan on the Orlando baseline fails (lineage + target)", rc == 1 and "FAIL" in status(r, "target", "lineage")
             and "FAIL" in status(r, "target", "artifacts_home"), f"exit {rc}, lineage {status(r, 'target', 'lineage')}")
    finally:
        subprocess.run(["git", "-C", str(REPO), "worktree", "remove", "--force", str(wt)], check=True)

    # 9. inputs changed during the run (a throwaway worktree of HEAD is edited mid-run)
    wt = tmp / "fw-moving"
    subprocess.run(["git", "-C", str(REPO), "worktree", "add", "-q", "--detach", str(wt), "HEAD"], check=True)
    try:
        def touch():
            time.sleep(1.5)
            with open(wt / "main/ui_kit.c", "a") as f:
                f.write("\n/* edited during the run */\n")
        slow = plan(tmp, "FIXTURE-moving", tests=[BASE["tests"][0]], required=["stills", "tests"])
        rc, r = review_run(a.sim, runs / "changed-during-run", slow, "--committed-art-diagnostic", repo=wt, during=touch)
        case("checkout edited during the run fails", rc == 1 and status(r, "inputs", "unchanged-during-run") == ["FAIL"],
             f"exit {rc}, {status(r, 'inputs', 'unchanged-during-run')}")
    finally:
        subprocess.run(["git", "-C", str(REPO), "worktree", "remove", "--force", str(wt)], check=True)

    # 10. a required PASS marker that never prints
    short = plan(tmp, "FIXTURE-short", tests=[{**BASE["tests"][0], "advance_ms": 4000}], required=["stills", "tests"])
    rc, r = review_run(a.sim, runs / "missing-marker", short, "--committed-art-diagnostic")
    case("missing ARTIFACTS_HOME_PASS fails", rc == 1 and status(r, "test") == ["FAIL"], f"exit {rc}")

    failed = [n for n, ok in cases if not ok]
    print(f"\n{len(cases) - len(failed)}/{len(cases)} controls behaved" + (f"; misbehaved: {failed}" if failed else ""))
    if not a.keep:
        shutil.rmtree(tmp, ignore_errors=True)
    return 1 if failed else 0


UNAV = "UNAVAILABLE"

if __name__ == "__main__":
    sys.exit(main())
