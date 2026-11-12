# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
import json
from pathlib import Path
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    'dg_frame_trace', Path(__file__).resolve().parents[2] / 'scripts/diagnostics/frame-trace.py')
frame_trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frame_trace)

TRACE = '''# dreamgpu frame trace 1: frames=2 flags=0x1 limit=67108864
# frame seq= stream= ctx=client:context:drawable OP function arguments
1 seq=9 stream=4 ctx=1:2:3 CALL glMatrixMode 0x1700=GL_MODELVIEW
1 seq=9 stream=4 ctx=1:2:3 DATA_CALL glLightfv 0x4000=GL_LIGHT0 0x1201=GL_DIFFUSE data=16 [1 1 1 1]
1 seq=9 stream=4 ctx=1:2:3 DATA_CALL glDrawRangeElements mode=0x4=GL_TRIANGLES data=220 layout=pos:FLOAT*3
  v0 pos=(0 0 0)
  idx0: 0
1 seq=9 stream=4 ctx=1:2:3 PRESENT - flags=0x0
2 seq=10 stream=4 ctx=1:2:3 CALL glMatrixMode 0x1700=GL_MODELVIEW err=4
2 seq=10 stream=4 ctx=1:2:3 PRESENT - flags=0x0
# end reason=frames frames=2 records=6 bytes=700
'''


class FrameTraceTests(unittest.TestCase):
    def test_summary_counts_records_payloads_errors_and_end(self):
        summary = frame_trace.summarize(TRACE.splitlines(True))
        self.assertEqual(summary['frames'], 2)
        self.assertEqual(summary['records'], 6)
        self.assertEqual(summary['vertex_lines'], 1)
        self.assertEqual(summary['end'], {'reason': 'frames', 'frames': 2, 'records': 6,
                                          'bytes': 700})
        rows = {(r['op'], r['function']): r for r in summary['functions']}
        self.assertEqual(rows['CALL', 'glMatrixMode']['count'], 2)
        self.assertEqual(rows['CALL', 'glMatrixMode']['errors'], 1)
        self.assertEqual(rows['DATA_CALL', 'glDrawRangeElements']['data_bytes'], 220)
        self.assertEqual(rows['PRESENT', '-']['count'], 2)

    def test_request_and_capture_poll_until_done(self):
        self.assertEqual(frame_trace.request(Path('/t/a'), 3, True, 8), '/t/a,3,vertices,limit=8')
        with self.assertRaises(SystemExit):
            frame_trace.request(Path('/t/a,b'), 1)
        states = iter(['armed', 'active', 'done'])
        calls = []

        def qmp(endpoint, commands):
            name, arguments = commands[0]
            calls.append((name, arguments))
            if name == 'qom-get':
                return [json.dumps({'schema': 1, 'state': next(states), 'path': '/t/a',
                                    'reason': 'frames', 'frames': 2, 'completed': 0})]
            return [None]
        with mock.patch.object(frame_trace, 'qmp_execute', qmp):
            result = frame_trace.capture('q', Path('/t/a'), 2, device='/dev0', poll=0)
        self.assertEqual(result['state'], 'done')
        self.assertEqual(calls[0], ('qom-set', {'path': '/dev0', 'property': 'diagnostic-trace',
                                                'value': '/t/a,2'}))
        self.assertEqual(sum(name == 'qom-get' for name, _ in calls), 3)


if __name__ == '__main__':
    unittest.main()
