# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import shutil
import subprocess
import unittest
from unittest import mock
from scripts.fixtures import app_removals as removals

ROOT = Path(__file__).resolve().parents[2]
OLD = b'fixture-owned DLL'
SHA = hashlib.sha256(OLD).hexdigest()


def entry(path='/SIERRA/Half-Life/dgpugl.dll'):
    return {'destination': path, 'sha256': SHA}


class RemovalTests(unittest.TestCase):
    def test_manifest_refuses_system_aliases_traversal_and_unknown_members(self):
        for path in ['/WINDOWS/system32/opengl32.dll', '/winnt/SYSTEM32/dgpugl.dll',
                     '/Windows./System/opengl32.dll', '/WINNT~1/System/dgpugl.dll',
                     '/app/../game/opengl32.dll', '/app//dgpugl.dll', '//app/dgpugl.dll',
                     '/app/dgpugl.dll:stream', '/app/savegame.dat', '/kernel32.dll',
                     '/app/subdir\\dgpugl.dll', '/app/dgpugl.dll ', '/app/./opengl32.dll']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                removals.parse([entry(path)])
        for rows in [[entry(), entry('/sierra/half-life/DGPUGL.DLL')],
                     [{'destination': '/app/opengl32.dll', 'sha256': 'unknown'}],
                     [{**entry(), 'allow_unknown': True}]]:
            with self.assertRaises(ValueError):
                removals.parse(rows)
        with self.assertRaises(ValueError):
            removals.parse([entry()], ['/SIERRA/Half-Life/DGPUGL.DLL'])

    def test_owned_root_provider_cleanup_for_fixed_helper_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root/'jrgopengl.dll'
            path.write_bytes(OLD)
            receipt = []
            removals.apply([entry('/jrgopengl.dll')], removals.Mounted(root), receipt)
            self.assertFalse(path.exists())
            self.assertTrue(receipt[0]['absence_verified'])
            path.write_bytes(b'foreign')
            with self.assertRaises(ValueError):
                removals.apply([entry('/jrgopengl.dll')], removals.Mounted(root), [])
            self.assertEqual(path.read_bytes(), b'foreign')

    def test_all_hashes_preflight_before_any_removal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root/'SIERRA/Half-Life'; app.mkdir(parents=True)
            (app/'DGPUGL.DLL').write_bytes(OLD)
            (app/'opengl32.dll').write_bytes(b'foreign')
            volume = removals.Mounted(root)
            rows = removals.parse([entry(), entry('/SIERRA/Half-Life/opengl32.dll')])
            receipt = []
            with self.assertRaises(ValueError):
                removals.apply(rows, volume, receipt)
            self.assertEqual((app/'DGPUGL.DLL').read_bytes(), OLD)
            self.assertEqual(receipt, [])
            rows = removals.parse([entry()])
            removals.apply(rows, volume, receipt)
            self.assertFalse((app/'DGPUGL.DLL').exists())
            self.assertEqual((app/'opengl32.dll').read_bytes(), b'foreign')
            self.assertEqual(receipt, [{'destination': rows[0]['destination'],
                                       'removed_sha256': SHA, 'absence_verified': True}])

    def test_links_and_ambiguous_names_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            real = root/'real'; real.mkdir(); (real/'dgpugl.dll').write_bytes(OLD)
            (root/'app').symlink_to(real, target_is_directory=True)
            volume = removals.Mounted(root)
            with self.assertRaises(ValueError):
                removals.apply(removals.parse([entry('/app/dgpugl.dll')]), volume, [])
            (real/'opengl32.dll').symlink_to(real/'dgpugl.dll')
            with self.assertRaises(ValueError):
                removals.apply(removals.parse([entry('/real/opengl32.dll')]), volume, [])
            (real/'DGPUGL.DLL').write_bytes(OLD)
            original = Path.iterdir
            def listing(path):
                if path == real:
                    return iter([real/'dgpugl.dll', real/'DGPUGL.DLL'])
                return original(path)
            with mock.patch.object(Path, 'iterdir', listing), self.assertRaises(ValueError):
                removals.apply(removals.parse([entry('/real/dgpugl.dll')]), volume, [])

    def test_changed_after_preflight_and_failed_absence_do_not_report_success(self):
        class Volume:
            count = 0
            deletes = 0
            def read(self, _):
                self.count += 1
                return OLD if self.count == 1 else b'foreign'
            def remove(self, _): self.deletes += 1
            def absent(self, _): return False
        volume = Volume(); receipt = []
        with self.assertRaises(ValueError):
            removals.apply([entry()], volume, receipt)
        self.assertEqual((volume.deletes, receipt), (0, []))
        volume.read = lambda _: OLD
        with self.assertRaises(ValueError):
            removals.apply([entry()], volume, receipt)
        self.assertEqual(receipt, [])

    @unittest.skipUnless(all(shutil.which(x) for x in ('mformat','mmd','mcopy','mtype','mdir','mdel')),
                         'mtools required for actual fresh FAT32 image gate')
    def test_actual_fat32_removal_and_last_file_absence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); image = root/'fat.raw'; source = root/'provider.dll'; source.write_bytes(OLD)
            def command(*args):
                return subprocess.check_output([str(x) for x in args], stderr=subprocess.PIPE)
            command('mformat','-C','-F','-T','131072','-i',image,'::')
            command('mmd','-i',image,'::/SIERRA','::/SIERRA/Half-Life')
            command('mcopy','-i',image,source,'::/SIERRA/Half-Life/DGPUGL.DLL')
            command('mcopy','-i',image,source,'::/jrgopengl.dll')
            volume = removals.Fat(str(image), command); receipt = []
            removals.apply([entry('/jrgopengl.dll')], volume, receipt)
            self.assertTrue(volume.absent('/jrgopengl.dll'))
            receipt = []
            self.assertEqual(volume.read('/SIERRA/Half-Life/dgpugl.dll'), OLD)
            removals.apply(removals.parse([entry()]), volume, receipt)
            self.assertTrue(volume.absent('/SIERRA/Half-Life/dgpugl.dll'))
            self.assertTrue(receipt[0]['absence_verified'])
            self.assertEqual(source.read_bytes(), OLD)

    def test_both_cold_stagers_reject_removal_before_copying_source(self):
        for script, function in [('nt-install.py', 'install'), ('win9x-stage.py', 'stage')]:
            spec = importlib.util.spec_from_file_location(script.replace('-', '_'), ROOT/'scripts/fixtures'/script)
            module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); source = root/'source.qcow2'; source.write_bytes(b'untouched source')
                runner = root/'runner.exe'; runner.write_bytes(OLD)
                manifest = root/'manifest.json'
                manifest.write_text(json.dumps({'source': str(source), 'source_sha256': module.digest(source),
                    'files': [{'source': str(runner), 'sha256': SHA, 'destination': '/DGPUBEN.EXE'}],
                    'removals': [entry('/WINDOWS/System/dgpugl.dll')]}))
                with mock.patch('platform.system', return_value='Linux'), self.assertRaises(ValueError):
                    getattr(module, function)(manifest, root/'output', 'qemu-img')
                self.assertEqual(source.read_bytes(), b'untouched source')
                self.assertFalse((root/'output').exists())


if __name__ == '__main__':
    unittest.main()
