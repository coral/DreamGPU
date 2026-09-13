"""Compilation database selection must preserve each actual target variant."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('check_cpp', Path(__file__).resolve().parents[1] / 'scripts/quality/check-cpp.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class DatabaseTests(unittest.TestCase):
    def test_relative_paths_distinct_defines_and_explicit_adjustments(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            own, donor, unrelated = (root / name for name in ('owned.cpp', 'donor.c', 'upstream.c'))
            rows = []
            for source, define in ((own, '-DMODE=1'), (own, '-DMODE=2'), (own, '-DMODE=1'),
                                   (donor, '-DABI=4'), (unrelated, '-DNO=1')):
                rows.append({'directory': str(root), 'file': source.name,
                             'arguments': ['cross-g++', define, '-march=pentium3', '-mno-sse2',
                                           '-fanalyzer', '-o', 'object.o', '-c', source.name]})
            database = root / 'compile_commands.json'
            database.write_text(json.dumps(rows))
            selected = MODULE.compile_entries([database], {own}, {donor}, ['--target=i686-w64-windows-gnu'], ['-fanalyzer'])
            self.assertEqual(len(selected), 3)
            for entry, arguments in selected:
                self.assertIn('-march=pentium3', arguments)
                self.assertIn('-mno-sse2', arguments)
                self.assertIn('--target=i686-w64-windows-gnu', arguments)
                self.assertNotIn('-fanalyzer', arguments)
                self.assertNotIn('object.o', arguments)
                self.assertNotIn(Path(entry['file']).name, arguments)
            self.assertEqual([args[0] for _, args in selected], ['-DMODE=1', '-DMODE=2', '-DABI=4'])

    def test_gitless_snapshot_excludes_upstream_and_build_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            included = ['guest/driver.cpp', 'scripts/tests/oracle.c',
                        'vendor/qemu/hw/display/dreamgpu-gl.c',
                        'vendor/qemu/tests/qtest/dreamgpu-test.c',
                        'vendor/qemu/audio/jukeaudio.c']
            excluded = ['vendor/qemu/hw/display/vga.c', 'vendor/wine/source.c',
                        'target/generated.cpp', 'guest/target/generated.cpp']
            for name in included + excluded:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('/* fixture */\n')
            self.assertEqual(set(MODULE.maintained_files(root)), {root / p for p in included})

    def test_empty_or_malformed_command_fails(self):
        with self.assertRaises(ValueError):
            MODULE.compiler_arguments({'arguments': ['gcc', '-o']}, [], [])


if __name__ == '__main__':
    unittest.main()
