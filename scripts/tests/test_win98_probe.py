#!/usr/bin/env python3
"""Fixture preparation preserves unrelated settings and rejects ambiguous disks."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
spec = importlib.util.spec_from_file_location('win98_probe', (Path(__file__).resolve().parents[2] / 'scripts/diagnostics/win98-probe.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)

class Preparation(unittest.TestCase):
    def test_only_windows_run_changes(self):
        text = '[windows]\r\nload=thing.exe\r\nrun=old.exe\r\n[unrelated]\r\nrun=keep.exe\r\n'
        value = probe.startup_ini(text, 'dgacc.exe')
        self.assertIn('load=thing.exe\r\nrun=C:\\DGACC.EXE', value)
        self.assertIn('[unrelated]\r\nrun=keep.exe', value)
        self.assertNotIn('old.exe', value)

    def test_absent_and_duplicate_run(self):
        self.assertEqual(probe.startup_ini('[windows]\nrun=x\nRUN=y\n','x.exe'),
                         '[windows]\r\nrun=C:\\X.EXE\r\n')
        self.assertIn('[windows]\r\nrun=C:\\X.EXE\r\n[other]', probe.startup_ini('[other]\nx=y','x.exe'))

    def test_mbr_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'disk'
            path.write_bytes(b'\0'*512)
            with self.assertRaises(ValueError): probe.partition_offset(path)
            data = bytearray(512); data[510:] = b'\x55\xaa'
            path.write_bytes(data)
            with self.assertRaises(ValueError): probe.partition_offset(path)
            struct.pack_into('<I',data,454,63); path.write_bytes(data)
            self.assertEqual(probe.partition_offset(path),32256)

if __name__ == '__main__': unittest.main()
