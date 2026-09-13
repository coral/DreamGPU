
# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('win9x_stage', (Path(__file__).resolve().parents[2] / 'scripts/fixtures/win9x-stage.py'))
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class StartupMigration(unittest.TestCase):
    def test_preserves_other_sections_and_line_endings(self):
        before = b'[windows]\r\nload=\r\nrun=C:\\JRGBENCH.EXE\r\n[other]\nrun=keep.exe\n'
        self.assertEqual(stage.migrate_startup(before), before.replace(b'C:\\JRGBENCH.EXE', b'C:\\DGPUBEN.EXE'))

    def test_current_runner_is_stable(self):
        data = b'[windows]\r\nrun=C:\\DGPUBEN.EXE\r\n'
        self.assertEqual(stage.migrate_startup(data), data)

    def test_rejects_ambiguous_or_unrelated_startup(self):
        for data in (b'[windows]\nrun=unrelated.exe\n',
                     b'[windows]\nrun=C:\\JRGBENCH.EXE other.exe\n',
                     b'[windows]\nrun=C:\\JRGBENCH.EXE\nrun=C:\\JRGBENCH.EXE\n',
                     b'[windows]\nload=\n', b'[other]\nrun=C:\\JRGBENCH.EXE\n'):
            with self.subTest(data=data), self.assertRaises(ValueError):
                stage.migrate_startup(data)


if __name__ == '__main__':
    unittest.main()
