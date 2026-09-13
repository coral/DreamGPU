"""Actual manifest/member identity checks for a launched native ELF closure."""
from pathlib import Path
import json
import tempfile
import unittest
from scripts.automation.native_runtime import checked_execution, digest


class NativeRuntimeTests(unittest.TestCase):
    def make(self, root):
        executable=root/'programs/qemu-system-i386';executable.parent.mkdir();executable.write_bytes(b'ELF fixture');executable.chmod(0o755)
        manifest=root/'manifest.json'
        value={'schema':1,'files':{'programs/qemu-system-i386':{'sha256':digest(executable),'size':11,'mode':0o755}},'platform_files':{}}
        manifest.write_text(json.dumps(value))
        artifact={'runtime':{'path':str(manifest),'directory':str(root),'sha256':digest(manifest)},'execution_path':str(executable)}
        return artifact,executable,manifest,value

    def test_raw_and_exact_runtime_execution(self):
        self.assertIsNone(checked_execution({},Path))
        with tempfile.TemporaryDirectory() as tmp:
            artifact,executable,_,_=self.make(Path(tmp))
            self.assertEqual(checked_execution(artifact,Path)['path'],str(executable))
            executable.write_bytes(b'changed ELF')
            with self.assertRaisesRegex(ValueError,'member identity'):checked_execution(artifact,Path)

    def test_mode_symlink_and_manifest_tampering(self):
        for mutation in ('mode','link','manifest'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as tmp:
                artifact,executable,manifest,_=self.make(Path(tmp))
                if mutation=='mode':executable.chmod(0o644)
                elif mutation=='link':
                    replacement=executable.with_suffix('.real');executable.rename(replacement);executable.symlink_to(replacement)
                else:manifest.write_text('{}')
                with self.assertRaises(ValueError):checked_execution(artifact,Path)

    def test_execution_outside_manifest_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            artifact,_,_,_=self.make(Path(tmp));artifact['execution_path']='/unrelated/qemu'
            with self.assertRaises(ValueError):checked_execution(artifact,Path)
