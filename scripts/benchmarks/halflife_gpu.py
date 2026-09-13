# SPDX-License-Identifier: GPL-2.0-or-later
"""Owned native submission and presentation evidence around the HL launch window.

The retail engine exposes no exact frame interval. These acknowledged boundaries
include loading; they are never described as per-timedemo-frame GPU attribution.
"""
import hashlib
import json
from pathlib import Path
import time

from scripts.benchmarks.bench import WebSocket, qmp_execute
from scripts.benchmarks.game import diagnostic_device, diagnostic_stats
from scripts.benchmarks.sample import owned_qemu
from scripts.diagnostics.counters import summarize


def evidence(before, after, capture):
    delta = summarize(before, after)
    samples = capture.get('samples', [])
    start, end = capture.get('start_us'), capture.get('end_us')
    if (type(start) is not int or type(end) is not int or start < 0 or end <= start
            or any(type(s.get('ts')) is not int or not start <= s['ts'] <= end for s in samples)):
        raise ValueError('GPU capture timestamps are outside its recorded window')
    received = [s for s in samples if s.get('name') == 'gpu.drawable.received']
    submitted = [s for s in samples if s.get('name') == 'frame.submitted']
    frames = {(s['id'], s['value']) for s in received}
    draw_names = {'Begin', 'End', 'DrawArrays', 'DrawElements', 'DrawPixels', 'Bitmap',
                  'CopyPixels', 'CallList', 'CallLists', 'EvalMesh1', 'EvalMesh2'}
    draws = sum(row['count'] for row in delta['gl']['functions'] if row['name'].removeprefix('gl') in draw_names)
    checks = {'native_gl_batches': delta['gl']['batches'] > 0,
              'native_draw_commands': draws > 0,
              'multiple_native_drawable_frames': len(frames) >= 2,
              'host_submission_after_native_frame': bool(received) and any(
                  s['ts'] >= min(row['ts'] for row in received) for s in submitted),
              'capture_without_drops': capture.get('dropped') == 0,
              'complete_native_function_counts': delta['function_totals_complete']}
    return {'passed': all(checks.values()), 'checks': checks, 'native_delta': delta,
            'native_draw_commands': draws, 'unique_native_frames': len(frames),
            'scope': 'Owned process-resume through summary-observed window, including loading. '
                     'Native GL submissions plus presented GPU drawables; exact engine interval unknown.'}


class GpuProof:
    def __init__(self, state, output):
        self.state, self.output = state, Path(output)
        self.device = self.ws = None
        self.counters = self.capture_active = False
        self.before = self.after = self.capture = None
        self.report = {'requested': True, 'passed': False, 'exact_engine_interval': 'unknown',
                       'instrumentation': 'Native diagnostic counters and host event capture enabled only between acknowledged launch boundaries.'}

    def phase(self, kind):
        if kind == 'PROCESS_READY':
            if self.device is not None:
                raise ValueError('GPU proof is single use')
            self.report['native_pid'] = owned_qemu(self.state)
            if qmp_execute(self.state['qmp'], [('query-status', {})])[0].get('status') != 'running':
                raise ValueError('Fixture is not running')
            self.device = diagnostic_device(self.state['qmp'])
            if not self.device:
                raise ValueError('Native diagnostic counters are unavailable')
            if qmp_execute(self.state['qmp'], [('qom-get', {
                    'path': self.device, 'property': 'diagnostic-counters'})])[0] is not False:
                raise ValueError('Native diagnostic counters already owned')
            self.counters = True
            qmp_execute(self.state['qmp'], [('qom-set', {
                'path': self.device, 'property': 'diagnostic-counters', 'value': True})])
            self.before = diagnostic_stats(self.state['qmp'], self.device)
            self.ws = WebSocket(self.state['ws'], timeout=5)
            self.ws.command('start_capture', 'ok')
            self.capture_active = True
            self.report['armed_monotonic_seconds'] = time.monotonic()
            return {'native_counters': self.before, 'capture_armed': True}
        if kind == 'TIMEDEMO_RESULT':
            if not self.capture_active:
                raise ValueError('GPU proof was not armed before process resume')
            try:
                self.after = diagnostic_stats(self.state['qmp'], self.device)
                self.capture = self.ws.command('stop_capture', 'performance_capture')['capture']
                self.capture_active = False
                self.report['summary_observed_monotonic_seconds'] = time.monotonic()
                self.report.update(evidence(self.before, self.after, self.capture))
                self.report['completed_at_summary'] = True
                return {'native_counters': self.after, 'gpu_evidence_passed': self.report['passed']}
            finally:
                self.close()
        return None

    def close(self):
        errors = []
        if self.capture_active:
            try:
                self.ws.command('stop_capture', 'performance_capture')
                self.capture_active = False
            except Exception as error:
                errors.append('capture cleanup: '+str(error))
        if self.counters:
            try:
                qmp_execute(self.state['qmp'], [('qom-set', {
                    'path': self.device, 'property': 'diagnostic-counters', 'value': False})])
                self.counters = False
            except Exception as error:
                errors.append('counter cleanup: '+str(error))
        if self.ws:
            self.ws.sock.close()
            self.ws = None
        if errors:
            self.report['passed'] = False
            self.report.setdefault('cleanup_errors', []).extend(errors)
            raise RuntimeError('; '.join(errors))

    def finish(self):
        try:
            self.close()
        except Exception as error:
            self.report['error'] = str(error)
        self.report['passed'] = bool(self.report['passed'] and self.report.get('completed_at_summary'))
        for name, value in [('native-before', self.before), ('native-after', self.after),
                            ('gpu-capture', self.capture)]:
            if value is not None:
                data = (json.dumps(value, indent=2)+'\n').encode()
                path = self.output/(name+'.json')
                path.write_bytes(data)
                self.report[name] = {'path': str(path), 'sha256': hashlib.sha256(data).hexdigest()}
        return self.report
