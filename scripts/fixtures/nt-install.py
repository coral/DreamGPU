#!/usr/bin/env python3
"""Install explicit files into an independent, cold-boot NTFS guest copy.

Linux host with qemu-img, losetup and ntfs-3g tools required. The manifest names
a stopped source qcow2, its SHA256, and files with explicit guest destinations
and SHA256 values. Source images are never mounted or modified. RAM snapshots
are intentionally discarded: new driver/process binaries require a cold boot.
No guest networking, registry dumps, shell commands or UI automation are used.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import struct
import subprocess
import tempfile


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(*args):
    try:
        return subprocess.check_output([str(arg) for arg in args], stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as error:
        detail = error.stderr.decode(errors='replace')[-4096:]
        raise ValueError(f'{args[0]} failed ({error.returncode}): {detail}') from error


def guest_directory(value):
    path = PurePosixPath(value)
    if (not path.is_absolute() or '..' in path.parts or '\\' in value or
            path == PurePosixPath('/')):
        raise ValueError('Guest directories must be explicit absolute NTFS paths')
    return path


def create_directories(loop, root, directories):
    """Create only recorded directories on the isolated raw disk, then unmount."""
    root.mkdir()
    run('sudo', '-n', 'ntfs-3g', loop, root,
        '-o', f'uid={os.getuid()},gid={os.getgid()},umask=077,noexec,nodev,nosuid')
    try:
        for directory in directories:
            current = root
            for part in directory.parts[1:]:
                current = current / part
                if current.is_symlink():
                    raise ValueError('Guest directory traverses a symbolic link')
                current.mkdir(exist_ok=True)
    finally:
        run('sudo', '-n', 'umount', root)


def install(manifest_path, output, qemu_img):
    if platform.system() != 'Linux':
        raise ValueError('NTFS copy installation runs on the Linux host')
    data = json.loads(Path(manifest_path).read_text())
    source = Path(data['source']).resolve()
    if digest(source) != data['source_sha256']:
        raise ValueError('Source disk identity mismatch')
    directories = [guest_directory(value) for value in data.get('directories', [])]
    files = []
    destinations = set()
    for item in data['files']:
        local = Path(item['source']).resolve()
        guest = PurePosixPath(item['destination'])
        if (not guest.is_absolute() or '..' in guest.parts or
                '\\' in str(guest) or str(guest).casefold() in destinations):
            raise ValueError('Guest paths must be unique absolute NTFS paths')
        if digest(local) != item['sha256']:
            raise ValueError(f'Package identity mismatch: {local}')
        destinations.add(str(guest).casefold())
        files.append((local, str(guest), item['sha256']))
    if not files:
        raise ValueError('No files to install')
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'schema_version': 1, 'state': 'preparing', 'input': data,
              'cold_boot_required': True, 'source_modified': False}
    destination = output / 'disk.qcow2'
    try:
        with tempfile.TemporaryDirectory(prefix='nt-install-', dir=output) as directory:
            raw = Path(directory) / 'disk.raw'
            # qemu-img acquires the normal image lock; never force shared access.
            run(qemu_img, 'convert', '-f', 'qcow2', '-O', 'raw', source, raw)
            with raw.open('rb') as stream:
                mbr = stream.read(512)
            if mbr[510:512] != b'\x55\xaa':
                raise ValueError('Source is not an MBR disk')
            partitions = [struct.unpack_from('<B3sB3sII', mbr, 446 + i * 16)
                          for i in range(4)]
            ntfs = [part for part in partitions if part[2] == 7]
            if len(ntfs) != 1:
                raise ValueError('Expected one primary NTFS partition')
            offset, size = ntfs[0][4] * 512, ntfs[0][5] * 512
            if not offset or not size or offset + size > raw.stat().st_size:
                raise ValueError('Invalid NTFS partition bounds')
            loop = run('sudo', '-n', 'losetup', '--find', '--show',
                       '--offset', offset, '--sizelimit', size, raw).decode().strip()
            try:
                if directories:
                    create_directories(loop, Path(directory) / 'ntfs', directories)
                for local, guest, checksum in files:
                    run('sudo', '-n', 'ntfscp', loop, local, guest)
                    actual = hashlib.sha256(run('sudo', '-n', 'ntfscat', loop, guest)).hexdigest()
                    if actual != checksum:
                        raise ValueError(f'Installed bytes differ: {guest}')
            finally:
                run('sudo', '-n', 'losetup', '-d', loop)
            run(qemu_img, 'convert', '-f', 'raw', '-O', 'qcow2', raw, destination)
        check = json.loads(run(qemu_img, 'check', '--output=json', destination))
        if check.get('corruptions') or check.get('check-errors'):
            raise ValueError('Installed disk failed its image check')
        if digest(source) != data['source_sha256']:
            raise ValueError('Source changed during installation')
        report.update(state='installed', disk=str(destination), disk_sha256=digest(destination),
                      image_check=check)
    except BaseException as error:
        report.update(state='failed', error=str(error))
        raise
    finally:
        (output / 'install.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qemu-img', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = install(args.manifest, args.output, args.qemu_img.resolve())
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
