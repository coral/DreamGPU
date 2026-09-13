"""Private NTFS directory staging rejects host traversal before file installation."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('nt_install', Path(__file__).resolve().parents[1] / 'scripts/fixtures/nt-install.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Directories(unittest.TestCase):
    def test_invalid_paths(self):
        for value in ['/', 'relative', '/a/../b', '/a\\b']:
            with self.subTest(value=value), self.assertRaises(ValueError):
                module.guest_directory(value)

    def test_mount_is_released_on_guest_symlink_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'mounted'
            outside = Path(temporary) / 'outside'
            outside.mkdir()
            calls = []
            def run(*args):
                calls.append(args)
                if args[2] == 'ntfs-3g':
                    (root / 'redirect').symlink_to(outside, target_is_directory=True)
            with patch.object(module, 'run', run), self.assertRaises(ValueError):
                module.create_directories('/dev/test-loop', root,
                    [module.guest_directory('/redirect/forbidden')])
            self.assertFalse((outside / 'forbidden').exists())
            self.assertEqual(calls[-1][:3], ('sudo', '-n', 'umount'))


if __name__ == '__main__':
    unittest.main()
