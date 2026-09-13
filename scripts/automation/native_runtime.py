"""Exact process identity for Cargo's authenticated native runtime launcher."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import stat


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def checked_execution(artifact, resolve):
    """Legacy raw ELFs remain valid; launchers require their explicit closure receipt."""
    runtime = artifact.get('runtime')
    if runtime is None:
        return None
    manifest_path = resolve(runtime['path']).resolve()
    if manifest_path.is_symlink() or digest(manifest_path) != runtime['sha256']:
        raise ValueError('Native runtime manifest identity mismatch')
    directory = resolve(runtime['directory']).resolve()
    if manifest_path != directory / 'manifest.json':
        raise ValueError('Native runtime manifest is outside its recorded directory')
    manifest = json.loads(manifest_path.read_bytes())
    if manifest.get('schema') != 1 or not isinstance(manifest.get('files'), dict) or not manifest['files']:
        raise ValueError('Invalid native runtime manifest')
    for name, record in manifest['files'].items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or not relative.parts or any(p in ('.', '..') for p in relative.parts) or str(relative) != name:
            raise ValueError('Unsafe native runtime member')
        path = directory / name
        metadata = path.lstat()
        if (not stat.S_ISREG(metadata.st_mode) or not path.resolve().is_relative_to(directory) or
                metadata.st_size != record['size'] or stat.S_IMODE(metadata.st_mode) != record['mode'] or
                digest(path) != record['sha256']):
            raise ValueError(f'Native runtime member identity mismatch: {name}')
    for name, record in manifest['platform_files'].items():
        if not Path(name).is_absolute() or digest(name) != record['sha256']:
            raise ValueError(f'Native platform identity mismatch: {name}')
    executable = resolve(artifact['execution_path']).resolve()
    relative = executable.relative_to(directory).as_posix()
    if len(Path(relative).parts) != 2 or not relative.startswith('programs/') or relative not in manifest['files']:
        raise ValueError('Native execution path is not a recorded program')
    return {'path': str(executable), 'sha256': manifest['files'][relative]['sha256'],
            'runtime_manifest': str(manifest_path), 'runtime_sha256': runtime['sha256']}
