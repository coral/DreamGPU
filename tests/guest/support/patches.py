"""Apply reviewed source patches only to their exact recorded inputs and outputs."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import shutil
import subprocess
import tempfile


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def relative(name):
    path = PurePosixPath(name)
    if not name or not path.parts or path.is_absolute() or '..' in path.parts or '\\' in name:
        raise ValueError(f'Invalid patch path: {name!r}')
    return Path(path)


def apply(source, manifest_path):
    """Validate the complete patch transaction before updating a generated tree.

    Work happens outside the checkout so Git cannot select an enclosing repository.
    No fuzzy or unchecked substitutions are used. A failed check leaves inputs intact.
    """
    source, manifest_path = Path(source), Path(manifest_path)
    manifest = json.loads(manifest_path.read_text())
    if manifest.get('schema') != 1 or not manifest.get('files') or not manifest.get('patches'):
        raise ValueError('Invalid checked-patch manifest')
    files = {relative(name): value for name, value in manifest['files'].items()}
    patches = []
    for entry in manifest['patches']:
        path = manifest_path.parent / relative(entry['file'])
        if path.is_symlink() or not path.is_file() or digest(path) != entry['sha256']:
            raise ValueError(f'Patch identity mismatch: {path}')
        patches.append(path.resolve())
    for name, identity in files.items():
        path = source / name
        if path.is_symlink() or not path.is_file() or digest(path) != identity['before_sha256']:
            raise ValueError(f'Upstream source identity mismatch: {name}')
    with tempfile.TemporaryDirectory(prefix='dreamgpu-patches-') as temporary:
        stage = Path(temporary)
        for name in files:
            (stage / name).parent.mkdir(parents=True, exist_ok=True)
            if files[name].get('normalize_lf', False):
                # Some pinned Windows donors use CRLF; record the old builder's
                # text-mode normalization explicitly without obscuring code diffs.
                (stage / name).write_bytes((source / name).read_bytes().replace(b'\r\n', b'\n'))
            else:
                shutil.copyfile(source / name, stage / name)
        for patch in patches:
            command = ['git', 'apply', '--no-index', '--whitespace=nowarn']
            subprocess.run([*command, '--check', str(patch)], cwd=stage, check=True)
            subprocess.run([*command, str(patch)], cwd=stage, check=True)
        if {path.relative_to(stage) for path in stage.rglob('*') if not path.is_dir()} != set(files):
            raise ValueError('Patch changes files outside the recorded manifest')
        for name, identity in files.items():
            path = stage / name
            if path.is_symlink() or not path.is_file() or digest(path) != identity['after_sha256']:
                raise ValueError(f'Patched source identity mismatch: {name}')
        for name in files:
            shutil.copyfile(stage / name, source / name)
    return {'manifest_sha256': digest(manifest_path), 'files': manifest['files']}
