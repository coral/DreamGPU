
# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import Mock, patch
import zlib

spec = importlib.util.spec_from_file_location('lifecycle', (Path(__file__).resolve().parents[2] / 'scripts/diagnostics/lifecycle.py'))
lifecycle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lifecycle)


class FakeSocket:
    def __init__(self):
        self.active = 'primary'
        self.states = {'primary': 'Running', 'peer': 'Paused'}

    def command(self, kind, expected, **fields):
        if kind == 'get_active':
            return {'vm_id': self.active}
        if kind == 'get_vm_state':
            return {'vms': [{'id': key, 'state': value} for key, value in self.states.items()]}
        if kind == 'switch_vm':
            self.active = fields['vm_id']
            self.states = {key: 'Running' if key == self.active else 'Paused' for key in self.states}
        elif kind == 'switch_to_landing_page':
            self.active = None
        else:
            raise AssertionError(kind)
        return {}


class LifecycleTests(unittest.TestCase):
    def test_prepared_canvas_rejects_black_or_stale_blue_retained_window(self):
        def png(rgb):
            def chunk(kind, payload):
                return struct.pack('!I', len(payload)) + kind + payload + struct.pack('!I', zlib.crc32(kind + payload))
            raw = (b'\0' + bytes(rgb) * 128) * 128
            return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!IIBBBBB', 128, 128, 8, 2, 0, 0, 0)) +
                    chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
        self.assertTrue(lifecycle.retained_pixels(png((255, 0, 0)))['passed'])
        for rgb in ((0, 0, 0), (0, 0, 255)):
            result = lifecycle.retained_pixels(png(rgb))
            self.assertFalse(result['passed'])
            self.assertEqual(result['mismatched'], 256)

    def test_requires_prebooted_peer_and_active_primary(self):
        socket = FakeSocket()
        lifecycle.preflight(socket, 'primary', 'peer')
        socket.states['peer'] = 'Stopped'
        with self.assertRaises(ValueError):
            lifecycle.preflight(socket, 'primary', 'peer')

    def test_roundtrip_checks_pause_resume_then_landing(self):
        socket = FakeSocket()
        events = []
        lifecycle.transitions(socket, 'primary', 'peer', events)
        self.assertEqual([item['target'] for item in events], ['peer', 'primary', None, 'primary'])
        self.assertEqual(events[0]['states']['primary'], 'Paused')
        self.assertEqual(events[-1]['states']['peer'], 'Paused')

    def test_transition_failure_restores_primary_for_guest_cleanup(self):
        socket = Mock()
        with patch.object(lifecycle, 'switch', side_effect=RuntimeError('bounded transition failure')):
            with self.assertRaisesRegex(RuntimeError, 'bounded'):
                lifecycle.transitions(socket, 'primary', 'peer', [])
        socket.command.assert_called_once_with('switch_vm', 'ok', vm_id='primary')

    def test_resource_blocker_failure_only_cleans_its_unique_name(self):
        with patch.object(lifecycle, 'qmp_execute', side_effect=[['Error: active GL resources'], ['']]) as qmp:
            with self.assertRaisesRegex(RuntimeError, 'active GL'):
                lifecycle.resource_checkpoint('owned-qmp')
        commands = [call.args[1][0][1]['command-line'] for call in qmp.call_args_list]
        self.assertEqual(commands[1], commands[0].replace('savevm ', 'delvm '))

    def test_checkpoint_only_deletes_its_unique_owned_name(self):
        with patch.object(lifecycle, 'qmp_execute', return_value=['']) as qmp:
            report = lifecycle.resource_checkpoint('owned-qmp')
        commands = [call.args[1][0][1]['command-line'] for call in qmp.call_args_list]
        self.assertEqual(commands, ['savevm ' + report['name'], 'delvm ' + report['name']])


if __name__ == '__main__':
    unittest.main()
