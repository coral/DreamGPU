#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run one fixed full-installer operation through bounded, clean cold boots.

Requires an independent prepared fixture containing the hash-pinned system
acceptance helpers and a recorded guest shutdown helper. Every serial request,
installer result, boot and cloned disk is retained. A pending receipt is never
activation. Errors stop immediately; no unrecorded retry or forced guest reset.
"""
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import json
from pathlib import Path
import time

from scripts.fixtures import fixture

OPERATIONS = {"install": "sysinstall", "upgrade": "sysupgrade",
              "rollback": "sysrollback", "uninstall": "sysremove", "resume": "sysresume",
              "repair": "sysrepair", "recover": "sysrecover"}


def receipt(text, operation):
    values = []
    for line in text.splitlines():
        if line.startswith('{"schema":1,"operation":'):
            values.append(json.loads(line))
    if len(values) != 1:
        raise ValueError('Expected exactly one installer operation receipt')
    value = values[0]
    if value.get('operation') != operation or type(value.get('terminal')) is not bool:
        raise ValueError('Installer receipt operation/terminal mismatch')
    code, phase, intent = (value.get(key) for key in ('installer_exit', 'phase', 'intent'))
    if operation == "sysrecover" and intent not in (2, 3):
        raise ValueError("Recovery must retain an owned reverse operation")
    if code == 11 and not value['terminal'] and phase in (0, 1, 3, 4):
        return value
    valid = ((code == 0 and phase == 2 and intent in (0, 1, 4)) or
             (code == 12 and phase == 5 and intent == 2) or
             (code == 13 and phase == 5 and intent == 3))
    if not value['terminal'] or not valid:
        raise ValueError('Installer receipt is neither a pending boot nor verified completion')
    return value


def run(directory, output, operation, shutdown_program, max_reboots=4):
    fixture.validate_guest_program(shutdown_program)
    if not 0 <= max_reboots <= 8:
        raise ValueError('Reboot bound must be between zero and eight')
    directory, output = Path(directory).resolve(), Path(output).resolve()
    source = json.loads((directory / 'run.json').read_text())
    if source['state'] not in ('prepared', 'ready'):
        raise ValueError('Requires a prepared or ready independent fixture')
    output.mkdir(parents=True, exist_ok=False)
    ledger = {'schema_version': 1, 'state': 'running', 'operation': operation,
              'initial_fixture': str(directory), 'shutdown_program': shutdown_program,
              'controller': fixture.controller_identity(),
              'automation_sha256': fixture.digest(__file__), 'poweroff_timeout_seconds': 120, 'steps': []}
    record = output / 'run.json'
    fixture.write_json(record, ledger)
    try:
        command = OPERATIONS[operation]
        for boot in range(max_reboots + 1):
            step = {'boot': boot, 'fixture': str(directory), 'command': command}
            ledger['steps'].append(step)
            fixture.write_json(record, ledger)
            source = json.loads((directory / 'run.json').read_text())
            if source['state'] == 'prepared':
                source = fixture.start(directory, timeout=120)
            probe_dir = output / f'operation-{boot}'
            result = fixture.hl.run_demo(source['serial'], command, probe_dir, timeout=480, probe=True)
            step['probe_result'] = str(probe_dir / 'run.json')
            if result['state'] != 'completed':
                step['failure'] = result.get('error', {'message': 'Probe did not complete'})
                raise RuntimeError(step['failure']['message'])
            step['receipt'] = receipt((probe_dir / 'engine-output.txt').read_text(), command)
            fixture.write_json(record, ledger)
            if step['receipt']['terminal']:
                ledger.update(state='completed', final_fixture=str(directory),
                              terminal=step['receipt'])
                break
            if boot == max_reboots:
                raise RuntimeError('Installer exceeded the recorded reboot bound')
            fixture.bootstrap_program(source['qmp'], shutdown_program)
            step['shutdown_requested'] = True
            fixture.write_json(record, ledger)
            deadline = time.monotonic() + 120
            while Path(source['qmp']).exists() and time.monotonic() < deadline:
                time.sleep(.2)
            if Path(source['qmp']).exists():
                raise TimeoutError('Guest did not power off; refusing to force a reset')
            # fixture.stop authenticates the enclosing Juke PID before cleanup.
            fixture.stop(directory)
            step['clean_poweroff_observed'] = True
            manifest = dict(source['source'])
            manifest.update(source_fixture=str(directory), disk=source['disk'], snapshot=None)
            path = output / f'cold-{boot + 1}.json'
            fixture.write_json(path, manifest)
            directory = output / f'cold-{boot + 1}'
            fixture.prepare(path, directory)
            step['stopped_disk_sha256'] = fixture.digest(source['disk'])
            fixture.write_json(record, ledger)
            command = 'sysresume'
    except BaseException as error:
        ledger.update(state='error', error=str(error), final_fixture=str(directory))
        raise
    finally:
        fixture.write_json(record, ledger)
    return ledger


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--operation', choices=OPERATIONS, default='install')
    parser.add_argument('--shutdown-program', required=True)
    parser.add_argument('--max-reboots', type=int, default=4)
    args = parser.parse_args()
    print(json.dumps(run(args.fixture, args.output, args.operation,
                         args.shutdown_program, args.max_reboots), indent=2))


if __name__ == '__main__':
    main()
