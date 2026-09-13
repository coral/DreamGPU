"""Bounded CPU samples contained inside observed guest measurement boundaries."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

from scripts.automation.native_runtime import digest as native_digest
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import shlex
import shutil
import sys
import threading
import time


def owned_qemu(state):
    """Only a direct child with this exact frozen executable and QMP socket."""
    if type(state.get('pid')) is not int or state['pid']<=0:
        raise ValueError('Fixture must record a positive owned application PID')
    children=subprocess.check_output(['pgrep','-P',str(state['pid'])],text=True).split()
    if len(children)>32:raise ValueError('Too many fixture children for bounded sampler discovery')
    execution=state.get('native_execution')
    expected=str(Path(execution['path'] if execution else state['native']).resolve());matches=[]
    if execution and native_digest(expected)!=execution['sha256']:
        raise ValueError('Native execution identity changed before sampling')
    for child in children:
        if not child.isdecimal():raise ValueError('Invalid child PID')
        if execution and sys.platform=='linux' and str(Path(f'/proc/{child}/exe').resolve())!=expected:continue
        value=subprocess.check_output(['ps','-p',child,'-o','ppid=','-o','args='],text=True).strip()
        parts=value.split(None,1)
        if len(parts)!=2 or parts[0]!=str(state['pid']):continue
        command=parts[1]
        if not command.startswith(expected+' '):continue
        arguments=shlex.split(command[len(expected):])
        endpoints=[arguments[index+1] for index,value in enumerate(arguments[:-1]) if value=='-qmp']
        # Juke gives QEMU separate control and event monitors. Ownership is
        # established by one exact recorded control socket, not monitor count.
        if not 1<=len(endpoints)<=4:continue
        paths=[endpoint.removeprefix('unix:').split(',',1)[0] for endpoint in endpoints]
        if len(set(paths))!=len(paths):continue
        if paths.count(state['qmp'])==1:matches.append(int(child))
    if len(matches)!=1:raise ValueError('Expected one exact fixture-owned QEMU for sampling')
    return matches[0]


class MeasurementSample:
    def __init__(self, state, output):
        self.state=state;self.output=Path(output);self.process=None
        self.thread=None;self.done=threading.Event();self.log=None;self.started=False
        self.report={'requested':True,'valid_for_measured_rendering':False,
                     'scope':'CPU sampling perturbs execution; this is an interior sample contained inside host-observed MEASURING/MEASURED, not coverage of the whole game or an exact engine-frame interval.',
                     'exact_engine_interval':'unknown'}

    def start(self):
        if self.started:raise RuntimeError('Sampler instances are single-use')
        self.started=True
        pid=owned_qemu(self.state)
        if sys.platform=='darwin':
            path=self.output/'qemu.sample.txt'
            command=['/usr/bin/sample',str(pid),'2','-file',str(path)]
            tool='macOS sample'
        elif sys.platform=='linux':
            perf,timeout=shutil.which('perf'),shutil.which('timeout')
            if not perf or not timeout:raise ValueError('Linux sampling requires perf and timeout')
            path=self.output/'qemu.perf.data'
            command=['sudo','-n',timeout,'--signal=INT','--kill-after=1','8s',
                     perf,'record','--clockid','mono','-e','cpu-clock','-F','499',
                     '--call-graph','dwarf,8192','--max-size','32M','-p',str(pid),
                     '-o',str(path.resolve()),'--','/bin/sleep','2']
            tool='Linux perf cpu-clock, 499 Hz with DWARF stacks'
        else:raise ValueError('CPU sampling requires macOS or Linux')
        if path.exists() or path.is_symlink():raise FileExistsError('Sampler recording already exists')
        self.log=(self.output/'qemu.sample-tool.log').open('xb')
        self.report.update(pid=pid,path=str(path),tool=tool,command=command,
                           started_utc=datetime.now(timezone.utc).isoformat(),
                           start_monotonic_seconds=time.monotonic(),duration_requested_seconds=2)
        try:
            self.process=subprocess.Popen(command,stdout=self.log,stderr=subprocess.STDOUT)
        except BaseException:
            self.log.close();self.log=None;raise
        def collect():
            try:self.report['exit_code']=self.process.wait()
            except Exception as error:self.report['error']=str(error)
            finally:
                self.report['end_monotonic_seconds']=time.monotonic()
                self.report['finished_utc']=datetime.now(timezone.utc).isoformat()
                self.log.close();self.done.set()
        self.thread=threading.Thread(target=collect,name='dg-sample',daemon=True)
        try:self.thread.start()
        except BaseException:
            self.process.terminate()
            try:self.process.wait(timeout=.5)
            except subprocess.TimeoutExpired:
                self.process.kill();self.process.wait(timeout=.5)
            self.log.close();self.done.set()
            self.report['error']='Sampler collector could not start'
            raise

    def finish(self, measured_end=None):
        if self.process is None:return self.report
        path=Path(self.report['path'])
        self.report['recording_bytes']=path.stat().st_size if path.is_file() else 0
        complete=self.done.is_set()
        self.report['measurement_end_monotonic_seconds']=measured_end
        self.report['valid_for_measured_rendering']=bool(complete and measured_end is not None and
            self.report.get('exit_code')==0 and self.report['recording_bytes']>0 and
            self.report['end_monotonic_seconds']<=measured_end)
        if not complete:
            self.report['error']='Sampler did not finish inside the measured interval; terminated during bounded cleanup'
            if self.process.poll() is None:
                self.process.terminate()
                try:self.process.wait(timeout=.5)
                except subprocess.TimeoutExpired:
                    self.process.kill();self.process.wait(timeout=.5)
            self.thread.join(timeout=1)
            if self.thread.is_alive():self.report['error']='Sampler collection thread did not finish after bounded process cleanup'
        return self.report
