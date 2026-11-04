#!/usr/bin/env python3
"""Workstation review loop: real firmware UI -> real LVGL simulator -> review bundle.

    python3 tools/review_loop/review.py --plan orlando_home [--sim PATH] [--art DIR ...] [--integrate]

One run does, in order, and records every step:

  1. provenance   commits, branches, dirty files and SHA-256 of every file the render uses
  2. target       the plan's registered simulator target exists, its files exist, and the
                  firmware build (main/CMakeLists.txt) compiles the same sources
  3. assets       every LVGL image descriptor the target can reach is self-consistent
                  (format, width, height, stride, array length)
  4. art          optional: Artwork Workshop exports (--art) compared byte-for-byte with the
                  firmware arrays they should have produced; --integrate puts them into a
                  disposable copy of the firmware and renders that copy instead
  5. renders      stills, animation captures, interaction tests and reference scenes, each
                  through the existing `ws-ui` CLI of the workstation-lvgl-sim checkout
  6. bundle       native 320x240 PNGs, contact sheets, crops, a GIF, results.json,
                  manifest.json, DOTTIE.md and a zip

It never edits the firmware checkout, commits, converts art into firmware for keeps, builds
with ESP-IDF, flashes or deploys. A stage that cannot run says UNAVAILABLE; nothing is
mocked. Exit code: 0 = every deterministic check passed (warnings allowed), 1 = something
failed, 2 = a required stage could not run.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageDraw, ImageFont
except ImportError:  # the art converters in tools/ need Pillow too
    sys.exit("review.py needs Pillow: python3 -m pip install pillow")

W, H = 320, 240
HERE = Path(__file__).resolve().parent
DEFAULT_REPO = HERE.parents[1]
DEFAULT_SIM = Path(os.environ.get("WS_SIM", "/opt/workstation/lvgl-sim"))
BPP = {"ARGB8888": 4, "XRGB8888": 4, "RGB888": 3, "RGB565": 2, "A8": 1, "L8": 1}

PASS, WARN, FAIL, UNAVAILABLE, INFO = "PASS", "WARN", "FAIL", "UNAVAILABLE", "INFO"


def sha256(path: Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def now_utc() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def run(cmd: list[str], cwd: Path | None = None, timeout: int = 120) -> tuple[int, str]:
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout + r.stderr
    except FileNotFoundError as e:
        return 127, str(e)
    except subprocess.TimeoutExpired:
        return 124, f"timed out after {timeout}s"


class Results:
    """Every stage result, in order. Nothing is reported as passing unless it ran."""

    def __init__(self):
        self.items: list[dict] = []

    def add(self, stage: str, name: str, status: str, detail: str, **data):
        self.items.append({"stage": stage, "name": name, "status": status, "detail": detail, **data})
        print(f"  [{status:11}] {stage}/{name}: {detail}", flush=True)

    def overall(self) -> tuple[str, int]:
        states = {i["status"] for i in self.items}
        if FAIL in states:
            return "FAIL", 1
        if any(i["status"] == UNAVAILABLE and i.get("required") for i in self.items):
            return "INCOMPLETE", 2
        return ("PASS_WITH_WARNINGS" if WARN in states or UNAVAILABLE in states else "PASS"), 0


# ------------------------------------------------------------------ provenance

def git_info(path: Path) -> dict:
    def g(*args):
        rc, out = run(["git", "-C", str(path), *args])
        return out.strip() if rc == 0 else None
    url = g("remote", "get-url", "origin") or ""
    info = {"path": str(path), "commit": g("rev-parse", "HEAD"), "branch": g("rev-parse", "--abbrev-ref", "HEAD"),
            "remote": re.sub(r"//[^/@]*@", "//", url)}
    status = g("status", "--porcelain", "--untracked-files=no")
    info["dirty_files"] = [ln[3:] for ln in (status or "").splitlines()]
    return info


def target_files(repo: Path, target: dict) -> list[str]:
    """Every firmware file a render of this target compiles or includes (repo-relative)."""
    files = [target["adapter"], *target["sources"], "sdkconfig", "tools/uisim/targets.json"]
    queue = [repo / f for f in files if f.endswith((".c", ".h"))]
    seen = set(files)
    while queue:
        text = queue.pop().read_text(errors="replace")
        for inc in re.findall(r'#include\s+"([^"]+)"', text):
            p = repo / "main" / inc
            rel = f"main/{inc}"
            if p.is_file() and rel not in seen:
                seen.add(rel)
                files.append(rel)
                queue.append(p)
    return files


# ------------------------------------------------------------------ LVGL image arrays

DESC_RE = re.compile(r"lv_image_dsc_t\s+(\w+)\s*=\s*\{(.*?)\};", re.S)
NUM_RE = re.compile(r"0[xX][0-9a-fA-F]+|\d+")


def strip_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)


def find_array(text: str, name: str) -> tuple[int, int] | None:
    """(start, end) of the initializer body of `name[] = { ... }` in text."""
    m = re.search(rf"\b{re.escape(name)}\s*\[\s*\]\s*(?:__attribute__\s*\(\(.*?\)\)\s*)?=\s*\{{", text, re.S)
    if not m:
        return None
    end = text.find("}", m.end())
    return (m.end(), end) if end > 0 else None


def array_bytes(text: str, name: str) -> bytes | None:
    span = find_array(text, name)
    if not span:
        return None
    values = [int(v, 0) for v in NUM_RE.findall(strip_comments(text[span[0]:span[1]]))]
    if any(v > 255 for v in values):
        return None
    return bytes(values)


def descriptors(text: str) -> list[dict]:
    out = []
    for name, body in DESC_RE.findall(strip_comments(text)):
        d = {"name": name}
        for key, rx in (("cf", r"LV_COLOR_FORMAT_(\w+)"), ("w", r"\.w\s*=\s*(\d+)"), ("h", r"\.h\s*=\s*(\d+)"),
                        ("stride", r"\.stride\s*=\s*(\d+)"), ("data", r"\.data\s*=\s*(?:\([^)]*\)\s*)?&?(\w+)")):
            m = re.search(rx, body)
            d[key] = (int(m.group(1)) if m.group(1).isdigit() else m.group(1)) if m else None
        out.append(d)
    return out


def check_descriptor(d: dict, text: str) -> tuple[str, str]:
    if None in (d["cf"], d["w"], d["h"], d["data"]):
        return INFO, "not a literal descriptor (macro or computed fields); not checked"
    bpp = BPP.get(d["cf"])
    if bpp is None and d["cf"] != "RGB565A8":
        return INFO, f"format {d['cf']} not checked by this tool"
    stride = d["stride"] or d["w"] * (bpp or 2)
    expected = stride * d["h"] + (d["w"] * d["h"] if d["cf"] == "RGB565A8" else 0)
    data = array_bytes(text, d["data"])
    if data is None:
        return INFO, f"data array {d['data']} not found in the same file; not checked"
    if stride < d["w"] * (bpp or 2):
        return FAIL, f"stride {stride} < width {d['w']} x {bpp or 2} bytes"
    if len(data) != expected:
        return FAIL, f"{d['cf']} {d['w']}x{d['h']} needs {expected} bytes, array has {len(data)}"
    return PASS, f"{d['cf']} {d['w']}x{d['h']}, {len(data)} bytes"


def convert(img: Image.Image, fmt: str) -> bytes:
    """The same lossless conversions the firmware converters use (tools/convert_*.py,
    tools/orlando_overnight/convert_*.py): PNG RGBA -> LVGL little-endian bytes."""
    rgba = img.convert("RGBA")
    if fmt == "ARGB8888":
        return rgba.tobytes("raw", "BGRA")
    if fmt == "RGB565":
        out = bytearray()
        for r, g, b, a in rgba.getdata():
            if a != 255:
                raise ValueError("RGB565 source has transparent pixels")
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            out += bytes((v & 255, v >> 8))
        return bytes(out)
    raise ValueError(f"no converter for {fmt}")


# ------------------------------------------------------------------ Artwork Workshop exports

def find_export(art_dirs: list[Path], project: str, frame: int) -> tuple[Image.Image, dict] | tuple[None, str]:
    """Locate frame `frame` of Workshop project `project`. Preference: a delivery package
    (pixelorama-workshop/delivery-v1, hashes verified), then an exported frame_NNN.png, then
    an atlas.png + atlas.json (atlas-v1) from the worker volume."""
    problems = []
    for root in art_dirs:
        packages = []
        for m in root.rglob("manifest.json"):
            try:
                j = json.loads(m.read_text())
            except (OSError, ValueError):
                continue
            if j.get("schema") == "pixelorama-workshop/delivery-v1" and j.get("project") == project:
                packages.append((j.get("revision", 0), m, j))
        for rev, m, j in sorted(packages, key=lambda t: t[0], reverse=True):
            bad = [f["name"] for f in j.get("files", []) if not (m.parent / f["name"]).is_file()
                   or sha256(m.parent / f["name"]) != f["sha256"]]
            if bad:
                problems.append(f"{m.parent}: files missing or not matching manifest SHA-256: {bad}")
                continue
            atlas = next((m.parent / f["name"] for f in j["files"] if f["name"].endswith("-atlas.png")), None)
            rect = next((f for f in j["atlas"]["frames"] if f.get("index") == frame), None)
            if atlas and rect:
                img = Image.open(atlas).crop((rect["x"], rect["y"], rect["x"] + rect["width"], rect["y"] + rect["height"]))
                return img, {"kind": "delivery package", "path": str(atlas), "sha256": sha256(atlas),
                             "revision": rev, "checkpoint": j.get("checkpoint"), "frame": frame}
        for p in root.rglob(f"{project}/frame_{frame:03}.png"):
            return Image.open(p), {"kind": "exported frame", "path": str(p), "sha256": sha256(p), "frame": frame}
        for aj in root.rglob(f"{project}/atlas.json"):
            j = json.loads(aj.read_text())
            rect = next((f for f in j.get("frames", []) if f.get("index") == frame), None)
            png = aj.parent / j.get("image", "atlas.png")
            if rect and png.is_file():
                img = Image.open(png).crop((rect["x"], rect["y"], rect["x"] + rect["width"], rect["y"] + rect["height"]))
                return img, {"kind": "worker atlas", "path": str(png), "sha256": sha256(png), "frame": frame}
    return None, "; ".join(problems) or f"no export of {project} frame {frame} under {', '.join(map(str, art_dirs))}"


def make_work_copy(repo: Path, dest: Path) -> Path:
    """The smallest copy the renderer needs: main/, tools/uisim/, sdkconfig. Never the original."""
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    shutil.copytree(repo / "main", dest / "main")
    shutil.copytree(repo / "tools" / "uisim", dest / "tools" / "uisim")
    shutil.copy2(repo / "sdkconfig", dest / "sdkconfig")
    return dest


def replace_array(path: Path, name: str, data: bytes):
    text = path.read_text()
    start, end = find_array(text, name)
    path.write_text(text[:start] + ",".join(map(str, data)) + text[end:])


# ------------------------------------------------------------------ simulator calls

class Sim:
    def __init__(self, sim: Path, repo: Path, log: list):
        self.cli, self.repo, self.log = sim / "bin" / "ws-ui", repo, log
        self.cache = Path(os.environ.get("WS_UI_CACHE", Path.home() / ".cache" / "workstation-ui")) / "renders"

    def render(self, target: str, fixture: str | None, advance_ms: int, state: dict | None = None) -> dict:
        cmd = [str(self.cli), "--repo", str(self.repo), "render", target, "--advance-ms", str(advance_ms)]
        if fixture:
            cmd += ["--fixture", fixture]
        for k, v in (state or {}).items():
            cmd += ["--set", f"{k}={str(v).lower() if isinstance(v, bool) else v}"]
        rc, out = run(cmd, timeout=900)
        self.log.append({"cmd": cmd, "rc": rc, "output": out.strip()[-4000:]})
        m = re.search(r"files: (.*?)/screen\.png", out)
        rdir = Path(m.group(1)) if m else None
        if rdir is None:
            m = re.search(r"render (\S+?): ", out)
            rdir = self.cache / m.group(1) if m else None
        return {"cmd": cmd, "rc": rc, "ok": rc == 0 and m is not None and (rdir / "screen.png").is_file(),
                "dir": rdir, "output": out}


def keep(render: dict, dest: Path, stem: str) -> dict:
    """Copy the render record into the bundle at once (the renderer keeps only 60)."""
    dest.mkdir(parents=True, exist_ok=True)
    kept = {}
    src = render["dir"]
    for fname, out in (("screen.png", f"{stem}.png"), ("view.png", f"{stem}@2x.png"), ("checks.json", f"{stem}.checks.json"),
                       ("tree.json", f"{stem}.tree.json"), ("log.txt", f"{stem}.log.txt"), ("state.json", f"{stem}.state.json")):
        if src and (src / fname).is_file():
            shutil.copy2(src / fname, dest / out)
            kept[fname] = dest / out
    if not kept.get("log.txt"):
        (dest / f"{stem}.log.txt").write_text(render["output"])
        kept["log.txt"] = dest / f"{stem}.log.txt"
    return kept


# ------------------------------------------------------------------ images

def font(size=12):
    try:
        return ImageFont.load_default(size)
    except TypeError:
        return ImageFont.load_default()


def sheet(cells: list[tuple[Image.Image, str, bool]], cols: int, title: str) -> Image.Image:
    """Grid of native-size images. Labels and flag borders sit in the margins, so every
    image pixel is exactly what the simulator produced."""
    pad, lab, top = 6, 16, 22
    cw = max(c[0].width for c in cells) + 2 * pad
    ch = max(c[0].height for c in cells) + 2 * pad + lab
    rows = (len(cells) + cols - 1) // cols
    out = Image.new("RGB", (cw * cols, top + ch * rows), (24, 24, 28))
    d = ImageDraw.Draw(out)
    d.text((pad, 4), title, fill=(235, 235, 235), font=font(13))
    for i, (img, label, flagged) in enumerate(cells):
        x, y = (i % cols) * cw, top + (i // cols) * ch
        if flagged:
            d.rectangle([x + 1, y + 1, x + cw - 2, y + ch - 2], outline=(230, 60, 60), width=3)
        out.paste(img.convert("RGB"), (x + pad, y + pad))
        d.text((x + pad, y + pad + img.height + 1), label, fill=(255, 120, 120) if flagged else (200, 200, 200), font=font(12))
    return out


def changed_pixels(a: Image.Image, b: Image.Image) -> tuple[int, tuple | None]:
    diff = ImageChops.difference(a.convert("RGB"), b.convert("RGB"))
    r, g, bl = diff.split()
    mask = ImageChops.lighter(ImageChops.lighter(r, g), bl).point(lambda v: 255 if v else 0)
    return mask.histogram()[255], mask.getbbox()


def motion_stats(frames: list[tuple[int, Image.Image]]) -> dict:
    """Deterministic frame-to-frame facts plus heuristic hints. Hints point a reviewer at a
    moment; they are not a judgement that the motion is right or wrong."""
    steps = []
    for (t0, a), (t1, b) in zip(frames, frames[1:]):
        n, box = changed_pixels(a, b)
        steps.append({"from_ms": t0, "to_ms": t1, "changed_px": n, "bbox": box})
    moving = sorted(s["changed_px"] for s in steps if s["changed_px"])
    median = moving[len(moving) // 2] if moving else 0
    hints = []
    if not moving:
        hints.append({"kind": "no-motion", "detail": "no pixel changed across the whole capture"})
    limit = max(3 * median, 150)

    def overlaps(a, b):
        return a and b and a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
    for i, s in enumerate(steps):
        if s["changed_px"] > limit:
            nxt = steps[i + 1] if i + 1 < len(steps) else None
            continues = bool(nxt and overlaps(s["bbox"], nxt["bbox"]))
            kind = "large-step" if continues or nxt is None else "large-step-then-still"
            hints.append({"kind": kind, "at_ms": s["to_ms"], "changed_px": s["changed_px"], "bbox": s["bbox"],
                          "detail": f"{s['changed_px']} px changed in one step (median moving step {median} px)"
                                    + ("; that area keeps moving next frame (a jump or a cut)" if continues else
                                       "; nothing moves there in the next frame: something appeared, vanished or "
                                       "stopped dead here" if nxt else "; last captured step")})
    # last change: where motion settles (an exit that ends abruptly shows up here too)
    last = max((s["to_ms"] for s in steps if s["changed_px"]), default=None)
    return {"frames": len(frames), "median_moving_step_px": median, "last_change_ms": last, "steps": steps, "hints": hints}


def pick(frames: list, flagged_ms: set, limit: int = 20) -> list:
    if len(frames) <= limit:
        return frames
    idx = {round(i * (len(frames) - 1) / (limit - 1)) for i in range(limit)}
    for i, (t, _) in enumerate(frames):
        if t in flagged_ms:
            idx |= {max(0, i - 1), i}
    return [frames[i] for i in sorted(idx)]


# ------------------------------------------------------------------ the run

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plan", required=True, help="plan name in tools/review_loop/plans/ or a path")
    ap.add_argument("--repo", type=Path, default=DEFAULT_REPO, help="firmware checkout (default: this one)")
    ap.add_argument("--sim", type=Path, default=DEFAULT_SIM, help="workstation-lvgl-sim checkout ($WS_SIM)")
    ap.add_argument("--art", type=Path, action="append", default=[], help="folder with Artwork Workshop exports or delivery packages")
    ap.add_argument("--integrate", action="store_true", help="render a disposable firmware copy with the --art exports put in")
    ap.add_argument("--out", type=Path, help="bundle folder (default ~/workstation-reviews/<time>-<plan>)")
    ap.add_argument("--only", choices=["stills", "motion", "tests", "references"], action="append",
                    help="run only these render groups (default: all)")
    a = ap.parse_args(argv)

    plan_path = a.plan if a.plan.endswith(".json") else HERE / "plans" / f"{a.plan}.json"
    plan = json.loads(Path(plan_path).read_text())
    repo = a.repo.resolve()
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%S")
    out = (a.out or Path.home() / "workstation-reviews" / f"{stamp}-{plan['name']}").resolve()
    work = out.parent / f".{out.name}-work"
    if out.exists() and any(out.iterdir()):
        sys.exit(f"refusing to overwrite non-empty {out}")
    out.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    res, commands = Results(), []
    started = now_utc()
    print(f"review {plan['name']} -> {out}")

    # 1. provenance --------------------------------------------------------------
    fw = git_info(repo)
    sim = git_info(a.sim) if (a.sim / ".git").exists() else {"path": str(a.sim), "commit": None}
    lvgl = git_info(a.sim / "simulator" / "lib" / "lvgl") if (a.sim / "simulator/lib/lvgl/.git").exists() else None
    manifest = {"schema": "workstation-review/v1", "plan": plan["name"], "plan_file": str(plan_path),
                "plan_sha256": sha256(plan_path), "started_utc": started, "firmware": fw, "simulator": sim,
                "lvgl": lvgl, "art_inputs": [str(p) for p in a.art], "integrate": a.integrate,
                "tools": {"python": sys.version.split()[0], "gcc": run(["gcc", "--version"])[1].splitlines()[0]}}
    if not a.sim.joinpath("bin", "ws-ui").is_file():
        res.add("provenance", "simulator", UNAVAILABLE, f"no ws-ui at {a.sim}/bin (pass --sim or set WS_SIM)", required=True)
        return finish(out, work, plan, manifest, res, commands)
    res.add("provenance", "firmware", PASS if not fw["dirty_files"] else WARN,
            f"{fw['branch']} {fw['commit'][:12]}" + (f", {len(fw['dirty_files'])} uncommitted file(s) included" if fw["dirty_files"] else ", clean"))
    res.add("provenance", "simulator", PASS if sim.get("commit") else WARN,
            f"{sim.get('branch')} {(sim.get('commit') or 'not a git checkout')[:12]}; LVGL {(lvgl or {}).get('commit', '?')[:12]}")

    # 2. target integrity -------------------------------------------------------
    reg = json.loads((repo / "tools/uisim/targets.json").read_text())["targets"]
    names = [plan["target"]] + [r["target"] for r in plan.get("references", [])]
    for name in dict.fromkeys(names):
        t = reg.get(name)
        if not t:
            res.add("target", name, FAIL, "not registered in tools/uisim/targets.json")
            continue
        missing = [p for p in [t["adapter"], *t["sources"]] if not (repo / p).is_file()]
        missing += [f"main/{h}" for h in t.get("headers", []) if not (repo / "main" / h).is_file()]
        cmake = (repo / "main/CMakeLists.txt").read_text()
        unbuilt = [s for s in t["sources"] if f'"{Path(s).name}"' not in cmake]
        if missing:
            res.add("target", name, FAIL, f"missing files: {missing}")
        elif unbuilt:
            res.add("target", name, WARN, f"simulator links files the firmware build does not compile: {unbuilt}")
        else:
            res.add("target", name, PASS, f"{len(t['sources'])} firmware sources, all in main/CMakeLists.txt; adapter {t['adapter']}")
    target = reg.get(plan["target"])
    if not target:
        return finish(out, work, plan, manifest, res, commands)
    files = target_files(repo, target)
    manifest["target"] = {"name": plan["target"], "ui": plan.get("ui"), "description": target.get("description"),
                          "files": {f: sha256(repo / f) for f in files}}

    # 3. asset integrity ----------------------------------------------------------
    n_ok, bad, unchecked = 0, [], 0
    for f in files:
        text = (repo / f).read_text(errors="replace")
        if "lv_image_dsc_t" not in text:
            continue
        for d in descriptors(text):
            status, detail = check_descriptor(d, text)
            if status == PASS:
                n_ok += 1
            elif status == FAIL:
                bad.append(f"{f}:{d['name']}: {detail}")
            else:
                unchecked += 1
    res.add("assets", "descriptors", FAIL if bad else PASS,
            "; ".join(bad) if bad else f"{n_ok} image descriptors consistent ({unchecked} not literal, not checked)")

    # 4. art fidelity / integration ---------------------------------------------------
    render_repo = repo
    bindings = plan.get("art", [])
    if bindings and not a.art:
        res.add("art", "exports", UNAVAILABLE, f"{len(bindings)} art binding(s) in the plan, no --art folder given: "
                "export fidelity NOT checked; renders show the art already committed in firmware")
    elif a.art:
        if a.integrate:
            render_repo = make_work_copy(repo, work / "firmware-under-review")
        receipts = []
        for b in bindings:
            label = b["symbol"]
            src = b.get("source")
            if not src:
                res.add("art", label, UNAVAILABLE, b.get("note", "no Workshop source recorded for this asset"))
                continue
            img, info = find_export(a.art, src["project"], src.get("frame", 0))
            if img is None:
                res.add("art", label, UNAVAILABLE, info)
                continue
            text = (repo / b["file"]).read_text()
            d = next((d for d in descriptors(text) if d["name"] == label), None)
            fw_bytes = array_bytes(text, d["data"]) if d else None
            if fw_bytes is None:
                res.add("art", label, FAIL, f"descriptor/array {label} not found in {b['file']}")
                continue
            if img.size != (d["w"], d["h"]):
                res.add("art", label, FAIL, f"export is {img.size[0]}x{img.size[1]}, firmware slot is {d['w']}x{d['h']}", source=info)
                continue
            try:
                want = convert(img, d["cf"])
            except ValueError as e:
                res.add("art", label, FAIL, str(e), source=info)
                continue
            bpp = BPP[d["cf"]]
            diff_px = sum(want[i:i + bpp] != fw_bytes[i:i + bpp] for i in range(0, len(want), bpp))
            receipts.append({"symbol": label, "file": b["file"], "source": info, "identical": diff_px == 0, "differing_px": diff_px})
            if diff_px == 0:
                res.add("art", label, PASS, f"firmware bytes identical to {info['kind']} {src['project']}#{src.get('frame', 0)}", source=info)
            elif a.integrate:
                replace_array(render_repo / b["file"], d["data"], want)
                again = array_bytes((render_repo / b["file"]).read_text(), d["data"])
                res.add("art", label, PASS if again == want else FAIL,
                        f"{diff_px} px differed; integrated into the disposable copy only (renders below use it)", source=info)
            else:
                res.add("art", label, WARN, f"{diff_px} of {d['w'] * d['h']} px differ from {info['kind']} "
                        f"{src['project']}#{src.get('frame', 0)}: renders show the OLD firmware art (use --integrate)", source=info)
        manifest["art"] = receipts
        if a.integrate:
            for b in {b["file"] for b in bindings if b.get("source")}:
                dest = out / "integrated" / b
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(render_repo / b, dest)
            res.add("art", "integration", INFO, "renders below use a disposable firmware copy with the --art exports "
                    "put in (changed files kept in integrated/); the firmware checkout was not modified")
            manifest["rendered_from"] = {"kind": "disposable copy with integrated art", "path": str(render_repo),
                                         "base_commit": fw["commit"],
                                         "changed_files": {b["file"]: sha256(render_repo / b["file"]) for b in bindings}}
    manifest.setdefault("rendered_from", {"kind": "firmware checkout", "path": str(repo), "commit": fw["commit"]})

    # 5. renders -----------------------------------------------------------------------
    simr = Sim(a.sim, render_repo, commands)
    groups = set(a.only or ["stills", "motion", "tests", "references"])
    stills_dir, motion_dir, tests_dir, ref_dir = out / "stills", out / "motion", out / "tests", out / "references"
    stills, refs, motion_sheets = [], [], []

    def native(kept, stage, name) -> Image.Image | None:
        img = Image.open(kept["screen.png"])
        if img.size != (W, H):
            res.add(stage, name, FAIL, f"screen.png is {img.size}, not native {W}x{H}")
            return None
        return img

    def mech(kept, stage, name, accepted):
        """accepted: {check: reason} from the plan, for findings that are intentional."""
        findings = json.loads(kept["checks.json"].read_text()) if kept.get("checks.json") else []
        warns = [f for f in findings if f["level"] == "warn" and f["check"] not in accepted]
        infos = [f for f in findings if f["level"] != "warn" or f["check"] in accepted]
        detail = f"{len(warns)} warning(s), {len(infos)} info/accepted finding(s)"
        if warns:
            detail += ": " + "; ".join(f"{w['check']}: {w['message']}" for w in warns[:4])
        took = sorted({f["check"] for f in findings if f["level"] == "warn" and f["check"] in accepted})
        if took:
            detail += "; accepted by plan: " + "; ".join(f"{c} ({accepted[c]})" for c in took)
        res.add(stage, name + ":checks", WARN if warns else PASS, detail)

    if "stills" in groups:
        for s in plan.get("stills", []):
            r = simr.render(plan["target"], s.get("fixture"), s.get("advance_ms", 400), s.get("state"))
            if not r["ok"]:
                res.add("still", s["name"], FAIL, "render failed: " + r["output"].strip()[-300:], command=r["cmd"])
                continue
            kept = keep(r, stills_dir, s["name"])
            img = native(kept, "still", s["name"])
            if img:
                res.add("still", s["name"], PASS, f"native {W}x{H} at {s.get('advance_ms', 400)} ms ({s.get('fixture') or 'default'})",
                        command=r["cmd"], files=[str(p.relative_to(out)) for p in kept.values()])
                stills.append((s, img, kept))
                mech(kept, "still", s["name"], s.get("accept", {}))

    if "motion" in groups:
        for m in plan.get("motion", []):
            frames = []
            mdir = motion_dir / m["name"]
            if m.get("mode", "capture") == "capture":
                cap = work / "capture" / m["name"]
                shutil.rmtree(cap, ignore_errors=True)
                cap.mkdir(parents=True)
                r = simr.render(plan["target"], m.get("fixture"), m["advance_ms"], {**m.get("state", {}), "capture_dir": str(cap)})
                if not r["ok"]:
                    res.add("motion", m["name"], FAIL, "render failed: " + r["output"].strip()[-300:], command=r["cmd"])
                    continue
                keep(r, mdir, "final")
                for p in sorted(cap.glob("frame-*.rgb")):
                    raw = p.read_bytes()
                    if len(raw) == W * H * 3:
                        frames.append((int(p.stem.split("-")[1]), Image.frombytes("RGB", (W, H), raw)))
                cmd_note = r["cmd"]
            else:
                cmd_note = []
                for t in m["sample_ms"]:
                    r = simr.render(plan["target"], m.get("fixture"), t, m.get("state"))
                    cmd_note.append(r["cmd"])
                    if r["ok"]:
                        frames.append((t, Image.open(r["dir"] / "screen.png").convert("RGB")))
            if len(frames) < 2:
                res.add("motion", m["name"], FAIL, f"only {len(frames)} frame(s) captured: the adapter may not support "
                        "capture_dir, or the render failed", command=cmd_note)
                continue
            (mdir / "frames").mkdir(parents=True, exist_ok=True)
            for t, img in frames:
                img.save(mdir / "frames" / f"{t:05d}.png")
            st = motion_stats(frames)
            (mdir / "motion.json").write_text(json.dumps({k: v for k, v in st.items()}, indent=1))
            flagged = {h["at_ms"] for h in st["hints"] if "at_ms" in h}
            change = {s["to_ms"]: s["changed_px"] for s in st["steps"]}
            chosen = pick(frames, flagged)
            sh = sheet([(img, f"{t} ms  d={change.get(t, 0)}px", t in flagged) for t, img in chosen], 4,
                       f"{plan['name']} / motion {m['name']}: {len(chosen)} of {len(frames)} frames, native 320x240; "
                       "red = large one-step change")
            sh.save(mdir / "contact-sheet.png")
            motion_sheets.append((m["name"], mdir / "contact-sheet.png"))
            durations = [max(20, b[0] - a_[0]) for a_, b in zip(frames, frames[1:])] + [400]
            frames[0][1].save(mdir / "animation.gif", save_all=True, append_images=[f for _, f in frames[1:]],
                              duration=durations, loop=0)
            res.add("motion", m["name"], WARN if st["hints"] else PASS,
                    f"{len(frames)} frames {frames[0][0]}-{frames[-1][0]} ms; last change at {st['last_change_ms']} ms; "
                    f"{len(st['hints'])} hint(s) for review" + (": " + "; ".join(
                        f"{h['kind']}@{h.get('at_ms', '-')}ms" for h in st["hints"][:5]) if st["hints"] else ""),
                    command=cmd_note, hints=st["hints"])

    if "tests" in groups:
        for t in plan.get("tests", []):
            r = simr.render(plan["target"], t.get("fixture"), t["advance_ms"], t.get("state"))
            kept = keep(r, tests_dir, t["name"]) if r["dir"] else {}
            log = kept["log.txt"].read_text() if kept.get("log.txt") else r["output"]
            if not r["ok"]:
                res.add("test", t["name"], FAIL, "simulator run failed (an assert aborts the run): " + log.strip()[-400:], command=r["cmd"])
            elif t["require"] not in log:
                res.add("test", t["name"], FAIL, f"render finished but '{t['require']}' was never printed: the test did "
                        f"not reach its end within {t['advance_ms']} ms", command=r["cmd"])
            else:
                line = next(ln for ln in log.splitlines() if t["require"] in ln)
                res.add("test", t["name"], PASS, line.strip(), command=r["cmd"])

    if "references" in groups:
        for ref in plan.get("references", []):
            r = simr.render(ref["target"], ref.get("fixture"), ref.get("advance_ms", 400), ref.get("state"))
            if not r["ok"]:
                res.add("reference", ref["name"], FAIL, "render failed: " + r["output"].strip()[-300:], command=r["cmd"])
                continue
            kept = keep(r, ref_dir, ref["name"])
            refs.append((ref, Image.open(kept["screen.png"])))
            res.add("reference", ref["name"], INFO, f"rendered {ref['target']} for side-by-side comparison: {ref.get('why', '')}",
                    command=r["cmd"])

    # 6. review images -------------------------------------------------------------
    if stills:
        cells = [(img, f"{s['name']}  ({s.get('fixture') or 'default'} @{s.get('advance_ms', 400)}ms)", False) for s, img, _ in stills]
        cells += [(img, f"REFERENCE {r['name']} (not under review)", False) for r, img in refs]
        sheet(cells, 3, f"{plan['name']}: native 320x240 simulator stills, firmware {fw['commit'][:10]}").save(out / "review-sheet.png")
        crops = []
        by_name = {s["name"]: img for s, img, _ in stills}
        for c in plan.get("focus", []):
            img = by_name.get(c["still"])
            if img:
                x1, y1, x2, y2 = c["rect"]
                k = c.get("scale", 4)
                crops.append((img.crop((x1, y1, x2, y2)).resize(((x2 - x1) * k, (y2 - y1) * k), Image.NEAREST),
                              f"{c['name']}  [{c['still']} {x1},{y1}-{x2},{y2} x{k}]", False))
        if crops:
            sheet(crops, 2, f"{plan['name']}: focus crops (nearest-neighbour enlargements, no smoothing)").save(out / "focus-crops.png")
    return finish(out, work, plan, manifest, res, commands)


def finish(out: Path, work: Path, plan: dict, manifest: dict, res: Results, commands: list) -> int:
    overall, code = res.overall()
    manifest["finished_utc"] = now_utc()
    manifest["overall"] = overall
    (out / "results.json").write_text(json.dumps({"overall": overall, "results": res.items}, indent=1, default=str))
    (out / "commands.json").write_text(json.dumps(commands, indent=1))
    (out / "DOTTIE.md").write_text(dottie(plan, manifest, res, out))
    manifest["files"] = {str(p.relative_to(out)): sha256(p) for p in sorted(out.rglob("*")) if p.is_file() and p.name != "manifest.json"}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1, default=str))
    z = out.with_suffix(".zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(out.rglob("*")):
            if p.is_file():
                zf.write(p, Path(out.name) / p.relative_to(out))
    shutil.rmtree(work, ignore_errors=True)
    print(f"\n{overall}: {out}\nzip: {z} ({z.stat().st_size // 1024} KB)")
    return code


def dottie(plan: dict, manifest: dict, res: Results, out: Path) -> str:
    fw, sim = manifest["firmware"], manifest["simulator"]
    overall, _ = res.overall()
    rows = "\n".join(f"| {i['stage']} | {i['name']} | {i['status']} | {i['detail'].replace('|', '/')[:300]} |" for i in res.items)
    hints = [f"- motion `{i['name']}`: {h['detail']} (t={h.get('at_ms', '-')} ms, bbox {h.get('bbox')})"
             for i in res.items if i["stage"] == "motion" for h in i.get("hints", [])]
    questions = "\n".join(f"{n}. {q}" for n, q in enumerate(plan.get("visual_review", []), 1))
    present = sorted(str(p.relative_to(out)) for p in out.rglob("*.png") if "/frames/" not in str(p) and "@2x" not in p.name)
    rendered = manifest.get("rendered_from", {})
    return f"""# Review request: {plan['name']} ({plan.get('ui', '')})

Deterministic result: **{overall}**. That covers only what a program can check. It does not say
the art looks good, and nothing here was run on the BOX-3.

## What this is

- Firmware `{fw.get('branch')}` @ `{fw.get('commit')}`{' with uncommitted files: ' + ', '.join(fw['dirty_files']) if fw.get('dirty_files') else ''}
- Rendered from: {rendered.get('kind')} ({rendered.get('path')})
- Simulator `{sim.get('branch')}` @ `{sim.get('commit')}`, LVGL `{(manifest.get('lvgl') or {}).get('commit')}`
- Target `{plan['target']}`: {manifest.get('target', {}).get('description', '')}
- Every image is the simulator's own 320x240 RGB565 frame. `*@2x.png` are 2x nearest-neighbour copies for phones.
  Sheets add labels and red flag borders only in the margins.

## Files to look at first

1. `review-sheet.png`: every still, plus reference scene(s) marked REFERENCE.
2. `motion/*/contact-sheet.png`: animation frames with times; `motion/*/animation.gif` to watch
   (GIF uses a palette, so judge pixels from the PNGs). All frames: `motion/*/frames/`.
3. `focus-crops.png`: enlarged areas the plan asks about.

All PNGs: {', '.join(present)}

## Deterministic checks (program-verified)

| stage | item | status | detail |
|---|---|---|---|
{rows}

Motion hints (where to look, not verdicts):
{chr(10).join(hints) or '- none'}

## Your visual judgement (no program checked these)

{questions}

General, for every still and motion: grounding (does each object sit on or in something, with
contact or shadow?), silhouette readability at 320x240, texture and material detail, depth and
light direction, composition (do the landmarks form one place or float as separate stickers?),
palette coherence, and motion continuity (does anything pop, vanish, overlap wrongly or freeze?).
Compare against the REFERENCE scene for the quality bar, not for its content.

## How to answer, so the next correction can be acted on

For each problem, one line:
`[still or motion name] [where: element and x,y in 320x240 pixels] [category: art | layout | motion | software | unsure] [severity: blocker | major | minor] [what is wrong] [the correction you suggest]`

Say "art" when the fix is in the Pixelorama artwork, "layout"/"motion" when it is in where or how
firmware places or animates it, "software" for anything that looks like a bug.

## Not proven by this bundle

- Physical screen colours, viewing angle, glare, frame rate, touch feel, audio, internal RAM/heap and
  flash use: the simulator models none of them.
- An ESP-IDF build: not run here. Device: not flashed, not deployed.
- Art quality: only your review (and Mike's) can judge it.
"""


if __name__ == "__main__":
    sys.exit(main())
