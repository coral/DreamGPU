"""Checked source patches fail closed when pinned inputs or outputs drift."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('checked_patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(patches)


def sha(value):
    return hashlib.sha256(value).hexdigest()


class CheckedPatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        (self.source / 'driver.cpp').write_bytes(b'old\n')
        self.patch = self.root / 'driver.patch'
        self.patch.write_text('--- a/driver.cpp\n+++ b/driver.cpp\n@@ -1 +1 @@\n-old\n+new\n')
        self.manifest = self.root / 'manifest.json'
        self.data = {'schema': 1, 'patches': [{'file': 'driver.patch', 'sha256': patches.digest(self.patch)}],
                     'files': {'driver.cpp': {'before_sha256': sha(b'old\n'), 'after_sha256': sha(b'new\n')}}}

    def apply(self):
        self.manifest.write_text(json.dumps(self.data))
        return patches.apply(self.source, self.manifest)

    def test_exact_patch(self):
        result = self.apply()
        self.assertEqual((self.source / 'driver.cpp').read_bytes(), b'new\n')
        self.assertEqual(result['manifest_sha256'], patches.digest(self.manifest))

    def test_input_drift(self):
        (self.source / 'driver.cpp').write_bytes(b'upstream update\n')
        with self.assertRaisesRegex(ValueError, 'Upstream source identity'):
            self.apply()
        self.assertEqual((self.source / 'driver.cpp').read_bytes(), b'upstream update\n')

    def test_patch_drift(self):
        self.patch.write_text(self.patch.read_text().replace('+new', '+other'))
        with self.assertRaisesRegex(ValueError, 'Patch identity'):
            self.apply()
        self.assertEqual((self.source / 'driver.cpp').read_bytes(), b'old\n')

    def test_recorded_crlf_normalization(self):
        (self.source / 'driver.cpp').write_bytes(b'old\r\n')
        self.data['files']['driver.cpp'].update(before_sha256=sha(b'old\r\n'), normalize_lf=True)
        self.apply()
        self.assertEqual((self.source / 'driver.cpp').read_bytes(), b'new\n')

    def test_output_mismatch_preserves_source(self):
        self.data['files']['driver.cpp']['after_sha256'] = sha(b'wrong\n')
        with self.assertRaisesRegex(ValueError, 'Patched source identity'):
            self.apply()
        self.assertEqual((self.source / 'driver.cpp').read_bytes(), b'old\n')

    def test_unrecorded_output_is_rejected(self):
        self.patch.write_text(self.patch.read_text() + '--- /dev/null\n+++ b/extra.cpp\n@@ -0,0 +1 @@\n+extra\n')
        self.data['patches'][0]['sha256'] = patches.digest(self.patch)
        with self.assertRaisesRegex(ValueError, 'outside the recorded manifest'):
            self.apply()
        self.assertFalse((self.source / 'extra.cpp').exists())
        self.assertEqual((self.source / 'driver.cpp').read_bytes(), b'old\n')

    def test_escaping_paths(self):
        for name in ('../driver.cpp', '/driver.cpp', '.', 'a/../../driver.cpp', 'a\\b'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                patches.relative(name)


if __name__ == '__main__':
    unittest.main()
