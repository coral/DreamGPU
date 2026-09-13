"""Linux perf control acknowledgements enclose an observed retail game lifecycle.

These boundaries cover process resume through console-summary observation. They
are deliberately not labelled as the engine's exact timedemo frame interval.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import os
from pathlib import Path
import select
import stat
import re
import tempfile
import shutil
import subprocess
import sys
import time

from scripts.benchmarks.sample import owned_qemu


def event_summary(recording):
    """Inspect real recorded event times after guest cleanup; bound tool time/output."""
    perf, timeout = shutil.which('perf'), shutil.which('timeout')
    if not perf or not timeout:
        raise ValueError('perf and timeout are required to inspect captured event timestamps')
    command = ['sudo', '-n', timeout, '--signal=TERM', '--kill-after=1', '10s',
               perf, 'script', '-G', '--ns', '-F', 'time', '-i', str(recording.resolve())]
    # Spool rather than retaining arbitrary profiler output in memory. The
    # recording itself is already capped at128 MiB by the owned perf process.
    with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        result = subprocess.run(command, stdout=output, stderr=errors, timeout=12)
        if result.returncode:
            raise RuntimeError(f'perf timestamp inspection exited {result.returncode}')
        if output.tell() > 16 * 1024 * 1024:
            raise ValueError('perf timestamp listing exceeds16 MiB bound')
        output.seek(0)
        count, first, last = 0, None, None
        for line in output:
            if not line.strip():
                continue
            match = re.fullmatch(rb'\s*([0-9]+\.[0-9]+):\s*', line)
            if not match:
                raise ValueError('unexpected perf timestamp record')
            stamp = float(match[1])
            first = stamp if first is None else min(first, stamp)
            last = stamp if last is None else max(last, stamp)
            count += 1
    return {'clock': 'CLOCK_MONOTONIC', 'events': count,
            'first_event_monotonic_seconds': first, 'last_event_monotonic_seconds': last,
            'scope': 'Actual recorded software CPU sample events; not engine frame timestamps.'}


class LaunchSample:
    def __init__(self, state, output):
        if sys.platform != 'linux':
            raise ValueError('Acknowledged launch sampling currently requires Linux perf')
        self.state = state
        self.output = Path(output)
        self.process = None
        self.log = None
        self.control = self.ack = None
        self.stopped = False
        self.started = False
        self.created_fifos = []
        self.optional_ack_nul = False
        self.report = {
            'requested': True, 'coverage_proven': False, 'exact_engine_interval': 'unknown',
            'clock': 'CLOCK_MONOTONIC (perf --clockid mono)',
            'scope': 'Sampler enabled before owned process resume and disabled after completed console summary observation; launch/loading/log visibility are included.',
            'instrumentation': '499 Hz software cpu-clock sampling with DWARF stacks perturbs execution; this is a diagnostic run.',
        }

    def command(self, command, timeout):
        deadline = time.monotonic() + timeout
        sent = time.monotonic()
        os.write(self.control, command.encode('ascii') + b'\n')
        reply = bytearray()
        while True:
            if self.process.poll() is not None:
                raise RuntimeError('perf exited before control acknowledgement; inspect perf-tool.log')
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('perf control acknowledgement deadline')
            if not select.select([self.ack], [], [], remaining)[0]:
                raise TimeoutError('perf control acknowledgement deadline')
            chunk = os.read(self.ack, 32)
            # Linux perf writes sizeof("ack\\n"), including its terminating NUL.
            # Accept the documented line form too; a fragmented optional NUL
            # belongs to the previous reply, never to the next command's ack.
            if self.optional_ack_nul and chunk:
                if chunk.startswith(b'\0'):
                    chunk = chunk[1:]
                self.optional_ack_nul = False
            reply.extend(chunk)
            if not b'ack\n\0'.startswith(reply):
                raise RuntimeError('unexpected perf control acknowledgement')
            if reply in (b'ack\n', b'ack\n\0'):
                self.optional_ack_nul = reply == b'ack\n'
                return {'command_sent_monotonic_seconds': sent,
                        'ack_observed_monotonic_seconds': time.monotonic(),
                        'ack_bytes_hex': reply.hex()}

    def start(self):
        if self.started or self.stopped:
            raise RuntimeError('Sampler instances are single-use')
        self.started = True
        pid = owned_qemu(self.state)
        perf, timeout = shutil.which('perf'), shutil.which('timeout')
        if not perf or not timeout:
            raise ValueError('Linux perf and timeout are required for bounded acknowledged sampling')
        self.output.mkdir(parents=True, exist_ok=True)
        control_path, ack_path = self.output/'perf-control.fifo', self.output/'perf-ack.fifo'
        try:
            recording=self.output/'perf.data'
            if recording.exists() or recording.is_symlink():
                raise FileExistsError('Sampler recording already exists')
            for path in (control_path, ack_path):
                os.mkfifo(path, 0o600)
                identity = path.lstat()
                self.created_fifos.append((path, identity.st_dev, identity.st_ino))
            self.control = os.open(control_path, os.O_RDWR | os.O_NONBLOCK)
            self.ack = os.open(ack_path, os.O_RDWR | os.O_NONBLOCK)
            self.log = (self.output/'perf-tool.log').open('xb')
            command = ['sudo', '-n', timeout, '--signal=INT', '--kill-after=2', '130s',
                       perf, 'record', '--clockid', 'mono', '-e', 'cpu-clock', '-D', '-1',
                       '--control', f'fifo:{control_path.resolve()},{ack_path.resolve()}',
                       '--max-size', '128M', '-F', '499', '--call-graph', 'dwarf,8192',
                       '-p', f'{pid},{self.state["pid"]}', '-o', str((self.output/'perf.data').resolve()),
                       '--', '/bin/cat']
            self.report.update(pids=[pid, self.state['pid']], command=command,
                               tool_spawn_monotonic_seconds=time.monotonic())
            # The fixed cat workload receives EOF when this pipe closes, giving
            # perf a normal bounded shutdown without signalling an unrelated PID.
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                             stdout=self.log, stderr=subprocess.STDOUT)
            self.report['enable'] = self.command('enable', 4)
        except BaseException:
            self.stop()
            raise

    def stop(self):
        if self.stopped:
            return
        self.stopped = True
        if self.process is not None:
            if self.process.poll() is None:
                try:
                    self.report['disable'] = self.command('disable', 1)
                except Exception as error:
                    self.report['stop_error'] = str(error)
                finally:
                    self.process.stdin.close()
                try:
                    self.process.wait(timeout=1.5)
                except subprocess.TimeoutExpired:
                    # The fixed privileged timeout remains a final bound even
                    # after controller death; no arbitrary root PID kill is used.
                    self.report['stop_error'] = 'perf did not exit after fixed workload EOF; 130s supervisor remains the cleanup bound'
            self.report['exit_code'] = self.process.poll()
            self.report['tool_exit_observed_monotonic_seconds'] = time.monotonic()
        for name in ('control', 'ack'):
            descriptor = getattr(self, name)
            if descriptor is not None:
                os.close(descriptor)
                setattr(self, name, None)
        if self.log:
            self.log.close()
            self.log = None
        for path, device, inode in self.created_fifos:
            try:
                identity = path.lstat()
                if (identity.st_dev, identity.st_ino) == (device, inode) and stat.S_ISFIFO(identity.st_mode):
                    path.unlink()
            except FileNotFoundError:
                pass
        self.created_fifos.clear()

    def phase(self, kind, observed):
        if kind == 'PROCESS_READY':
            self.start()
        elif kind == 'TIMEDEMO_RESULT':
            self.report['summary_observed_monotonic_seconds'] = observed
            self.stop()

    def finish(self, boundaries):
        self.stop()
        recording = self.output/'perf.data'
        self.report['recording_bytes'] = recording.stat().st_size if recording.is_file() else 0
        if self.report.get('exit_code') == 0 and self.report['recording_bytes'] > 0 and 'recorded_events' not in self.report:
            try:
                self.report['recorded_events'] = event_summary(recording)
            except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
                self.report['inspection_error'] = str(error)
        ready = boundaries.get('PROCESS_READY', {})
        summary = boundaries.get('TIMEDEMO_RESULT', {})
        enable = self.report.get('enable', {})
        disable = self.report.get('disable', {})
        lower = ready.get('ack_sent_monotonic_seconds')
        upper = summary.get('host_monotonic_seconds')
        self.report['coverage_proven'] = bool(
            ready.get('ack') == 'CONTINUE' and lower is not None and upper is not None and
            enable.get('ack_observed_monotonic_seconds', float('inf')) <= lower and
            disable.get('command_sent_monotonic_seconds', float('-inf')) >= upper and
            self.report.get('exit_code') == 0 and self.report['recording_bytes'] > 0 and
            self.report.get('recorded_events', {}).get('events', 0) > 0 and
            not self.report.get('stop_error') and not self.report.get('inspection_error'))
        if lower is not None and upper is not None:
            self.report['enclosing_host_window'] = [lower, upper]
        return self.report
