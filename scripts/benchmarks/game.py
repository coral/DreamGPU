#!/usr/bin/env python3
"""One fixed UT game attempt with guest, host GPU and native error evidence."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import base64
import hashlib
import time
import importlib.util
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys

from scripts.benchmarks.bench import WebSocket, qmp_execute
from scripts.diagnostics.counters import summarize as summarize_counters
from scripts.benchmarks.sample import MeasurementSample

_spec=importlib.util.spec_from_file_location('dg_halflife',(Path(__file__).resolve().parents[2] / 'scripts/benchmarks/halflife.py'))
hl=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(hl)
TRACE='dreamgpu_gl_reject'
TRACE_LIMIT=2*1024*1024


def diagnostic_device(endpoint):
    """Find only this QEMU's counter interface with a bounded QOM walk."""
    for container in ('/machine/peripheral','/machine/peripheral-anon'):
        entries=qmp_execute(endpoint,[('qom-list',{'path':container})])[0]
        if len(entries)>512:raise ValueError('QOM diagnostic discovery exceeded512 devices')
        for entry in entries:
            if entry.get('type')!='child<dreamgpu>':continue
            path=container+'/'+entry['name']
            properties=qmp_execute(endpoint,[('qom-list',{'path':path})])[0]
            names={prop['name'] for prop in properties}
            if {'diagnostic-counters','diagnostic-stats'}<=names:return path
    return None


def diagnostic_stats(endpoint,path):
    value=qmp_execute(endpoint,[('qom-get',{'path':path,'property':'diagnostic-stats'})])[0]
    result=json.loads(value) if isinstance(value,str) else value
    if not isinstance(result,dict) or result.get('schema')!=1:raise ValueError('Unknown native diagnostic schema')
    return result


def evaluate(guest,capture,native_text):
    """Keep engine lifetime, GPU activity and failure evidence separate."""
    received=[s for s in capture.get('samples',[]) if s['name']=='gpu.drawable.received']
    imported=[s for s in capture.get('samples',[]) if s['name']=='frame.gpu_imported']
    frames=received or imported
    frames=sorted(frames,key=lambda s:s['ts'])
    unique={(s['id'],s['value']) for s in frames}
    submitted=[s for s in capture.get('samples',[]) if s['name']=='frame.submitted' and frames and s['ts']>=frames[0]['ts']]
    rejected=[line for line in native_text.splitlines() if re.search(r'(^|\s)'+TRACE+r'\b',line)]
    checks={'guest_engine_provider_lifetime':guest.get('state')=='completed' and guest.get('result',{}).get('passed') is True,
            'multiple_gpu_frames_received':len(unique)>=2,
            'host_submission_after_gpu_receipt':bool(submitted),
            'zero_dropped_samples':capture.get('dropped')==0,
            'zero_native_gl_rejections':not rejected}
    first,last=(frames[0]['ts'],frames[-1]['ts']) if frames else (None,None)
    return {'passed':all(checks.values()),'checks':checks,'gpu_frame_event':frames[0]['name'] if frames else None,
            'unique_gpu_frames':len(unique),'first_gpu_receipt_us':first,'last_gpu_receipt_us':last,
            'gpu_receipt_span_seconds':(last-first)/1e6 if frames else None,
            'submissions_after_first_gpu_receipt':len(submitted),'native_rejections':rejected,
            'scope':'Whole bounded game attempt. GPU activity is not a visual pixel oracle or steady game FPS; setup/clear frames may be included.',
            'game_fps':None}


def write(path,value):
    path.write_text(json.dumps(value,indent=2)+'\n')


def ensure_host_visible(socket,pid,evidence,timeout=5):
    """One owned activation request; observe visibility before starting guest work."""
    began=time.monotonic();deadline=began+timeout
    evidence.update(passed=False,requested=False,observations=[],focused_required=False)
    previous_timeout=socket.sock.gettimeout()
    try:
        socket.sock.settimeout(min(2,timeout))
        socket.command('focus_window','ok');evidence['requested']=True
        while True:
            remaining=deadline-time.monotonic()
            if remaining<=0:raise RuntimeError('Owned host window stayed minimized or occluded; benchmark not started')
            socket.sock.settimeout(min(2,remaining))
            observed=socket.command('get_window_state','window_state')
            if type(observed.get('occluded')) is not bool:
                raise ValueError('Host cannot report window occlusion; benchmark not started')
            if type(observed.get('minimized')) is not bool:
                if sys.platform!='linux':
                    raise ValueError('Host cannot report window minimization; benchmark not started')
                # Wayland deliberately cannot expose minimized state to clients.
                # KWin observes/restores only the uniquely matched fixture PID.
                from scripts.automation.kwin import observe
                compositor=observe(pid,restore=True)
                observed=dict(observed,minimized=compositor['minimized'],compositor=compositor)
            evidence['observations'].append(dict(observed,seconds=time.monotonic()-began))
            if observed['minimized'] is False and observed['occluded'] is False:
                evidence['passed']=True
                return
            time.sleep(min(.05,max(0,deadline-time.monotonic())))
    except Exception as error:
        evidence['error']=str(error)
        raise
    finally:
        evidence['duration_seconds']=time.monotonic()-began
        socket.sock.settimeout(previous_timeout)


def attempt(fixture,game,output,sample=False,*,phase_hook=None):
    if sample and sys.platform not in ("darwin","linux"):raise ValueError("--sample requires macOS or Linux")
    fixture=Path(fixture).resolve();output=Path(output).resolve()
    manifest=fixture/'run.json';state=json.loads(manifest.read_text())
    if state.get('state')!='ready':raise ValueError('Fixture must already be ready; this command never boots or retries it')
    if type(state.get('pid')) is not int or state['pid']<=0:raise ValueError('Fixture must record an owned application PID')
    command=shlex.split(subprocess.check_output(['ps','-p',str(state['pid']),'-o','args='],text=True))
    if '--config-dir' not in command or command.index('--config-dir')+1>=len(command) or command[command.index('--config-dir')+1]!=str(fixture):
        raise ValueError('Recorded PID does not own this fixture')
    if qmp_execute(state['qmp'],[('query-status',{})])[0].get('status')!='running':raise ValueError('Owned guest is not running')
    native=fixture/'native-trace.log';before=native.stat()
    previous=qmp_execute(state['qmp'],[('trace-event-get-state',{'name':TRACE})])[0]
    if len(previous)!=1 or previous[0].get('state') not in ('enabled','disabled'):
        raise ValueError('Native rejection trace is unavailable; cannot make an error-free acceptance claim')
    output.mkdir(parents=True,exist_ok=False)
    capture={};guest={};errors=[];socket=None;capturing=False;trace_enabled=False
    visibility={}
    diagnostics={'available':False,'boundaries':{}};device=None;screenshot=None
    sampler=MeasurementSample(state,output) if sample else None;profile=None
    def phase(kind):
        nonlocal screenshot,profile
        boundary={'host_monotonic_seconds':time.monotonic()}
        diagnostics['boundaries'][kind]=boundary
        if sampler and kind=='MEASURING':sampler.start()
        if sampler and kind=='MEASURED':profile=sampler.finish(boundary['host_monotonic_seconds'])
        if device:boundary['native']=diagnostic_stats(state['qmp'],device)
        if phase_hook:phase_hook(kind,socket,state)
        if kind=='MEASURED':
            started=time.monotonic()
            shot=socket.command('get_rendered_screenshot','screenshot',vm_id=state.get('machine'),include_cursor=False)
            png=base64.b64decode(shot['png_base64'],validate=True)
            if not png.startswith(b'\x89PNG\r\n\x1a\n'):raise ValueError('Rendered screenshot is not PNG')
            path=output/'measured-rendered.png';path.write_bytes(png)
            screenshot={'path':str(path),'sha256':hashlib.sha256(png).hexdigest(),'width':shot['width'],'height':shot['height'],
                        'phase':kind,'duration_seconds':time.monotonic()-started,
                        'scope':'One authoritative Juke GPU-composited frame at MEASURED, before request-matched capture acknowledgement permits owned-process cleanup. Excludes CRT and final overlays. Capture cost is outside the native measured interval.'}
    try:
        socket=WebSocket(state['ws'],timeout=2)
        ensure_host_visible(socket,state['pid'],visibility)
        socket.sock.settimeout(15)
        trace_enabled=True
        qmp_execute(state['qmp'],[('set_link',{'name':'net0','up':False}),
            ('trace-event-set-state',{'name':TRACE,'enable':True})])
        device=diagnostic_device(state['qmp'])
        if device:
            if qmp_execute(state['qmp'],[('qom-get',{'path':device,'property':'diagnostic-counters'})])[0]:
                raise ValueError('Native counters already owned by another capture')
            qmp_execute(state['qmp'],[('qom-set',{'path':device,'property':'diagnostic-counters','value':True})])
            diagnostics.update(available=True,device=device)
            diagnostics['attempt_start']=diagnostic_stats(state['qmp'],device)
        socket.command('start_capture','ok');capturing=True
        guest=hl.run_demo(state['serial'],game,output/'guest',manifest,30,30,90,probe=True,on_phase=phase)
        for name,event in guest.get('measurement_phases',{}).items():
            if 'callback_error' in event:errors.append(name+': '+event['callback_error'])
    except Exception as error:
        errors.append(str(error))
    finally:
        if sampler and profile is None:
            try:profile=sampler.finish()
            except Exception as error:
                profile=sampler.report;profile['error']='sampler cleanup: '+str(error)
                profile['valid_for_measured_rendering']=False
        if device and diagnostics['available']:
            try:diagnostics['attempt_end']=diagnostic_stats(state['qmp'],device)
            except Exception as error:errors.append('native diagnostics: '+str(error))
            try:qmp_execute(state['qmp'],[('qom-set',{'path':device,'property':'diagnostic-counters','value':False})])
            except Exception as error:errors.append('native counter disable: '+str(error))
        if capturing:
            try:capture=socket.command('stop_capture','performance_capture')['capture']
            except Exception as error:errors.append('host capture: '+str(error))
        if socket:socket.close()
        if trace_enabled:
            try:qmp_execute(state['qmp'],[('trace-event-set-state',{'name':TRACE,'enable':previous[0]['state']=='enabled'})])
            except Exception as error:errors.append('native trace restore: '+str(error))
    native_text='';native_end=before.st_size
    try:
        after=native.stat();native_end=after.st_size
        if (before.st_dev,before.st_ino)!=(after.st_dev,after.st_ino) or after.st_size<before.st_size:
            raise ValueError('Native trace file changed identity or was truncated')
        if after.st_size-before.st_size>TRACE_LIMIT:raise ValueError('Native attempt trace exceeds bounded2MiB collection')
        with native.open('rb') as source:
            source.seek(before.st_size);native_text=source.read(TRACE_LIMIT).decode('utf-8',errors='replace')
    except Exception as error:errors.append('native trace: '+str(error))
    write(output/'host-capture.json',capture);(output/'native-rejections.log').write_text(native_text)
    verdict=evaluate(guest,capture,native_text)
    if all('native' in diagnostics['boundaries'].get(kind,{}) for kind in ('MEASURING','MEASURED')):
        try:
            summary=summarize_counters(diagnostics['boundaries']['MEASURING']['native'],diagnostics['boundaries']['MEASURED']['native'])
            write(output/'counter-delta.json',summary)
            diagnostics['measured_delta']=str(output/'counter-delta.json')
        except (KeyError,TypeError,ValueError) as error:
            errors.append('native measured counters: '+str(error))
    if sampler and not (profile or {}).get('valid_for_measured_rendering'):
        errors.append('Requested CPU sample is not a completed nonempty recording inside the observed measurement interval')
    if errors:verdict['passed']=False
    if sampler:write(output/'sample.json',profile)
    report={'schema':1,'game':game,'fixture':str(fixture),'guest_result':str(output/'guest/run.json'),
            'verdict':verdict,'errors':errors,'diagnostics':diagnostics,'host_visibility':visibility,'rendered_screenshot':screenshot,
            'native_trace_offsets':[before.st_size,native_end],'cpu_sample':profile}
    write(output/'run.json',report)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game',choices=['utd3d','utglide'])
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--sample',action='store_true',help='Mac/Linux: take a bounded two-second CPU sample of the exact owned QEMU inside MEASURING/MEASURED; records containment and instrumentation cost')
    args=parser.parse_args()
    try:report=attempt(args.fixture,args.game,args.output,args.sample)
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as error:
        print(str(error),file=sys.stderr);return 1
    print(('PASS' if report['verdict']['passed'] else 'FAIL')+' '+args.game+' ('+str(args.output/'run.json')+')')
    return 0 if report['verdict']['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
