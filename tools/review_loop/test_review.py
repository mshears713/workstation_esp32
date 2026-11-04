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
            (Path(tmp) / "compile_commands.json").write_text(json.dumps([{"file": "/x/main/ui_home.c", "command": "cc"}]))
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
        self.assertIn("native art not established (committed-art diagnostic)", rd["not_ready_because"])
        self.assertEqual(rd["delivery"], "not verified (this tool does not deliver)")

    def test_blocked_checkpoint_is_not_verified(self):
        r = review.Results()
        r.add("art", "package:thing", review.FAIL, "", required=True)
        rd = review.readiness(r, {"firmware": {}, "firmware_build": "not run"}, "FAIL", "checkpoint validation")
        self.assertEqual(rd["art_verified"], "blocked")
        self.assertFalse(rd["ready_for_visual_acceptance"])


if __name__ == "__main__":
    unittest.main()
