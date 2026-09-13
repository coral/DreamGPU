# SPDX-License-Identifier: GPL-2.0-or-later
"""Hash-bound removal of explicitly named application providers on private copies."""
import hashlib
import re
import stat
from pathlib import Path, PurePosixPath

PROVIDERS = frozenset({
    'opengl32.dll', 'dgpugl.dll', 'jrgopengl.dll', 'glide2x.dll',
    'wined3d.dll', 'winedd.dll', 'wined8.dll', 'wined9.dll',
    'ddraw.dll', 'd3d8.dll', 'd3d9.dll', 'dgddr.dll',
})


def parse(entries, installed=()):
    """Each optional removals item requires destination and the exact prior SHA."""
    if not isinstance(entries, list) or len(entries) > 128:
        raise ValueError('Application removals must be a bounded list')
    used = {str(path).casefold() for path in installed}
    result = []
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {'destination', 'sha256'}:
            raise ValueError('Removal requires only destination and sha256')
        value, checksum = entry['destination'], entry['sha256']
        if not isinstance(value, str) or not isinstance(checksum, str):
            raise ValueError('Removal path and hash must be strings')
        path = PurePosixPath(value)
        if (not path.is_absolute() or value.startswith("//") or str(path) != value or len(value) >= 240 or
                len(path.parts) < 2 or path.name.casefold() not in PROVIDERS or
                any(part.casefold() in {'windows', 'winnt'} for part in path.parts) or
                any(part in {'.', '..'} or part.endswith(('.', ' ')) or
                    any(ord(c) < 32 or ord(c) >= 127 or c in '\\:*?"<>|~' for c in part)
                    for part in path.parts[1:]) or
                not re.fullmatch('[0-9a-f]{64}', checksum)):
            raise ValueError('Removal must name a canonical application provider and exact SHA256')
        if value.casefold() in used:
            raise ValueError('Removal destinations must be unique and cannot also be installed')
        used.add(value.casefold())
        result.append({'destination': value, 'sha256': checksum})
    return result


def apply(entries, volume, receipt):
    """Preflight every hash before deleting; verify each removed name is absent."""
    entries = parse(entries)
    for entry in entries:
        current = volume.read(entry['destination'])
        if hashlib.sha256(current).hexdigest() != entry['sha256']:
            raise ValueError('Application removal identity mismatch: '+entry['destination'])
    for entry in entries:
        name = entry['destination']
        # Recheck immediately before mutation; no unknown or newly changed bytes.
        if hashlib.sha256(volume.read(name)).hexdigest() != entry['sha256']:
            raise ValueError('Application removal changed after preflight: '+name)
        volume.remove(name)
        if not volume.absent(name):
            raise ValueError('Application removal did not leave absence: '+name)
        receipt.append({'destination': name, 'removed_sha256': entry['sha256'],
                        'absence_verified': True})


class Mounted:
    """Private mounted NTFS copy; reject links/reparse paths at every component."""
    def __init__(self, root):
        self.root = Path(root)
        if not stat.S_ISDIR(self.root.lstat().st_mode):
            raise ValueError('Removal root must be a real mounted directory')

    def locate(self, value, missing=False):
        current = self.root
        for part in PurePosixPath(value).parts[1:]:
            if not stat.S_ISDIR(current.lstat().st_mode):
                raise ValueError('Removal path traverses a link or non-directory')
            matches = [p for p in current.iterdir() if p.name.casefold() == part.casefold()]
            if len(matches) > 1:
                raise ValueError('Ambiguous case-insensitive guest path')
            if not matches:
                if missing:
                    return None
                raise ValueError('Expected owned application file is missing: '+value)
            current = matches[0]
            if stat.S_ISLNK(current.lstat().st_mode):
                raise ValueError('Removal path traverses a symbolic link')
        mode = current.lstat().st_mode
        if not stat.S_ISREG(mode) or current.stat().st_size > 16*1024*1024:
            raise ValueError('Removal member must be a bounded regular DLL')
        return current

    def read(self, value):
        return self.locate(value).read_bytes()

    def remove(self, value):
        self.locate(value).unlink()

    def absent(self, value):
        return self.locate(value, missing=True) is None


class Fat:
    """FAT has no symbolic links; mtools accesses only its on-disk directories."""
    def __init__(self, volume, command):
        self.volume, self.command = volume, command

    def locate(self, value, missing=False):
        current = ''
        parts = PurePosixPath(value).parts[1:]
        for index, part in enumerate(parts):
            raw = self.command('mdir', '-b', '-i', self.volume, '::'+current+'/')
            lines = raw.decode('ascii').splitlines()
            if len(lines) > 4096:
                raise ValueError('FAT directory exceeds removal inspection bound')
            matches = [line for line in lines
                       if line.rstrip('/').rsplit('/', 1)[-1].casefold() == part.casefold()]
            if len(matches) > 1:
                raise ValueError('Ambiguous FAT removal path')
            if not matches:
                if missing:
                    return None
                raise ValueError('Expected owned FAT application file is missing: '+value)
            is_dir = matches[0].endswith('/')
            if is_dir != (index < len(parts)-1):
                raise ValueError('FAT removal path has the wrong file/directory type')
            current += '/' + matches[0].rstrip('/').rsplit('/', 1)[-1]
        return current

    def read(self, value):
        actual = self.locate(value)
        data = self.command('mtype', '-i', self.volume, '::'+actual)
        if len(data) > 16*1024*1024:
            raise ValueError('FAT removal file exceeds bound')
        return data

    def remove(self, value):
        self.command('mdel', '-i', self.volume, '::'+self.locate(value))

    def absent(self, value):
        return self.locate(value, missing=True) is None
