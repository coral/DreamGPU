#!/usr/bin/env python3
"""Prepare and resume a disposable, manifest-described serial benchmark fixture.

The source must be stopped. Its complete qcow2 chain is copied (using host
copy-on-write cloning where available), including internal RAM snapshots. No
source image, installed guest, frozen executable or source configuration changes.
Startup restores a pre-3D snapshot paused, disables net0, then waits for one
request-ID-tagged serial READY response. Cold setup may record one fixed login
and executable launch; workload commands are never retried implicitly.
Manifest paths are relative to the repository root unless absolute.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PureWindowsPath
import platform
import re
import secrets
import shlex
import signal
import socket
import subprocess
import sys
import time
import tomllib

from scripts.automation.native_runtime import checked_execution
from scripts.benchmarks.bench import qmp_execute, qmp_screenshot, probe_barcode

ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location('dg_halflife', (Path(__file__).resolve().parents[2] / 'scripts/benchmarks/halflife.py'))
hl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(hl)


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def controller_identity():
    """Record controller inputs separately from the frozen guest/native binaries."""
    paths = ('scripts/fixtures/fixture.py', 'scripts/benchmarks/bench.py',
             'scripts/benchmarks/halflife.py', 'scripts/automation/vm.py',
             'scripts/automation/native_runtime.py')
    return {path: digest(ROOT / path) for path in paths}


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def resolve(value):
    path = Path(value)
    return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()


def checked_artifact(value):
    path = resolve(value['path'])
    actual = digest(path)
    if actual != value['sha256']:
        raise ValueError(f'Artifact hash mismatch: {path}')
    return path


def checked_firmware(value):
    """Validate an explicit flat firmware directory without following member links."""
    if value is None:
        return None
    if not isinstance(value, dict) or not isinstance(value.get('path'), str) or not value['path']:
        raise ValueError('Firmware requires a directory path and nonempty file hash mapping')
    files = value.get('files')
    if not isinstance(files, dict) or not files:
        raise ValueError('Firmware requires a nonempty file hash mapping')
    directory = resolve(value['path'])
    if not directory.is_dir():
        raise ValueError(f'Firmware directory is missing: {directory}')
    for name, expected in files.items():
        if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.,-]*', name):
            raise ValueError('Firmware members must be safe basenames, not paths')
        if not isinstance(expected, str) or not re.fullmatch(r'[a-f0-9]{64}', expected):
            raise ValueError(f'Firmware member needs a lowercase SHA256: {name}')
        member = directory / name
        if member.is_symlink() or not member.is_file():
            raise ValueError(f'Firmware member must be a regular file without symlink escape: {member}')
        if digest(member) != expected:
            raise ValueError(f'Firmware hash mismatch: {member}')
    return {'path': str(directory), 'files': dict(files)}


def replace_setting(text, table, key, value):
    """Change one scalar without reserializing unrelated hardware settings."""
    pattern = rf'(?ms)(^\[{re.escape(table)}\]\s*\n)(.*?)(?=^\[|\Z)'
    matches = list(re.finditer(pattern, text))
    if len(matches) != 1:
        raise ValueError(f'Expected exactly one [{table}] table')
    match = matches[0]
    body = match[2]
    replacement = f'{key} = {json.dumps(value)}'
    line = rf'(?m)^{re.escape(key)}\s*=.*$'
    if re.search(line, body):
        body = re.sub(line, lambda _: replacement, body)
    else:
        body += replacement + '\n'
    return text[:match.start()] + match[1] + body + text[match.end():]


def copy_image(source, destination):
    # Snapshot metadata must survive: qemu-img convert would drop RAM snapshots.
    if platform.system() == 'Darwin':
        subprocess.run(['cp', '-c', str(source), str(destination)], check=True)
    else:
        subprocess.run(['cp', '--reflink=auto', '--sparse=always', str(source), str(destination)], check=True)


def prepare(manifest_path, output):
    source_bytes = Path(manifest_path).read_bytes()
    manifest = json.loads(source_bytes)
    if manifest.get('schema_version') != 1:
        raise ValueError('Unsupported fixture manifest version')
    machine = manifest['machine']
    if not re.fullmatch(r'[A-Za-z0-9_-]+', machine):
        raise ValueError('Invalid machine identifier')
    snapshot = manifest.get('snapshot')
    if snapshot is not None and not re.fullmatch(r'[A-Za-z0-9_-]+', snapshot):
        raise ValueError('Invalid pre-3D snapshot identifier')
    if manifest.get('readiness', 'serial') not in ('serial', 'desktop-probe'):
        raise ValueError('Unknown readiness protocol')
    login_delay = manifest.get('login_return_after_seconds')
    login_marker = manifest.get('login_debugcon_marker')
    if login_marker is not None and (snapshot is not None or login_delay is not None or
            not isinstance(login_marker, str) or not 0 < len(login_marker) <= 128 or
            '\n' in login_marker or '\r' in login_marker):
        raise ValueError('login_debugcon_marker requires a bounded single-line cold-boot event without a fixed delay')
    login_key = manifest.get('login_key', 'ret')
    if login_key not in ('ret', 'esc') or ('login_key' in manifest and login_delay is None and login_marker is None):
        raise ValueError('login_key must be ret/esc with a recorded cold-boot login trigger')
    if login_delay is not None and (snapshot is not None or not 0 < login_delay <= 60):
        raise ValueError('A single login Return is supported only for cold boot, after at most 60 seconds')
    guest_program = manifest.get('guest_start_program')
    if guest_program is not None:
        validate_guest_program(guest_program)
        if snapshot is not None or login_delay is None:
            raise ValueError('A one-shot guest bootstrap command requires cold boot and a recorded login delay')
    firmware = checked_firmware(manifest.get('firmware'))
    source = resolve(manifest['source_fixture'])
    if not (source / 'resources/fonts').is_dir():
        raise ValueError('Fixture source needs resources/fonts before preparing a Juke launch')
    native = checked_artifact(manifest['native'])
    native_execution = checked_execution(manifest['native'], resolve)
    app = checked_artifact(manifest['app'])
    launcher = checked_artifact(manifest['launcher'])
    image_tool = resolve(manifest['qemu_img'])
    disk = resolve(manifest['disk'])
    chain = json.loads(subprocess.check_output([str(image_tool), 'info', '--output=json', '--backing-chain', str(disk)]))
    if not chain or any(item['format'] != 'qcow2' for item in chain):
        raise ValueError('Only complete qcow2 snapshot chains are supported')
    if snapshot is not None and snapshot not in [item['name'] for item in chain[0].get('snapshots', [])]:
        raise ValueError(f'Pre-3D snapshot {snapshot!r} is absent')
    # Acquires QEMU's image lock; fail rather than clone a live source VM.
    # No repair flag: this never mutates the source image.
    check = subprocess.run([str(image_tool), 'check', '-q', str(disk)], capture_output=True, text=True)
    if check.returncode not in (0, 3):
        raise ValueError('Source image check failed: ' + check.stdout + check.stderr)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / 'manifest-input.json').write_bytes(source_bytes)
    report = {'schema_version': 1, 'state': 'preparing', 'source_manifest_sha256': hashlib.sha256(source_bytes).hexdigest(),
              'preparation_controller': controller_identity(),
              'source': manifest, 'fixture': str(output), 'machine': machine, 'snapshot': snapshot,
              'native': str(native), 'native_execution': native_execution, 'app': str(app), 'launcher': str(launcher), 'firmware': firmware,
              'created_utc': datetime.now(timezone.utc).isoformat(),
              'source_image_check': {'exit_code': check.returncode, 'output': check.stdout + check.stderr,
                                     'note': 'Exit 3 is leaked allocation only; source is never repaired.'}}
    write_json(output / 'run.json', report)
    try:
        copied = []
        source_images = []
        machine_dir = output / 'machines' / machine
        machine_dir.mkdir(parents=True)
        for index, item in enumerate(chain):
            original = Path(item['filename']).resolve()
            destination = machine_dir / ('disk.qcow2' if index == 0 else f'backing-{index}.qcow2')
            copy_image(original, destination)
            destination.chmod(destination.stat().st_mode | 0o600)
            source_images.append({'path': str(original), 'sha256': digest(destination)})
            copied.append(destination)
        # Point each private copy at its private parent; snapshots use the same
        # backing image header. Rebase changes only copied headers, never data.
        for index in range(len(copied) - 2, -1, -1):
            subprocess.run([str(image_tool), 'rebase', '-u', '-f', 'qcow2', '-F', 'qcow2',
                            '-b', str(copied[index + 1]), str(copied[index])], check=True)
        token = secrets.token_hex(6)
        serial = f'/tmp/dg-{token}-serial.sock'
        qtest = f'/tmp/dg-{token}-qtest.sock'
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        config = (source / 'config.toml').read_text()
        if '[control]' not in config:
            config += '\n[control]\n'
        config = replace_setting(config, 'control', 'listen', f'127.0.0.1:{port}')
        (output / 'config.toml').write_text(config)
        text = (source / 'machines' / machine / 'machine.toml').read_text()
        parsed = tomllib.loads(text)
        # Preserve media identity and hardware; resolve paths relative to the
        # original machine directory before moving the configuration.
        for media in parsed.get('qemu', {}).get('cdroms', []):
            if not Path(media['path']).is_absolute():
                raise ValueError('Fixture CD media paths must be absolute; record host-specific media in the source config')
            if not Path(media['path']).is_file():
                raise ValueError(f"Missing fixture media: {media['path']}")
        text = replace_setting(text, 'qemu', 'qemu_binary', str(output / 'qemu-system-i386'))
        text = replace_setting(text, 'qemu', 'disk', 'disk.qcow2')
        (machine_dir / 'machine.toml').write_text(text)
        if (source / 'resources').exists():
            (output / 'resources').symlink_to((source / 'resources').resolve(), target_is_directory=True)
        args = [str(native), '-S', *(['-L', firmware['path']] if firmware else []), *(['-loadvm', snapshot] if snapshot else []), '-D', str(output / 'native-trace.log'),
                '-chardev', f'socket,id=dgpuben,path={serial},server=on,wait=off',
                '-serial', 'chardev:dgpuben', '-qtest', f'unix:{qtest},server=on,wait=off']
        wrapper = output / 'qemu-system-i386'
        wrapper.write_text('#!/bin/sh\nexec ' + shlex.join(args) + ' "$@"\n')
        wrapper.chmod(0o755)
        report.update(state='prepared', serial=serial, qtest=qtest, ws=f'ws://127.0.0.1:{port}',
                      disk=str(copied[0]), copied_chain=[str(path) for path in copied], source_images=source_images,
                      machine_config_sha256=digest(machine_dir / 'machine.toml'))
    except BaseException as error:
        report.update(state='prepare_failed', error=str(error))
        raise
    finally:
        write_json(output / 'run.json', report)
    return report


def validate_guest_program(program):
    if (not isinstance(program, str) or len(program) > 128 or
            not PureWindowsPath(program).is_absolute() or
            PureWindowsPath(program).suffix.lower() != '.exe' or
            any(char in program for char in '\r\n\0"') or
            not all(char.isascii() and (char.isalnum() or char in ' :\\._-') for char in program)):
        raise ValueError('Guest bootstrap program must be a bounded absolute Windows .exe path without arguments')
    return program


def bootstrap_program(endpoint, program):
    """One recorded Run command for initial cold setup, without image decisions."""
    program = validate_guest_program(program)
    spec = importlib.util.spec_from_file_location('dg_vm', (Path(__file__).resolve().parents[2] / 'scripts/automation/vm.py'))
    vm = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(vm)
    vm.key(endpoint, 'meta_l+r')
    # Cold NT startup can take over a second to finish creating the Run edit.
    # Sending sooner dropped the opening quote and drive prefix, turning
    # C:\DGPUBEN.EXE into \DGPUBEN.EXE and making serial discovery time out.
    time.sleep(1.5)
    vm.type_text(endpoint, '"' + program + '"')
    vm.key(endpoint, 'ret')


def decode_instance(value):
    if not isinstance(value,str) or not re.fullmatch(r'[0-9a-f]{64}-[0-9a-f]{32}',value):
        raise hl.ProtocolError('Malformed runner process identity')
    identity,instance=value.split('-')
    return {'identity':identity,'instance':instance}


def inspect_runner(endpoint,timeout):
    """Read one idle runner identity before launching an installer once."""
    request=secrets.token_hex(16)
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
        connection.settimeout(timeout);connection.connect(endpoint)
        serial=hl.Serial(connection);deadline=time.monotonic()+timeout
        serial.send(f'INSPECT {request}',timeout)
        try:
            kind,value=serial.receive(request,deadline)
        except hl.GuestError as error:
            if error.code!='bad-command':raise
            serial.send(f'IDENTIFY {request}',max(0.001,deadline-time.monotonic()))
            kind,value=serial.receive(request,deadline)
            if kind!='IDENTITY' or not re.fullmatch(r'[0-9a-f]{64}',value or ''):
                raise hl.ProtocolError('Legacy runner build identity unavailable')
            return {'identity':value,'instance':None}
        if kind!='INSTANCE':raise hl.ProtocolError('Expected runner process identity')
        return decode_instance(value)


def launch_installer(endpoint, timeout):
    """One fixed request. Never retry an installation after a lost response."""
    request = secrets.token_hex(16)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(timeout)
        connection.connect(endpoint)
        serial = hl.Serial(connection)
        serial.send(f'INSTALL {request}', timeout)
        kind, _ = serial.receive(request, time.monotonic() + timeout)
        if kind != 'INSTALLING':
            raise hl.ProtocolError('Expected fixed installer launch acknowledgement')
    return request


PRELAUNCH_INSTALL_ERRORS = {'install-media-unavailable', 'install-ambiguous-media', 'install-launch-failed'}


def install_package(fixture, package, timeout):
    path = fixture/'run.json'
    report = json.loads(path.read_text())
    identity = json.loads(package.read_text())['runner_identity']
    if not re.fullmatch(r'[0-9a-f]{64}', identity):
        raise ValueError('Invalid replacement runner build identity')
    previous = report.get('runner_install_attempt')
    # Migrate an already-recorded explicit prelaunch rejection as well. This is
    # only reached by another deliberate install invocation, never an auto retry.
    if (report.get('runner_before_install') and previous and previous.get('state') == 'failed' and
            previous.get('error') in {'guest error: '+code for code in PRELAUNCH_INSTALL_ERRORS}):
        previous.update(state='rejected_before_launch', before=report.pop('runner_before_install'))
        write_json(path, report)
    if report.get('runner_before_install'):
        raise ValueError('An installer handoff is already pending; resolve it with verify-runner')
    if previous:
        report.setdefault('runner_install_history', []).append(previous)
    before = inspect_runner(report['serial'], min(timeout, 10))
    if before['instance'] is None:
        raise ValueError('INSTALL requires an instance-aware runner; bootstrap the shared runner once')
    report['runner_before_install'] = before
    report['runner_install_attempt'] = {'state': 'requested', 'expected_identity': identity,
        'requested_utc': datetime.now(timezone.utc).isoformat(), 'command': 'INSTALL'}
    write_json(path, report)
    try:
        request = launch_installer(report['serial'], min(timeout, 30))
        report['runner_install_attempt'].update(state='installer_started', request_id=request)
        write_json(path, report)
        report['runner_identity_request_id'] = ready(report['serial'], timeout, identity, before['instance'])
        report['runner_identity'] = identity
        report['runner_last_install_from'] = before
        del report['runner_before_install']
        report['runner_install_attempt']['state'] = 'replacement_ready'
    except BaseException as error:
        report['runner_install_attempt'].update(state='failed', error=str(error))
        if isinstance(error, hl.GuestError) and error.code in PRELAUNCH_INSTALL_ERRORS:
            report['runner_install_attempt'].update(state='rejected_before_launch', error_code=error.code,
                before=report.pop('runner_before_install'))
        raise
    finally:
        write_json(path, report)
    return report


def ready(endpoint, timeout, expected_identity=None, previous_instance=None):
    if expected_identity is not None and (len(expected_identity)!=64 or any(c not in "0123456789abcdef" for c in expected_identity)):
        raise ValueError("Expected runner identity must be a lowercase SHA256")
    if previous_instance is not None and (not expected_identity or not re.fullmatch(r'[0-9a-f]{32}',previous_instance)):
        raise ValueError('Replacement readiness needs a build hash and prior process identity')
    request = secrets.token_hex(16)
    last_response = 'none'
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(timeout)
        connection.connect(endpoint)
        serial = hl.Serial(connection)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            # Before the guest opens COM1, a cold boot can discard an early
            # UART byte stream. Discovery repeats PING only, never RUN/PROBE.
            remaining = deadline - time.monotonic()
            poll_started=time.monotonic()
            command='INSPECT' if previous_instance else 'IDENTIFY' if expected_identity else 'PING'
            serial.send(f'{command} {request}', remaining)
            try:
                kind,identity = serial.receive(request, min(deadline, time.monotonic() + 1))
            except TimeoutError:
                continue
            except hl.GuestError as error:
                if not expected_identity or error.code!='bad-command':raise
                kind,identity='OLD_RUNNER',None
            last_response = f'{kind} {str(identity)[:160]}'
            if expected_identity:
                if previous_instance and kind=='INSTANCE':
                    inspected=decode_instance(identity)
                    if inspected['identity']==expected_identity and inspected['instance']!=previous_instance:return request
                elif not previous_instance and kind=='IDENTITY' and identity==expected_identity:return request
                time.sleep(max(0,min(deadline,poll_started+1)-time.monotonic()))
                continue
            if kind != 'READY':
                raise hl.ProtocolError('Expected runner READY')
            return request
    raise TimeoutError(f'Serial runner discovery deadline expired; expected '
                       f'{expected_identity or "READY"}; last response: {last_response}')


def resume_guest(endpoint, deadline=None):
    # A Unix socket path exists after bind(), before listen()/QMP readiness.
    # Retry only this read-only handshake, never set_link or cont: a connection
    # failure after a mutation must retain its original ambiguous outcome.
    while True:
        try:
            status = qmp_execute(endpoint, [('query-status', {})])[0]
            break
        except (ConnectionRefusedError, FileNotFoundError, TimeoutError):
            remaining = 0 if deadline is None else deadline - time.monotonic()
            if remaining <= 0:
                raise
            time.sleep(min(.05, remaining))
    if status['status'] not in ('prelaunch', 'paused'):
        raise RuntimeError(f'Guest was not paused before network disable: {status}')
    qmp_execute(endpoint, [('set_link', {'name': 'net0', 'up': False}), ('cont', {})])
    return status


def failure_diagnostics(output, report):
    """One bounded visual diagnostic on failure; never replace the real error."""
    if not report.get('qmp'):
        return
    existing = output / 'not-ready.png'
    if existing.exists():
        report['failure_screenshot'] = str(existing)
        return
    try:
        path = output / 'startup-failure.png'
        path.write_bytes(qmp_screenshot(report['qmp']))
        report['failure_screenshot'] = str(path)
    except Exception as error:
        report['failure_screenshot_error'] = str(error)


def start(output, timeout=60):
    output = Path(output).resolve()
    report = json.loads((output / 'run.json').read_text())
    if report['state'] != 'prepared':
        raise ValueError('Start requires a newly prepared fixture; never silently restore over a previous run')
    for artifact in ('native', 'app', 'launcher'):
        checked_artifact(report['source'][artifact])
    if checked_execution(report['source']['native'], resolve) != report.get('native_execution'):
        raise ValueError('Native execution identity changed after fixture preparation')
    firmware = checked_firmware(report['source'].get('firmware'))
    if firmware != report.get('firmware'):
        raise ValueError('Firmware directory identity changed after fixture preparation')
    if Path(report['serial']).exists():
        raise ValueError('Serial socket already exists; another fixture may own it')
    # A controller fix may legitimately differ from the one which prepared the
    # disk. Preserve both identities instead of silently attributing its behavior
    # to the same guest/native build or overwriting the preparation evidence.
    report['startup_controller'] = controller_identity()
    began = time.monotonic()
    with (output / 'juke.log').open('xb') as log:
        process = subprocess.Popen([report['launcher'], '--config-dir', str(output), '-c', '-m', report['machine']],
                                   cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    report.update(state='starting', pid=process.pid, network_link_before_cont=None)
    write_json(output / 'run.json', report)
    try:
        deadline = began + timeout
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f'Juke exited with status {process.returncode}; see juke.log')
            text = (output / 'juke.log').read_text(errors='replace')
            matches = re.findall(r'-qmp unix:([^,\s]+)', text)
            if matches and Path(matches[0]).exists():
                report['qmp'] = matches[0]
                report['prelaunch'] = resume_guest(report['qmp'], deadline=deadline)
                report['network_link_before_cont'] = False
                break
            time.sleep(.1)
        else:
            raise TimeoutError('QMP socket did not become available; see juke.log')
        login_delay = report['source'].get('login_return_after_seconds')
        login_marker = report['source'].get('login_debugcon_marker')
        if login_marker is not None:
            while time.monotonic() < deadline:
                debug = output / 'debugcon.log'
                if debug.exists() and login_marker in debug.read_text(errors='replace'):
                    key = report['source'].get('login_key', 'ret')
                    qmp_execute(report['qmp'], [('send-key', {'keys': [{'type': 'qcode', 'data': key}], 'hold-time': 50})])
                    report['login_trigger'] = {'marker': login_marker, 'key': key, 'invocations': 1}
                    break
                if process.poll() is not None:
                    raise RuntimeError('Juke exited before login event')
                time.sleep(.1)
            else:
                raise TimeoutError('Recorded cold-boot login event did not arrive')
        if login_delay is not None:
            if time.monotonic() + login_delay >= deadline:
                raise TimeoutError('Startup deadline is too short for the recorded login delay')
            time.sleep(login_delay)
            login_key = report['source'].get('login_key', 'ret')
            qmp_execute(report['qmp'], [('send-key', {'keys': [{'type': 'qcode', 'data': login_key}], 'hold-time': 50})])
        guest_program = report['source'].get('guest_start_program')
        if guest_program is not None:
            if time.monotonic() + 10 >= deadline:
                raise TimeoutError('Startup deadline is too short for the one-shot guest bootstrap')
            time.sleep(3)  # Let the known blank-password login finish starting Explorer.
            bootstrap_program(report['qmp'], guest_program)
            report['guest_bootstrap'] = {'program': guest_program, 'invocations': 1}
        if report['source'].get('readiness', 'serial') == 'desktop-probe':
            while time.monotonic() < deadline:
                png = qmp_screenshot(report['qmp'])
                barcode = probe_barcode(png)
                if barcode is not None and barcode[1] == 0:
                    (output / 'ready.png').write_bytes(png)
                    report['probe_ready'] = {'sequence': barcode[0], 'workload': barcode[1]}
                    break
                time.sleep(.1)
            else:
                (output / 'not-ready.png').write_bytes(png)
                raise TimeoutError('Startup probe did not report input-only acknowledgement')
        else:
            report['ready_request_id'] = ready(report['serial'], max(.001, deadline - time.monotonic()),
                                               expected_identity=report['source'].get('runner_identity'))
        report.update(state='ready', startup_seconds=time.monotonic() - began)
    except BaseException as error:
        report.update(state='start_failed', error=str(error))
        failure_diagnostics(output, report)
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except OSError as cleanup_error:
                report['cleanup_error'] = str(cleanup_error)
        raise
    finally:
        write_json(output / 'run.json', report)
    return report


def stop(output):
    output = Path(output).resolve()
    report = json.loads((output / 'run.json').read_text())
    if report['state'] not in ('ready', 'paused', 'stopped'):
        raise ValueError('Stop requires a fixture recorded as ready, paused or stopped')
    pid = report['pid']
    found = subprocess.run(['ps', '-p', str(pid), '-o', 'args='], text=True, capture_output=True)
    if found.returncode and report['state'] == 'stopped':
        return report
    command = found.stdout.strip()
    if '--config-dir ' + str(output) not in command or not command:
        raise ValueError('Recorded PID no longer owns this fixture; refusing to signal it')
    # A clean Windows poweroff exits QEMU but can leave the enclosing Juke
    # window running. Its verified process group still needs explicit cleanup.
    if Path(report['qmp']).exists():
        qmp_execute(report['qmp'], [('quit', {})])
    os.killpg(pid, signal.SIGTERM)
    report['state'] = 'stopped'
    report['app_cleanup'] = 'Verified owned process group stopped, including after guest poweroff'
    write_json(output / 'run.json', report)
    return report


def record_package(output, media, members):
    """Record host package identity; this does not attest guest installation."""
    output, media, members = Path(output).resolve(), Path(media).resolve(), Path(members).resolve()
    report = json.loads((output / 'run.json').read_text())
    if report['state'] != 'ready':
        raise ValueError('Package provenance requires a ready owned fixture')
    files = sorted(members.iterdir())
    if not media.is_file() or not files or any(not path.is_file() or path.is_symlink() for path in files):
        raise ValueError('Package requires a media file and a flat directory of regular member files')
    record = {
        'recorded_utc': datetime.now(timezone.utc).isoformat(),
        'media': str(media), 'media_sha256': digest(media),
        'member_directory': str(members),
        'files': {path.name: digest(path) for path in files},
        'verification': 'host inputs only; no ISO-content comparison or guest filesystem readback',
    }
    report.setdefault('package_inputs', []).append(record)
    write_json(output / 'run.json', report)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prepare_command = commands.add_parser('prepare', help='Copy a stopped fixture and its snapshot chain')
    prepare_command.add_argument('manifest', type=Path)
    prepare_command.add_argument('--output', type=Path, required=True)
    start_command = commands.add_parser('start', help='Restore paused, disconnect NIC, wait for serial READY, detach')
    start_command.add_argument('fixture', type=Path)
    start_command.add_argument('--timeout', type=float, default=60)
    identity_command = commands.add_parser('verify-runner', help='Wait for the exact source-built replacement runner, never launch a probe')
    identity_command.add_argument('fixture', type=Path)
    identity_command.add_argument('--package', type=Path, required=True, help='Runner package manifest.json')
    identity_command.add_argument('--timeout', type=float, default=120)
    install_command=commands.add_parser('begin-install',help='Record the old runner process before launching one fixed installer')
    install_command.add_argument('fixture',type=Path)
    install_command.add_argument('--timeout',type=float,default=10)
    fixed_install = commands.add_parser('install', help='Launch one fixed optical installer over serial and require a new runner instance')
    fixed_install.add_argument('fixture', type=Path)
    fixed_install.add_argument('--package', type=Path, required=True, help='Expected replacement runner manifest.json')
    fixed_install.add_argument('--timeout', type=float, default=120)
    commands.add_parser('stop', help='Quit only this disposable fixture').add_argument('fixture', type=Path)
    package_command = commands.add_parser('record-package', help='Record media/member identity without claiming guest installation')
    package_command.add_argument('fixture', type=Path)
    package_command.add_argument('--media', type=Path, required=True)
    package_command.add_argument('--members', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'prepare':
            report = prepare(args.manifest, args.output)
        elif args.command == 'start':
            if not 0 < args.timeout <= 120:
                raise ValueError('Startup timeout must be in (0, 120] seconds')
            report = start(args.fixture, args.timeout)
        elif args.command == 'record-package':
            report = record_package(args.fixture, args.media, args.members)
        elif args.command == 'install':
            if not 0 < args.timeout <= 120:
                raise ValueError('Install timeout must be in (0,120] seconds')
            report = install_package(args.fixture, args.package, args.timeout)
        elif args.command == 'begin-install':
            if not 0<args.timeout<=30:raise ValueError('Inspection timeout must be in (0,30] seconds')
            path=args.fixture/'run.json';report=json.loads(path.read_text())
            if report.get('runner_before_install'):raise ValueError('An installer handoff is already pending')
            report['runner_before_install']=inspect_runner(report['serial'],args.timeout)
            write_json(path,report)
        elif args.command == 'verify-runner':
            if not 0 < args.timeout <= 120:raise ValueError('Identity timeout must be in (0, 120] seconds')
            path=args.fixture/'run.json';report=json.loads(path.read_text())
            identity=json.loads(args.package.read_text())['runner_identity']
            before=report.get('runner_before_install',{})
            if before and before.get('instance') is None and before.get('identity')==identity:
                raise ValueError('Old runner cannot attest same-build replacement; upgrade to the instance-aware runner first')
            report['runner_identity_request_id']=ready(report['serial'],args.timeout,identity,before.get('instance'))
            report['runner_identity']=identity
            if before:
                report['runner_last_install_from']=before
                del report['runner_before_install']
            write_json(path,report)
        else:
            report = stop(args.fixture)
        print(json.dumps(report, indent=2))
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, hl.ProtocolError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
