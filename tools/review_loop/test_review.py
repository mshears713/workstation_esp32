"""Unit tests for review.py's own logic (python3 -m unittest tools/review_loop/test_review.py).

Everything here is a synthetic FIXTURE built in a temp folder: no firmware art, no simulator.
The end-to-end check is a real run of review.py (see README.md)."""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import review  # noqa: E402


def header(name, w, h, data, cf="ARGB8888", hexfmt=False):
    body = ",".join((f"0x{b:02x}" if hexfmt else str(b)) for b in data)
    return (f"static const uint8_t {name}_data[]={{{body}}};\n"
            f"static const lv_image_dsc_t {name}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_{cf},"
            f".w={w},.h={h},.stride={w * review.BPP[cf]}}},.data_size=sizeof({name}_data),.data={name}_data}};\n")


class Arrays(unittest.TestCase):
    def test_decimal_and_hex_arrays_parse(self):
        for hexfmt in (False, True):
            text = header("a", 2, 1, [1, 2, 3, 255, 0, 0, 0, 0], hexfmt=hexfmt)
            self.assertEqual(review.array_bytes(text, "a_data"), bytes([1, 2, 3, 255, 0, 0, 0, 0]))

    def test_consistent_descriptor_passes(self):
        text = header("a", 2, 2, [7] * 16)
        d = review.descriptors(text)[0]
        self.assertEqual((d["cf"], d["w"], d["h"], d["data"]), ("ARGB8888", 2, 2, "a_data"))
        self.assertEqual(review.check_descriptor(d, text)[0], review.PASS)

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


class Convert(unittest.TestCase):
    def test_argb8888_is_bgra(self):
        img = Image.new("RGBA", (1, 1), (10, 20, 30, 40))
        self.assertEqual(review.convert(img, "ARGB8888"), bytes([30, 20, 10, 40]))

    def test_rgb565_matches_convert_scene(self):
        img = Image.new("RGBA", (1, 1), (255, 128, 8, 255))
        v = (31 << 11) | (32 << 5) | 1
        self.assertEqual(review.convert(img, "RGB565"), bytes([v & 255, v >> 8]))

    def test_rgb565_refuses_transparency(self):
        with self.assertRaises(ValueError):
            review.convert(Image.new("RGBA", (1, 1), (0, 0, 0, 0)), "RGB565")


class Exports(unittest.TestCase):
    def package(self, root, tamper=False):
        """A FIXTURE delivery package in the pixelorama-workshop/delivery-v1 layout."""
        pkg = root / "MCP Workshop Results/pixelorama/thing/r0003-abc"
        pkg.mkdir(parents=True)
        atlas = Image.new("RGBA", (4, 2))
        atlas.putpixel((2, 0), (255, 0, 0, 255))  # frame 1, top-left pixel
        atlas.save(pkg / "thing-r0003-atlas.png")
        (pkg / "thing-r0003.pxo").write_bytes(b"pxo")
        files = [{"name": n, "role": "", "bytes": (pkg / n).stat().st_size,
                  "sha256": hashlib.sha256((pkg / n).read_bytes()).hexdigest()} for n in ("thing-r0003.pxo", "thing-r0003-atlas.png")]
        frames = [{"index": i, "x": 2 * i, "y": 0, "width": 2, "height": 2, "duration_ms": 100} for i in (0, 1)]
        (pkg / "manifest.json").write_text(json.dumps({"schema": "pixelorama-workshop/delivery-v1", "project": "thing",
                                                       "revision": 3, "checkpoint": "r0003", "files": files,
                                                       "atlas": {"width": 4, "height": 2, "frames": frames}}))
        if tamper:
            (pkg / "thing-r0003.pxo").write_bytes(b"changed after delivery")
        return pkg

    def test_delivery_package_frame_is_cropped_from_atlas(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.package(Path(tmp))
            img, info = review.find_export([Path(tmp)], "thing", 1)
            self.assertEqual(info["kind"], "delivery package")
            self.assertEqual(info["revision"], 3)
            self.assertEqual(img.size, (2, 2))
            self.assertEqual(img.convert("RGBA").getpixel((0, 0)), (255, 0, 0, 255))

    def test_tampered_package_is_refused_not_used(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.package(Path(tmp), tamper=True)
            img, why = review.find_export([Path(tmp)], "thing", 1)
            self.assertIsNone(img)
            self.assertIn("not matching manifest SHA-256", why)

    def test_missing_export_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            img, why = review.find_export([Path(tmp)], "nothing", 0)
            self.assertIsNone(img)
            self.assertIn("no export of nothing frame 0", why)


class Motion(unittest.TestCase):
    def frames(self, vanish_at=None, n=10):
        """FIXTURE: a 6x6 block moving 2 px per step; optionally hidden from step vanish_at."""
        out = []
        for i in range(n):
            img = Image.new("RGB", (320, 240), (40, 80, 40))
            if vanish_at is None or i < vanish_at:
                for x in range(20 + 2 * i, 26 + 2 * i):
                    for y in range(100, 106):
                        img.putpixel((x, y), (250, 250, 250))
            out.append((50 * (i + 1), img))
        return out

    def test_smooth_motion_has_no_hints(self):
        st = review.motion_stats(self.frames())
        self.assertEqual(st["hints"], [])
        self.assertEqual(st["median_moving_step_px"], 24)

    def test_frozen_capture_is_reported(self):
        frozen = [(t, self.frames()[0][1]) for t in (50, 100, 150)]
        self.assertEqual(review.motion_stats(frozen)["hints"][0]["kind"], "no-motion")

    def test_sprite_vanishing_is_flagged_at_that_moment(self):
        """Like the real coaster car: moves ~1 px per frame, then is hidden in one frame."""
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


class Overall(unittest.TestCase):
    def test_required_unavailable_is_incomplete_not_pass(self):
        r = review.Results()
        r.add("provenance", "simulator", review.UNAVAILABLE, "missing", required=True)
        self.assertEqual(r.overall(), ("INCOMPLETE", 2))

    def test_any_fail_wins(self):
        r = review.Results()
        r.add("a", "b", review.PASS, "")
        r.add("a", "c", review.FAIL, "")
        self.assertEqual(r.overall(), ("FAIL", 1))


if __name__ == "__main__":
    unittest.main()
