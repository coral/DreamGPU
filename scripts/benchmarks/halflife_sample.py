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
import shutil
import subprocess
import sys
import time

from scripts.benchmarks.sample import owned_qemu


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
        self.report = {
            'requested': True, 'coverage_proven': False, 'exact_engine_interval': 'unknown',
            'clock': 'CLOCK_MONOTONIC (perf --clockid mono)',
            'scope': 'Sampler enabled before owned process resume and disabled after completed console summary observation; launch/loading/log visibility are included.',
            'instrumentation': '499 Hz cycle sampling with DWARF stacks perturbs execution; this is a diagnostic run.',
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
            reply.extend(os.read(self.ack, 32))
            if len(reply) > 16 or (b'\n' in reply and reply != b'ack\n'):
                raise RuntimeError('unexpected perf control acknowledgement')
            if reply == b'ack\n':
                return {'command_sent_monotonic_seconds': sent,
                        'ack_observed_monotonic_seconds': time.monotonic()}

    def start(self):
        pid = owned_qemu(self.state)
        perf, timeout = shutil.which('perf'), shutil.which('timeout')
        if not perf or not timeout:
            raise ValueError('Linux perf and timeout are required for bounded acknowledged sampling')
        self.output.mkdir(parents=True, exist_ok=True)
        control_path, ack_path = self.output/'perf-control.fifo', self.output/'perf-ack.fifo'
        for path in (control_path, ack_path):
            os.mkfifo(path, 0o600)
        self.control = os.open(control_path, os.O_RDWR | os.O_NONBLOCK)
        self.ack = os.open(ack_path, os.O_RDWR | os.O_NONBLOCK)
        self.log = (self.output/'perf-tool.log').open('wb')
        command = ['sudo', '-n', timeout, '--signal=INT', '--kill-after=2', '130s',
                   perf, 'record', '--clockid', 'mono', '-D', '-1',
                   '--control', f'fifo:{control_path.resolve()},{ack_path.resolve()}',
                   '--max-size', '128M', '-F', '499', '--call-graph', 'dwarf,8192',
                   '-p', f'{pid},{self.state["pid"]}', '-o', str((self.output/'perf.data').resolve()),
                   '--', '/bin/cat']
        self.report.update(pids=[pid, self.state['pid']], command=command,
                           tool_spawn_monotonic_seconds=time.monotonic())
        try:
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
        for name in ('perf-control.fifo', 'perf-ack.fifo'):
            path = self.output/name
            if path.exists():
                path.unlink()

    def phase(self, kind, observed):
        if kind == 'PROCESS_READY':
            self.start()
        elif kind == 'TIMEDEMO_RESULT':
            self.report['summary_observed_monotonic_seconds'] = observed
            self.stop()

    def finish(self, boundaries):
        self.stop()
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
            self.report.get('exit_code') == 0 and not self.report.get('stop_error'))
        if lower is not None and upper is not None:
            self.report['enclosing_host_window'] = [lower, upper]
        return self.report
