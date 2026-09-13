# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual counter/capture evidence and boundary-owned cleanup, no guest launch."""
if __package__ in (None, ""):
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import copy
import tempfile
import unittest
from unittest.mock import patch
from scripts.benchmarks import halflife_gpu as gpu
from scripts.tests.test_counters import snapshot


def inputs():
    before = snapshot()
    after = copy.deepcopy(before)
    after['elapsed_us'] += 100
    after['gl']['batches'] += 5
    after['gl']['records'] += 5
    after['gl']['functions'][0]['count'] += 5
    capture = {'dropped': 0, 'start_us': 0, 'end_us': 30, 'samples': [
        {'name': 'gpu.drawable.received', 'id': 1, 'value': 1, 'ts': 10},
        {'name': 'gpu.drawable.received', 'id': 1, 'value': 2, 'ts': 20},
        {'name': 'frame.submitted', 'id': 1, 'value': 2, 'ts': 21}]}
    return before, after, capture


class EvidenceTests(unittest.TestCase):
    def test_real_counter_deltas_need_native_draws_and_presented_frames(self):
        before, after, capture = inputs()
        self.assertTrue(gpu.evidence(before, after, capture)['passed'])
        for mutate in (lambda a, c: c.update(dropped=1),
                       lambda a, c: c.update(samples=[]),
                       lambda a, c: a['gl'].update(function_overflow=1),
                       lambda a, c: a['gl']['functions'][0].update(count=2)):
            a, c = copy.deepcopy(after), copy.deepcopy(capture)
            mutate(a, c)
            self.assertFalse(gpu.evidence(before, a, c)['passed'])
        after['elapsed_us'] = 0
        with self.assertRaises(ValueError):
            gpu.evidence(before, after, capture)

    def test_owned_boundary_capture_cleanup_and_failed_summary(self):
        for failed in (False, True):
            with self.subTest(failed=failed), tempfile.TemporaryDirectory() as directory:
                before, after, capture = inputs()
                calls = []
                def qmp(endpoint, commands):
                    self.assertEqual(endpoint, 'owned-qmp')
                    name, args = commands[0]
                    calls.append((name, args))
                    if name == 'query-status': return [{'status': 'running'}]
                    if name == 'qom-get': return [False]
                    return [{}]
                class Socket:
                    def close(self): calls.append(('close', {}))
                class WS:
                    sock = Socket()
                    def __init__(self, endpoint, timeout): assert endpoint == 'owned-ws'
                    def command(self, name, reply):
                        calls.append((name, {}))
                        if name == 'stop_capture':
                            if failed: raise OSError('lost capture')
                            return {'capture': capture}
                        return {}
                with patch.object(gpu, 'owned_qemu', return_value=44), \
                     patch.object(gpu, 'diagnostic_device', return_value='/owned'), \
                     patch.object(gpu, 'diagnostic_stats', side_effect=[before, after]), \
                     patch.object(gpu, 'qmp_execute', side_effect=qmp), patch.object(gpu, 'WebSocket', WS):
                    proof = gpu.GpuProof({'qmp': 'owned-qmp', 'ws': 'owned-ws'}, directory)
                    proof.phase('PROCESS_READY')
                    self.assertTrue(proof.capture_active)
                    self.assertIsNone(proof.phase('PROCESS_RESUMED'))
                    if failed:
                        with self.assertRaises(Exception): proof.phase('TIMEDEMO_RESULT')
                    else:
                        self.assertTrue(proof.phase('TIMEDEMO_RESULT')['gpu_evidence_passed'])
                    self.assertFalse(proof.counters)
                    report = proof.finish()
                    self.assertEqual(report['passed'], not failed)
                    self.assertTrue(any(n == 'qom-set' and a['value'] is False for n, a in calls))
                    self.assertIn('unknown', report['exact_engine_interval'])

if __name__ == '__main__':
    unittest.main()
