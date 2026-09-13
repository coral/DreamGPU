#!/usr/bin/env python3
"""Prepare one stopped Win98 diagnostic system-ICD copy, without booting it.

The source must already have the DreamGPU driver installed. This replaces its
existing pair only in a fresh image, adds the diagnostic ICD and fixed bootstrap,
and retains exact old bytes. Microsoft OpenGL and registry hives are untouched.
On cold boot DGICDBT backs up/sets one diagnostic registry value and starts the
runner; the normal sysgl route remains a separate bounded proof.
"""
if __package__ in (None, ''):
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location('win98_stage', ROOT / 'scripts/fixtures/win9x-stage.py')
_stage = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_stage)
DESTINATIONS = {
    'driver': '/WINDOWS/SYSTEM/DGPUMINI.DRV',
    'vxd': '/WINDOWS/SYSTEM/DGPUMINI.VXD',
    'icd': '/WINDOWS/SYSTEM/DGPUICD.DLL',
    'bootstrap': '/DGICDBT.EXE',
    'runner': '/DGPUBEN.EXE',
    'probe': '/DGSYSGL.EXE',
}
PROTECTED = ('/WINDOWS/SYSTEM/OPENGL32.DLL', '/WINDOWS/SYSTEM.DAT', '/WINDOWS/USER.DAT')


def digest(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def validate(data):
    if data.get('schema') != 1 or set(data.get('files', {})) != set(DESTINATIONS):
        raise ValueError('Require exact six diagnostic inputs, schema 1')
    source = Path(data['source']).resolve(strict=True)
    if digest(source) != data['source_sha256']:
        raise ValueError('Stopped source hash differs')
    for role, entry in data['files'].items():
        path = Path(entry['path']).resolve(strict=True)
        if path.stat().st_size > 16 * 1024 * 1024 or digest(path) != entry['sha256']:
            raise ValueError('Diagnostic artifact hash/bound differs: ' + role)
        prior = entry.get('before_sha256')
        if role in ('driver', 'vxd') and not prior:
            raise ValueError('Installed driver pair must have explicit prior hashes')
        if prior is not None and (len(prior) != 64 or any(c not in '0123456789abcdef' for c in prior)):
            raise ValueError('Invalid exact prior identity: ' + role)
    return source


def startup(before):
    checked = _stage.migrate_startup(before)
    # The existing parser already proves one exact [windows] run entry.
    return checked.replace(b'run=C:\\DGPUBEN.EXE', b'run=C:\\DGICDBT.EXE', 1)


def command(*args):
    return subprocess.check_output([str(a) for a in args], stderr=subprocess.PIPE)


def stage(manifest, output, qemu_img):
    data = json.loads(Path(manifest).read_text())
    source = validate(data)
    info = json.loads(command(qemu_img, 'info', '--output=json', '--backing-chain', source))
    if len(info) != 1 or info[0]['format'] != 'qcow2':
        raise ValueError('Require independent stopped qcow2 image')
    command(qemu_img, 'check', source)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    raw = output / 'stage.raw'
    report = dict(schema=1, state='preparing', source=str(source), source_sha256=data['source_sha256'],
                  scope='diagnostic_only', production_ready=False, files=[], protected={})
    try:
        command(qemu_img, 'convert', '-O', 'raw', source, raw)
        with raw.open('rb') as f:
            mbr = f.read(512)
        entries = [mbr[n:n+16] for n in range(446, 510, 16)]
        fat = [p for p in entries if p[4] in (0x0b, 0x0c)]
        if mbr[510:512] != b'\x55\xaa' or len(fat) != 1:
            raise ValueError('Require exactly one FAT32 primary partition')
        volume = str(raw) + '@@' + str(struct.unpack_from('<I', fat[0], 8)[0] * 512)
        for path in PROTECTED:
            report['protected'][path] = hashlib.sha256(command('mtype', '-i', volume, '::' + path)).hexdigest()
        old_ini = command('mtype', '-i', volume, '::/WINDOWS/WIN.INI')
        new_ini = startup(old_ini)
        (output / 'WIN.INI.before').write_bytes(old_ini)
        (output / 'WIN.INI.after').write_bytes(new_ini)
        old_dir = output / 'originals'
        old_dir.mkdir()
        # Complete destination preflight before changing any staged guest file.
        for role, destination in DESTINATIONS.items():
            entry = data['files'][role]
            listed = command('mdir', '-b', '-i', volume, '::' + str(Path(destination).parent)).decode('latin1').splitlines()
            exists = any(line.rstrip('/').split('/')[-1].casefold() == Path(destination).name.casefold() for line in listed)
            old = command('mtype', '-i', volume, '::' + destination) if exists else None
            actual = hashlib.sha256(old).hexdigest() if old is not None else None
            if actual != entry.get('before_sha256'):
                raise ValueError('Unexpected prior destination: ' + destination)
            if old is not None:
                (old_dir / role).write_bytes(old)
            report['files'].append(dict(role=role, destination=destination, before_sha256=actual, sha256=entry['sha256']))
        for role, destination in DESTINATIONS.items():
            entry = data['files'][role]
            command('mcopy', '-o', '-i', volume, entry['path'], '::' + destination)
            if hashlib.sha256(command('mtype', '-i', volume, '::' + destination)).hexdigest() != entry['sha256']:
                raise ValueError('Readback mismatch: ' + role)
        command('mcopy', '-o', '-i', volume, output / 'WIN.INI.after', '::/WINDOWS/WIN.INI')
        if command('mtype', '-i', volume, '::/WINDOWS/WIN.INI') != new_ini:
            raise ValueError('Startup readback differs')
        for path, expected in report['protected'].items():
            if hashlib.sha256(command('mtype', '-i', volume, '::' + path)).hexdigest() != expected:
                raise ValueError('Protected system bytes changed: ' + path)
        disk = output / 'disk.qcow2'
        command(qemu_img, 'convert', '-O', 'qcow2', raw, disk)
        command(qemu_img, 'check', disk)
        if digest(source) != data['source_sha256']:
            raise ValueError('Original stopped source changed')
        report.update(state='prepared', disk=str(disk), disk_sha256=digest(disk), source_modified=False)
        raw.unlink()
    except BaseException as error:
        report.update(state='failed', error=str(error))
        raise
    finally:
        (output / 'run.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qemu-img', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(stage(args.manifest, args.output, args.qemu_img), indent=2))


if __name__ == '__main__':
    main()
