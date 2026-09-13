#!/usr/bin/env python3
"""Atomically stage a hash-pinned graphics package in an independent stopped disk.

Only the two supported application directories are writable. Original disk and
application bytes are preserved; no guest network, registry or system DLL edits.
Linux ntfs-3g/mtools perform filesystem access on a private raw conversion.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

APPLICATIONS = ('/SIERRA/Half-Life', '/UT99/System')
DLLS = ('dgpugl.dll', 'glide2x.dll', 'wined3d.dll', 'wined8.dll', 'wined9.dll', 'winedd.dll')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(*args):
    try:
        return subprocess.check_output([str(x) for x in args], stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as error:
        raise ValueError(error.stderr.decode(errors='replace')[-4096:]) from error


def checked_package(path, identity):
    path = Path(path).resolve()
    manifest = json.loads((path/'package.json').read_text())
    files = manifest['files']
    if manifest.get('schema') == 1:
        canonical = json.dumps(files, sort_keys=True).encode()
    elif (manifest.get('schema') == 2 and
          manifest.get('identity_scheme') == 'sha256-json-utf8-sorted-compact'):
        canonical = json.dumps(files, sort_keys=True, ensure_ascii=False,
                               separators=(',', ':')).encode()
    else:
        raise ValueError('Unsupported package identity schema')
    actual = hashlib.sha256(canonical).hexdigest()
    if actual != identity or manifest['identity'] != identity:
        raise ValueError('Package identity mismatch')
    application = path/'application'
    if application.is_symlink() or not application.is_dir():
        raise ValueError('Package application directory is not an owned directory')
    payloads = {}
    for name in DLLS:
        member = 'application/'+name
        file = path/member
        if file.is_symlink() or not file.is_file() or digest(file) != files.get(member):
            raise ValueError('Package payload mismatch: '+member)
        payloads[name] = file
    return payloads


def partition(raw):
    with raw.open('rb') as stream:
        mbr = stream.read(512)
    if len(mbr) != 512 or mbr[510:] != b'\x55\xaa':
        raise ValueError('Expected an MBR disk')
    entries = [struct.unpack_from('<B3sB3sII', mbr, 446+i*16) for i in range(4)]
    supported = [e for e in entries if e[2] in (7, 6, 0x0b, 0x0c, 0x0e)]
    if len(supported) != 1:
        raise ValueError('Expected exactly one primary NTFS/FAT partition')
    e = supported[0]
    offset, size = e[4]*512, e[5]*512
    if not offset or not size or offset+size > raw.stat().st_size:
        raise ValueError('Invalid primary partition bounds')
    return ('ntfs' if e[2] == 7 else 'fat'), offset, size


class Volume:
    def __init__(self, raw):
        self.kind, self.offset, self.size = partition(raw)
        self.raw, self.loop = raw, None
        self.names = {}

    def __enter__(self):
        if self.kind == 'ntfs':
            self.loop = run('sudo', '-n', 'losetup', '--find', '--show', '--offset',
                            self.offset, '--sizelimit', self.size, self.raw).decode().strip()
        return self

    def __exit__(self, *_):
        if self.loop:
            run('sudo', '-n', 'losetup', '-d', self.loop)

    def listing(self, directory):
        if directory not in APPLICATIONS:
            raise ValueError('Unsupported application directory')
        if directory not in self.names:
            if self.kind == 'ntfs':
                data = run('sudo', '-n', 'ntfsls', '-p', directory, self.loop)
            else:
                data = run('mdir', '-b', '-i', str(self.raw)+'@@'+str(self.offset), '::'+directory+'/*')
            self.names[directory] = {line.rstrip('/').split('/')[-1].casefold(): line.rstrip('/').split('/')[-1]
                                     for line in data.decode().splitlines() if line.strip()}
        return self.names[directory]

    def read(self, directory, name):
        actual = self.listing(directory).get(name.casefold())
        if actual is None:
            return None
        guest = directory+'/'+actual
        if self.kind == 'ntfs':
            return run('sudo', '-n', 'ntfscat', self.loop, guest)
        return run('mtype', '-i', str(self.raw)+'@@'+str(self.offset), '::'+guest)

    def write(self, directory, name, source):
        if directory not in APPLICATIONS or (name not in DLLS and not (directory == '/UT99/System' and name == 'dgddr.dll')):
            raise ValueError('Unsupported application member')
        guest = directory+'/'+self.listing(directory).get(name.casefold(), name)
        if self.kind == 'ntfs':
            run('sudo', '-n', 'ntfscp', self.loop, source, guest)
        else:
            run('mcopy', '-o', '-i', str(self.raw)+'@@'+str(self.offset), source, '::'+guest)
        self.names.pop(directory, None)


def app_payloads(directory, payloads):
    result = dict(payloads)
    if directory == '/UT99/System' and 'winedd.dll' in payloads:
        # The original UT D3DDrv import is explicitly redirected to this
        # same-length app-local filename; audit the bytes actually loaded.
        result['dgddr.dll'] = payloads['winedd.dll']
    return result


def update_members(volume, applications, payloads, backup):
    """Capture every original before changing any file; verify every write."""
    originals = []
    for index, directory in enumerate(applications):
        for name, source in app_payloads(directory, payloads).items():
            previous = volume.read(directory, name)
            record = {'destination': directory+'/'+name, 'installed_sha256': digest(source),
                      'previous_sha256': None, 'previous_absent': previous is None}
            if previous is not None:
                destination = backup/str(index)/name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(previous)
                record.update(previous_sha256=digest(destination), backup=str(destination.relative_to(backup.parent)))
            originals.append(record)
    previous_hashes = {entry['destination']: entry['previous_sha256'] for entry in originals}
    for directory in applications:
        for name, source in app_payloads(directory, payloads).items():
            expected = digest(source)
            if previous_hashes[directory+'/'+name] == expected:
                continue  # Preserve accepted translator bytes without rewriting them.
            volume.write(directory, name, source)
            actual = volume.read(directory, name)
            if actual is None or hashlib.sha256(actual).hexdigest() != expected:
                raise ValueError('Installed byte mismatch: '+directory+'/'+name)
    return originals


def install(manifest, output, qemu_img):
    data = json.loads(Path(manifest).read_text())
    source, output = Path(data['source']).resolve(), Path(output).resolve()
    applications = data['applications']
    if (not isinstance(applications, list) or not applications or
            len(set(applications)) != len(applications) or any(x not in APPLICATIONS for x in applications)):
        raise ValueError('Name each supported application directory exactly once')
    if output.exists():
        raise ValueError('Output already exists; choose a new successor')
    if digest(source) != data['source_sha256']:
        raise ValueError('Stopped source disk identity mismatch')
    payloads = checked_package(data['package'], data['package_identity'])
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.app-install-', dir=output.parent) as temporary:
        stage = Path(temporary)
        raw = stage/'disk.raw'
        run(qemu_img, 'convert', '-f', 'qcow2', '-O', 'raw', source, raw)
        with Volume(raw) as volume:
            originals = update_members(volume, applications, payloads, stage/'backup')
            filesystem = volume.kind
        disk = stage/'disk.qcow2'
        run(qemu_img, 'convert', '-f', 'raw', '-O', 'qcow2', raw, disk)
        check = json.loads(run(qemu_img, 'check', '--output=json', disk))
        if check.get('corruptions') or check.get('check-errors'):
            raise ValueError('Output image check failed')
        if digest(source) != data['source_sha256']:
            raise ValueError('Source changed during staging')
        report = {'schema': 1, 'state': 'installed', 'input': data, 'filesystem': filesystem,
                  'source_modified': False, 'disk_sha256': digest(disk), 'image_check': check,
                  'members': originals, 'cold_boot_required': True}
        raw.unlink()
        (stage/'install.json').write_text(json.dumps(report, indent=2)+'\n')
        # Same-filesystem rename publishes only a complete validated successor.
        stage.rename(output)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qemu-img', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(install(args.manifest, args.output, args.qemu_img), indent=2))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error)+'\n')


if __name__ == '__main__':
    main()
