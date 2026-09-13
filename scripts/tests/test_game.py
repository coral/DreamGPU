"""Combined acceptance must reject engine-only and failed native paths."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import unittest
import json
import tempfile
from unittest.mock import patch,Mock
spec=importlib.util.spec_from_file_location('dg_game',(Path(__file__).resolve().parents[2] / 'scripts/benchmarks/game.py'))
game=importlib.util.module_from_spec(spec);spec.loader.exec_module(game)

class GameAcceptanceTests(unittest.TestCase):
    def test_discovers_actual_anonymous_qemu_dg_gpu_counter_properties(self):
        entries=[[],[{'name':'device[1]','type':'child<dreamgpu>'}],
                 [{'name':'diagnostic-counters','type':'bool'},{'name':'diagnostic-stats','type':'string'}]]
        with patch.object(game,'qmp_execute',side_effect=[[entry] for entry in entries]):
            self.assertEqual(game.diagnostic_device('qmp'),'/machine/peripheral-anon/device[1]')

    def test_counter_phases_capture_one_rendered_frame_and_disable_on_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);fixture=root/'fixture';fixture.mkdir()
            (fixture/'run.json').write_text(json.dumps({'state':'ready','pid':123,'qmp':'qmp','ws':'ws://fixture','serial':'serial'}))
            (fixture/'native-trace.log').write_text('');calls=[];stats={'schema':1,'gl':{'batches':2}}
            def qmp(endpoint,commands):
                result=[]
                for name,args in commands:
                    calls.append((name,args))
                    if name=='query-status':result.append({'status':'running'})
                    elif name=='trace-event-get-state':result.append([{'state':'disabled'}])
                    elif name=='qom-get':result.append(False if args['property']=='diagnostic-counters' else json.dumps(stats))
                    else:result.append({})
                return result
            socket=Mock()
            socket.command.side_effect=[{}, {'minimized':False,'occluded':False,'focused':False}, {}, {'png_base64':game.base64.b64encode(b'\x89PNG\r\n\x1a\nfixture').decode(),'width':1,'height':1}, {'capture':{'dropped':0,'samples':[]}}]
            def run(*args,**kwargs):
                kwargs['on_phase']('MEASURING');kwargs['on_phase']('MEASURED')
                raise RuntimeError('guest cleanup failed')
            with patch.object(game.subprocess,'check_output',return_value='juke --config-dir '+str(fixture.resolve())),patch.object(game,'qmp_execute',side_effect=qmp),patch.object(game,'diagnostic_device',return_value='/retro'),patch.object(game,'WebSocket',return_value=socket),patch.object(game.hl,'run_demo',side_effect=run):
                report=game.attempt(fixture,'utd3d',root/'result')
            self.assertTrue(report['diagnostics']['available'])
            self.assertEqual(set(report['diagnostics']['boundaries']),{'MEASURING','MEASURED'})
            self.assertTrue((root/'result/measured-rendered.png').is_file())
            self.assertEqual(report['rendered_screenshot']['phase'],'MEASURED')
            self.assertEqual(sum(call.args[0]=='get_rendered_screenshot' for call in socket.command.call_args_list),1)
            self.assertIn(('qom-set',{'path':'/retro','property':'diagnostic-counters','value':False}),calls)
            self.assertFalse(report['verdict']['passed'])

    def test_visibility_request_is_not_observation_and_focus_is_not_required(self):
        socket=Mock();evidence={}
        socket.command.side_effect=[{},
            {'minimized':False,'occluded':True,'focused':True},
            {'minimized':False,'occluded':False,'focused':False}]
        with patch.object(game.time,'sleep') as sleep:
            game.ensure_host_visible(socket,123,evidence)
        self.assertTrue(evidence['passed'])
        self.assertEqual(len(evidence['observations']),2)
        self.assertFalse(evidence['observations'][-1]['focused'])
        self.assertEqual([c.args[0] for c in socket.command.call_args_list],
                         ['focus_window','get_window_state','get_window_state'])
        sleep.assert_called_once()

    def test_visibility_deadline_fails_without_busy_looping_or_repeat_focus(self):
        socket=Mock();evidence={};clock=[0.0]
        socket.command.side_effect=lambda command,*args: {} if command=='focus_window' else {
            'minimized':False,'occluded':True,'focused':False}
        def sleep(seconds):clock[0]+=seconds
        with patch.object(game.time,'monotonic',side_effect=lambda:clock[0]),patch.object(game.time,'sleep',side_effect=sleep):
            with self.assertRaisesRegex(RuntimeError,'benchmark not started'):
                game.ensure_host_visible(socket,123,evidence,timeout=.1)
        self.assertFalse(evidence['passed'])
        self.assertEqual(len(evidence['observations']),2)
        self.assertEqual(sum(c.args[0]=='focus_window' for c in socket.command.call_args_list),1)

    def test_wayland_uses_exact_pid_compositor_observation_without_requiring_focus(self):
        socket=Mock();evidence={}
        socket.command.side_effect=[{}, {'minimized':None,'occluded':False,'focused':False}]
        with patch.object(game.sys,'platform','linux'),patch('scripts.automation.kwin.observe',return_value={
                'minimized':False,'focused':False,'pid':123,'observer':'KWin compositor'}) as observe:
            game.ensure_host_visible(socket,123,evidence)
        observe.assert_called_once_with(123,restore=True)
        self.assertTrue(evidence['passed'])
        self.assertEqual(evidence['observations'][0]['compositor']['pid'],123)

    def test_unsupported_focus_command_records_failure_before_capture_or_guest_work(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);fixture=root/'fixture';fixture.mkdir()
            (fixture/'run.json').write_text(json.dumps({'state':'ready','pid':123,'qmp':'qmp','ws':'ws://fixture','serial':'serial'}))
            (fixture/'native-trace.log').write_text('')
            socket=Mock();socket.command.side_effect=RuntimeError('Invalid command: focus_window')
            with patch.object(game.subprocess,'check_output',return_value='juke --config-dir '+str(fixture.resolve())), \
                 patch.object(game,'qmp_execute',side_effect=[[{'status':'running'}],[[{'state':'disabled'}]]]) as qmp, \
                 patch.object(game,'WebSocket',return_value=socket),patch.object(game.hl,'run_demo') as run:
                report=game.attempt(fixture,'utd3d',root/'result')
            run.assert_not_called()
            self.assertEqual([call.args[0] for call in socket.command.call_args_list],['focus_window'])
            self.assertEqual(qmp.call_count,2) # Read-only checks; no capture/trace mutation.
            self.assertFalse(report['host_visibility']['passed'])
            self.assertIn('focus_window',report['host_visibility']['error'])
            self.assertFalse(report['verdict']['passed'])
            socket.close.assert_called_once()

    def test_engine_pass_needs_real_gpu_activity_and_clean_trace(self):
        guest={'state':'completed','result':{'passed':True}}
        capture={'dropped':0,'samples':[]}
        self.assertFalse(game.evaluate(guest,capture,'')['passed'])
        capture['samples']=[{'name':'gpu.drawable.received','ts':1000,'id':9,'value':1},
                            {'name':'gpu.drawable.received','ts':4000,'id':9,'value':2},
                            {'name':'frame.submitted','ts':5000,'id':8,'value':2}]
        verdict=game.evaluate(guest,capture,'');self.assertTrue(verdict['passed'])
        self.assertEqual(verdict['gpu_receipt_span_seconds'],.003);self.assertIsNone(verdict['game_fps'])
        self.assertFalse(game.evaluate(guest,capture,'dreamgpu_gl_reject seq=1 a4=0')['passed'])
        capture['dropped']=1;self.assertFalse(game.evaluate(guest,capture,'')['passed'])
    def test_duplicate_receipts_and_preceding_cpu_submission_do_not_pass(self):
        guest={'state':'completed','result':{'passed':True}}
        first={'name':'gpu.drawable.received','ts':20,'id':9,'value':1}
        capture={'dropped':0,'samples':[dict(first),dict(first),{'name':'frame.submitted','ts':10,'id':7,'value':1}]}
        verdict=game.evaluate(guest,capture,'')
        self.assertFalse(verdict['checks']['multiple_gpu_frames_received'])
        self.assertFalse(verdict['checks']['host_submission_after_gpu_receipt'])

    def test_failure_still_saves_capture_and_restores_trace_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);fixture=root/'fixture';fixture.mkdir()
            (fixture/'run.json').write_text(json.dumps({'state':'ready','pid':123,'qmp':'qmp','ws':'ws://fixture','serial':'serial'}))
            (fixture/'native-trace.log').write_text('')
            socket=Mock();socket.command.side_effect=[{}, {'minimized':False,'occluded':False,'focused':False}, {}, {'capture':{'dropped':0,'samples':[]}}]
            with patch.object(game.subprocess,'check_output',return_value='juke --config-dir '+str(fixture.resolve())), \
                 patch.object(game,'qmp_execute',side_effect=[[{'status':'running'}],[[{'state':'disabled'}]],[{},{}],[{}]]) as qmp, \
                 patch.object(game,'WebSocket',return_value=socket), \
                 patch.object(game,'diagnostic_device',return_value=None), \
                 patch.object(game.hl,'run_demo',side_effect=RuntimeError('bounded guest failure')):
                result=game.attempt(fixture,'utd3d',root/'result')
            self.assertFalse(result['verdict']['passed'])
            self.assertEqual(result['errors'],['bounded guest failure'])
            self.assertEqual(qmp.call_args_list[-1].args[1],[('trace-event-set-state',{'name':game.TRACE,'enable':False})])
            socket.close.assert_called_once()
            self.assertTrue((root/'result/host-capture.json').is_file())

if __name__=='__main__':unittest.main()
