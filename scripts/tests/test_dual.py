
# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import unittest
import tempfile
import json
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('dual',(Path(__file__).resolve().parents[2] / 'scripts/diagnostics/dual.py'))
dual=importlib.util.module_from_spec(spec);spec.loader.exec_module(dual)

class Socket:
    def __init__(self,states):self.states=iter(states);self.requests=[]
    def command(self,name,event,**fields):
        self.requests.append((name,fields))
        return {'type':'ok'} if name=='set_window_minimized' else {'minimized':next(self.states),'occluded':False,'focused':False}

class Tests(unittest.TestCase):
    def test_reused_pid_rejected_before_any_window_control(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'run.json').write_text(json.dumps({'state':'ready','pid':42,'ws':'unused'}))
            with patch.object(dual.subprocess,'check_output',return_value='juke --config-dir /different-fixture'),patch.object(dual,'WebSocket') as socket:
                with self.assertRaisesRegex(ValueError,'does not own'):dual.attempt(root,root/'out')
                socket.assert_not_called()
    def test_preexisting_minimization_is_preserved_on_preflight_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'run.json').write_text(json.dumps({'state':'ready','pid':42,'ws':'unused'}))
            socket=Socket([True]);socket.close=lambda:None
            with patch.object(dual.subprocess,'check_output',return_value='juke --config-dir '+str(root.resolve())),patch.object(dual,'WebSocket',return_value=socket):
                with self.assertRaisesRegex(ValueError,'Start with'):dual.attempt(root,root/'out')
                self.assertFalse(any(name=='set_window_minimized' for name,_ in socket.requests))

    def test_request_ack_does_not_prove_transition(self):
        socket=Socket([False,False,True]);events=[]
        with patch.object(dual.time,'sleep'):dual.transition(socket,True,events)
        self.assertEqual(sum(name=='set_window_minimized' for name,_ in socket.requests),1)
        self.assertEqual(sum(name=='get_window_state' for name,_ in socket.requests),3)
        self.assertTrue(events[0]['observed']['minimized'])
    def test_unknown_os_state_cannot_pass(self):
        with self.assertRaisesRegex(ValueError,'cannot report'):dual.window_state(Socket([None]))
    def test_unchanged_os_state_has_bounded_deadline(self):
        with patch.object(dual.time,'monotonic',side_effect=[0,0,3]),patch.object(dual.time,'sleep'):
            with self.assertRaisesRegex(RuntimeError,'did not reach'):dual.transition(Socket([False]),True,[])
    def test_exact_two_window_oracle_rejects_one_wrong_pixel(self):
        rows=[]
        for y in range(88):
            row=bytearray(300*4)
            for x in range(80,88):row[x*4:x*4+4]=bytes((0,0,255,255))
            for x in range(272,280):row[x*4:x*4+4]=bytes((255,255,0,255))
            rows.append((y,row,4))
        with patch.object(dual.life.pixels,'png_rows',return_value=iter(rows)):
            self.assertTrue(dual.canvas_pixels(b'ignored')['passed'])
        rows[80][1][80*4]=255
        with patch.object(dual.life.pixels,'png_rows',return_value=iter(rows)):
            self.assertEqual(dual.canvas_pixels(b'ignored')['mismatched'],1)

if __name__=='__main__':unittest.main()

class KWinIdentityTests(unittest.TestCase):
    def test_unique_pid_nonce_and_real_boolean_required(self):
        from scripts.automation.kwin import validate
        good={'token':'own','pid':42,'count':1,'minimized':True,'window':'unique'}
        self.assertTrue(validate(good,42,'own')['minimized'])
        for changes in ({'pid':43},{'token':'stale'},{'count':0},{'count':2},{'minimized':1},{'minimized':None}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):validate(good|changes,42,'own')
