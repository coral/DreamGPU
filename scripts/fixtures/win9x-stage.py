#!/usr/bin/env python3
"""Stage explicit application files and the runner into an independent FAT32 copy.

Does not install/register display drivers or change registry hives. The source
must be stopped and hash-pinned. Guest SetupAPI installation and cold activation
are separate gates. Only the exact existing benchmark WIN.INI entry is migrated. Optional removals
name application provider DLLs with their exact previous SHA256; absence is
verified on the independent copy and recorded. Windows/WinNT paths are refused.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import subprocess
from scripts.fixtures import app_removals, guest_tools


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def command(*args):
    return subprocess.check_output([str(arg) for arg in args], stderr=subprocess.PIPE)


def migrate_startup(data):
    text = data.decode('latin1')
    lines = text.splitlines(keepends=True)
    section = None
    matches = []
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith('[') and stripped.endswith(']'):
            section = stripped.lower()
        if section == '[windows]' and '=' in line and line.split('=', 1)[0].strip().lower() == 'run':
            if line.split('=', 1)[1].strip().lower() not in (r'c:\jrgbench.exe', r'c:\dgpuben.exe'):
                raise ValueError('WIN.INI run entry is not the expected fixture benchmark')
            matches.append(index)
    if len(matches) != 1:
        raise ValueError('Require exactly one existing fixture benchmark startup entry')
    index = matches[0]
    ending = '\r\n' if lines[index].endswith('\r\n') else '\n' if lines[index].endswith('\n') else ''
    lines[index] = 'run=C:\\DGPUBEN.EXE' + ending
    return ''.join(lines).encode('latin1')


def stage(manifest_path, output, qemu_img):
    data = json.loads(Path(manifest_path).read_text())
    probe_helpers = guest_tools.validate(data)
    source = Path(data['source']).resolve(strict=True)
    if digest(source) != data['source_sha256']:
        raise ValueError('Stopped source identity differs')
    files = []
    destinations = set()
    for entry in data['files']:
        local = Path(entry['source']).resolve(strict=True)
        guest = PurePosixPath(entry['destination'])
        if (not guest.is_absolute() or '..' in guest.parts or '\\' in str(guest) or
                str(guest).casefold() in destinations or len(str(guest)) >= 240):
            raise ValueError('Require unique bounded absolute FAT paths')
        if str(guest).casefold().startswith('/windows/'):
            raise ValueError('Application staging cannot replace Windows files or registry hives')
        if local.stat().st_size > 32 * 1024 * 1024 or digest(local) != entry['sha256']:
            raise ValueError('Application file identity/bound differs')
        destinations.add(str(guest).casefold())
        files.append((local, str(guest), entry['sha256']))
    removals = app_removals.parse(data.get('removals', []), destinations)
    if '/dgpuben.exe' not in destinations:
        raise ValueError('Explicit source-built runner is required')
    info = json.loads(command(qemu_img, 'info', '--output=json', '--backing-chain', source))
    if len(info) != 1 or info[0]['format'] != 'qcow2':
        raise ValueError('Require an independent stopped qcow2 source')
    command(qemu_img, 'check', source)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    raw = output / 'stage.raw'
    report = {'schema': 1, 'state': 'staging', 'source': str(source),
              'source_sha256': data['source_sha256'], 'files': [], 'removals': [],
              'probe_helpers': probe_helpers,
              'driver_registration_changed': False}
    try:
        command(qemu_img, 'convert', '-O', 'raw', source, raw)
        with raw.open('rb') as stream:
            mbr = stream.read(512)
            if mbr[510:512] != b'\x55\xaa' or mbr[450] not in (0x0b, 0x0c):
                raise ValueError('Require a primary FAT32 partition')
            offset = struct.unpack_from('<I', mbr, 454)[0] * 512
        volume = str(raw) + '@@' + str(offset)
        app_removals.apply(removals, app_removals.Fat(volume, command), report['removals'])
        before = command('mtype', '-i', volume, '::/WINDOWS/WIN.INI')
        after = migrate_startup(before)
        (output / 'WIN.INI.before').write_bytes(before)
        (output / 'WIN.INI.after').write_bytes(after)
        for local, guest, expected in files:
            command('mcopy', '-o', '-i', volume, local, '::' + guest)
            actual = hashlib.sha256(command('mtype', '-i', volume, '::' + guest)).hexdigest()
            if actual != expected:
                raise ValueError('FAT file readback differs: ' + guest)
            report['files'].append({'destination': guest, 'sha256': actual, 'exact_readback': True})
        command('mcopy', '-o', '-i', volume, output / 'WIN.INI.after', '::/WINDOWS/WIN.INI')
        if command('mtype', '-i', volume, '::/WINDOWS/WIN.INI') != after:
            raise ValueError('Startup INI readback differs')
        disk = output / 'disk.qcow2'
        command(qemu_img, 'convert', '-O', 'qcow2', raw, disk)
        command(qemu_img, 'check', disk)
        if digest(source) != data['source_sha256']:
            raise ValueError('Source identity changed during staging')
        report.update(state='staged', disk=str(disk), disk_sha256=digest(disk), source_modified=False,
                      startup_before_sha256=hashlib.sha256(before).hexdigest(),
                      startup_after_sha256=hashlib.sha256(after).hexdigest())
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
