#!/usr/bin/env python3
"""Stage an accepted stopped guest and adopt its shared profile without losing local presentation settings."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import secrets
import shlex
import shutil
import socket
import subprocess
import tempfile
import tomllib

PROFILES = ('retro-win98', 'retro-win2000', 'retro-winxp')
ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    with Path(path).open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def resolve(path):
    path = Path(path)
    return (ROOT/path).resolve() if not path.is_absolute() else path.resolve()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')


def checked_file(path, expected):
    if path.is_symlink() or not path.is_file() or digest(path) != expected:
        raise ValueError(f'File identity changed: {path}')


def process_alive(pid):
    if not isinstance(pid, int) or pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def stopped_fixture(fixture):
    report = json.loads((fixture/'run.json').read_text())
    if report.get('state') != 'stopped':
        raise ValueError('Accepted source fixture must be stopped')
    for name in ('pid', 'app_pid', 'qemu_pid'):
        if process_alive(report.get(name)):
            raise ValueError(f'Source fixture process is still alive: {name}')
    if report.get('qmp'):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(.2)
            try:
                connection.connect(report['qmp'])
            except (FileNotFoundError, ConnectionRefusedError):
                pass
            else:
                raise ValueError('Source fixture QMP is still reachable')
    return report


def machine_stopped(machine):
    """Refuse a production Juke or QEMU using this directory; do not stop it."""
    data_root = machine.parent.parent.resolve()
    processes = subprocess.check_output(['ps', '-axo', 'pid=,command='], text=True)
    for line in processes.splitlines():
        words = line.strip().split(None, 1)
        if len(words) != 2 or int(words[0]) == os.getpid():
            continue
        try:
            args = shlex.split(words[1])
        except ValueError:
            continue
        if not args:
            continue
        executable = Path(args[0]).name
        if executable.startswith('qemu-system-') and any(str(machine) in word for word in args):
            raise ValueError('Destination machine has an active QEMU process')
        if executable == 'juke':
            explicit = next((args[i+1] for i, value in enumerate(args[:-1]) if value == '--config-dir'), None)
            explicit = next((value.split('=', 1)[1] for value in args if value.startswith('--config-dir=')), explicit)
            # Default/bundle paths and relative options depend on that process's
            # working directory. Do not guess them from this command's cwd.
            if explicit is None or not Path(explicit).is_absolute() or Path(explicit).resolve() == data_root:
                raise ValueError('Destination data directory has an active Juke process')


def profile_config(original, profile, disk, cdroms):
    """Keep non-backend text byte-for-byte; archive old backend tables as comments."""
    if profile not in PROFILES:
        raise ValueError('Unsupported shared Windows profile')
    parsed = tomllib.loads(original)
    if not isinstance(parsed.get('vm'), dict):
        raise ValueError('Expected a [vm] configuration table')
    sections = re.split(r'(?m)(?=^\s*\[(?!#))', original)
    kept, archived = [], []
    for section in sections:
        match = re.match(r'\s*\[\[?\s*([^]\n]+?)\s*\]\]?', section)
        table = match[1].strip().split('.')[0].strip('"\'') if match else None
        if table in ('qemu', '86box'):
            archived.append(section)
        else:
            if table == 'vm':
                pattern = r'(?m)^(\s*backend\s*=\s*)("[^"\n]*"|\'[^\'\n]*\')([^\n]*)$'
                if len(re.findall(pattern, section)) != 1:
                    raise ValueError('Expected one simple vm.backend string')
                section = re.sub(pattern, lambda m: m[1]+'"qemu"'+m[3], section)
            kept.append(section)
    candidate = ''.join(kept)
    if not candidate.endswith('\n'):
        candidate += '\n'
    candidate += '\n'
    if archived:
        candidate += '# Previous backend configuration retained for reference:\n'
        candidate += ''.join('# '+line if line.strip() else '#\n' for block in archived for line in block.splitlines(keepends=True))
        if not candidate.endswith('\n'):
            candidate += '\n'
        candidate += '\n'
    candidate += '[qemu]\nprofile = '+json.dumps(profile)+'\nprofile_version = 1\ndisk = '+json.dumps(disk)+'\n'
    for media in cdroms:
        candidate += '\n[[qemu.cdroms]]\npath = '+json.dumps(media['path'])+'\ninterface = "ide"\nindex = '+str(media['index'])+'\n'
    result = tomllib.loads(candidate)
    if result['vm']['backend'] != 'qemu' or set(result['qemu'])-{'profile', 'profile_version', 'disk', 'cdroms'}:
        raise ValueError('Could not remove legacy backend settings safely')
    for key, value in parsed.items():
        if key not in ('vm', 'qemu', '86box') and result.get(key) != value:
            raise ValueError(f'Presentation settings changed: {key}')
    return candidate


def verify_package(directory, expected):
    manifest = json.loads((directory/'package.json').read_text())
    files = manifest['files']
    if manifest.get('schema') == 1:
        encoded = json.dumps(files, sort_keys=True).encode()
    elif (manifest.get('schema') == 2 and
          manifest.get('identity_scheme') == 'sha256-json-utf8-sorted-compact'):
        encoded = json.dumps(files, sort_keys=True, ensure_ascii=False,
                             separators=(',', ':')).encode('utf-8')
    else:
        raise ValueError('Unknown graphics package identity scheme')
    identity = hashlib.sha256(encoded).hexdigest()
    if identity != expected or manifest.get('identity') != expected:
        raise ValueError('Shared graphics package identity mismatch')
    if not isinstance(files, dict) or not files:
        raise ValueError('Missing graphics package inventory')
    for name, checksum in files.items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or str(relative) != name or '\\' in name:
            raise ValueError('Invalid package member path')
        checked_file(directory/name, checksum)
    actual = set()
    for path in directory.rglob('*'):
        if path.is_symlink():
            raise ValueError('Symlink in graphics package')
        if path.is_file():
            actual.add(str(path.relative_to(directory)))
    metadata = {'package.json'}
    if manifest.get('schema') == 2 and 'manifest.json' in actual:
        # Cargo retains its source/build receipt beside the payload manifest.
        # Its inventory must agree with the authenticated payload inventory;
        # runner_manifest.json is added by Cargo after that receipt is written.
        build = json.loads((directory/'manifest.json').read_text())
        expected_files = {name: checksum for name, checksum in files.items()
                          if name != 'runner_manifest.json'}
        if (build.get('schema') != 1 or build.get('files') != expected_files or
                build.get('build_identity') != manifest.get('runner_identity')):
            raise ValueError('Build receipt differs from graphics package inventory')
        metadata.add('manifest.json')
    if actual != set(files)|metadata:
        raise ValueError('Unrecorded graphics package member')
    return manifest


def copy_package(source, resources, identity):
    verify_package(source, identity)
    target = resources/identity
    resources.mkdir(parents=True, exist_ok=True)
    if target.exists():
        verify_package(target, identity)
        return target
    with tempfile.TemporaryDirectory(prefix='.adopt-package-', dir=resources) as temporary:
        staged = Path(temporary)/'package'
        shutil.copytree(source, staged)
        verify_package(staged, identity)
        for path in staged.rglob('*'):
            path.chmod(0o555 if path.is_dir() else 0o444)
        # rename refuses an existing nonempty package; never replace one.
        staged.rename(target)
        target.chmod(0o555)
    return target


@contextmanager
def machine_lock(machine):
    path = machine/'.dreamgpu-adopt.lock'
    with path.open('a') as lock:
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError('Another adoption owns this machine') from error
        yield


def prepare(args):
    machine, fixture = args.machine.resolve(), args.fixture.resolve()
    config = machine/'machine.toml'
    if config.is_symlink():
        raise ValueError('Destination config must be a regular file')
    original = config.read_bytes()
    source_bytes = args.source_manifest.read_bytes()
    source = json.loads(source_bytes)
    if source['profile'] != args.profile or not source.get('shutdown') or not source.get('evidence'):
        raise ValueError('Source manifest must identify the profile, recorded shutdown and acceptance evidence')
    checked_file(fixture/'run.json', source['fixture_sha256'])
    report = stopped_fixture(fixture)
    disk = resolve(report['disk'])
    checked_file(disk, source['disk_sha256'])
    evidence = []
    for item in source['evidence']:
        path = resolve(item['path']); checked_file(path, item['sha256'])
        evidence.append({'path': str(path), 'sha256': item['sha256']})
    media = []
    indices = {0}
    for item in source.get('cdroms', []):
        path = resolve(item['path']); checked_file(path, item['sha256'])
        index = item['index']
        if type(index) is not int or index not in (1, 2, 3) or index in indices:
            raise ValueError('Profile CD-ROMs need distinct IDE indices1..3 (disk owns0)')
        indices.add(index); media.append({'path': str(path), 'index': index})
    image_tool = args.qemu_img.resolve()
    verify_package(args.package, source['package_identity'])
    with machine_lock(machine):
        machine_stopped(machine)
        checked_file(config, hashlib.sha256(original).hexdigest())
        name = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+secrets.token_hex(4)
        stage = machine/'.dreamgpu-adoptions'/name
        stage.mkdir(parents=True, exist_ok=False)
        (stage/'original-machine.toml').write_bytes(original)
        (stage/'source-manifest.json').write_bytes(source_bytes)
        state = {'schema': 1, 'state': 'preparing', 'machine': str(machine), 'fixture': str(fixture),
                 'profile': args.profile, 'source_disk': str(disk), 'source_disk_sha256': source['disk_sha256'],
                 'original_config_sha256': hashlib.sha256(original).hexdigest(), 'evidence': evidence,
                 'shutdown': source['shutdown'], 'source_manifest_sha256': hashlib.sha256(source_bytes).hexdigest(),
                 'qemu_img': str(image_tool), 'qemu_img_sha256': digest(image_tool)}
        write_json(stage/'provenance.json', state)
        try:
            # Default QEMU read locking; no -U/force-share and no repair of source.
            subprocess.run([str(image_tool), 'convert', '-f', 'qcow2', '-O', 'qcow2', str(disk), str(stage/'disk.qcow2')], check=True)
            info = json.loads(subprocess.check_output([str(image_tool), 'info', '--output=json', str(stage/'disk.qcow2')]))
            if info['format'] != 'qcow2' or info.get('backing-filename') or info.get('snapshots'):
                raise ValueError('Adopted disk must be standalone qcow2 without RAM snapshots')
            subprocess.run([str(image_tool), 'check', '-q', str(stage/'disk.qcow2')], check=True)
            checked_file(disk, source['disk_sha256'])
            stopped_fixture(fixture)
            resources = args.resources.resolve() if args.resources else machine.parent.parent/'resources'/'retro'
            package = copy_package(args.package, resources, source['package_identity'])
            candidate = profile_config(original.decode(), args.profile, str((stage/'disk.qcow2').relative_to(machine)), media)
            (stage/'candidate-machine.toml').write_text(candidate)
            state.update(state='prepared', disk=str(stage/'disk.qcow2'), disk_sha256=digest(stage/'disk.qcow2'),
                         package=str(package), package_identity=source['package_identity'],
                         candidate_config_sha256=digest(stage/'candidate-machine.toml'), source_modified=False)
        except BaseException as error:
            state.update(state='prepare_failed', error=str(error)); raise
        finally:
            write_json(stage/'provenance.json', state)
    return stage, state


def apply(stage):
    stage = stage.resolve()
    state = json.loads((stage/'provenance.json').read_text())
    if state.get('schema') != 1 or state.get('state') != 'prepared':
        raise ValueError('Adoption must be fully prepared and not previously applied')
    machine = Path(state['machine']); config = machine/'machine.toml'
    if stage.parent != machine/'.dreamgpu-adoptions':
        raise ValueError('Adoption staging directory differs from its destination machine')
    with machine_lock(machine):
        stopped_fixture(Path(state['fixture'])); machine_stopped(machine)
        checked_file(config, state['original_config_sha256'])
        checked_file(stage/'original-machine.toml', state['original_config_sha256'])
        checked_file(stage/'candidate-machine.toml', state['candidate_config_sha256'])
        checked_file(stage/'disk.qcow2', state['disk_sha256'])
        verify_package(Path(state['package']), state['package_identity'])
        replacement = stage/'applying-machine.toml'
        with replacement.open('xb') as file:
            file.write((stage/'candidate-machine.toml').read_bytes()); file.flush(); os.fsync(file.fileno())
        replacement.chmod(config.stat().st_mode & 0o777)
        checked_file(config, state['original_config_sha256'])
        os.replace(replacement, config)
        state.update(state='applied', applied_utc=datetime.now(timezone.utc).isoformat(),
                     original_disk_retained=True, config_backup=str(stage/'original-machine.toml'))
        write_json(stage/'provenance.json', state)
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    staged = commands.add_parser('prepare', help='Stage disk, config backup/candidate and verified package; leave active config untouched')
    for name in ('fixture', 'source-manifest', 'machine', 'package', 'qemu-img'):
        staged.add_argument('--'+name, type=Path, required=True)
    staged.add_argument('--profile', choices=PROFILES, required=True)
    staged.add_argument('--resources', type=Path, help='Default: destination data directory/resources/retro')
    activated = commands.add_parser('apply', help='Revalidate prepared artifacts and atomically replace machine.toml')
    activated.add_argument('stage', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'prepare':
            stage, state = prepare(args)
            print(json.dumps({'stage': str(stage), 'state': state['state'], 'disk_sha256': state['disk_sha256'], 'package_identity': state['package_identity']}, indent=2))
        else:
            print(json.dumps(apply(args.stage), indent=2))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error)+'\n')

if __name__ == '__main__':
    main()
