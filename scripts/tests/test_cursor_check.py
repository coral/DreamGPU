
# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec = importlib.util.spec_from_file_location("dg_cursor_check", (Path(__file__).resolve().parents[2] / 'scripts/diagnostics/cursor-check.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def capture(with_cursor):
    """Independent raw RGB fixture: four binary cursor quadrants on a checkerboard."""
    width, height = 640, 480
    data = bytearray()
    for y in range(height):
        data.append(0)
        for x in range(width):
            rgb = (192, 128, 64) if (((x - 64) // 16) ^ ((y - 64) // 16)) & 1 else (32, 64, 96)
            if with_cursor and 320 <= x < 352 and 256 <= y < 288:
                if y < 272:
                    rgb = (0, 0, 0) if x < 336 else (255, 255, 255)
                elif x >= 336:
                    rgb = tuple(255 - c for c in rgb)
            data.extend(rgb)

    def chunk(kind, payload):
        return struct.pack("!I", len(payload)) + kind + payload + struct.pack("!I", zlib.crc32(kind + payload))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(data)) + chunk(b"IEND", b""))


class CursorCheckTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.drawn, cls.hidden = capture(True), capture(False)

    def test_exact_native_cursor_pixels(self):
        result = checker.verify(self.drawn)
        self.assertTrue(result["pass"])
        self.assertEqual(result["verified_pixels"], 180224)

    def test_missing_overlay_is_not_a_pass(self):
        result = checker.verify(self.hidden)
        self.assertFalse(result["pass"])
        self.assertEqual(result["mismatches"], 768)

    def test_hide_really_removes_cursor(self):
        self.assertTrue(checker.verify(self.hidden, "hidden")["pass"])
        self.assertFalse(checker.verify(self.drawn, "hidden")["pass"])

    def test_nt5_createicon_alpha_reference(self):
        self.assertEqual(checker.compose((192, 128, 64), 16, 0, "alpha"), (128, 64, 32))
        self.assertEqual(checker.compose((32, 64, 96), 0, 16, "alpha"), (0, 254, 0))

    def test_stale_position_is_rejected(self):
        self.assertFalse(checker.verify(self.drawn, left=352, top=288)["pass"])


if __name__ == "__main__":
    unittest.main()
