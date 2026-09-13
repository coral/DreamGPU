# SPDX-License-Identifier: GPL-2.0-or-later
"""Bind explicitly requested probe upgrades to the runner's actual launch paths."""
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re

ROOT = Path(__file__).resolve().parents[2]


def registered():
    source = (ROOT / 'tools/benchmark/runner.cpp').read_text()
    table = source.split('static const PROBE_SPEC Probes[] = {', 1)[1].split('};', 1)[0]
    entries = re.findall(r'\{\s*("[^"\n]+")\s*,\s*("(?:[^"\\]|\\.)*")\s*,', table)
    result = {}
    for name, path in entries:
        name, path = json.loads(name), PureWindowsPath(json.loads(path))
        if name in result or not path.is_absolute():
            raise ValueError('Ambiguous runner probe table')
        result[name] = path
    if not result:
        raise ValueError('Runner probe table is empty')
    return result


def validate(manifest):
    """A probe declaration requires an exact staged file, not just its basename.

    Other application/installer files remain unrestricted by the probe table.
    Their ordinary destination/hash checks belong to the disk stager.
    """
    expected = manifest.get('probe_helpers', {})
    if not isinstance(expected, dict):
        raise ValueError('probe_helpers must map fixed probe names to SHA256 hashes')
    if not expected:
        return []
    paths, supplied = registered(), {}
    for entry in manifest['files']:
        path = PurePosixPath(entry['destination'])
        if not path.is_absolute() or '..' in path.parts or '\\' in str(path):
            raise ValueError('Probe deployment requires absolute bounded guest paths')
        key = str(PureWindowsPath('C:/', *path.parts[1:])).casefold()
        if key in supplied:
            raise ValueError('Duplicate case-insensitive guest deployment path')
        supplied[key] = entry['sha256']
    verified = []
    for name, digest in expected.items():
        if name not in paths or not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError('Unknown probe or invalid expected helper identity')
        path = paths[name]
        if supplied.get(str(path).casefold()) != digest:
            raise ValueError(f'{name} must stage its exact helper at {path}')
        verified.append({'probe': name, 'path': str(path), 'sha256': digest})
    return verified
