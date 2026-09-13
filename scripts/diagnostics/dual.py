#!/usr/bin/env python3
"""One observed host-minimize and concurrent guest GPU process acceptance."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import importlib.util
import json
from pathlib import Path
import time
import sys
import shlex
import subprocess
from scripts.benchmarks.bench import WebSocket

spec=importlib.util.spec_from_file_location('dg_lifecycle',(Path(__file__).resolve().parents[2] / 'scripts/diagnostics/lifecycle.py'))
life=importlib.util.module_from_spec(spec);spec.loader.exec_module(life)


def window_state(socket,pid=None,restore=False):
    state=socket.command('get_window_state','window_state')
    if type(state.get('minimized')) is not bool:
        if sys.platform=='linux' and pid is not None:
            from scripts.automation.kwin import observe
            return observe(pid,restore=restore)
        raise ValueError('Window system cannot report minimization')
    return state


def transition(socket,minimized,events,timeout=2,pid=None):
    began=time.monotonic()
    socket.command('set_window_minimized','ok',minimized=minimized)
    while time.monotonic()-began<timeout:
        state=window_state(socket,pid,restore=not minimized)
        if state['minimized'] is minimized:
            events.append({'requested_minimized':minimized,'observed':state,'seconds':time.monotonic()-began})
            return
        time.sleep(.02)
    raise RuntimeError('Host window did not reach the requested OS minimized state')


def canvas_pixels(raw):
    checked=mismatched=0
    for y,row,channels in life.pixels.png_rows(raw):
        if y>=88:break
        if y<80:continue
        if len(row)<280*channels:raise ValueError('Prepared canvas is smaller than fixed guest windows')
        for start,color in ((80,(0,0,255)),(272,(255,255,0))):
            for x in range(start,start+8):
                checked+=1;mismatched+=tuple(row[x*channels:x*channels+3])!=color
    return {'checked':checked,'mismatched':mismatched,'passed':checked==128 and mismatched==0,
            'scope':'Two distinct GPU windows after actual host restore, prepared canvas before CRT'}


def attempt(fixture,output):
    fixture=Path(fixture).resolve();output=Path(output).resolve()
    state=json.loads((fixture/'run.json').read_text())
    if state.get('state')!='ready' or type(state.get('pid')) is not int or state['pid']<=0:
        raise ValueError('A ready fixture with its owned app PID is required')
    argv=shlex.split(subprocess.check_output(['ps','-p',str(state['pid']),'-o','args='],text=True))
    if '--config-dir' not in argv or argv.index('--config-dir')+1>=len(argv) or argv[argv.index('--config-dir')+1]!=str(fixture):
        raise ValueError('Recorded PID does not own this fixture; refusing window control')
    control=WebSocket(state['ws'],timeout=3)
    events=[];errors=[];report=None;transition_started=False
    try:
        original=window_state(control,state['pid'])
        if original['minimized']:raise ValueError('Start with the owned Juke window restored')
        if control.command('get_active','active_vm').get('vm_id')!=state['machine']:
            raise ValueError('Fixture VM must be active')
        def phase(kind,socket,_state):
            nonlocal transition_started
            if kind=='MEASURING':
                transition_started=True
                transition(socket,True,events,pid=state['pid'])
                # Returning successfully is the only path that sends MINIMIZED
                # over serial; child work remains blocked before this proof.
            elif kind=='MEASURED':
                if window_state(socket,state['pid'])['minimized'] is not True:
                    raise RuntimeError('Host was restored before minimized GPU work completed')
                transition(socket,False,events,pid=state['pid'])
        report=life.game.attempt(fixture,'dual',output,phase_hook=phase)
    finally:
        # Restore the owned host window even after failed/lost phase replies.
        try:
            if transition_started and window_state(control,state['pid'])['minimized']:
                transition(control,False,[],timeout=2,pid=state['pid'])
        except Exception as error:errors.append('Host restore: '+str(error))
        control.close()
    evidence={'schema':1,'host_transitions':events,'errors':errors,
              'scope':'Two actual guest GPU processes, fresh native readback/swap while host OS reports minimized, restored canvas and independent forced exit/survivor draw; no FPS claim'}
    try:evidence['canvas']=canvas_pixels((output/'measured-rendered.png').read_bytes())
    except (OSError,ValueError) as error:errors.append('Canvas: '+str(error))
    if report and report['verdict']['passed']:
        try:evidence['resource_checkpoint']=life.resource_checkpoint(state['qmp'])
        except Exception as error:errors.append('Post-exit resources: '+str(error))
    evidence['passed']=bool(report and report['verdict']['passed'] and len(events)==2 and
                            evidence.get('canvas',{}).get('passed') and not errors)
    life.game.write(output/'dual.json',evidence)
    return evidence


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=attempt(args.fixture,args.output)
    print(('PASS' if result['passed'] else 'FAIL')+' dual ('+str(args.output/'dual.json')+')')
    return 0 if result['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
