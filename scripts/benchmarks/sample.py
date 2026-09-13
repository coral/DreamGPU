"""Bounded Mac CPU sampling tied to a known guest measurement interval."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

from datetime import datetime, timezone
from pathlib import Path
import subprocess
import threading
import time


def owned_qemu(state):
    """Only a direct child with this exact frozen executable and QMP socket."""
    children=subprocess.check_output(['pgrep','-P',str(state['pid'])],text=True).split()
    if len(children)>32:raise ValueError('Too many fixture children for bounded sampler discovery')
    expected=str(Path(state['native']).resolve());matches=[]
    for child in children:
        if not child.isdecimal():raise ValueError('Invalid child PID')
        value=subprocess.check_output(['ps','-p',child,'-o','ppid=','-o','args='],text=True).strip()
        parts=value.split(None,1)
        if len(parts)!=2 or parts[0]!=str(state['pid']):continue
        command=parts[1]
        if command.startswith(expected+' ') and state['qmp'] in command:matches.append(int(child))
    if len(matches)!=1:raise ValueError('Expected one exact fixture-owned QEMU for sampling')
    return matches[0]


class MeasurementSample:
    def __init__(self, state, output):
        self.state=state;self.output=Path(output);self.process=None
        self.thread=None;self.done=threading.Event();self.log=None
        self.report={'requested':True,'valid_for_measured_rendering':False,
                     'scope':'CPU sampling perturbs execution; only a completed sample entirely inside MEASURING/MEASURED is usable.'}

    def start(self):
        pid=owned_qemu(self.state)
        path=self.output/'qemu.sample.txt'
        self.log=(self.output/'qemu.sample-tool.log').open('wb')
        self.report.update(pid=pid,path=str(path),started_utc=datetime.now(timezone.utc).isoformat(),
                           start_monotonic_seconds=time.monotonic(),duration_requested_seconds=2)
        try:
            self.process=subprocess.Popen(['/usr/bin/sample',str(pid),'2','-file',str(path)],
                                          stdout=self.log,stderr=subprocess.STDOUT)
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
        complete=self.done.is_set()
        self.report['measurement_end_monotonic_seconds']=measured_end
        self.report['valid_for_measured_rendering']=bool(complete and measured_end is not None and
            self.report.get('exit_code')==0 and self.report['end_monotonic_seconds']<=measured_end)
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
