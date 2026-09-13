#!/usr/bin/env python3
import importlib.util
import hashlib
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('win98_icd', ROOT / 'scripts/fixtures/win98-icd.py')
icd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(icd)


class Preparation(unittest.TestCase):
    def test_fixed_startup_preserves_unrelated_entries(self):
        data = b'[windows]\r\nrun=C:\\DGPUBEN.EXE\r\n[other]\nrun=keep.exe\n'
        self.assertEqual(icd.startup(data), data.replace(b'run=C:\\DGPUBEN.EXE', b'run=C:\\DGICDBT.EXE'))
        with self.assertRaises(ValueError):
            icd.startup(b'[windows]\nrun=unrelated.exe\n')

    def test_input_contract_rejects_unknown_roles_and_unpinned_existing_driver(self):
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp) / 'input'
            file.write_bytes(b'fixed')
            sha = hashlib.sha256(b'fixed').hexdigest()
            data = dict(schema=1, source=str(file), source_sha256=sha,
                        files={role: dict(path=str(file), sha256=sha, before_sha256=sha if role in ('driver', 'vxd') else None)
                               for role in icd.DESTINATIONS})
            self.assertEqual(icd.validate(data), file.resolve())
            data['files']['driver']['before_sha256'] = None
            with self.assertRaises(ValueError):
                icd.validate(data)
            data['files']['driver']['before_sha256'] = sha
            data['files']['system_hive'] = data['files']['driver']
            with self.assertRaises(ValueError):
                icd.validate(data)
            del data['files']['system_hive']
            file.write_bytes(b'changed')
            with self.assertRaises(ValueError):
                icd.validate(data)


if __name__ == '__main__':
    unittest.main()
