#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check captured host chords against the guest's read-only DGKEYOBS observer.

Requires a ready private fixture, E:\\DGDRV.EXE containing DGKEYOBS, and the
consumer window already focused with mouse capture active. The guest game must
already be running. No game launch, menu input or display-mode changes occur.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from scripts.automation.host_input import HostInput
from scripts.benchmarks.bench import WebSocket
from scripts.benchmarks.halflife import run_demo


def run(fixture, output, guest_pid, guest_window):
    state = json.loads((fixture / 'run.json').read_text())
    if state['state'] != 'ready':
        raise ValueError('A ready private fixture is required')
    output.mkdir(parents=True, exist_ok=False)
    events = []

    def inject(started):
        ws = WebSocket(state['ws'])
        host = HostInput()
        try:
            # Process creation precedes the observer's first key-state sample.
            # Final assertions require every chord, so missed startup is a failure.
            time.sleep(.25)

            def send(kind, key):
                window = ws.command('get_window_state', 'window_state')
                if kind == 'KeyDown' and (not window['focused'] or window['occluded']):
                    raise RuntimeError('Consumer lost host focus before input')
                host.send({kind: key})
                events.append({'after_started_seconds': time.monotonic() - started,
                               'event': {kind: key}})

            for modifiers in ([], ['LeftShift'], ['LeftCtrl', 'LeftShift']):
                for key in modifiers:
                    send('KeyDown', key)
                    time.sleep(.08)
                for key in ('W', 'A', 'S', 'D'):
                    send('KeyDown', key)
                    time.sleep(.25)
                    send('KeyUp', key)
                    time.sleep(.08)
                for key in reversed(modifiers):
                    send('KeyUp', key)
                    time.sleep(.08)
        finally:
            host.close()
            ws.close()
            (output / 'host-events.json').write_text(json.dumps(events, indent=2) + '\n')

    result = run_demo(state['serial'], 'ntupdate', output / 'guest', timeout=70,
                      probe=True, on_started=inject)
    log = output / 'guest/engine-output.txt'
    rows = re.findall(r'ms=(\d+) keys=([0-9a-f]+) foreground=([0-9a-f]+) pid=(\d+)',
                      log.read_text() if log.exists() else '')
    observed = {int(bits, 16) for _, bits, _, _ in rows}
    required = {modifiers | (1 << key) for modifiers in (0, 1 << 4, (1 << 4) | (1 << 6))
                for key in range(4)}
    checks = {
        'observer_completed': result['state'] == 'completed',
        'all_chords_observed': required <= observed,
        'game_kept_foreground': bool(rows) and all(
            int(pid) == guest_pid and int(window, 16) == guest_window
            for _, _, window, pid in rows),
        'no_alt_or_windows_keys': bool(rows) and all(not bits & 0xf00 for bits in observed),
        'all_keys_released': bool(rows) and int(rows[-1][1], 16) == 0,
    }
    report = {'fixture': str(fixture), 'guest_pid': guest_pid, 'guest_window': guest_window,
              'input_boundary': 'host OS -> consumer -> guest', 'checks': checks,
              'passed': all(checks.values()), 'samples': len(rows)}
    (output / 'acceptance.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--guest-pid', type=int, required=True)
    parser.add_argument('--guest-window', type=lambda value: int(value, 0), required=True)
    args = parser.parse_args()
    report = run(args.fixture.resolve(), args.output.resolve(), args.guest_pid, args.guest_window)
    print(json.dumps(report, indent=2))
    sys.exit(0 if report['passed'] else 1)
