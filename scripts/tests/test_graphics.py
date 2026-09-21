#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The graphics job must not turn skipped tests or source-only evidence into acceptance."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('graphics', Path(__file__).resolve().parents[1] / 'quality/graphics.py')
graphics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(graphics)


class GraphicsTests(unittest.TestCase):
    def setUp(self):
        self.data = graphics.inventory()

    def test_source_and_installed_evidence_are_separate(self):
        for suite in ('source', 'source-i686'):
            selected = graphics.commands(self.data, suite)
            self.assertTrue(selected)
            for name, command in selected:
                self.assertEqual(self.data['tests'][name]['scope'], suite)
                self.assertNotEqual(self.data['tests'][name]['scope'], 'installed-api')
        native = dict(graphics.commands(self.data, 'native'))
        self.assertIn('--ignored', native['native-gpu'])
        self.assertIn('--test-threads=1', native['native-gpu'])
        self.assertIn('native-build', native)

    def test_local_implementation_never_implies_six_platform_acceptance(self):
        data = copy.deepcopy(self.data)
        data['catalog_complete'] = True
        for item in data['contracts']:
            item['implementation'] = 'implemented'
            item['advertised'] = True
        gaps = graphics.acceptance_gaps(data)
        self.assertEqual(len(gaps), 6)
        for pair in data['platforms']:
            self.assertTrue(any(f'{pair["guest"]}/{pair["host"]}' in gap for gap in gaps))

    def test_invalid_inventory_is_rejected(self):
        bad = []
        data = copy.deepcopy(self.data); data['platforms'].pop(); bad.append(data)
        data = copy.deepcopy(self.data); data['contracts'].append(data['contracts'][0]); bad.append(data)
        data = copy.deepcopy(self.data); data['tests']['map']['path'] = '../escape.py'; bad.append(data)
        data = copy.deepcopy(self.data); data['contracts'][0]['tests'] = ['nonexistent']; bad.append(data)
        for data in bad:
            with tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / 'inventory.json'
                path.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    graphics.inventory(path)

    def run_job(self, transcript, status=0, changed=False):
        def execute(command, **kwargs):
            kwargs['stdout'].write(transcript)
            self.assertNotIn('DREAMGPU_QEMU_PATH', kwargs['env'])
            self.assertEqual(kwargs['env']['DREAMGPU_BUILD'], 'native')
            return subprocess.CompletedProcess(command, status)
        before = {'working_source_sha256': 'before'}
        after = {'working_source_sha256': 'after'} if changed else before
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'job'
            with patch.object(graphics.platform, 'platform', return_value='test-host'), \
                 patch.object(graphics, 'commands', return_value=[('native-gpu', ['fake-test'])]), \
                 patch.object(graphics, 'identity', side_effect=[before, after]), \
                 patch.object(graphics.subprocess, 'run', side_effect=execute), \
                 patch.dict(graphics.os.environ, {'DREAMGPU_QEMU_PATH': '/stale/qemu'}):
                result = graphics.run(self.data, 'source', output)
            report = json.loads((output / 'report.json').read_text())
            self.assertFalse(report['semantic_complete'])
            self.assertEqual(report['passed'], result == 0)
            self.assertEqual(report['results'][0]['log_sha256'], graphics.digest(output / 'native-gpu.log'))
            return result, report

    def test_native_job_rejects_empty_ignored_or_failed_tests(self):
        for transcript, status in [
            ('test result: ok. 0 passed; 0 failed; 0 ignored;', 0),
            ('test result: ok. 0 passed; 0 failed; 39 ignored;', 0),
            ('test result: ok. 2 passed; 0 failed; 1 ignored;', 0),
            ('test result: ok. 2 passed; 0 failed; 0 ignored;', 1),
            ('no test result', 0),
        ]:
            with self.subTest(transcript=transcript, status=status):
                self.assertEqual(self.run_job(transcript, status)[0], 1)
        self.assertEqual(self.run_job('test result: ok. 39 passed; 0 failed; 0 ignored;')[0], 0)

    def test_source_change_invalidates_an_otherwise_passing_run(self):
        status, report = self.run_job('test result: ok. 39 passed; 0 failed; 0 ignored;', changed=True)
        self.assertEqual(status, 1)
        self.assertFalse(report['source_unchanged'])
        self.assertTrue(report['results'][0]['passed'])


if __name__ == '__main__':
    unittest.main()
