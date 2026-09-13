#!/usr/bin/env python3
"""One serial-driven retained-window, VM switch, CRT and forced-exit check."""

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
import sys
import time
import tomllib
import uuid

from scripts.benchmarks.bench import WebSocket, qmp_execute

spec = importlib.util.spec_from_file_location('dg_game', (Path(__file__).resolve().parents[2] / 'scripts/benchmarks/game.py'))
game = importlib.util.module_from_spec(spec)
spec.loader.exec_module(game)
spec = importlib.util.spec_from_file_location('dg_pixels', (Path(__file__).resolve().parents[2] / 'scripts/diagnostics/cursor-check.py'))
pixels = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pixels)


def retained_pixels(png):
    checked = mismatched = 0
    for y, row, channels in pixels.png_rows(png):
        if y >= 112:
            break
        if y < 96:
            continue
        if len(row) < 112 * channels:
            raise ValueError('Prepared screenshot is narrower than the retained window')
        for x in range(96, 112):
            checked += 1
            mismatched += tuple(row[x * channels:x * channels + 3]) != (255, 0, 0)
    return {'checked': checked, 'mismatched': mismatched,
            'passed': checked == 256 and mismatched == 0,
            'scope': 'Exact red retained GPU window in prepared Juke canvas after VM roundtrip; before CRT'}


def states(socket):
    return {vm['id']: vm for vm in socket.command('get_vm_state', 'vm_detailed_state')['vms']}


def preflight(socket, primary, peer):
    if primary == peer:
        raise ValueError('Lifecycle requires two independent VMs')
    current = states(socket)
    if current.get(primary, {}).get('state') != 'Running':
        raise ValueError('Primary must already be running')
    if current.get(peer, {}).get('state') not in ('Running', 'Paused'):
        raise ValueError('Peer must already be booted with its NIC down')
    if socket.command('get_active', 'active_vm').get('vm_id') != primary:
        raise ValueError('Primary must already be active')


def switch(socket, target, expected, events, timeout=3):
    started = time.monotonic()
    if target is None:
        socket.command('switch_to_landing_page', 'ok')
    else:
        socket.command('switch_vm', 'ok', vm_id=target)
    while time.monotonic() - started < timeout:
        active = socket.command('get_active', 'active_vm').get('vm_id')
        current = states(socket)
        if active == target and all(current.get(vm, {}).get('state') == value for vm, value in expected.items()):
            events.append({'target': target, 'seconds': time.monotonic() - started,
                           'host_monotonic_seconds': time.monotonic(),
                           'states': {vm: item['state'] for vm, item in current.items()}})
            return
        time.sleep(.02)
    raise RuntimeError(f'VM transition to {target!r} did not reach {expected!r}')


def transitions(socket, primary, peer, events):
    try:
        switch(socket, peer, {primary: 'Paused', peer: 'Running'}, events)
        switch(socket, primary, {primary: 'Running', peer: 'Paused'}, events)
        switch(socket, None, {}, events)
        switch(socket, primary, {primary: 'Running', peer: 'Paused'}, events)
    except Exception:
        # Restore the owned primary so its bounded guest cleanup can complete.
        socket.command('switch_vm', 'ok', vm_id=primary)
        raise


def resource_checkpoint(endpoint):
    """A unique private checkpoint must pass QEMU's live-GL migration blocker."""
    name = 'dg_lifecycle_' + uuid.uuid4().hex
    failure = None
    try:
        result = qmp_execute(endpoint, [('human-monitor-command', {'command-line': 'savevm ' + name})])[0]
        if result.strip():
            raise RuntimeError('Post-exit checkpoint failed: ' + result.strip())
    except Exception as error:
        failure = error
    finally:
        # A failed reply may follow partial creation. Only delete our unique name.
        try:
            result = qmp_execute(endpoint, [('human-monitor-command', {'command-line': 'delvm ' + name})])[0]
            if result.strip():
                raise RuntimeError('Private checkpoint cleanup failed: ' + result.strip())
        except Exception as error:
            if failure is None:
                failure = error
            else:
                failure = RuntimeError(f'{failure}; cleanup: {error}')
    if failure is not None:
        raise failure
    return {'name': name, 'saved_and_deleted': True, 'scope': 'Native GL migration blocker released after forced process exit'}


def attempt(fixture, peer, output):
    fixture = Path(fixture).resolve()
    output = Path(output).resolve()
    state = json.loads((fixture / 'run.json').read_text())
    primary = state['machine']
    if not re.fullmatch(r'[A-Za-z0-9_-]+', peer):
        raise ValueError('Invalid peer identifier')
    meta = state.get('lifecycle_peer', {})
    if meta.get('id') != peer or meta.get('nic_down_before_cont') is not True:
        raise ValueError('Fixture lacks prewarmed isolated peer evidence')
    for vm, shader in ((primary, 'crt-lottes'), (peer, 'none')):
        config = tomllib.loads((fixture / 'machines' / vm / 'machine.toml').read_text())
        if config.get('render', {}).get('shader') != shader:
            raise ValueError(f'{vm} must use {shader} for this fixed lifecycle check')
    socket = WebSocket(state['ws'], timeout=3)
    try:
        preflight(socket, primary, peer)
    finally:
        socket.close()
    log = fixture / 'juke.log'
    offset = log.stat().st_size
    events = []

    def phase(kind, socket, state):
        if kind == 'MEASURING':
            previous = socket.sock.gettimeout()
            socket.sock.settimeout(3)
            try:
                transitions(socket, primary, peer, events)
            finally:
                socket.sock.settimeout(previous)

    report = game.attempt(fixture, 'lifecycle', output, phase_hook=phase)
    evidence = {'schema': 1, 'transitions': events, 'errors': [],
                'scope': 'Guest exact retained-window pixels, pause/resume, landing, CRT reload and forced process exit. Prepared screenshot excludes CRT; no FPS or final CRT pixel claim.'}
    with log.open('rb') as stream:
        stream.seek(offset)
        raw = stream.read(2 * 1024 * 1024 + 1)
        if len(raw) > 2 * 1024 * 1024:
            raise ValueError('Lifecycle log exceeds bounded 2 MiB collection')
        text = raw.decode('utf-8', errors='replace')
    (output / 'lifecycle-app.log').write_text(text)
    evidence['crt_reload_count'] = text.count('CRT filter chain loaded successfully')
    evidence['crt_disabled'] = 'CRT shader disabled' in text
    evidence['render_errors'] = [line for line in text.splitlines() if re.search(r'(?i)(\bERROR\b|failed to load shader|CRT shader failed|validation error)', line)]
    try:
        evidence['retained_canvas'] = retained_pixels((output / 'measured-rendered.png').read_bytes())
    except (OSError, ValueError) as error:
        evidence['errors'].append('Retained canvas: ' + str(error))
    if report['verdict']['passed']:
        try:
            evidence['resource_checkpoint'] = resource_checkpoint(state['qmp'])
        except Exception as error:
            evidence['errors'].append(str(error))
    evidence['passed'] = (report['verdict']['passed'] and len(events) == 4 and
                          evidence['crt_reload_count'] >= 1 and evidence['crt_disabled'] and
                          evidence.get('retained_canvas', {}).get('passed') is True and
                          not evidence['render_errors'] and not evidence['errors'])
    game.write(output / 'lifecycle.json', evidence)
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--peer', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        report = attempt(args.fixture, args.peer, args.output)
    except (OSError, ValueError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(('PASS' if report['passed'] else 'FAIL') + ' lifecycle (' + str(args.output / 'lifecycle.json') + ')')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
