#!/usr/bin/env python3
"""Prepare and prewarm one private NT peer for the fixed lifecycle acceptance.

Call prepare-peer before starting the primary fixture, then prewarm after the
primary reaches READY. No workload is launched, no screenshot is inspected, and
an existing peer is never silently restarted. Both complete qcow2 chains remain
private; the peer is exposed to the same Juke process by an owned directory link.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import time

from scripts.benchmarks.bench import WebSocket, qmp_execute
from scripts.benchmarks.sample import owned_qemu

_spec=importlib.util.spec_from_file_location('fixture',(Path(__file__).resolve().parents[2] / 'scripts/fixtures/fixture.py'))
fixture=importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fixture)


def write(path,report):
    path.write_text(json.dumps(report,indent=2)+'\n')


def control_qmps(text):
    # The app consumes the events monitor persistently. It cannot serve commands.
    return [value for value in re.findall(r'-qmp unix:([^,\s]+)',text)
            if not value.endswith('-events-qmp.sock')]


def prepare_peer(output):
    output=output.resolve();path=output/'run.json';report=json.loads(path.read_text())
    if report['state']!='prepared' or 'lifecycle_peer' in report:
        raise ValueError('Peer preparation requires a fresh, unstarted primary fixture')
    peer=Path(str(output)+'-peer')
    peer_report=fixture.prepare(output/'manifest-input.json',peer)
    source=peer/'machines'/report['machine']
    link=output/'machines/nt-peer'
    link.symlink_to(source,target_is_directory=True)
    primary=output/'machines'/report['machine']/'machine.toml'
    primary.write_text(fixture.replace_setting(primary.read_text(),'render','shader','crt-lottes'))
    peer_config=source/'machine.toml'
    text=fixture.replace_setting(peer_config.read_text(),'vm','name','Lifecycle independent NT peer')
    peer_config.write_text(fixture.replace_setting(text,'render','shader','none'))
    peer_report['machine_config_sha256']=fixture.digest(peer_config)
    write(peer/'run.json',peer_report)
    report['machine_config_sha256']=fixture.digest(primary)
    report['lifecycle_peer']={'id':'nt-peer','fixture':str(peer),
        'serial':peer_report['serial'],'state':'prepared',
        'configuration':'Private full qcow2 chain and separate wrapper/serial. Primary CRT, peer unfiltered.'}
    write(path,report)
    return report


def prewarm(output,timeout):
    output=output.resolve();path=output/'run.json';report=json.loads(path.read_text())
    peer=report.get('lifecycle_peer',{})
    if report['state']!='ready' or peer.get('state')!='prepared':
        raise ValueError('Prewarm requires a READY primary and a fresh prepared peer')
    command=subprocess.check_output(['ps','-p',str(report['pid']),'-o','args='],text=True).strip()
    if not command.startswith(report['app']+' ') or '--config-dir '+str(output) not in command:
        raise ValueError('Recorded app PID does not own this fixture')
    peer_report=json.loads((Path(peer['fixture'])/'run.json').read_text())
    log=output/'juke.log';offset=log.stat().st_size;deadline=time.monotonic()+timeout
    peer['state']='starting';write(path,report)
    ws=WebSocket(report['ws'],timeout=10)
    try:ws.command('switch_vm','ok',vm_id='nt-peer')
    finally:ws.close()
    control=None;discovery_deadline=min(deadline,time.monotonic()+10)
    while time.monotonic()<discovery_deadline:
        with log.open(errors='replace') as stream:stream.seek(offset);candidates=control_qmps(stream.read())
        if candidates and Path(candidates[-1]).exists():control=candidates[-1];break
        time.sleep(.1)
    if control is None:raise TimeoutError('New peer control QMP was not created')
    owner=dict(report,qmp=control)
    peer['pid']=owned_qemu(owner);peer['qmp']=control
    state=qmp_execute(control,[('query-status',{})])[0]
    if state['running']:raise ValueError('Peer ran before its network was disabled')
    peer['prelaunch']=state
    qmp_execute(control,[('set_link',{'name':'net0','up':False}),('cont',{})])
    peer['nic_down_before_cont']=True;boot=time.monotonic();write(path,report)
    delay=peer_report['source'].get('login_return_after_seconds')
    if delay is None:
        raise ValueError('Peer requires the recorded fixed cold NT login')
    while time.monotonic()-boot<delay:
        time.sleep(max(0,min(.5,delay-(time.monotonic()-boot))))
    qmp_execute(control,[('send-key',{'keys':[{'type':'qcode','data':'ret'}],'hold-time':50})])
    peer['cold_bootstrap']={'login_return_after_seconds':delay,'invocations':1}
    peer['state']='prewarmed'
    peer['readiness_scope']='Backend running with NIC disabled before cont; peer is only a host switching target, not a serial probe owner. No peer serial READY claim.'
    write(path,report)
    ws=WebSocket(report['ws'],timeout=10)
    try:
        ws.command('switch_vm','ok',vm_id=report['machine'])
        resume_deadline=min(deadline,time.monotonic()+3)
        while time.monotonic()<resume_deadline:
            state={v['id']:v['state'] for v in ws.command('get_vm_state','vm_detailed_state')['vms']}
            if state.get(report['machine'])=='Running' and state.get('nt-peer')=='Paused':break
            time.sleep(.02)
        else:raise TimeoutError('Primary did not finish resuming after peer preparation')
    finally:ws.close()
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['prepare-peer','prewarm'])
    parser.add_argument('fixture',type=Path)
    parser.add_argument('--timeout',type=float,default=90)
    args=parser.parse_args()
    if not 50<=args.timeout<=180:parser.error('timeout must be 50..180 seconds')
    report=prepare_peer(args.fixture) if args.command=='prepare-peer' else prewarm(args.fixture,args.timeout)
    print(json.dumps(report['lifecycle_peer'],indent=2))


if __name__=='__main__':main()
