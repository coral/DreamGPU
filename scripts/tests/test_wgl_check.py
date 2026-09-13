
# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("dg_wgl_check", (Path(__file__).resolve().parents[2] / 'scripts/diagnostics/wgl-check.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def rows(change=None):
    """Independent screen rectangles; model the observed lower UV rounding edge."""
    for y in range(768):
        row = bytearray(1024 * 3)
        if 64 <= y < 304:
            left, right = ((0, 0, 255), (255, 255, 0)) if y < 184 else ((255, 0, 0), (0, 255, 0))
            row[64*3:224*3] = bytes(left) * 160
            row[224*3:384*3] = bytes(right) * 160
            row[416*3:576*3] = bytes((255, 255, 0)) * 160
            row[576*3:736*3] = bytes((0, 255, 255)) * 160
            if 177 <= y < 191:
                row[214*3:234*3] = bytes((255, 255, 255)) * 20
        if change and y == change[1]:
            row[change[0]*3:change[0]*3+3] = bytes(change[2])
        yield y, row, 3


class WglCheckTests(unittest.TestCase):
    def check_image(self, change=None):
        with patch.object(checker.pixels, "png_rows", return_value=rows(change)):
            return checker.verify(b"", textured=True)

    def test_oriented_texture_and_bounded_sampling_edge(self):
        result = self.check_image()
        self.assertTrue(result["pass"], result)
        self.assertEqual(result["verified_pixels"], 153600)
        self.assertEqual(result["sampling_boundary_pixels"], 20)

    def test_boundary_does_not_accept_arbitrary_color(self):
        result = self.check_image((214, 191, (0, 0, 255)))
        self.assertFalse(result["pass"])
        self.assertEqual(result["mismatches"], 1)

    def test_adjacent_nonboundary_remains_exact(self):
        result = self.check_image((214, 190, (255, 0, 0)))
        self.assertFalse(result["pass"])
        self.assertEqual(result["mismatches"], 1)


if __name__ == "__main__":
    unittest.main()
