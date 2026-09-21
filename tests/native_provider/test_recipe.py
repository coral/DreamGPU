#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Replay every checked provider edit and execute source contracts from its archive."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
recipe = ROOT / 'support/native/mesa'
pin = json.loads((recipe / 'source.json').read_text())
archive = Path(sys.argv[1])
with archive.open('rb') as stream:
    assert hashlib.file_digest(stream, 'sha256').hexdigest() == pin['sha256']
with tempfile.TemporaryDirectory(prefix='dreamgpu-provider-recipe-') as directory:
    work = Path(directory)
    with tarfile.open(archive) as source:
        for name, identity in pin['files'].items():
            relative = Path(name)
            assert not relative.is_absolute() and '..' not in relative.parts
            data = source.extractfile(pin['directory'] + '/' + name).read()
            assert hashlib.sha256(data).hexdigest() == identity['before'], name
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    for name, expected in pin['patches'].items():
        patch = recipe / name
        assert hashlib.sha256(patch.read_bytes()).hexdigest() == expected, name
        command = ['git', 'apply', '--no-index', '--whitespace=nowarn']
        subprocess.run([*command, '--check', str(patch)], cwd=work, check=True)
        subprocess.run([*command, str(patch)], cwd=work, check=True)
    for name, identity in pin['files'].items():
        assert hashlib.sha256((work / name).read_bytes()).hexdigest() == identity['after'], name
    tests = Path(__file__).resolve().parent
    for name, target in [('test_border_map.py', 'src/mesa/state_tracker/st_texture.c'),
                         ('test_border_storage.py', 'src/mesa/state_tracker/st_cb_texture.c'),
                         ('test_border_budget.py', '')]:
        subprocess.run([sys.executable, str(tests / name), str(work / target)], check=True)
print('PASS checked Mesa archive, patch and output identities with actual-source sanitizer contracts')
