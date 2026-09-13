import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('app_install', Path(__file__).resolve().parents[1]/'scripts/fixtures/app-install.py')
app = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app)


class PackageTests(unittest.TestCase):
    def package(self, root, schema):
        (root/'application').mkdir()
        files = {}
        for name in app.DLLS:
            file = root/'application'/name
            file.write_bytes(('checked '+name).encode())
            files['application/'+name] = app.digest(file)
        canonical = (json.dumps(files, sort_keys=True) if schema == 1 else
                     json.dumps(files, sort_keys=True, ensure_ascii=False, separators=(',', ':')))
        identity = hashlib.sha256(canonical.encode()).hexdigest()
        manifest = {'schema': schema, 'files': files, 'identity': identity}
        if schema == 2: manifest['identity_scheme'] = 'sha256-json-utf8-sorted-compact'
        (root/'package.json').write_text(json.dumps(manifest))
        return identity

    def test_historical_and_cargo_payload_identity(self):
        for schema in (1, 2):
            with self.subTest(schema=schema), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                identity = self.package(root, schema)
                self.assertEqual(set(app.checked_package(root, identity)), set(app.DLLS))
                with self.assertRaisesRegex(ValueError, 'identity mismatch'):
                    app.checked_package(root, '0'*64)
                (root/'application/dgpugl.dll').write_bytes(b'changed')
                with self.assertRaisesRegex(ValueError, 'payload mismatch'):
                    app.checked_package(root, identity)

    def test_unknown_schema_and_symlink_directory_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            identity = self.package(root, 2)
            manifest = json.loads((root/'package.json').read_text())
            manifest['identity_scheme'] = 'source-only'
            (root/'package.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'Unsupported'):
                app.checked_package(root, identity)
            manifest['identity_scheme'] = 'sha256-json-utf8-sorted-compact'
            (root/'package.json').write_text(json.dumps(manifest))
            (root/'application').rename(root/'elsewhere')
            (root/'application').symlink_to(root/'elsewhere', target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'owned directory'):
                app.checked_package(root, identity)


class TransactionTests(unittest.TestCase):
    def test_backup_precedes_mutation_and_preserves_absence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = root/'dgpugl.dll'; payload.write_bytes(b'new')
            class Volume:
                values = {('/SIERRA/Half-Life', 'dgpugl.dll'): b'original'}
                def read(self, directory, name): return self.values.get((directory, name))
                def write(self, directory, name, source):
                    self_test.assertEqual((root/'backup/0/dgpugl.dll').read_bytes(), b'original')
                    self.values[directory, name] = source.read_bytes()
            self_test = self
            records = app.update_members(Volume(), app.APPLICATIONS, {'dgpugl.dll': payload}, root/'backup')
            self.assertEqual(records[0]['previous_sha256'], hashlib.sha256(b'original').hexdigest())
            self.assertTrue(records[1]['previous_absent'])

    def test_unchanged_payload_is_preserved_without_write(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); payload = root/'dll'; payload.write_bytes(b'accepted')
            class Volume:
                def read(self, *_): return b'accepted'
                def write(self, *_): raise AssertionError('unchanged bytes rewritten')
            records = app.update_members(Volume(), [app.APPLICATIONS[0]], {'dgpugl.dll': payload}, root/'backup')
            self.assertEqual(records[0]['previous_sha256'], records[0]['installed_sha256'])

    def test_rejected_write_does_not_publish_or_change_source(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root/'source.qcow2'; source.write_bytes(b'untouched')
            payload = root/'dll'; payload.write_bytes(b'new')
            manifest = root/'manifest.json'
            manifest.write_text(json.dumps({'source': str(source), 'source_sha256': app.digest(source),
                'applications': [app.APPLICATIONS[0]], 'package': 'unused', 'package_identity': 'unused'}))
            class Volume:
                kind = 'ntfs'
                def __init__(self, raw): pass
                def __enter__(self): return self
                def __exit__(self, *_): pass
                def read(self, *_): return b'original'
                def write(self, *_): raise ValueError('injected write failure')
            def run(*args):
                self.assertEqual(args[1], 'convert')
                Path(args[-1]).write_bytes(source.read_bytes())
            with patch.object(app, 'checked_package', return_value={'dgpugl.dll': payload}), patch.object(app, 'Volume', Volume), patch.object(app, 'run', run):
                with self.assertRaisesRegex(ValueError, 'injected'):
                    app.install(manifest, root/'output', 'qemu-img')
            self.assertFalse((root/'output').exists())
            self.assertEqual(source.read_bytes(), b'untouched')
            self.assertFalse(list(root.glob('.app-install-*')))

    def test_ut_redirected_import_maps_to_checked_winedd(self):
        original = {'winedd.dll': Path('/checked/winedd.dll')}
        self.assertEqual(app.app_payloads('/UT99/System', original)['dgddr.dll'], original['winedd.dll'])
        self.assertNotIn('dgddr.dll', app.app_payloads('/SIERRA/Half-Life', original))

    def test_bounds_and_path_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); raw=root/'disk'; raw.write_bytes(b'\0'*512)
            with self.assertRaisesRegex(ValueError, 'MBR'): app.partition(raw)
            manifest=root/'manifest.json'
            manifest.write_text(json.dumps({'source': str(raw), 'source_sha256': app.digest(raw),
                'applications': ['/WINDOWS/System32'], 'package': 'unused', 'package_identity': 'unused'}))
            with self.assertRaisesRegex(ValueError, 'supported application'):
                app.install(manifest, root/'output', 'unused')
            self.assertFalse((root/'output').exists())


if __name__ == '__main__': unittest.main()
