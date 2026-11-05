#!/usr/bin/env python3
"""Workstation review loop: exact inputs -> real LVGL simulator -> review bundle.

    python3 tools/review_loop/review.py --plan artifacts_home --sim PATH --art-lock LOCK.json --art DIR
    python3 tools/review_loop/review.py --plan artifacts_home --sim PATH --committed-art-diagnostic

Evidence rules this tool enforces:

  * Inputs are frozen first. The firmware files a render can read (main/, tools/uisim/,
    sdkconfig, build/lock files) are copied into the run's snapshot and hashed; every render
    uses the snapshot. At the end the live checkout and simulator are hashed again: if
    anything changed during the run the result FAILS (the evidence no longer matches them).
  * Art selection is exact. --art-lock pins, per Workshop project, the delivery package's
    revision, checkpoint, manifest SHA-256 and file SHA-256s. Only that package can satisfy
    it. If it is missing, corrupt, ambiguous or the wrong size, that binding FAILS and the
    run is blocked. Older packages and loose exports are never used instead.
  * Committed-art renders are a separate, labelled diagnostic (--committed-art-diagnostic).
    They never count as validation of a native checkpoint.
  * A plan names its required stages. A required stage that failed, could not run or is
    unsupported makes the technical result FAIL or INCOMPLETE (exit 1 / 2), never PASS.
  * Firmware build membership is only claimed from a configured build (--build-dir with
    compile_commands.json). Without one it is reported as not run.
  * Readiness is reported on separate axes; this tool never sets visual acceptance,
    delivery, firmware build or hardware acceptance.

It never edits the firmware checkout (integration happens in the snapshot), commits,
promotes art, runs an ESP-IDF build, flashes or deploys.
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
    import PIL
    from PIL import Image, ImageChops, ImageDraw, ImageFont
except ImportError:  # the art converters in tools/ need Pillow too
    sys.exit("review.py needs Pillow: python3 -m pip install pillow")

W, H = 320, 240
HERE = Path(__file__).resolve().parent
DEFAULT_REPO = HERE.parents[1]
DEFAULT_SIM = Path(os.environ.get("WS_SIM", "/opt/workstation/lvgl-sim"))
BPP = {"ARGB8888": 4, "XRGB8888": 4, "RGB888": 3, "RGB565": 2, "A8": 1, "L8": 1}

PASS, WARN, FAIL, UNAVAILABLE, UNSUPPORTED, INFO, SKIPPED = (
    "PASS", "WARN", "FAIL", "UNAVAILABLE", "UNSUPPORTED", "INFO", "SKIPPED")
BLOCKING = {FAIL, UNAVAILABLE, UNSUPPORTED}

# What a render can read from the firmware checkout; frozen into the snapshot.
SNAPSHOT_DIRS = ["main", "tools/uisim"]
SNAPSHOT_FILES = ["sdkconfig", "sdkconfig.defaults", "CMakeLists.txt", "dependencies.lock", "partitions.csv"]
# Never copied, hashed into the manifest or bundled (private device configuration).
PRIVATE_NAMES = {"wifi_credentials.h"}
PRIVATE_SUFFIXES = (".key", ".pem.key")
# The LVGL sources the renderer compiles are part of what a render depends on: hashed at the
# start (provenance) and again at the end (drift), like the simulator's own files. LVGL's
# configuration comes from the firmware sdkconfig, which the snapshot already freezes.
SIM_IDENTITY = ["bin", "wsui", "simulator/main.c", "simulator/ws_fixture.c", "simulator/hal",
                "simulator/export", "simulator/include", "simulator/lib/lvgl/src", "simulator/lib/lvgl/lvgl.h"]
SECRET_PATTERNS = [re.compile(p) for p in (
    r"-----BEGIN [A-Z ]*PRIVATE KEY-----", r"\bgh[pousr]_[A-Za-z0-9]{30,}", r"\bgithub_pat_[A-Za-z0-9_]{30,}",
    r"\bAKIA[0-9A-Z]{16}\b", r"\bsk-[A-Za-z0-9]{32,}", r"(?i)\b(password|passwd|api[_-]?key|secret|token)\s*[=:]\s*\S{8,}",
    r"\bya29\.[A-Za-z0-9_-]{20,}", r"(?i)WIFI_(PASS|PASSWORD|SSID)\s+\"")]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


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


def is_private(path: Path) -> bool:
    return path.name in PRIVATE_NAMES or path.name.endswith(PRIVATE_SUFFIXES)


RENDER_GROUPS = ("stills", "motion", "tests", "references")
ENTRY_KEYS = {"stills": ("name",), "motion": ("name", "advance_ms"), "tests": ("name", "require", "advance_ms"),
              "references": ("name", "target"), "art": ("symbol", "file")}


def load_plan(path: Path) -> tuple[dict, list[str]]:
    """The plan and every structural problem in it. A broken plan is reported, never a traceback;
    a required stage that lists nothing can never pass."""
    try:
        plan = json.loads(path.read_text())
    except (OSError, ValueError) as e:
        return {"name": path.stem}, [f"unreadable plan {path}: {e}"]
    if not isinstance(plan, dict):
        return {"name": path.stem}, [f"plan {path} is not a JSON object"]
    errors = [f"plan has no '{k}'" for k in ("name", "target") if not isinstance(plan.get(k), str)]
    if not isinstance(plan.get("name"), str):
        plan["name"] = path.stem
    required = plan.get("required", ["stills", "tests"])
    if not isinstance(required, list):
        return {"name": path.stem, **plan}, errors + ["'required' must be a list"]
    unknown = sorted(set(required) - set(ENTRY_KEYS))
    if unknown:
        errors.append(f"unknown required stage(s) {unknown}; known: {sorted(ENTRY_KEYS)}")
    for group, keys in ENTRY_KEYS.items():
        entries = plan.get(group, [])
        if not isinstance(entries, list):
            errors.append(f"'{group}' must be a list")
            continue
        if group in required and not entries:
            errors.append(f"plan requires '{group}' but lists none: an empty required stage cannot pass")
        for n, e in enumerate(entries):
            gone = [k for k in keys if not isinstance(e, dict) or k not in e]
            if gone:
                errors.append(f"{group}[{n}] lacks {gone}")
        names = [e.get("name") for e in entries if isinstance(e, dict) and "name" in e]
        dup = sorted({x for x in names if names.count(x) > 1})
        if dup:
            errors.append(f"{group}: duplicate names {dup} (their files would overwrite each other)")
    return plan, errors


def native_receipt(manifest: dict, files: dict[str, str]) -> tuple[str, str]:
    """What the package says about native editability. This tool cannot reopen a .pxo, so the best
    it can report is a consistent Workshop receipt (a CLAIM); a filename or hash is never evidence."""
    nc = manifest.get("native_check")
    if not isinstance(nc, dict):
        return UNAVAILABLE, ("UNVERIFIED: the package manifest has no native_check receipt (no fresh Pixelorama "
                             "reopen recorded); a .pxo name or matching SHA-256 does not establish native validity")
    if nc.get("match") is not True or nc.get("same_structure") is False:
        return FAIL, "the package's native_check receipt records a failed or incomplete native check"
    atlas = next((sha for name, sha in files.items() if name.endswith("-atlas.png")), None)
    if atlas is None or nc.get("atlas_sha256") != atlas:
        return FAIL, "native_check atlas_sha256 is not the packaged atlas: the receipt is not about these bytes"
    if not nc.get("verify_boot_id") or nc.get("verify_boot_id") == nc.get("reference_boot_id"):
        return FAIL, "native_check has no separate verify process (verify_boot_id missing or equal to the reference)"
    return INFO, (f"CLAIMED by the Workshop receipt: fresh reopen (boot {str(nc['verify_boot_id'])[:12]}) matched the "
                  f"{nc.get('reference', '?')} reference; not re-run by this tool")


class Results:
    """Every stage result, in order. Nothing is reported as passing unless it ran."""

    def __init__(self):
        self.items: list[dict] = []

    def add(self, stage: str, name: str, status: str, detail: str, required: bool = False, **data):
        self.items.append({"stage": stage, "name": name, "status": status, "required": required, "detail": detail, **data})
        print(f"  [{status:11}] {stage}/{name}{' (required)' if required else ''}: {detail}", flush=True)

    def technical(self) -> tuple[str, int]:
        """FAIL (1): something failed. INCOMPLETE (2): a required stage could not run or is
        unsupported. PASS_WITH_WARNINGS / PASS (0) only when every required stage passed."""
        if any(i["status"] == FAIL for i in self.items):
            return "FAIL", 1
        if any(i["status"] in BLOCKING and i["required"] for i in self.items):
            return "INCOMPLETE", 2
        states = {i["status"] for i in self.items}
        return ("PASS_WITH_WARNINGS" if states & {WARN, UNAVAILABLE, UNSUPPORTED} else "PASS"), 0

    def stage_ok(self, stage: str) -> bool:
        got = [i for i in self.items if i["stage"] == stage]
        return bool(got) and all(i["status"] not in BLOCKING for i in got)


# ------------------------------------------------------------------ provenance

def git_info(path: Path) -> dict:
    def g(*args):
        rc, out = run(["git", "-C", str(path), *args])
        return out.strip() if rc == 0 else None
    url = g("remote", "get-url", "origin") or ""
    status = g("status", "--porcelain", "--untracked-files=all") or ""
    return {"commit": g("rev-parse", "HEAD"), "branch": g("rev-parse", "--abbrev-ref", "HEAD"),
            "remote": re.sub(r"//[^/@]*@", "//", url),
            "dirty_files": [ln[3:] for ln in status.splitlines() if not ln.startswith("??")],
            "untracked_files": [ln[3:] for ln in status.splitlines() if ln.startswith("??")]}


def tree_hashes(root: Path, entries: list[str]) -> dict[str, str]:
    out = {}
    for e in entries:
        p = root / e
        files = [p] if p.is_file() else sorted(q for q in p.rglob("*") if q.is_file()) if p.is_dir() else []
        for f in files:
            if is_private(f) or "__pycache__" in f.parts:
                continue
            out[str(f.relative_to(root))] = sha256(f)
    return out


def digest(hashes: dict[str, str]) -> str:
    return hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()


def drift(before: dict[str, str], after: dict[str, str]) -> list[str]:
    """Files added, removed or changed between two hash maps."""
    return sorted({k for k in before.keys() | after.keys() if before.get(k) != after.get(k)})


def freeze(repo: Path, dest: Path) -> tuple[dict[str, str], list[str]]:
    """Copy what a render can read into dest; return {file: sha256} and excluded private names."""
    excluded = []
    for d in SNAPSHOT_DIRS:
        src = repo / d
        for f in sorted(src.rglob("*")):
            if not f.is_file() or "__pycache__" in f.parts:
                continue
            if is_private(f):
                excluded.append(str(f.relative_to(repo)))
                continue
            t = dest / f.relative_to(repo)
            t.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, t)
    for f in SNAPSHOT_FILES:
        if (repo / f).is_file():
            shutil.copy2(repo / f, dest / f)
    return tree_hashes(dest, SNAPSHOT_DIRS + SNAPSHOT_FILES), excluded


def tool_identity(sim: Path, snapshot: Path) -> dict:
    ident = {"python": sys.version.split()[0], "pillow": PIL.__version__,
             "review_py_sha256": sha256(Path(__file__)),
             "gcc": (run(["gcc", "--version"])[1].splitlines() or ["?"])[0],
             "ar": (run(["ar", "--version"])[1].splitlines() or ["?"])[0]}
    code = ("import sys,json;sys.path.insert(0,sys.argv[1]);from wsui import build as b;from pathlib import Path;"
            "h=b.kconfig_header(Path(sys.argv[2])/'sdkconfig');c=b._lvgl_commit();f=' '.join([b.CC,*b.LIB_CFLAGS]);"
            "print(json.dumps({'lvgl_version':b.lvgl_version(),'lvgl_commit':c,'lvgl_config_hash':b._sha(h.encode(),c.encode(),f.encode()),"
            "'lvgl_cflags':f,'firmware_lvgl_lock':b.firmware_lvgl_version(Path(sys.argv[2]))}))")
    rc, out = run([sys.executable, "-c", code, str(sim), str(snapshot)])
    try:
        ident.update(json.loads(out.strip().splitlines()[-1]) if rc == 0 else {"lvgl_identity_error": out.strip()[-300:]})
    except (ValueError, IndexError):
        ident["lvgl_identity_error"] = out.strip()[-300:]
    return ident


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
            if p.is_file() and rel not in seen and not is_private(p):
                seen.add(rel)
                files.append(rel)
                queue.append(p)
    return files


def build_membership(build_dir: Path, repo: Path, sources: list[str]) -> tuple[str, str]:
    """Effective configured build evidence: is each source (by its project-relative path, not its
    basename) a compile unit in compile_commands.json? Each entry's project root is the parent of
    its build 'directory' (ESP-IDF builds in <project>/build), so a build configured in another
    checkout still maps main/x.c to main/x.c, and components/y/x.c never counts as main/x.c."""
    cc = build_dir / "compile_commands.json"
    if not cc.is_file():
        return UNAVAILABLE, f"{cc} not found: firmware build membership not verified (configure an ESP-IDF build first)"
    units = set()
    try:
        for e in json.loads(cc.read_text()):
            bdir = Path(e.get("directory") or build_dir)
            f = Path(e["file"])
            f = Path(os.path.normpath(f if f.is_absolute() else bdir / f))
            try:
                units.add(f.relative_to(Path(os.path.normpath(bdir)).parent).as_posix())
            except ValueError:
                pass   # outside that project (toolchain, managed components elsewhere)
    except (ValueError, KeyError, TypeError, AttributeError) as e:
        return FAIL, f"unreadable compile_commands.json: {e}"
    missing = [s for s in sources if s not in units]
    if missing:
        return WARN, f"simulator links sources this configured build does not compile: {missing}"
    return PASS, f"all {len(sources)} sources are compile units of the configured build {build_dir}"


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


def replace_array(path: Path, name: str, data: bytes):
    text = path.read_text()
    start, end = find_array(text, name)
    path.write_text(text[:start] + ",".join(map(str, data)) + text[end:])


# ------------------------------------------------------------------ exact art selection

class ArtError(Exception):
    pass


def load_lock(path: Path) -> dict[str, dict]:
    lock = json.loads(path.read_text())
    if lock.get("schema") != "workstation-review/art-lock-v1":
        raise ArtError(f"{path}: schema must be workstation-review/art-lock-v1")
    out = {}
    for e in lock.get("selection", []):
        for k in ("project", "revision", "checkpoint", "manifest_sha256", "files"):
            if k not in e:
                raise ArtError(f"{path}: selection entry missing '{k}'")
        if e["project"] in out:
            raise ArtError(f"{path}: project {e['project']} selected twice")
        out[e["project"]] = e
    return out


def select_package(art_dirs: list[Path], sel: dict) -> tuple[Path, dict, list[str]]:
    """The one delivery package that is exactly `sel`, verified. Raises ArtError otherwise.
    Other packages of the same project are reported, never used."""
    project = sel["project"]
    matches, others = [], []
    for root in art_dirs:
        for m in sorted(root.rglob("manifest.json")):
            try:
                j = json.loads(m.read_text())
            except (OSError, ValueError):
                continue
            if j.get("schema") != "pixelorama-workshop/delivery-v1" or j.get("project") != project:
                continue
            if j.get("revision") == sel["revision"] and j.get("checkpoint") == sel["checkpoint"]:
                matches.append((m, j))
            else:
                others.append(f"r{j.get('revision')} {j.get('checkpoint')} at {m.parent}")
    if not matches:
        raise ArtError(f"selected package {project} r{sel['revision']} {sel['checkpoint']} not found"
                       + (f" (ignored other packages: {others})" if others else ""))
    if len(matches) > 1:
        raise ArtError(f"selected package {project} r{sel['revision']} found {len(matches)} times: ambiguous "
                       f"({[str(m.parent) for m, _ in matches]})")
    m, j = matches[0]
    if sha256(m) != sel["manifest_sha256"]:
        raise ArtError(f"{m}: manifest SHA-256 {sha256(m)[:12]} is not the locked {sel['manifest_sha256'][:12]}")
    listed = {f["name"]: f for f in j.get("files", [])}
    if set(listed) != set(sel["files"]):
        raise ArtError(f"{m}: package files {sorted(listed)} differ from the locked set {sorted(sel['files'])}")
    for name, want in sel["files"].items():
        p = m.parent / name
        if not p.is_file():
            raise ArtError(f"{p}: selected file missing")
        got = sha256(p)
        if got != want or got != listed[name]["sha256"] or p.stat().st_size != listed[name].get("bytes", p.stat().st_size):
            raise ArtError(f"{p}: SHA-256 {got[:12]} does not match lock {want[:12]} / manifest {listed[name]['sha256'][:12]}")
    return m.parent, j, others


def frame_from_package(pkg: Path, manifest: dict, frame: int) -> Image.Image:
    atlas = next((pkg / f["name"] for f in manifest["files"] if f["name"].endswith("-atlas.png")), None)
    rect = next((f for f in manifest.get("atlas", {}).get("frames", []) if f.get("index") == frame), None)
    if atlas is None or rect is None:
        raise ArtError(f"{pkg}: no atlas or no frame {frame} in the manifest")
    img = Image.open(atlas)
    want = (manifest["atlas"].get("width"), manifest["atlas"].get("height"))
    if img.size != want:
        raise ArtError(f"{atlas}: atlas is {img.size}, manifest says {want}")
    return img.crop((rect["x"], rect["y"], rect["x"] + rect["width"], rect["y"] + rect["height"]))


def find_loose(art_dirs: list[Path], project: str, frame: int):
    """Diagnostic only: a loose exported frame or worker atlas. Never satisfies an art lock."""
    for root in art_dirs:
        for p in root.rglob(f"{project}/frame_{frame:03}.png"):
            return Image.open(p), {"kind": "loose exported frame (diagnostic)", "path": str(p), "sha256": sha256(p)}
        for aj in root.rglob(f"{project}/atlas.json"):
            j = json.loads(aj.read_text())
            rect = next((f for f in j.get("frames", []) if f.get("index") == frame), None)
            png = aj.parent / j.get("image", "atlas.png")
            if rect and png.is_file():
                return (Image.open(png).crop((rect["x"], rect["y"], rect["x"] + rect["width"], rect["y"] + rect["height"])),
                        {"kind": "loose worker atlas (diagnostic)", "path": str(png), "sha256": sha256(png)})
    return None, f"no loose export of {project} frame {frame}"


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
            m2 = re.search(r"render (\S+?)[):]", out)
            rdir = self.cache / m2.group(1) if m2 else None
        ok = rc == 0 and m is not None and rdir is not None and (rdir / "screen.png").is_file()
        return {"cmd": cmd, "rc": rc, "ok": ok, "dir": rdir, "output": out}


def keep(render: dict, dest: Path, stem: str) -> dict:
    """Copy the render record into the bundle at once (the renderer keeps only 60), including
    the log and adapter source of a FAILED render."""
    dest.mkdir(parents=True, exist_ok=True)
    kept = {}
    src = render["dir"]
    for fname, out in (("screen.png", f"{stem}.png"), ("view.png", f"{stem}@2x.png"), ("checks.json", f"{stem}.checks.json"),
                       ("tree.json", f"{stem}.tree.json"), ("log.txt", f"{stem}.log.txt"), ("state.json", f"{stem}.state.json")):
        if src and (src / fname).is_file():
            shutil.copy2(src / fname, dest / out)
            kept[fname] = dest / out
    (dest / f"{stem}.cli.txt").write_text(" ".join(render["cmd"]) + f"\nexit {render['rc']}\n" + render["output"])
    kept["cli"] = dest / f"{stem}.cli.txt"
    if not kept.get("log.txt"):
        kept["log.txt"] = kept["cli"]
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


def secret_scan(root: Path) -> list[str]:
    hits = []
    for p in sorted(root.rglob("*")):
        if not p.is_file() or p.suffix in (".png", ".gif", ".rgb", ".zip"):
            continue
        if is_private(p):
            hits.append(f"{p.relative_to(root)}: private file name")
            continue
        text = p.read_text(errors="replace")
        for rx in SECRET_PATTERNS:
            if rx.search(text):
                hits.append(f"{p.relative_to(root)}: matches {rx.pattern[:30]}")
                break
    return hits


# ------------------------------------------------------------------ the run

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plan", required=True, help="plan name in tools/review_loop/plans/ or a path")
    ap.add_argument("--repo", type=Path, default=DEFAULT_REPO, help="firmware checkout (default: this one)")
    ap.add_argument("--sim", type=Path, default=DEFAULT_SIM, help="workstation-lvgl-sim checkout ($WS_SIM)")
    ap.add_argument("--art-lock", type=Path, help="art-lock-v1 JSON pinning the exact Workshop packages")
    ap.add_argument("--art", type=Path, action="append", default=[], help="folder(s) holding Workshop delivery packages")
    ap.add_argument("--art-loose", type=Path, action="append", default=[],
                    help="DIAGNOSTIC: compare loose exports (frame_NNN.png / worker atlas); never validates a checkpoint")
    ap.add_argument("--committed-art-diagnostic", action="store_true",
                    help="render the art already committed in firmware, labelled as a diagnostic, not checkpoint validation")
    ap.add_argument("--build-dir", type=Path, help="configured ESP-IDF build dir (compile_commands.json) for build membership")
    ap.add_argument("--out", type=Path, help="run folder (default ~/workstation-reviews/<time>-<plan>)")
    ap.add_argument("--only", choices=["stills", "motion", "tests", "references"], action="append",
                    help="run only these render groups (the skipped required groups make the run INCOMPLETE)")
    a = ap.parse_args(argv)

    plan_path = Path(a.plan if a.plan.endswith(".json") else HERE / "plans" / f"{a.plan}.json")
    plan, plan_errors = load_plan(plan_path)
    repo, simdir = a.repo.resolve(), a.sim.resolve()
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%S")
    out = (a.out or Path.home() / "workstation-reviews" / f"{stamp}-{plan['name']}").resolve()
    if out.exists() and any(out.iterdir()):
        sys.exit(f"refusing to overwrite non-empty {out}")
    work = out.parent / f".{out.name}-work"
    shutil.rmtree(work, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True)
    res, commands = Results(), []
    req = plan.get("required", ["stills", "tests"])
    required = {r for r in req if isinstance(r, str)} if isinstance(req, list) else set()
    print(f"review {plan['name']} -> {out}")
    manifest = {"schema": "workstation-review/v2", "run_id": out.name, "plan": plan["name"],
                "plan_sha256": sha256(plan_path) if plan_path.is_file() else None, "started_utc": now_utc(),
                "required_stages": sorted(required) if isinstance(plan.get("required", []), list) else None,
                "arguments": {k: (str(v) if isinstance(v, Path) else [str(x) for x in v] if isinstance(v, list) else v)
                              for k, v in vars(a).items()}}

    # 0. the plan itself -------------------------------------------------------
    for err in plan_errors:
        res.add("plan", "structure", FAIL, err, required=True)
    if plan_errors:
        return finish(out, work, plan, manifest, res, commands)
    res.add("plan", "structure", PASS, f"{plan_path.name}: required {sorted(required)}; "
            + ", ".join(f"{len(plan.get(g, []))} {g}" for g in (*RENDER_GROUPS, "art")))

    # 1. freeze inputs --------------------------------------------------------
    if not (simdir / "bin" / "ws-ui").is_file():
        res.add("inputs", "simulator", UNAVAILABLE, f"no ws-ui at {simdir}/bin (pass --sim or set WS_SIM)", required=True)
        return finish(out, work, plan, manifest, res, commands)
    if not (repo / "tools/uisim/targets.json").is_file():
        res.add("inputs", "firmware", FAIL, f"{repo} has no tools/uisim/targets.json")
        return finish(out, work, plan, manifest, res, commands)
    fw = git_info(repo)
    if not fw["commit"]:
        manifest["firmware"] = {**fw, "path": str(repo)}
        res.add("inputs", "firmware", FAIL, f"{repo} is not a git checkout with a commit: the renders could not be "
                "tied to a firmware revision", required=True)
        return finish(out, work, plan, manifest, res, commands)
    snap = work / "firmware-snapshot"
    fw_hashes, excluded = freeze(repo, snap)
    sim_before = tree_hashes(simdir, SIM_IDENTITY)
    sim = git_info(simdir)
    lvgl = git_info(simdir / "simulator" / "lib" / "lvgl") if (simdir / "simulator/lib/lvgl/.git").exists() else {}
    dirty = fw["dirty_files"] + [u for u in fw["untracked_files"] if any(u.startswith(d) for d in SNAPSHOT_DIRS)]
    (out / "inputs").mkdir()
    if fw["dirty_files"]:
        rc, diff = run(["git", "-C", str(repo), "diff", "HEAD", "--", *SNAPSHOT_DIRS, *SNAPSHOT_FILES])
        (out / "inputs" / "firmware-dirty.diff").write_text(diff)
    manifest["firmware"] = {**fw, "path": str(repo), "snapshot_digest": digest(fw_hashes), "snapshot_files": len(fw_hashes),
                            "excluded_private_files": excluded,
                            "dirty_diff_sha256": sha256(out / "inputs/firmware-dirty.diff") if fw["dirty_files"] else None}
    manifest["simulator"] = {**sim, "path": str(simdir), "identity_digest": digest(sim_before),
                             "lvgl_submodule": {"commit": lvgl.get("commit"), "dirty_files": lvgl.get("dirty_files")}}
    (out / "inputs" / "firmware-files.sha256.json").write_text(json.dumps(fw_hashes, indent=0, sort_keys=True))
    (out / "inputs" / "simulator-files.sha256.json").write_text(json.dumps(sim_before, indent=0, sort_keys=True))
    manifest["tools"] = tool_identity(simdir, snap)
    res.add("inputs", "firmware", PASS if not dirty else WARN,
            f"{fw['branch']} {fw['commit'][:12]}; snapshot of {len(fw_hashes)} files (digest {digest(fw_hashes)[:12]})"
            + (f"; PROVISIONAL: {len(dirty)} uncommitted/untracked file(s) included, diff kept" if dirty else ", clean")
            + (f"; excluded private: {excluded}" if excluded else ""))
    res.add("inputs", "simulator", PASS if sim.get("commit") and not sim["dirty_files"] else WARN,
            f"{sim.get('branch')} {(sim.get('commit') or 'not git')[:12]}"
            + (f", {len(sim['dirty_files'])} dirty" if sim.get("dirty_files") else "") + f"; LVGL {(lvgl.get('commit') or '?')[:12]}"
            + (f"; LVGL config {manifest['tools'].get('lvgl_config_hash')}" if manifest["tools"].get("lvgl_config_hash") else ""))
    if "lvgl_identity_error" in manifest["tools"]:
        res.add("inputs", "lvgl-identity", FAIL, manifest["tools"]["lvgl_identity_error"])

    # 2. target, lineage and build membership --------------------------------------
    try:
        reg = json.loads((snap / "tools/uisim/targets.json").read_text())["targets"]
        if not isinstance(reg, dict):
            raise TypeError("'targets' is not an object")
    except (ValueError, KeyError, TypeError) as e:
        res.add("target", "registry", FAIL, f"tools/uisim/targets.json unreadable: {e}", required=True)
        return finish(out, work, plan, manifest, res, commands)
    targets_used = {plan["target"], *[x.get("target", plan["target"]) for g in ("stills", "motion", "tests")
                                      for x in plan.get(g, [])], *[r["target"] for r in plan.get("references", [])]}
    for name in sorted(targets_used):
        t = reg.get(name)
        if not t:
            res.add("target", name, FAIL, f"not registered in tools/uisim/targets.json of {fw['commit'][:12]}"
                    + (" (this plan is for a different firmware baseline)" if plan.get("baseline") else ""))
            continue
        if not isinstance(t, dict) or not isinstance(t.get("adapter"), str) or not isinstance(t.get("sources"), list):
            res.add("target", name, FAIL, "registration lacks an 'adapter' string or a 'sources' list", required=True)
            reg[name] = None
            continue
        missing = [p for p in [t["adapter"], *t["sources"]] if not (snap / p).is_file()]
        missing += [f"main/{h}" for h in t.get("headers", []) if not (snap / "main" / h).is_file()]
        res.add("target", name, FAIL if missing else PASS,
                f"missing files: {missing}" if missing else f"registered; adapter and {len(t['sources'])} sources present")
    base = plan.get("baseline")
    if base:
        rc, _ = run(["git", "-C", str(repo), "merge-base", "--is-ancestor", base["commit"], "HEAD"])
        res.add("target", "lineage", PASS if rc == 0 else FAIL,
                f"HEAD descends from {base['label']} {base['commit'][:12]}" if rc == 0 else
                f"HEAD {fw['commit'][:12]} is not a descendant of {base['label']} {base['commit'][:12]}: wrong firmware for this plan",
                required=True)
    target = reg.get(plan["target"])
    if a.build_dir:
        status, detail = build_membership(a.build_dir, repo, target["sources"] if target else [])
        res.add("build", "membership", status, detail)
        manifest["firmware_build"] = {"build_dir": str(a.build_dir), "membership": status}
    else:
        res.add("build", "membership", SKIPPED, "firmware build not run here: no configured ESP-IDF build supplied; "
                "whether the firmware compiles these sources is NOT established by this run")
        manifest["firmware_build"] = "not run"
    if not target or any(i["status"] == FAIL for i in res.items if i["stage"] in ("target", "inputs")):
        return finish(out, work, plan, manifest, res, commands)
    files = target_files(snap, target)
    manifest["target"] = {"name": plan["target"], "profile": plan.get("profile"), "ui": plan.get("ui"),
                          "description": target.get("description"), "files": {f: fw_hashes.get(f) for f in files}}

    # 3. asset integrity -------------------------------------------------------------
    n_ok, bad, unchecked = 0, [], 0
    for f in files:
        text = (snap / f).read_text(errors="replace")
        if "lv_image_dsc_t" not in text:
            continue
        for d in descriptors(text):
            status, detail = check_descriptor(d, text)
            n_ok += status == PASS
            unchecked += status == INFO
            if status == FAIL:
                bad.append(f"{f}:{d['name']}: {detail}")
    res.add("assets", "descriptors", FAIL if bad else PASS,
            "; ".join(bad) if bad else f"{n_ok} image descriptors consistent ({unchecked} not literal, not checked)")

    # 4. art: exact checkpoint validation, or a labelled diagnostic -------------------
    bindings = plan.get("art", [])
    art_required = "art" in required
    evidence = "none (no --art-lock and no diagnostic flag)"
    receipts = []
    frozen = {}   # project -> {file: sha256} of the frozen copy the renders read
    if a.art_lock:
        evidence = "checkpoint validation"
        try:
            lock = load_lock(a.art_lock)
        except (ArtError, ValueError, OSError) as e:
            lock = None
            res.add("art", "lock", FAIL, str(e), required=True)
        if lock is not None:
            shutil.copy2(a.art_lock, out / "inputs" / "art-lock.json")
            manifest["art_lock"] = {"path": str(a.art_lock), "sha256": sha256(a.art_lock), "selection": lock}
            packages = {}
            for project, sel in lock.items():
                try:
                    pkg, pj, others = select_package(a.art, sel)
                    dest = out / "inputs" / "art" / f"{project}-r{sel['revision']:04d}"
                    shutil.copytree(pkg, dest)   # the exact verified bytes, frozen with the run
                    # Verify the COPY: it, not the source folder, is what the renders read.
                    try:
                        got, pj, _ = select_package([dest], sel)
                    except ArtError as e:
                        raise ArtError(f"frozen copy differs from the verified package: {e}") from e
                    if got.resolve() != dest.resolve():
                        raise ArtError(f"frozen copy at {dest} did not verify as itself (found {got})")
                    frozen[project] = {"dir": str(dest), "sha256": {n: sha256(dest / n) for n in [*sel["files"], "manifest.json"]}}
                    packages[project] = (dest, pj)
                    res.add("art", f"package:{project}", PASS, f"r{sel['revision']} {sel['checkpoint']}: package integrity "
                            f"(manifest + {len(sel['files'])} files by SHA-256) verified at the source and again in the "
                            f"frozen copy" + (f"; ignored other packages: {others}" if others else ""),
                            required=True)
                    nstatus, ndetail = native_receipt(pj, sel["files"])
                    res.add("native", project, nstatus, ndetail)
                except ArtError as e:
                    res.add("art", f"package:{project}", FAIL, f"selected candidate BLOCKED: {e}", required=True)
            for b in bindings:
                src = b.get("source")
                if not src:
                    res.add("art", b["symbol"], UNSUPPORTED, b.get("note", "no Workshop source for this asset"), required=True)
                    continue
                if src["project"] not in lock:
                    res.add("art", b["symbol"], UNAVAILABLE, f"project {src['project']} not selected in the art lock", required=True)
                    continue
                if src["project"] not in packages:
                    res.add("art", b["symbol"], FAIL, f"its package {src['project']} is blocked (see above)", required=True)
                    continue
                pkg, pj = packages[src["project"]]
                try:
                    img = frame_from_package(pkg, pj, src.get("frame", 0))
                except ArtError as e:
                    res.add("art", b["symbol"], FAIL, str(e), required=True)
                    continue
                receipts.append(integrate(b, img, snap, res, required=True, origin={
                    "kind": "delivery package", "project": src["project"], "revision": pj["revision"],
                    "checkpoint": pj["checkpoint"], "frame": src.get("frame", 0)}))
    elif a.art_loose:
        evidence = "loose-export diagnostic"
        for b in bindings:
            src = b.get("source")
            if not src:
                res.add("art", b["symbol"], UNSUPPORTED, b.get("note", "no Workshop source for this asset"))
                continue
            img, info = find_loose(a.art_loose, src["project"], src.get("frame", 0))
            if img is None:
                res.add("art", b["symbol"], UNAVAILABLE, info)
                continue
            receipts.append(integrate(b, img, snap, res, required=False, origin=info))
        res.add("art", "checkpoint", UNAVAILABLE if art_required else INFO,
                "loose exports compared for diagnosis only: no checkpoint was selected or validated", required=art_required)
    elif a.committed_art_diagnostic:
        evidence = "committed-art diagnostic"
        res.add("art", "checkpoint", SKIPPED, "diagnostic run requested: renders show the art already committed in "
                "firmware; this is NOT validation of a native checkpoint")
    else:
        native = plan.get("native_art", {})
        res.add("art", "checkpoint", UNAVAILABLE, (native.get("status") or "no --art-lock given")
                + ": native checkpoint validation cannot run; use --committed-art-diagnostic for a labelled diagnostic",
                required=art_required)
        manifest["art_frozen"] = {}
    manifest["art"] = {"evidence_kind": evidence, "native_art": plan.get("native_art"), "receipts": receipts}
    changed = {f: sha256(snap / f) for f in {r["file"] for r in receipts if r.get("integrated")}}
    if changed:
        for f in changed:
            dest = out / "integrated" / f
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(snap / f, dest)
        res.add("art", "integration", INFO, f"integrated into the run snapshot only: {sorted(changed)}; checkout untouched")
    manifest["rendered_from"] = {"kind": "run snapshot" + (" with integrated art" if changed else ""),
                                 "base_commit": fw["commit"], "snapshot_digest": digest(fw_hashes),
                                 "integrated_files": changed}

    # 5. renders ---------------------------------------------------------------------
    # Renders show whatever art is in the snapshot. They run only for a verified lock or an
    # explicitly requested, labelled diagnostic; otherwise nothing is rendered at all.
    manifest["art_frozen"] = frozen
    blocked = None
    if evidence == "checkpoint validation" and not res.stage_ok("art"):
        blocked = "the selected art checkpoint is blocked (see art stage)"
    elif evidence.startswith("none"):
        blocked = ("no --art-lock and no explicit diagnostic flag: renders would show unselected art "
                   "(pass --art-lock, or --committed-art-diagnostic / --art-loose for a labelled diagnostic)")
    if blocked:
        for g in sorted(required & set(RENDER_GROUPS)):
            res.add(g, "group", UNAVAILABLE, f"not rendered: {blocked}", required=True)
        return finish(out, work, plan, manifest, res, commands, evidence=evidence)
    simr = Sim(simdir, snap, commands)
    groups = set(a.only or ["stills", "motion", "tests", "references"])
    for g in sorted(required & {"stills", "motion", "tests", "references"} - groups):
        res.add(g, "group", UNAVAILABLE, "required group not run in this invocation (--only)", required=True)
    stills, refs = [], []

    def native(kept, stage, name, req) -> Image.Image | None:
        img = Image.open(kept["screen.png"])
        if img.size != (W, H):
            res.add(stage, name, FAIL, f"screen.png is {img.size}, not native {W}x{H}", required=req)
            return None
        return img

    def mech(kept, stage, name, accepted):
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
        req = "stills" in required
        for s in plan.get("stills", []):
            tname = s.get("target", plan["target"])
            r = simr.render(tname, s.get("fixture"), s.get("advance_ms", 400), s.get("state"))
            kept = keep(r, out / "stills" if r["ok"] else out / "failed", s["name"])
            if not r["ok"]:
                res.add("still", s["name"], FAIL, "render failed: " + r["output"].strip()[-300:], required=req, command=r["cmd"])
                continue
            img = native(kept, "still", s["name"], req)
            if img:
                res.add("still", s["name"], PASS, f"{tname} native {W}x{H} at {s.get('advance_ms', 400)} ms "
                        f"({s.get('fixture') or 'default'})", required=req, command=r["cmd"])
                stills.append((s, img))
                mech(kept, "still", s["name"], s.get("accept", {}))

    if "motion" in groups:
        req = "motion" in required
        for m in plan.get("motion", []):
            tname = m.get("target", plan["target"])
            mdir = out / "motion" / m["name"]
            cap = work / "capture" / m["name"]
            cap.mkdir(parents=True)
            r = simr.render(tname, m.get("fixture"), m["advance_ms"], {**m.get("state", {}), "capture_dir": str(cap)})
            keep(r, mdir if r["ok"] else out / "failed", "final" if r["ok"] else f"motion-{m['name']}")
            if not r["ok"]:
                res.add("motion", m["name"], FAIL, "render failed: " + r["output"].strip()[-300:], required=req, command=r["cmd"])
                continue
            frames = []
            for p in sorted(cap.glob("frame-*.rgb")):
                raw = p.read_bytes()
                if len(raw) == W * H * 3:
                    frames.append((int(p.stem.split("-")[1]), Image.frombytes("RGB", (W, H), raw)))
            if len(frames) < 2:
                res.add("motion", m["name"], FAIL, f"only {len(frames)} frame(s): the adapter may not support capture_dir",
                        required=req, command=r["cmd"])
                continue
            (mdir / "frames").mkdir(parents=True, exist_ok=True)
            for t, img in frames:
                img.save(mdir / "frames" / f"{t:05d}.png")
            st = motion_stats(frames)
            st["timing"] = ("simulated LVGL time: 5 ms ticks, a frame captured every 50 ms of simulated time; "
                            "not the device's frame rate, and not native Pixelorama frame durations")
            (mdir / "motion.json").write_text(json.dumps(st, indent=1))
            flagged = {h["at_ms"] for h in st["hints"] if "at_ms" in h}
            change = {s["to_ms"]: s["changed_px"] for s in st["steps"]}
            chosen = pick(frames, flagged)
            sheet([(img, f"{t} ms  d={change.get(t, 0)}px", t in flagged) for t, img in chosen], 4,
                  f"{plan['name']} / motion {m['name']}: {len(chosen)} of {len(frames)} frames, native 320x240; "
                  "red = large one-step change").save(mdir / "contact-sheet.png")
            durations = [max(20, b[0] - a_[0]) for a_, b in zip(frames, frames[1:])] + [400]
            frames[0][1].save(mdir / "animation.gif", save_all=True, append_images=[f for _, f in frames[1:]],
                              duration=durations, loop=0)
            res.add("motion", m["name"], WARN if st["hints"] else PASS,
                    f"{len(frames)} frames {frames[0][0]}-{frames[-1][0]} ms (simulated); last change {st['last_change_ms']} ms; "
                    f"{len(st['hints'])} hint(s)" + (": " + "; ".join(f"{h['kind']}@{h.get('at_ms', '-')}ms"
                                                       for h in st["hints"][:5]) if st["hints"] else ""),
                    required=req, command=r["cmd"], hints=st["hints"])

    if "tests" in groups:
        req = "tests" in required
        for t in plan.get("tests", []):
            tname = t.get("target", plan["target"])
            r = simr.render(tname, t.get("fixture"), t["advance_ms"], t.get("state"))
            kept = keep(r, out / "tests", t["name"])
            log = kept["log.txt"].read_text(errors="replace")
            marker_seen = t["require"] in log
            if t.get("expect") == "fail":   # negative control: the assertions must catch the fault
                reached_run = r["ok"] or "run failed" in r["output"]       # not a compile/link error
                want = t.get("expect_message")
                caught = not (r["ok"] and marker_seen)
                if not reached_run:
                    res.add("test", t["name"], FAIL, "negative control did not reach the run stage (compile/link failure "
                            "proves nothing): " + r["output"].strip()[-300:], required=req, command=r["cmd"])
                elif not caught:
                    res.add("test", t["name"], FAIL, f"negative control did NOT fail: '{t['require']}' printed although "
                            f"{t.get('why', 'the fault was injected')}", required=req, command=r["cmd"])
                elif want and want not in log:
                    res.add("test", t["name"], FAIL, f"negative control failed for a different reason than expected "
                            f"('{want}' not in the log): " + log.strip()[-300:], required=req, command=r["cmd"])
                else:
                    line = next((ln for ln in log.splitlines() if want and want in ln), None) or \
                        f"'{t['require']}' never printed"
                    res.add("test", t["name"], PASS, f"negative control caught as expected ({t.get('why', '')}): {line.strip()[:220]}",
                            required=req, command=r["cmd"])
            elif not r["ok"]:
                res.add("test", t["name"], FAIL, "simulator run failed (an assert aborts the run): " + log.strip()[-400:],
                        required=req, command=r["cmd"])
            elif not marker_seen:
                res.add("test", t["name"], FAIL, f"render finished but '{t['require']}' was never printed: the test did "
                        f"not reach its end within {t['advance_ms']} ms", required=req, command=r["cmd"])
            else:
                line = next(ln for ln in log.splitlines() if t["require"] in ln)
                res.add("test", t["name"], PASS, line.strip()[:300], required=req, command=r["cmd"],
                        standins=t.get("standins"))

    if "references" in groups:
        for ref in plan.get("references", []):
            r = simr.render(ref["target"], ref.get("fixture"), ref.get("advance_ms", 400), ref.get("state"))
            kept = keep(r, out / "references" if r["ok"] else out / "failed", ref["name"])
            if not r["ok"]:
                res.add("reference", ref["name"], FAIL, "render failed: " + r["output"].strip()[-300:], command=r["cmd"])
                continue
            refs.append((ref, Image.open(kept["screen.png"])))
            res.add("reference", ref["name"], INFO, f"{ref['target']} for comparison: {ref.get('why', '')}", command=r["cmd"])

    # 6. review images ---------------------------------------------------------------
    if stills:
        label = "art from the locked checkpoint" if evidence == "checkpoint validation" else evidence.upper()
        cells = [(img, f"{s['name']} ({s.get('target', plan['target'])}:{s.get('fixture') or 'default'} @{s.get('advance_ms', 400)}ms)", False)
                 for s, img in stills]
        cells += [(img, f"REFERENCE {r['name']} (not under review)", False) for r, img in refs]
        sheet(cells, 3, f"{plan['name']}: native 320x240 simulator stills, firmware {fw['commit'][:10]}, {label}"
              ).save(out / "review-sheet.png")
        crops = []
        by_name = {s["name"]: img for s, img in stills}
        for c in plan.get("focus", []):
            img = by_name.get(c["still"])
            if img:
                x1, y1, x2, y2 = c["rect"]
                k = c.get("scale", 4)
                crops.append((img.crop((x1, y1, x2, y2)).resize(((x2 - x1) * k, (y2 - y1) * k), Image.NEAREST),
                              f"{c['name']}  [{c['still']} {x1},{y1}-{x2},{y2} x{k}]", False))
        if crops:
            sheet(crops, 2, f"{plan['name']}: focus crops (nearest-neighbour enlargements, no smoothing)").save(out / "focus-crops.png")

    # 7. did anything change while we ran? -------------------------------------------------
    fw_after = tree_hashes(repo, SNAPSHOT_DIRS + SNAPSHOT_FILES)
    moved = drift(fw_hashes, fw_after)   # snapshot hashes were taken before any integration
    head_after = git_info(repo)["commit"]
    sim_moved = drift(sim_before, tree_hashes(simdir, SIM_IDENTITY))
    if moved or head_after != fw["commit"]:
        res.add("inputs", "unchanged-during-run", FAIL, f"the firmware checkout changed while the run used its snapshot "
                f"(HEAD {fw['commit'][:12]} -> {(head_after or '?')[:12]}; files: {moved[:8]}): evidence is for the snapshot only, rerun",
                required=True)
    else:
        res.add("inputs", "unchanged-during-run", PASS, "firmware checkout identical at start and end", required=True)
    if sim_moved:
        res.add("inputs", "simulator-unchanged", FAIL, f"simulator files changed during the run: {sim_moved[:8]}", required=True)
    snap_now = tree_hashes(snap, SNAPSHOT_DIRS + SNAPSHOT_FILES)
    snap_moved = sorted(f for f in drift({**fw_hashes, **changed}, snap_now))
    res.add("inputs", "snapshot-unchanged", FAIL if snap_moved else PASS,
            f"the run snapshot changed while rendering: {snap_moved[:8]}" if snap_moved else
            "run snapshot (incl. integrated art) identical before and after the renders", required=True)
    for project, fz in frozen.items():
        dest = Path(fz["dir"])
        bad = sorted(n for n, h in fz["sha256"].items() if not (dest / n).is_file() or sha256(dest / n) != h)
        res.add("art", "frozen-unchanged", FAIL if bad else PASS,
                f"{project}: frozen package files changed during the run: {bad}" if bad else
                f"{project}: frozen package identical at the end of the run", required=True)
    return finish(out, work, plan, manifest, res, commands, evidence=evidence)


def integrate(b: dict, img: Image.Image, snap: Path, res: Results, required: bool, origin: dict) -> dict:
    """Compare one export with its firmware slot; put it into the snapshot when it differs."""
    label, path = b["symbol"], snap / b["file"]
    text = path.read_text()
    d = next((d for d in descriptors(text) if d["name"] == label), None)
    fw_bytes = array_bytes(text, d["data"]) if d else None
    rec = {"symbol": label, "file": b["file"], "source": origin}
    if fw_bytes is None:
        res.add("art", label, FAIL, f"descriptor/array {label} not found in {b['file']}", required=required)
        return {**rec, "result": "slot missing"}
    if img.size != (d["w"], d["h"]):
        res.add("art", label, FAIL, f"export is {img.size[0]}x{img.size[1]}, firmware slot is {d['w']}x{d['h']}", required=required)
        return {**rec, "result": "wrong dimensions"}
    try:
        want = convert(img, d["cf"])
    except ValueError as e:
        res.add("art", label, FAIL, str(e), required=required)
        return {**rec, "result": str(e)}
    bpp = BPP[d["cf"]]
    diff_px = sum(want[i:i + bpp] != fw_bytes[i:i + bpp] for i in range(0, len(want), bpp))
    if diff_px == 0:
        res.add("art", label, PASS, f"committed firmware bytes already identical to {origin.get('kind')}", required=required)
        return {**rec, "result": "identical", "integrated": False}
    replace_array(path, d["data"], want)
    ok = array_bytes(path.read_text(), d["data"]) == want
    res.add("art", label, PASS if ok else FAIL, f"{diff_px} px differed from the committed art; integrated into the run "
            "snapshot (renders use it)", required=required)
    return {**rec, "result": "integrated", "integrated": ok, "differing_px": diff_px}


def readiness(res: Results, manifest: dict, technical: str, evidence: str) -> dict:
    fw = manifest.get("firmware", {})
    art_items = [i for i in res.items if i["stage"] == "art"]
    native_items = [i for i in res.items if i["stage"] == "native"]
    if evidence == "checkpoint validation":
        art = ("verified (SHA-256 against the art lock, at the source and in the frozen copy the renders read)"
               if art_items and all(i["status"] not in BLOCKING for i in art_items) else "blocked")
        if not native_items:
            native = "UNVERIFIED: no package passed integrity, so no native receipt was read"
        elif any(i["status"] == FAIL for i in native_items):
            native = "FAILED: " + "; ".join(i["detail"] for i in native_items if i["status"] == FAIL)
        elif all(i["status"] == INFO for i in native_items):
            native = "CLAIMED by Workshop native_check receipt(s), consistent with the packaged bytes; not re-run by this tool"
        else:
            native = ("UNVERIFIED: no native_check receipt for " +
                      ", ".join(i["name"] for i in native_items if i["status"] != INFO) +
                      " (a .pxo filename or matching hash is not native evidence)")
    else:
        art = f"not established ({evidence})"
        native = f"not established ({evidence})"
    reasons = []
    if technical not in ("PASS", "PASS_WITH_WARNINGS"):
        reasons.append(f"technical result {technical}")
    if not art.startswith("verified"):
        reasons.append(f"art package {art}")
    if fw.get("dirty_files") or any("PROVISIONAL" in i["detail"] for i in res.items if i["name"] == "firmware"):
        reasons.append("provisional inputs (uncommitted firmware files)")
    return {
        "input_pinned": "pinned" if not any(i["status"] == FAIL for i in res.items if i["stage"] == "inputs") else "failed",
        "art_package_integrity": art,
        "art_native_editable": native,
        "integrated": "integrated into run snapshot" if manifest.get("rendered_from", {}).get("integrated_files") else "no (committed art)",
        "technical_checks": technical,
        "visual_review": "pending (Dottie)",
        "handoff": "prepared (local bundle)",
        "delivery": "not verified (this tool does not deliver)",
        "firmware_build": (manifest["firmware_build"] if isinstance(manifest.get("firmware_build"), str) else
                           f"membership {manifest['firmware_build'].get('membership')} (configured build inspected; no image built)"
                           if isinstance(manifest.get("firmware_build"), dict) else "not run (run stopped before the build stage)"),
        "hardware": "not run",
        "ready_for_visual_acceptance": not reasons,
        "not_ready_because": reasons,
        "promotion_ready": False,
        "promotion_needs": ["visual acceptance by Mike/Dottie", "native editability verified by a fresh Pixelorama "
                            "reopen (this tool reads receipts only)", "ESP-IDF build", "hardware check"],
    }


def finish(out: Path, work: Path, plan: dict, manifest: dict, res: Results, commands: list,
           evidence: str = "not reached") -> int:
    technical, code = res.technical()
    manifest["finished_utc"] = now_utc()
    manifest["technical_result"] = technical
    manifest["readiness"] = readiness(res, manifest, technical, evidence)
    (out / "results.json").write_text(json.dumps({"technical_result": technical, "readiness": manifest["readiness"],
                                                  "results": res.items}, indent=1, default=str))
    (out / "commands.json").write_text(json.dumps(commands, indent=1))
    (out / "DOTTIE.md").write_text(dottie(plan, manifest, res, out))
    leaks = secret_scan(out)
    if leaks:
        manifest["sanitization"] = {"status": "BLOCKED", "hits": leaks}
        (out / "manifest.json").write_text(json.dumps(manifest, indent=1, default=str))
        shutil.rmtree(work, ignore_errors=True)
        print(f"\nBUNDLE NOT ZIPPED: possible secrets: {leaks}")
        return 1
    manifest["sanitization"] = {"status": "scanned, no matches", "patterns": len(SECRET_PATTERNS),
                                "private_names_excluded": sorted(PRIVATE_NAMES)}
    manifest["files"] = {str(p.relative_to(out)): sha256(p) for p in sorted(out.rglob("*"))
                         if p.is_file() and p.name != "manifest.json"}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1, default=str))
    z = out.with_suffix(".zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(out.rglob("*")):
            if p.is_file():
                zi = zipfile.ZipInfo(str(Path(out.name) / p.relative_to(out)), date_time=(2026, 1, 1, 0, 0, 0))
                zi.compress_type = zipfile.ZIP_DEFLATED
                zf.writestr(zi, p.read_bytes())
    zsha = sha256(z)
    z.with_suffix(".zip.sha256").write_text(f"{zsha}  {z.name}\n")
    shutil.rmtree(work, ignore_errors=True)
    print(f"\n{technical} (art package: {manifest['readiness']['art_package_integrity'][:40]}; native: "
          f"{manifest['readiness']['art_native_editable'][:40]}; visual review pending): {out}\n"
          f"zip: {z} ({z.stat().st_size // 1024} KB) sha256 {zsha}")
    return code


def dottie(plan: dict, manifest: dict, res: Results, out: Path) -> str:
    fw, sim, rd = manifest.get("firmware", {}), manifest.get("simulator", {}), manifest["readiness"]
    rows = "\n".join(f"| {i['stage']} | {i['name']} | {i['status']}{' *' if i['required'] else ''} | "
                     f"{i['detail'].replace('|', '/')[:280]} |" for i in res.items)
    hints = [f"- motion `{i['name']}`: {h['detail']} (t={h.get('at_ms', '-')} ms, bbox {h.get('bbox')})"
             for i in res.items if i["stage"] == "motion" for h in i.get("hints", [])]
    questions = "\n".join(f"{n}. {q}" for n, q in enumerate(plan.get("visual_review", []), 1))
    standins = "\n".join(f"- {s}" for s in plan.get("standins", [])) or "- none declared"
    axes = "\n".join(f"| {k} | {v} |" for k, v in rd.items() if k not in ("not_ready_because",))
    kind = manifest.get("art", {}).get("evidence_kind", "not reached")
    return f"""# Review request: {plan['name']} ({plan.get('ui', '')})

**Technical result: {manifest.get('technical_result')}.** Art evidence: **{kind}**. Visual review: **pending**.
{'**This bundle shows the art already committed in firmware. It does not validate a native Pixelorama checkpoint.**' if kind != 'checkpoint validation' else ''}
Not ready for visual acceptance because: {'; '.join(rd['not_ready_because']) or 'nothing (technical and art gates passed)'}.

## Readiness (separate axes; this tool never sets visual, delivery, build or hardware)

| axis | state |
|---|---|
{axes}

## Exactly what was rendered

- Firmware `{fw.get('branch')}` @ `{fw.get('commit')}` (snapshot digest `{fw.get('snapshot_digest', '')[:16]}`,
  {fw.get('snapshot_files')} files){'; uncommitted: ' + ', '.join(fw['dirty_files']) if fw.get('dirty_files') else ', clean'}
- Simulator `{sim.get('branch')}` @ `{sim.get('commit')}`, LVGL `{(sim.get('lvgl_submodule') or {}).get('commit')}`,
  LVGL config `{manifest.get('tools', {}).get('lvgl_config_hash')}`
- Target `{plan.get('target')}`, profile `{plan.get('profile')}`: {manifest.get('target', {}).get('description', '')}
- Native art status: {(plan.get('native_art') or {}).get('status', 'n/a')}
- Every image is the simulator's own 320x240 RGB565 frame; `*@2x.png` are 2x nearest-neighbour copies.

## Stand-ins (host code that is not the firmware's own behaviour)

{standins}

## Files to look at first

1. `review-sheet.png`: every still (and REFERENCE scenes).
2. `motion/*/contact-sheet.png`, `motion/*/animation.gif` (palette GIF: judge pixels from `motion/*/frames/`).
3. `focus-crops.png`: enlarged areas the plan asks about.

## Deterministic results (* = required stage)

| stage | item | status | detail |
|---|---|---|---|
{rows}

Motion hints (where to look, not verdicts):
{chr(10).join(hints) or '- none'}

## Your visual judgement (no program checked these)

{questions}

## How to answer

One line per problem:
`[still/motion name] [time ms] [element, x,y in 320x240] [art | layout | motion | software | unsure] [blocker | major | minor] [what is wrong] [suggested correction]`

## Not proven by this bundle

Panel colour, viewing angle, frame rate, touch feel, audio, radar, internal RAM/heap/PSRAM, flash use, an
ESP-IDF build, delivery to you, and the device. Frame timing here is simulated LVGL time.
"""


if __name__ == "__main__":
    sys.exit(main())
