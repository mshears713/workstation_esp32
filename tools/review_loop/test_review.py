"""Unit tests for review.py's own logic (python3 -m unittest tools/review_loop/test_review.py).

Everything here is a synthetic FIXTURE built in a temp folder: no firmware art, no simulator.
End-to-end checks are real runs of review.py (README.md, "Negative controls")."""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import review  # noqa: E402


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def header(name, w, h, data, cf="ARGB8888", hexfmt=False):
    body = ",".join((f"0x{b:02x}" if hexfmt else str(b)) for b in data)
    return (f"static const uint8_t {name}_data[]={{{body}}};\n"
            f"static const lv_image_dsc_t {name}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_{cf},"
            f".w={w},.h={h},.stride={w * review.BPP[cf]}}},.data_size=sizeof({name}_data),.data={name}_data}};\n")


def package(root: Path, project="thing", revision=3, colour=(255, 0, 0, 255), size=(2, 2)):
    """A FIXTURE delivery package in the pixelorama-workshop/delivery-v1 layout; returns its lock entry."""
    pkg = root / f"MCP Workshop Results/pixelorama/{project}/r{revision:04d}-abc"
    pkg.mkdir(parents=True)
    w, h = size
    atlas = Image.new("RGBA", (2 * w, h))
    for x in range(w, 2 * w):
        for y in range(h):
            atlas.putpixel((x, y), colour)      # frame 1
    stem = f"{project}-r{revision:04d}"
    atlas.save(pkg / f"{stem}-atlas.png")
    (pkg / f"{stem}.pxo").write_bytes(f"pxo {revision}".encode())
    files = [{"name": n, "role": "", "bytes": (pkg / n).stat().st_size, "sha256": sha(pkg / n)}
             for n in (f"{stem}.pxo", f"{stem}-atlas.png")]
    frames = [{"index": i, "x": w * i, "y": 0, "width": w, "height": h, "duration_ms": 100} for i in (0, 1)]
    (pkg / "manifest.json").write_text(json.dumps({"schema": "pixelorama-workshop/delivery-v1", "project": project,
                                                   "revision": revision, "checkpoint": f"r{revision:04d}-abc", "files": files,
                                                   "atlas": {"width": 2 * w, "height": h, "frames": frames}}))
    return pkg, {"project": project, "revision": revision, "checkpoint": f"r{revision:04d}-abc",
                 "manifest_sha256": sha(pkg / "manifest.json"), "files": {f["name"]: f["sha256"] for f in files}}


class Arrays(unittest.TestCase):
    def test_decimal_and_hex_arrays_parse(self):
        for hexfmt in (False, True):
            text = header("a", 2, 1, [1, 2, 3, 255, 0, 0, 0, 0], hexfmt=hexfmt)
            self.assertEqual(review.array_bytes(text, "a_data"), bytes([1, 2, 3, 255, 0, 0, 0, 0]))

    def test_truncated_array_fails(self):
        text = header("a", 2, 2, [7] * 12)  # one pixel short
        status, detail = review.check_descriptor(review.descriptors(text)[0], text)
        self.assertEqual(status, review.FAIL)
        self.assertIn("needs 16 bytes, array has 12", detail)

    def test_replace_array_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "x.h"
            p.write_text(header("a", 1, 1, [1, 2, 3, 4]))
            review.replace_array(p, "a_data", bytes([9, 8, 7, 6]))
            text = p.read_text()
            self.assertEqual(review.array_bytes(text, "a_data"), bytes([9, 8, 7, 6]))
            self.assertEqual(review.check_descriptor(review.descriptors(text)[0], text)[0], review.PASS)

    def test_argb8888_is_bgra_and_rgb565_matches_converter(self):
        self.assertEqual(review.convert(Image.new("RGBA", (1, 1), (10, 20, 30, 40)), "ARGB8888"), bytes([30, 20, 10, 40]))
        v = (31 << 11) | (32 << 5) | 1
        self.assertEqual(review.convert(Image.new("RGBA", (1, 1), (255, 128, 8, 255)), "RGB565"), bytes([v & 255, v >> 8]))
        with self.assertRaises(ValueError):
            review.convert(Image.new("RGBA", (1, 1), (0, 0, 0, 0)), "RGB565")


class ExactSelection(unittest.TestCase):
    def test_exact_package_is_selected(self):
        with tempfile.TemporaryDirectory() as tmp:
            package(Path(tmp), revision=2, colour=(0, 255, 0, 255))
            pkg, sel = package(Path(tmp), revision=3)
            got, manifest, others = review.select_package([Path(tmp)], sel)
            self.assertEqual(got, pkg)
            self.assertEqual(len(others), 1)            # r2 seen, reported, not used
            img = review.frame_from_package(got, manifest, 1)
            self.assertEqual(img.convert("RGBA").getpixel((0, 0)), (255, 0, 0, 255))

    def test_corrupt_selected_with_valid_older_present_is_blocked_not_replaced(self):
        with tempfile.TemporaryDirectory() as tmp:
            package(Path(tmp), revision=2)              # valid older checkpoint
            pkg, sel = package(Path(tmp), revision=3)
            (pkg / "thing-r0003-atlas.png").write_bytes(b"corrupted after delivery")
            with self.assertRaises(review.ArtError) as e:
                review.select_package([Path(tmp)], sel)
            self.assertIn("thing-r0003-atlas.png", str(e.exception))

    def test_selected_missing_while_other_revisions_exist(self):
        with tempfile.TemporaryDirectory() as tmp:
            package(Path(tmp), revision=2)
            _, sel = package(Path(tmp) / "elsewhere", revision=3)
            with self.assertRaises(review.ArtError) as e:
                review.select_package([Path(tmp) / "MCP Workshop Results"], sel)
            self.assertIn("not found", str(e.exception))
            self.assertIn("ignored other packages", str(e.exception))

    def test_missing_file_blocks(self):
        with tempfile.TemporaryDirectory() as tmp:
            pkg, sel = package(Path(tmp))
            (pkg / "thing-r0003.pxo").unlink()
            with self.assertRaisesRegex(review.ArtError, "selected file missing"):
                review.select_package([Path(tmp)], sel)

    def test_manifest_hash_and_file_set_are_pinned(self):
        with tempfile.TemporaryDirectory() as tmp:
            pkg, sel = package(Path(tmp))
            with self.assertRaisesRegex(review.ArtError, "manifest SHA-256"):
                review.select_package([Path(tmp)], {**sel, "manifest_sha256": "0" * 64})
            with self.assertRaisesRegex(review.ArtError, "differ from the locked set"):
                review.select_package([Path(tmp)], {**sel, "files": {"thing-r0003.pxo": sel["files"]["thing-r0003.pxo"]}})

    def test_duplicate_selected_package_is_ambiguous(self):
        with tempfile.TemporaryDirectory() as tmp:
            _, sel = package(Path(tmp) / "a")
            package(Path(tmp) / "b")
            with self.assertRaisesRegex(review.ArtError, "ambiguous"):
                review.select_package([Path(tmp)], sel)

    def test_lock_schema_and_duplicates(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "lock.json"
            p.write_text(json.dumps({"schema": "x"}))
            with self.assertRaisesRegex(review.ArtError, "schema"):
                review.load_lock(p)
            e = {"project": "a", "revision": 1, "checkpoint": "c", "manifest_sha256": "0", "files": {}}
            p.write_text(json.dumps({"schema": "workstation-review/art-lock-v1", "selection": [e, e]}))
            with self.assertRaisesRegex(review.ArtError, "selected twice"):
                review.load_lock(p)

    def test_wrong_dimensions_fail_integration(self):
        with tempfile.TemporaryDirectory() as tmp:
            snap = Path(tmp)
            (snap / "main").mkdir()
            (snap / "main/x.h").write_text(header("a", 2, 2, [0] * 16))
            res = review.Results()
            rec = review.integrate({"symbol": "a", "file": "main/x.h"}, Image.new("RGBA", (3, 2)), snap, res, True, {})
            self.assertEqual(rec["result"], "wrong dimensions")
            self.assertEqual(res.technical(), ("FAIL", 1))
            self.assertEqual(review.array_bytes((snap / "main/x.h").read_text(), "a_data"), bytes(16))  # untouched


class Inputs(unittest.TestCase):
    def test_drift_detects_change_add_remove(self):
        self.assertEqual(review.drift({"a": "1", "b": "2"}, {"a": "1", "b": "3", "c": "4"}), ["b", "c"])
        self.assertEqual(review.drift({"a": "1"}, {}), ["a"])
        self.assertEqual(review.drift({"a": "1"}, {"a": "1"}), [])

    def test_freeze_excludes_private_config_and_hashes_the_copy(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo, dest = Path(tmp) / "fw", Path(tmp) / "snap"
            (repo / "main").mkdir(parents=True)
            (repo / "tools/uisim").mkdir(parents=True)
            (repo / "main/ui.c").write_text("int x;")
            (repo / "main/wifi_credentials.h").write_text('#define WIFI_PASS "nope"')
            (repo / "sdkconfig").write_text("CONFIG_LV_COLOR_DEPTH=16")
            hashes, excluded = review.freeze(repo, dest)
            self.assertEqual(excluded, ["main/wifi_credentials.h"])
            self.assertFalse((dest / "main/wifi_credentials.h").exists())
            self.assertEqual(set(hashes), {"main/ui.c", "sdkconfig"})
            (repo / "main/ui.c").write_text("int y;")    # a change during the run
            self.assertEqual(review.drift(hashes, review.tree_hashes(repo, ["main", "tools/uisim", "sdkconfig"])), ["main/ui.c"])

    def test_build_membership_needs_a_configured_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(review.build_membership(Path(tmp), Path(tmp), ["main/ui_home.c"])[0], review.UNAVAILABLE)
            (Path(tmp) / "compile_commands.json").write_text(json.dumps([{"directory": "/x/build", "file": "/x/main/ui_home.c", "command": "cc"}]))
            self.assertEqual(review.build_membership(Path(tmp), Path(tmp), ["main/ui_home.c"])[0], review.PASS)
            self.assertEqual(review.build_membership(Path(tmp), Path(tmp), ["main/ui_home.c", "main/ui_mouse.c"])[0], review.WARN)

    def test_secret_scan(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "log.txt").write_text("all fine")
            self.assertEqual(review.secret_scan(Path(tmp)), [])
            (Path(tmp) / "env.txt").write_text("GITHUB_TOKEN=ghp_" + "a" * 36)
            self.assertEqual(len(review.secret_scan(Path(tmp))), 1)


class Motion(unittest.TestCase):
    def test_sprite_vanishing_is_flagged_at_that_moment(self):
        """Like the Orlando coaster car: moves ~1 px per frame, then is hidden in one frame."""
        frames = []
        for i in range(10):
            img = Image.new("RGB", (320, 240), (40, 80, 40))
            if i < 6:
                for x in range(100 + i, 112 + i):
                    for y in range(50, 70):
                        img.putpixel((x, y), (250, 250, 250))
            frames.append((50 * (i + 1), img))
        hints = review.motion_stats(frames)["hints"]
        self.assertEqual([(h["kind"], h["at_ms"]) for h in hints], [("large-step-then-still", 350)])

    def test_frozen_capture_is_reported(self):
        img = Image.new("RGB", (320, 240))
        self.assertEqual(review.motion_stats([(50, img), (100, img)])["hints"][0]["kind"], "no-motion")


class Verdicts(unittest.TestCase):
    def test_required_unavailable_or_unsupported_is_incomplete(self):
        for status in (review.UNAVAILABLE, review.UNSUPPORTED):
            r = review.Results()
            r.add("art", "x", status, "", required=True)
            self.assertEqual(r.technical(), ("INCOMPLETE", 2))

    def test_optional_unavailable_is_only_a_warning(self):
        r = review.Results()
        r.add("art", "x", review.UNAVAILABLE, "")
        self.assertEqual(r.technical(), ("PASS_WITH_WARNINGS", 0))

    def test_any_fail_wins(self):
        r = review.Results()
        r.add("a", "b", review.PASS, "")
        r.add("a", "c", review.FAIL, "")
        self.assertEqual(r.technical(), ("FAIL", 1))

    def test_committed_art_diagnostic_is_never_ready(self):
        r = review.Results()
        r.add("still", "home", review.PASS, "", required=True)
        rd = review.readiness(r, {"firmware": {}, "firmware_build": "not run"}, "PASS", "committed-art diagnostic")
        self.assertFalse(rd["ready_for_visual_acceptance"])
        self.assertFalse(rd["promotion_ready"])
        self.assertIn("art package not established (committed-art diagnostic)", rd["not_ready_because"])
        self.assertEqual(rd["art_native_editable"], "not established (committed-art diagnostic)")
        self.assertEqual(rd["delivery"], "not verified (this tool does not deliver)")

    def test_blocked_checkpoint_is_not_verified(self):
        r = review.Results()
        r.add("art", "package:thing", review.FAIL, "", required=True)
        rd = review.readiness(r, {"firmware": {}, "firmware_build": "not run"}, "FAIL", "checkpoint validation")
        self.assertEqual(rd["art_package_integrity"], "blocked")
        self.assertFalse(rd["ready_for_visual_acceptance"])



# ------------------------------------------------------------------ whole-run regressions (MOCKED simulator)
#
# These call review.main() in-process against a throwaway git firmware repo and a FAKE ws-ui that
# records every call it receives. They prove what the runner does (render or not, what readiness
# says); they are not LVGL rendering. Real-simulator evidence comes from e2e_controls.py.

FAKE_WSUI = r"""#!/usr/bin/env python3
import os, sys, json
from pathlib import Path
from PIL import Image
out = Path(os.environ["FAKE_SIM_OUT"])
out.mkdir(parents=True, exist_ok=True)
with open(out / "CALLS", "a") as f:
    f.write(" ".join(sys.argv[1:]) + "\n")
n = len((out / "CALLS").read_text().splitlines())
d = out / f"render-{n}"
d.mkdir()
Image.new("RGB", (320, 240), (10, 20, 30)).save(d / "screen.png")
(d / "checks.json").write_text("[]")
(d / "log.txt").write_text("FAKE adapter log\nFAKE_PASS\n")
print(f"files: {d}/screen.png")
"""


def firmware(root: Path, git: bool = True, targets: dict | None = None) -> Path:
    """FIXTURE firmware checkout: one target 'fake' whose source holds one 2x2 ARGB8888 image 'ai_x'."""
    import subprocess
    repo = root / "fw"
    (repo / "main").mkdir(parents=True)
    (repo / "tools/uisim/targets").mkdir(parents=True)
    (repo / "tools/uisim/targets/fake.c").write_text('#include "ui_fake.h"\n')
    (repo / "main/ui_fake.h").write_text(header("ai_x", 2, 2, [7] * 16))
    (repo / "main/ui_fake.c").write_text('#include "ui_fake.h"\n')
    (repo / "sdkconfig").write_text("CONFIG_LV_COLOR_DEPTH=16\n")
    reg = targets or {"fake": {"adapter": "tools/uisim/targets/fake.c", "sources": ["main/ui_fake.c"],
                               "description": "FIXTURE"}}
    (repo / "tools/uisim/targets.json").write_text(json.dumps({"targets": reg}))
    if git:
        for cmd in (["init", "-q"], ["add", "-A"],
                    ["-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "fixture"]):
            subprocess.run(["git", "-C", str(repo), *cmd], check=True, capture_output=True)
    return repo


def fake_sim(root: Path) -> Path:
    sim = root / "sim"
    (sim / "bin").mkdir(parents=True)
    cli = sim / "bin/ws-ui"
    cli.write_text(FAKE_WSUI.replace("#!/usr/bin/env python3", "#!" + sys.executable, 1))
    cli.chmod(0o755)
    return sim


def plan_file(root: Path, **over) -> Path:
    p = {"name": "FIXTURE-plan", "target": "fake", "required": ["stills", "tests", "art"],
         "stills": [{"name": "home", "advance_ms": 100}],
         "tests": [{"name": "t", "advance_ms": 100, "require": "FAKE_PASS"}],
         "art": [{"symbol": "ai_x", "file": "main/ui_fake.h", "source": {"project": "thing", "frame": 1}}], **over}
    path = root / "plan.json"
    path.write_text(json.dumps(p))
    return path


class WholeRun(unittest.TestCase):
    def setUp(self):
        import os
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.calls = self.root / "sim-out"
        self._env = os.environ.get("FAKE_SIM_OUT")
        os.environ["FAKE_SIM_OUT"] = str(self.calls)
        self._identity = review.tool_identity
        review.tool_identity = lambda sim, snap: {"python": "x", "lvgl_commit": "FAKE", "lvgl_config_hash": "FAKE"}
        self.sim = fake_sim(self.root)

    def tearDown(self):
        import os
        review.tool_identity = self._identity
        if self._env is None:
            os.environ.pop("FAKE_SIM_OUT", None)
        else:
            os.environ["FAKE_SIM_OUT"] = self._env
        self._tmp.cleanup()

    def run_review(self, *args, repo=None, plan=None):
        import contextlib
        import io
        out = self.root / "out"
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            code = review.main(["--plan", str(plan or plan_file(self.root)), "--repo", str(repo or firmware(self.root)),
                                "--sim", str(self.sim), "--out", str(out), *map(str, args)])
        results = json.loads((out / "results.json").read_text())
        return code, results, out

    def rendered(self) -> list[str]:
        return (self.calls / "CALLS").read_text().splitlines() if (self.calls / "CALLS").exists() else []

    def statuses(self, results, stage, name=None):
        return [i["status"] for i in results["results"] if i["stage"] == stage and (name is None or i["name"] == name)]

    def lock_for(self, pkg_root: Path, **pkg) -> Path:
        _, sel = package(pkg_root, colour=(1, 2, 3, 255), **pkg)
        p = self.root / "lock.json"
        p.write_text(json.dumps({"schema": "workstation-review/art-lock-v1", "selection": [sel]}))
        return p

    # -- defect 1 (Dottie, real simulator): no art lock reported INCOMPLETE but still rendered home.png
    def test_harness_detects_rendering(self):
        """Positive control: an explicit committed-art diagnostic does call the renderer."""
        code, r, out = self.run_review("--committed-art-diagnostic")
        self.assertTrue(self.rendered())
        self.assertTrue((out / "stills/home.png").is_file())

    def test_committed_diagnostic_cannot_pass_a_plan_that_requires_art(self):
        code, r, out = self.run_review("--committed-art-diagnostic")
        self.assertEqual((code, r["technical_result"]), (2, "INCOMPLETE"))
        self.assertTrue((out / "stills/home.png").is_file())        # the labelled diagnostic still renders

    def test_no_art_lock_renders_nothing(self):
        code, r, out = self.run_review()
        self.assertEqual(code, 2)
        self.assertEqual(r["technical_result"], "INCOMPLETE")
        self.assertEqual(self.rendered(), [], "the simulator was called without an art lock")
        self.assertFalse((out / "stills").exists(), "stills were produced without an art lock")
        self.assertFalse(self.statuses(r, "still"))
        self.assertIn("UNAVAILABLE", self.statuses(r, "stills", "group"))

    def test_no_art_lock_renders_nothing_even_when_art_is_not_required(self):
        code, r, out = self.run_review(plan=plan_file(self.root, required=["stills", "tests"]))
        self.assertEqual(self.rendered(), [])
        self.assertFalse((out / "stills").exists())
        self.assertEqual(code, 2)

    # -- defect 2 (Dottie): a synthetic PXO got readiness 'art verified'
    def test_fixture_pxo_is_not_native_verified(self):
        lock = self.lock_for(self.root / "art")
        code, r, out = self.run_review("--art-lock", lock, "--art", self.root / "art")
        rd = r["readiness"]
        self.assertEqual(code, 0, r)
        self.assertNotEqual(rd.get("art_verified"), "verified")
        self.assertTrue(rd["art_package_integrity"].startswith("verified"), rd)
        self.assertTrue(rd["art_native_editable"].startswith("UNVERIFIED"), rd)
        self.assertEqual(rd["visual_review"], "pending (Dottie)")
        self.assertFalse(rd["promotion_ready"])

    def test_forged_consistent_receipt_is_only_a_claim(self):
        """A manifest native_check receipt is the Workshop's claim; this tool never re-runs a reopen."""
        art = self.root / "art"
        pkg, sel = package(art, colour=(1, 2, 3, 255))
        m = json.loads((pkg / "manifest.json").read_text())
        atlas = next(f for f in m["files"] if f["name"].endswith("-atlas.png"))
        m["native_check"] = {"match": True, "same_structure": True, "atlas_sha256": atlas["sha256"],
                             "reference": "live editor", "reference_boot_id": "a", "verify_boot_id": "b"}
        (pkg / "manifest.json").write_text(json.dumps(m))
        sel["manifest_sha256"] = sha(pkg / "manifest.json")
        lock = self.root / "lock.json"
        lock.write_text(json.dumps({"schema": "workstation-review/art-lock-v1", "selection": [sel]}))
        code, r, out = self.run_review("--art-lock", lock, "--art", art)
        native = r["readiness"]["art_native_editable"]
        self.assertTrue(native.startswith("CLAIMED"), native)
        self.assertIn("not re-run", native)

    def test_inconsistent_receipt_fails(self):
        self.assertEqual(review.native_receipt({"native_check": {"match": False}}, {})[0], review.FAIL)
        files = {"x-atlas.png": "a" * 64}
        bad = {"native_check": {"match": True, "same_structure": True, "atlas_sha256": "b" * 64,
                                "reference_boot_id": "a", "verify_boot_id": "b"}}
        self.assertEqual(review.native_receipt(bad, files)[0], review.FAIL)
        same_boot = {"native_check": {"match": True, "same_structure": True, "atlas_sha256": "a" * 64,
                                      "reference_boot_id": "a", "verify_boot_id": "a"}}
        self.assertEqual(review.native_receipt(same_boot, files)[0], review.FAIL)   # not a fresh process

    # -- priority 3: the frozen copy (what renders use) is re-verified
    def test_package_mutated_while_freezing_blocks_renders(self):
        lock = self.lock_for(self.root / "art")
        real = review.shutil.copytree

        def copy_then_mutate(src, dst, *a, **k):
            got = real(src, dst, *a, **k)
            for p in Path(dst).glob("*-atlas.png"):
                Image.new("RGBA", (4, 2), (9, 9, 9, 255)).save(p)    # changed after verification
            return got
        review.shutil.copytree = copy_then_mutate
        try:
            code, r, out = self.run_review("--art-lock", lock, "--art", self.root / "art")
        finally:
            review.shutil.copytree = real
        self.assertEqual(code, 1)
        self.assertIn("FAIL", self.statuses(r, "art", "package:thing"))
        self.assertEqual(self.rendered(), [])
        self.assertNotEqual(r["readiness"]["art_package_integrity"][:8], "verified")

    def test_frozen_package_mutated_during_renders_fails(self):
        lock = self.lock_for(self.root / "art")
        real = review.Sim.render

        def render_then_mutate(sim_self, *a, **k):
            for p in (self.root / "out/inputs/art").rglob("*-atlas.png"):
                p.write_bytes(b"changed during the run")
            return real(sim_self, *a, **k)
        review.Sim.render = render_then_mutate
        try:
            code, r, out = self.run_review("--art-lock", lock, "--art", self.root / "art")
        finally:
            review.Sim.render = real
        self.assertEqual(code, 1)
        self.assertEqual(self.statuses(r, "art", "frozen-unchanged"), ["FAIL"])

    # -- priority 2: structured results, no tracebacks; empty required groups never pass
    def test_empty_required_group_fails(self):
        code, r, out = self.run_review("--committed-art-diagnostic", plan=plan_file(self.root, tests=[]))
        self.assertEqual(code, 1)
        self.assertIn("FAIL", self.statuses(r, "plan"))

    def test_unknown_required_stage_fails(self):
        code, r, out = self.run_review("--committed-art-diagnostic", plan=plan_file(self.root, required=["still"]))
        self.assertEqual(code, 1)
        self.assertEqual(self.rendered(), [])

    def test_malformed_plan_entry_is_structured(self):
        code, r, out = self.run_review("--committed-art-diagnostic",
                                       plan=plan_file(self.root, tests=[{"name": "t", "advance_ms": 100}]))
        self.assertEqual(code, 1)
        self.assertIn("FAIL", self.statuses(r, "plan"))
        self.assertEqual(self.rendered(), [])

    def test_unreadable_plan_is_structured(self):
        bad = self.root / "broken.json"
        bad.write_text("{not json")
        code, r, out = self.run_review("--committed-art-diagnostic", plan=bad)
        self.assertEqual(code, 1)
        self.assertEqual(self.statuses(r, "plan"), ["FAIL"])

    def test_not_a_git_checkout_is_structured(self):
        code, r, out = self.run_review("--committed-art-diagnostic", repo=firmware(self.root, git=False))
        self.assertIn(code, (1, 2))
        self.assertIn("FAIL", self.statuses(r, "inputs", "firmware"))

    def test_missing_simulator_is_structured(self):
        import shutil as sh
        sh.rmtree(self.sim)
        code, r, out = self.run_review("--committed-art-diagnostic")
        self.assertEqual(code, 2)
        self.assertEqual(self.statuses(r, "inputs", "simulator"), ["UNAVAILABLE"])

    def test_malformed_target_registration_is_structured(self):
        repo = firmware(self.root, targets={"fake": {"adapter": "tools/uisim/targets/fake.c"}})   # no sources
        code, r, out = self.run_review("--committed-art-diagnostic", repo=repo)
        self.assertEqual(code, 1)
        self.assertIn("FAIL", self.statuses(r, "target", "fake"))
        self.assertEqual(self.rendered(), [])


class BuildMembershipPaths(unittest.TestCase):
    def test_same_basename_elsewhere_does_not_count(self):
        with tempfile.TemporaryDirectory() as tmp:
            proj = Path(tmp) / "proj"
            build = proj / "build"
            build.mkdir(parents=True)
            (build / "compile_commands.json").write_text(json.dumps([
                {"directory": str(build), "file": str(proj / "components/other/ui_home.c"), "command": "cc"},
                {"directory": str(build), "file": "../main/ui_kit.c", "command": "cc"}]))
            status, detail = review.build_membership(build, proj, ["main/ui_home.c", "main/ui_kit.c"])
            self.assertEqual(status, review.WARN, detail)
            self.assertIn("main/ui_home.c", detail)
            self.assertNotIn("main/ui_kit.c'", detail)


if __name__ == "__main__":
    unittest.main()
