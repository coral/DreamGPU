"""Sampler ownership, measured-interval claims and real subprocess cleanup."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from scripts.benchmarks import sample as sampler

class SampleTests(unittest.TestCase):
    def test_exact_child_binary_parent_and_qmp_required(self):
        state={'pid':31,'native':'/private/frozen/qemu-system-i386','qmp':'/tmp/owned-qmp'}
        with patch.object(sampler.subprocess,'check_output',side_effect=['41 42','31 /private/frozen/qemu-system-i386 -qmp /tmp/owned-qmp','99 /private/frozen/qemu-system-i386 -qmp /tmp/owned-qmp']):
            self.assertEqual(sampler.owned_qemu(state),41)
        with patch.object(sampler.subprocess,'check_output',side_effect=['41','31 /private/other/qemu-system-i386 -qmp /tmp/owned-qmp']):
            with self.assertRaisesRegex(ValueError,'exact fixture'):sampler.owned_qemu(state)

    @unittest.skipUnless(sys.platform == 'linux', 'actual /proc executable ownership')
    def test_launcher_exec_identity_uses_exact_real_elf(self):
        import os
        executable=str(Path(sys.executable).resolve())
        child=subprocess.Popen([executable,'-c','import time;time.sleep(20)','-qmp','unix:/tmp/owned-runtime-qmp,server=on'])
        try:
            state={'pid':os.getpid(),'native':'/a/frozen/launcher','qmp':'/tmp/owned-runtime-qmp',
                   'native_execution':{'path':executable,'sha256':sampler.native_digest(executable)}}
            self.assertEqual(sampler.owned_qemu(state),child.pid)
            state['native_execution']['sha256']='0'*64
            with self.assertRaisesRegex(ValueError,'identity changed'):sampler.owned_qemu(state)
        finally:
            child.terminate();child.wait(timeout=2)

    def launch(self,folder,code):
        original=subprocess.Popen;item=sampler.MeasurementSample({},Path(folder))
        def child(args,**kwargs):
            path=args[args.index('-file')+1] if '-file' in args else args[args.index('-o')+1]
            self.command=args
            return original([sys.executable,'-c','from pathlib import Path;Path('+repr(path)+').write_text("profile records");'+code],**kwargs)
        with patch.object(sampler,'owned_qemu',return_value=123),patch.object(sampler.subprocess,'Popen',side_effect=child):item.start()
        return item

    def test_completed_sample_has_observed_interval_and_exit(self):
        with tempfile.TemporaryDirectory() as folder:
            item=self.launch(folder,'print("sample complete")')
            self.assertTrue(item.done.wait(timeout=2))
            report=item.finish(time.monotonic())
            self.assertTrue(report['valid_for_measured_rendering']);self.assertEqual(report['exit_code'],0)
            self.assertLessEqual(report['end_monotonic_seconds'],report['measurement_end_monotonic_seconds'])
            self.assertTrue(item.log.closed)

    def test_sample_ending_after_boundary_or_failed_is_never_valid(self):
        with tempfile.TemporaryDirectory() as folder:
            item=self.launch(folder,'raise SystemExit(3)');self.assertTrue(item.done.wait(timeout=2))
            self.assertFalse(item.finish(time.monotonic())['valid_for_measured_rendering'])
        with tempfile.TemporaryDirectory() as folder:
            item=self.launch(folder,'pass');self.assertTrue(item.done.wait(timeout=2))
            self.assertFalse(item.finish(item.report['start_monotonic_seconds'])['valid_for_measured_rendering'])

    def test_unfinished_process_is_terminated_and_joined_without_claim(self):
        with tempfile.TemporaryDirectory() as folder:
            item=self.launch(folder,'import time;time.sleep(30)');start=time.monotonic()
            report=item.finish(start)
            self.assertLess(time.monotonic()-start,2);self.assertFalse(report['valid_for_measured_rendering'])
            self.assertIsNotNone(item.process.poll());self.assertFalse(item.thread.is_alive());self.assertTrue(item.log.closed)

    def test_qmp_prefix_collision_is_not_fixture_ownership(self):
        state={'pid':31,'native':'/private/frozen/qemu','qmp':'/tmp/owned-qmp'}
        with patch.object(sampler.subprocess,'check_output',side_effect=[
                '41','31 /private/frozen/qemu -qmp unix:/tmp/owned-qmp-other,server=on']):
            with self.assertRaisesRegex(ValueError,'exact fixture'):sampler.owned_qemu(state)
        with patch.object(sampler.subprocess,'check_output',side_effect=[
                '41','31 /private/frozen/qemu -qmp unix:/tmp/owned-qmp,server=on,wait=off']):
            self.assertEqual(sampler.owned_qemu(state),41)

    def test_control_and_event_monitors_preserve_exact_child_ownership(self):
        state={'pid':31,'native':'/private/frozen/qemu','qmp':'/tmp/owned-qmp'}
        command='31 /private/frozen/qemu -qmp unix:/tmp/owned-events-qmp,server,nowait -qmp unix:/tmp/owned-qmp,server,nowait'
        with patch.object(sampler.subprocess,'check_output',side_effect=['41',command]):
            self.assertEqual(sampler.owned_qemu(state),41)
        for changed in [command.replace('31 ', '99 ', 1),
                        command.replace('/tmp/owned-qmp,', '/tmp/other-qmp,'),
                        command+' -qmp unix:/tmp/owned-qmp,server=on',
                        command+' -qmp unix:/tmp/owned-events-qmp,server=on',
                        command+' -qmp /tmp/a -qmp /tmp/b -qmp /tmp/c']:
            with self.subTest(command=changed),patch.object(sampler.subprocess,'check_output',side_effect=['41',changed]):
                with self.assertRaisesRegex(ValueError,'exact fixture'):sampler.owned_qemu(state)
        with patch.object(sampler.subprocess,'check_output',side_effect=['41 42',command,command]):
            with self.assertRaisesRegex(ValueError,'exact fixture'):sampler.owned_qemu(state)

    def test_linux_uses_bounded_software_event_and_real_output_before_claim(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(sampler.sys,'platform','linux'),\
                patch.object(sampler.shutil,'which',side_effect=lambda n:'/usr/bin/'+n):
            item=self.launch(folder,'pass');self.assertTrue(item.done.wait(timeout=2))
            self.assertTrue(item.finish(time.monotonic())['valid_for_measured_rendering'])
            self.assertEqual(self.command[self.command.index('-e')+1],'cpu-clock')
            self.assertIn('8s',self.command)
            self.assertEqual(self.command[-3:],['--','/bin/sleep','2'])
            Path(item.report['path']).unlink()
            self.assertFalse(item.finish(time.monotonic())['valid_for_measured_rendering'])
            with self.assertRaisesRegex(RuntimeError,'single-use'):item.start()

if __name__=='__main__':unittest.main()
