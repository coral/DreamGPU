# SPDX-License-Identifier: GPL-2.0-or-later
"""Full installer automation: reject false success and do not replay completed work."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'dg_system_install', Path(__file__).resolve().parents[1] / 'fixtures/system-install.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def result(operation='sysinstall', code=0, phase=2, intent=0, terminal=True):
    return json.dumps({'schema': 1, 'operation': operation, 'installer_exit': code,
                       'intent': intent, 'phase': phase, 'epoch': 1,
                       'provider_generation': 1, 'terminal': terminal}, separators=(',', ':'))


class ReceiptTests(unittest.TestCase):
    def test_pending_is_not_terminal(self):
        value = installer.receipt(result(code=11, phase=0, terminal=False), 'sysinstall')
        self.assertFalse(value['terminal'])

    def test_reject_inconsistent_failed_or_duplicate_receipts(self):
        for text in (result(code=30), result(terminal=False), result(code=11),
                     result(code=0, phase=5), result(operation='sysremove'),
                     result() + '\n' + result(), 'PASS automated sysinstall'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                installer.receipt(text, 'sysinstall')

    def test_resume_distinguishes_rollback_from_uninstall(self):
        for code, intent in ((12, 2), (13, 3)):
            self.assertEqual(installer.receipt(
                result('sysresume', code, 5, intent), 'sysresume')['installer_exit'], code)
        with self.assertRaises(ValueError):
            installer.receipt(result('sysresume', 12, 5, 3), 'sysresume')

    def test_repair_accepts_its_completion_and_unchanged_noop(self):
        self.assertEqual(installer.OPERATIONS['repair'], 'sysrepair')
        for operation, intent in (('sysrepair', 4), ('sysresume', 4), ('sysrepair', 0)):
            with self.subTest(operation=operation, intent=intent):
                self.assertTrue(installer.receipt(
                    result(operation, intent=intent), operation)['terminal'])

    def test_recovery_retains_reverse_intent(self):
        self.assertEqual(installer.OPERATIONS['recover'], 'sysrecover')
        for code, intent in ((12, 2), (13, 3)):
            self.assertTrue(installer.receipt(
                result('sysrecover', code, 5, intent), 'sysrecover')['terminal'])
        with self.assertRaises(ValueError):
            installer.receipt(result('sysrecover'), 'sysrecover')
        with self.assertRaises(ValueError):
            installer.receipt(result('sysrecover', 11, 1, 0, False), 'sysrecover')

    def test_repair_exhaustion_retains_pending_and_does_not_repeat(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'fixture'
            source.mkdir()
            (source / 'run.json').write_text(json.dumps(
                {'state': 'ready', 'serial': '/tmp/test.sock'}))
            def probe(endpoint, command, output, **kwargs):
                self.assertEqual(command, 'sysrepair')
                self.assertEqual(kwargs['timeout'], 480)
                output.mkdir()
                (output / 'engine-output.txt').write_text(
                    result('sysrepair', 11, 1, 4, False))
                return {'state': 'completed'}
            with patch.object(installer.fixture.hl, 'run_demo', side_effect=probe) as call, \
                 patch.object(installer.fixture, 'bootstrap_program') as shutdown:
                with self.assertRaisesRegex(RuntimeError, 'reboot bound'):
                    installer.run(source, root / 'result', 'repair', r'C:\DGSTOP.EXE', 0)
            call.assert_called_once()
            shutdown.assert_not_called()
            ledger = json.loads((root / 'result/run.json').read_text())
            self.assertEqual(ledger['operation'], 'repair')
            self.assertFalse(ledger['steps'][0]['receipt']['terminal'])

    def test_completed_operation_runs_exactly_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / 'fixture'
            fixture.mkdir()
            (fixture / 'run.json').write_text(json.dumps({'state': 'ready', 'serial': '/tmp/test.sock'}))
            calls = []
            def probe(endpoint, command, output, **kwargs):
                calls.append(command)
                output.mkdir()
                (output / 'engine-output.txt').write_text(result())
                return {'state': 'completed'}
            with patch.object(installer.fixture.hl, 'run_demo', side_effect=probe), \
                 patch.object(installer.fixture, 'bootstrap_program') as shutdown:
                ledger = installer.run(fixture, root / 'result', 'install', r'C:\DGSTOP.EXE')
            self.assertEqual(calls, ['sysinstall'])
            self.assertEqual(ledger['state'], 'completed')
            shutdown.assert_not_called()

    def test_guest_failure_retains_original_error_without_reading_missing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'fixture'
            source.mkdir()
            (source / 'run.json').write_text(json.dumps(
                {'state': 'ready', 'serial': '/tmp/test.sock'}))
            failure = {'message': 'GuestError: installer exit 26'}
            with patch.object(installer.fixture.hl, 'run_demo', return_value={
                    'state': 'error', 'error': failure}) as probe, \
                 patch.object(installer.fixture, 'bootstrap_program') as shutdown:
                with self.assertRaisesRegex(RuntimeError, 'installer exit 26'):
                    installer.run(source, root / 'result', 'install', r'C:\DGSTOP.EXE')
            ledger = json.loads((root / 'result/run.json').read_text())
            self.assertEqual(ledger['steps'][0]['failure'], failure)
            self.assertEqual(ledger['error'], failure['message'])
            probe.assert_called_once()
            shutdown.assert_not_called()

    def test_reboot_bound_failure_keeps_pending_evidence_without_shutdown(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / 'fixture'
            fixture.mkdir()
            (fixture / 'run.json').write_text(json.dumps({'state': 'ready', 'serial': '/tmp/test.sock'}))
            def probe(endpoint, command, output, **kwargs):
                output.mkdir()
                (output / 'engine-output.txt').write_text(result(code=11, phase=1, terminal=False))
                return {'state': 'completed'}
            with patch.object(installer.fixture.hl, 'run_demo', side_effect=probe), \
                 patch.object(installer.fixture, 'bootstrap_program') as shutdown:
                with self.assertRaises(RuntimeError):
                    installer.run(fixture, root / 'result', 'install', r'C:\DGSTOP.EXE', 0)
            ledger = json.loads((root / 'result/run.json').read_text())
            self.assertEqual(ledger['state'], 'error')
            self.assertFalse(ledger['steps'][0]['receipt']['terminal'])
            shutdown.assert_not_called()


if __name__ == '__main__':
    unittest.main()
