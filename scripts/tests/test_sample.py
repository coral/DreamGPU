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

    def launch(self,folder,code):
        original=subprocess.Popen;item=sampler.MeasurementSample({},Path(folder))
        def child(args,**kwargs):return original([sys.executable,'-c',code],**kwargs)
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

if __name__=='__main__':unittest.main()
