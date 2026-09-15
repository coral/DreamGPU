"""Golden adoption: preserve user presentation, reject live sources/tampering, flatten before atomic switch."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('dg_adopt',(Path(__file__).resolve().parents[2] / 'scripts/fixtures/adopt.py'))
adopt=importlib.util.module_from_spec(spec);spec.loader.exec_module(adopt)
ORIGINAL='''# Personal machine\n[vm]\nname = "Windows"\nbackend = '86box' # retained note\nautostart = false\n\n[ui]\nname = "My Windows" # custom title\nicon = "my-icon.png"\n\n[qemu]\nargs = [\n  "-vga", "vmware",\n]\nqemu_binary = "/old/private/qemu-system-i386"\nstartup_snapshot = "old-snapshot"\n[qemu.platform.linux]\nargs = ["-enable-kvm"]\n\n[render]\n# My preferred CRT\nshader = "crt-lottes"\n[render.margin]\nall = 16\n[render.shader_params]\nwarpX = 0.04 # tuning\n'''

class AdoptTests(unittest.TestCase):
    def test_profiles_preserve_presentation_bytes_and_archive_legacy_backend(self):
        for profile in adopt.PROFILES:
            result=adopt.profile_config(ORIGINAL,profile,'.dreamgpu-adoptions/test/disk.qcow2',[])
            parsed=adopt.tomllib.loads(result)
            self.assertEqual(parsed['qemu'],{'profile':profile,'profile_version':1,'disk':'.dreamgpu-adoptions/test/disk.qcow2'})
            self.assertEqual(parsed['vm']['backend'],'qemu')
            self.assertIn('backend = "qemu" # retained note',result)
            for block in ('[ui]\nname = "My Windows" # custom title\nicon = "my-icon.png"\n\n',
                          '[render]\n# My preferred CRT\nshader = "crt-lottes"\n[render.margin]\nall = 16\n[render.shader_params]\nwarpX = 0.04 # tuning\n'):
                self.assertIn(block,result)
            self.assertIn('# startup_snapshot = "old-snapshot"',result)
        with self.assertRaises(ValueError):adopt.profile_config(ORIGINAL,'retro-win95','disk',[])

    def test_live_fixture_is_rejected_even_if_status_is_stale(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture=Path(temporary);path=fixture/'run.json'
            path.write_text(json.dumps({'state':'ready'}))
            with self.assertRaisesRegex(ValueError,'must be stopped'):adopt.stopped_fixture(fixture)
            path.write_text(json.dumps({'state':'stopped','pid':123}))
            with patch.object(adopt,'process_alive',return_value=True):
                with self.assertRaisesRegex(ValueError,'still alive'):adopt.stopped_fixture(fixture)

    def test_real_config_dir_forms_distinguish_destination_from_other_fixture(self):
        machine=Path('/production/files/machines/win2000')
        for option in ('--config-dir /production/files', '--config-dir=/production/files', '', '--config-dir files'):
            with patch.object(adopt.subprocess,'check_output',return_value='123 /build/juke '+option+'\n'):
                with self.assertRaisesRegex(ValueError,'active Juke'):adopt.machine_stopped(machine)
        for option in ('--config-dir /private/fixtures/other', '--config-dir=/private/fixtures/other'):
            with patch.object(adopt.subprocess,'check_output',return_value='123 /build/juke '+option+'\n'):
                adopt.machine_stopped(machine)
        with patch.object(adopt.subprocess,'check_output',return_value='123 /build/qemu-system-i386 -drive file=/production/files/machines/win2000/disk.qcow2\n'):
            with self.assertRaisesRegex(ValueError,'active QEMU'):adopt.machine_stopped(machine)

    def package(self,root):
        directory=root/'package';(directory/'application').mkdir(parents=True)
        (directory/'application/dgpugl.dll').write_bytes(b'fixed provider')
        files={'application/dgpugl.dll':adopt.digest(directory/'application/dgpugl.dll')}
        identity=hashlib.sha256(json.dumps(files,sort_keys=True).encode()).hexdigest()
        (directory/'package.json').write_text(json.dumps({'schema':1,'identity':identity,'files':files}))
        return directory,identity

    def test_package_identity_and_payload_are_both_enforced(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory,identity=self.package(Path(temporary))
            adopt.verify_package(directory,identity)
            with self.assertRaisesRegex(ValueError,'identity mismatch'):adopt.verify_package(directory,'0'*64)
            (directory/'application/dgpugl.dll').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'identity changed'):adopt.verify_package(directory,identity)

    def test_cargo_schema2_package_identity_and_payload_are_enforced(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory,_=self.package(Path(temporary))
            manifest=json.loads((directory/'package.json').read_text())
            identity=hashlib.sha256(json.dumps(manifest['files'],sort_keys=True,
                ensure_ascii=False,separators=(',',':')).encode('utf-8')).hexdigest()
            manifest.update(schema=2,identity_scheme='sha256-json-utf8-sorted-compact',identity=identity,
                            runner_identity='b'*64)
            (directory/'package.json').write_text(json.dumps(manifest))
            adopt.verify_package(directory,identity)
            build={'schema':1,'files':manifest['files'],'build_identity':'b'*64}
            (directory/'manifest.json').write_text(json.dumps(build))
            adopt.verify_package(directory,identity)
            build['build_identity']='c'*64
            (directory/'manifest.json').write_text(json.dumps(build))
            with self.assertRaisesRegex(ValueError,'Build receipt'):
                adopt.verify_package(directory,identity)
            build['build_identity']='b'*64
            (directory/'manifest.json').write_text(json.dumps(build))
            manifest['identity_scheme']='unknown'
            (directory/'package.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'identity scheme'):
                adopt.verify_package(directory,identity)
            manifest['identity_scheme']='sha256-json-utf8-sorted-compact'
            (directory/'package.json').write_text(json.dumps(manifest))
            (directory/'application/dgpugl.dll').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'identity changed'):
                adopt.verify_package(directory,identity)

    def test_actual_qcow_chain_flattens_before_config_switch_and_rejects_conflict(self):
        image_tool=adopt.ROOT/'target/qemu-build/qemu-img'
        if not image_tool.exists():self.skipTest('Source-built qemu-img unavailable')
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);machine=root/'files/machines/win2000';machine.mkdir(parents=True)
            config=machine/'machine.toml';config.write_text(ORIGINAL)
            old=machine/'original-disk.qcow2';old.write_bytes(b'original disk retained')
            fixture=root/'fixture';fixture.mkdir();base=fixture/'base.qcow2';disk=fixture/'disk.qcow2'
            subprocess.run([str(image_tool),'create','-q','-f','qcow2',str(base),'4M'],check=True)
            subprocess.run([str(image_tool),'create','-q','-f','qcow2','-F','qcow2','-b',str(base),str(disk)],check=True)
            subprocess.run([str(image_tool),'snapshot','-c','old-disk-snapshot',str(disk)],check=True)
            (fixture/'run.json').write_text(json.dumps({'state':'stopped','disk':str(disk)}))
            evidence=root/'accepted.json';evidence.write_text('{"recorded_gate":"passed"}')
            package,identity=self.package(root)
            manifest=root/'source.json';manifest.write_text(json.dumps({'profile':'retro-win2000',
                'fixture_sha256':adopt.digest(fixture/'run.json'),'disk_sha256':adopt.digest(disk),
                'package_identity':identity,'shutdown':'Recorded clean guest shutdown',
                'evidence':[{'path':str(evidence),'sha256':adopt.digest(evidence)}]}))
            args=argparse.Namespace(machine=machine,fixture=fixture,source_manifest=manifest,profile='retro-win2000',
                package=package,qemu_img=image_tool,resources=None)
            with patch.object(adopt,'machine_stopped'):
                stage,state=adopt.prepare(args)
                self.assertEqual(config.read_text(),ORIGINAL)
                self.assertEqual(old.read_bytes(),b'original disk retained')
                info=json.loads(subprocess.check_output([str(image_tool),'info','--output=json',state['disk']]))
                self.assertNotIn('backing-filename',info);self.assertFalse(info.get('snapshots'))
                self.assertEqual(adopt.digest(disk),json.loads(manifest.read_text())['disk_sha256'])
                config.write_text(ORIGINAL+'# concurrent edit\n')
                with self.assertRaisesRegex(ValueError,'identity changed'):adopt.apply(stage)
                self.assertTrue(config.read_text().endswith('# concurrent edit\n'))
                config.write_text(ORIGINAL)
                applied=adopt.apply(stage)
                self.assertEqual(applied['state'],'applied')
                self.assertEqual((stage/'original-machine.toml').read_text(),ORIGINAL)
                self.assertEqual(adopt.tomllib.loads(config.read_text())['qemu']['profile'],'retro-win2000')
                self.assertEqual(old.read_bytes(),b'original disk retained')
                with self.assertRaisesRegex(ValueError,'fully prepared'):adopt.apply(stage)

if __name__=='__main__':unittest.main()
