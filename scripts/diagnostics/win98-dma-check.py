#!/usr/bin/env python3
"""Run one fixed readonly Win98 DMA probe with matched native DMA completion evidence."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import signal
import subprocess
import time
from scripts.benchmarks.bench import qmp_execute
from scripts.benchmarks.sample import owned_qemu
_spec=importlib.util.spec_from_file_location('dg_halflife',(Path(__file__).resolve().parents[2] / 'scripts/benchmarks/halflife.py'))
hl=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(hl)
EVENTS=('ide_bus_exec_cmd','ide_dma_cb','dma_blk_io','dma_complete','ide_atapi_cmd_read_dma_cb_aio')
LIMIT=4*1024*1024
PTR=r'0x[0-9a-fA-F]+'
PATTERNS={
 'ata':re.compile(r'\bide_bus_exec_cmd IDE exec cmd: bus ('+PTR+r'); state ('+PTR+r'); cmd 0x([0-9a-fA-F]{2})\b'),
 'ide':re.compile(r'\bide_dma_cb IDEState ('+PTR+r'); sector_num=(\d+) n=(\d+) cmd=(DMA (?:READ|WRITE|TRIM|ATAPI))\b'),
 'start':re.compile(r'\bdma_blk_io dbs=('+PTR+r') bs=('+PTR+r') offset=(\d+) to_dev=([01])\b'),
 'done':re.compile(r'\bdma_complete dbs=('+PTR+r') ret=(-?\d+) cb=('+PTR+r')\b'),
 'atapi':re.compile(r'\bide_atapi_cmd_read_dma_cb_aio IDEState: ('+PTR+r'); aio read: lba=(\d+) n=(\d+)\b')}


def summarize(text):
    if len(text.encode())>LIMIT:raise ValueError('DMA trace exceeds bounded size')
    commands=Counter();active={};finished=[];unmatched=0;negative=0;submitted=0;atapi=0;pending=None
    for line in text.splitlines():
        if len(line)>4096:raise ValueError('DMA trace line exceeds bound')
        match=PATTERNS['ata'].search(line)
        if match:commands[match[3].lower()]+=1
        match=PATTERNS['ide'].search(line)
        if match:
            pending={'state':match[1],'sector':int(match[2]),'sectors':int(match[3]),'command':match[4]};submitted+=1
        match=PATTERNS['start'].search(line)
        if match:
            dbs=match[1]
            if dbs in active:raise ValueError('DMA request address reused before completion')
            offset,direction=int(match[3]),int(match[4]);ide=None
            if pending and offset==pending['sector']*512 and direction==int(pending['command']=='DMA WRITE') and pending['command'] in ('DMA READ','DMA WRITE'):
                ide=pending
            active[dbs]={'dbs':dbs,'offset':offset,'to_device':bool(direction),'ide':ide};pending=None
        match=PATTERNS['done'].search(line)
        if match:
            ret=int(match[2]);negative+=int(ret<0);request=active.pop(match[1],None)
            if request:finished.append(dict(request,result=ret,callback=match[3]))
            else:unmatched+=1
        if PATTERNS['atapi'].search(line):atapi+=1
    successful=[item for item in finished if item['result']==0 and item['ide'] and item['ide']['sectors']>0]
    return {'ata_commands':dict(commands),'ide_dma_submissions':submitted,'matched_successful_ide_dma':len(successful),
            'matched_successful_sectors':sum(item['ide']['sectors'] for item in successful),
            'negative_dma_completions':negative,'unfinished_dma_requests':len(active),'unmatched_completions':unmatched,
            'atapi_dma_read_submissions':atapi,'dma_completions':finished,
            'passed':bool(successful) and bool(commands['c8']+commands['ca']+commands['25']+commands['35']) and negative==0,
            'scope':'ATA DMA commands plus IDE-sector/direction-correlated block DMA requests completed with ret=0. Boundary partials remain visible. Diagnostic trace is not a throughput benchmark.'}


def capture(fixture,package,output):
    fixture=Path(fixture).resolve();package=Path(package).resolve();output=Path(output).resolve()
    state=json.loads((fixture/'run.json').read_text());manifest=json.loads(package.read_text())
    if state.get('state')!='ready':raise ValueError('Fixture must already be ready after cold restart')
    app=subprocess.check_output(['ps','-p',str(state['pid']),'-o','args='],text=True).strip()
    if '--config-dir '+str(fixture) not in app:raise ValueError('App PID does not own fixture')
    pid=owned_qemu(state)
    if qmp_execute(state['qmp'],[('query-status',{})])[0].get('status')!='running':raise ValueError('Owned guest is not running')
    iso=package.parent/'dma.iso'
    if manifest.get('readonly') is not True or hashlib.sha256(iso.read_bytes()).hexdigest()!=manifest['iso_sha256']:
        raise ValueError('Only an unchanged readonly DMA package is accepted')
    blocks=qmp_execute(state['qmp'],[('query-block',{})])[0]
    if not any(block.get('inserted',{}).get('file')==str(iso) for block in blocks):raise ValueError('Exact readonly ISO is not mounted')
    trace=fixture/'native-trace.log';before=trace.stat();previous={}
    for event in EVENTS:
        values=qmp_execute(state['qmp'],[('trace-event-get-state',{'name':event})])[0]
        if len(values)!=1 or values[0].get('state') not in ('enabled','disabled'):raise ValueError('Native trace event unavailable: '+event)
        previous[event]=values[0]['state']=='enabled'
    output.mkdir(parents=True,exist_ok=False);errors=[];guest={};enabled=[];began=time.monotonic()
    def interrupted(signum,frame):raise InterruptedError('DMA capture interrupted by signal '+str(signum))
    handlers={sig:signal.signal(sig,interrupted) for sig in (signal.SIGINT,signal.SIGTERM)}
    try:
        for event in EVENTS:
            enabled.append(event);qmp_execute(state['qmp'],[('trace-event-set-state',{'name':event,'enable':True})])
        guest=hl.run_demo(state['serial'],'update98',output/'guest',fixture/'run.json',30,30,120,probe=True)
    except Exception as error:errors.append(str(error))
    finally:
        for event in reversed(enabled):
            try:qmp_execute(state['qmp'],[('trace-event-set-state',{'name':event,'enable':previous[event]})])
            except Exception as error:errors.append('Restore '+event+': '+str(error))
        for sig,handler in handlers.items():signal.signal(sig,handler)
    after=trace.stat()
    if (before.st_dev,before.st_ino)!=(after.st_dev,after.st_ino) or after.st_size<before.st_size or after.st_size-before.st_size>LIMIT:
        raise ValueError('Native trace changed identity or exceeds bound')
    with trace.open('rb') as stream:stream.seek(before.st_size);raw=stream.read(after.st_size-before.st_size)
    (output/'native-dma.log').write_bytes(raw);evidence=summarize(raw.decode('utf8',errors='replace'))
    passed=evidence['passed'] and guest.get('state')=='completed' and guest.get('result',{}).get('passed') is True and not errors
    report={'schema':1,'fixture':str(fixture),'qemu_pid':pid,'readonly_package':str(package),'duration_seconds':time.monotonic()-began,
            'trace_offsets':[before.st_size,after.st_size],'trace_sha256':hashlib.sha256(raw).hexdigest(),'previous_event_states':previous,
            'evidence':evidence,'guest_result':str(output/'guest/run.json'),'errors':errors,'passed':passed}
    (output/'run.json').write_text(json.dumps(report,indent=2)+'\n');return report


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--package',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=capture(a.fixture,a.package,a.output);print(('PASS' if result['passed'] else 'FAIL')+' DMA completion ('+str(a.output/'run.json')+')');return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
