"""Build and exercise the actual Rust launcher, including private-child identity refusal."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]


class NativeLauncherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.uname().sysname!='Linux':raise unittest.SkipTest('Linux ELF runtime contract')
        cls.temporary=tempfile.TemporaryDirectory(prefix='dreamgpu-launcher-tests-')
        cls.root=Path(cls.temporary.name)
        source=cls.root/'source';shutil.copytree(ROOT/'support/native/launcher',source)
        program=b'#!/bin/sh\nprintf "%s\\n" "$LD_LIBRARY_PATH" "$__EGL_VENDOR_LIBRARY_DIRS" "$1" "$2"\n'
        cls.program=program
        cls.manifest={'schema':1,'files':{'programs/qemu-system-i386':{'sha256':hashlib.sha256(program).hexdigest(),'size':len(program),'mode':0o755}},'platform_files':{}}
        cls.data=json.dumps(cls.manifest).encode();cls.identity=hashlib.sha256(cls.data).hexdigest()
        cls.runtime=cls.root/'runtime'/cls.identity;(cls.runtime/'programs').mkdir(parents=True)
        (cls.runtime/'manifest.json').write_bytes(cls.data)
        path=cls.runtime/'programs/qemu-system-i386';path.write_bytes(program);path.chmod(0o755)
        env=dict(os.environ,DREAMGPU_RUNTIME_MANIFEST=str(cls.runtime/'manifest.json'),DREAMGPU_RUNTIME_DIRECTORY=str(cls.runtime),DREAMGPU_RUNTIME_PROGRAM='qemu-system-i386')
        subprocess.run(['cargo','build','--quiet','--locked','--manifest-path',str(source/'Cargo.toml'),'--target-dir',str(cls.root/'build')],env=env,check=True)
        (cls.root/'bin').mkdir();cls.launcher=cls.root/'bin/qemu-system-i386';shutil.copy2(cls.root/'build/debug/dreamgpu-native-launcher',cls.launcher)

    @classmethod
    def tearDownClass(cls):cls.temporary.cleanup()

    def setUp(self):
        path=self.runtime/'programs/qemu-system-i386'
        if path.is_symlink():path.unlink()
        path.write_bytes(self.program);path.chmod(0o755)

    def run_launcher(self,*args,path=None):
        return subprocess.run([str(path or self.launcher),*args],capture_output=True,text=True)

    def test_real_child_arguments_environment_and_metadata(self):
        before=dict(os.environ)
        result=self.run_launcher('space argument','literal$()')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout.splitlines(),[str(self.runtime/'lib'),str(self.runtime/'egl'),'space argument','literal$()'])
        self.assertEqual(dict(os.environ),before)
        metadata=json.loads(self.run_launcher('--dreamgpu-runtime').stdout)
        self.assertEqual(metadata['program'],str(self.runtime/'programs/qemu-system-i386'))
        self.assertEqual(metadata['manifest_sha256'],self.identity)

    def test_changed_or_symlinked_member_is_never_executed(self):
        path=self.runtime/'programs/qemu-system-i386';path.write_bytes(b'changed')
        self.assertEqual(self.run_launcher().returncode,126)
        path.unlink();path.symlink_to('/bin/sh')
        self.assertEqual(self.run_launcher().returncode,126)

    def test_relocated_launcher_uses_exact_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/'bin/qemu';target.parent.mkdir();shutil.copy2(self.launcher,target)
            metadata=json.loads(self.run_launcher('--dreamgpu-runtime',path=target).stdout)
            self.assertEqual(metadata['directory'],str(self.runtime))

    def test_present_bad_adjacent_package_cannot_fall_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);target=root/'bin/qemu';target.parent.mkdir();shutil.copy2(self.launcher,target)
            (root/'runtime'/self.identity).mkdir(parents=True)
            self.assertEqual(self.run_launcher('--dreamgpu-runtime',path=target).returncode,126)
