#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit graphics verification and honest semantic-coverage reporting.

Source, native transport and installed API evidence are distinct. This command
never starts or installs into a Windows VM. Native verification builds QEMU and
runs every ignored diskless GPU test, failing if the test binary runs no tests.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / 'tests/graphics/contracts.json'
IMPLEMENTATIONS = {'implemented', 'emulated', 'partial', 'missing', 'unsupported', 'unverified'}


def inventory(path=INVENTORY):
    data = json.loads(path.read_text())
    if data['schema'] != 1 or type(data['catalog_complete']) is not bool:
        raise ValueError('Unsupported inventory schema')
    expected = {(g, h) for g in ('windows98', 'windows2000', 'windowsxp') for h in ('macos', 'linux')}
    pairs = [(p['guest'], p['host']) for p in data['platforms']]
    if len(pairs) != 6 or set(pairs) != expected:
        raise ValueError('The six required OS/host combinations must be explicit')
    if set(data['areas']) != {str(i) for i in range(1, 22)}:
        raise ValueError('The inventory must retain all 21 requirement areas')
    for test in data['tests'].values():
        path = Path(test['path'])
        if path.is_absolute() or '..' in path.parts or not (ROOT / path).is_file():
            raise ValueError(f'Invalid test source: {path}')
        if test['scope'] not in {'source', 'source-i686', 'native-transport', 'installed-api'}:
            raise ValueError('Unknown evidence scope')
    seen = set()
    areas = set()
    for item in data['contracts']:
        if item['id'] in seen:
            raise ValueError(f'Duplicate contract: {item["id"]}')
        seen.add(item['id'])
        areas.add(str(item['area']))
        if str(item['area']) not in data['areas']:
            raise ValueError('Unknown requirement area')
        for key in ('id', 'api', 'operation', 'format', 'resource', 'usage', 'pool', 'remaining'):
            if not isinstance(item[key], str) or not item[key].strip():
                raise ValueError(f'Missing {key}: {item["id"]}')
        if item['implementation'] not in IMPLEMENTATIONS:
            raise ValueError(f'Unknown implementation status: {item["id"]}')
        if type(item['mandatory']) is not bool or not (
                type(item['advertised']) is bool or item['advertised'] == 'unverified'):
            raise ValueError('Mandatory and advertised status must be explicit')
        if any(name not in data['tests'] for name in item['tests']):
            raise ValueError(f'Unknown test: {item["id"]}')
        if item['implementation'] in {'implemented', 'emulated'} and not item['tests']:
            raise ValueError(f'Implementation without a test: {item["id"]}')
    if areas != set(data['areas']):
        raise ValueError('A requirement area has no contracts')
    return data


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def identity():
    """Include uncommitted and untracked source, not just the current commit."""
    names = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT).split(b'\0')
    tree = hashlib.sha256()
    for name in sorted(set(names) - {b''}):
        path = ROOT / os.fsdecode(name)
        if path.is_file():
            tree.update(name + b'\0' + bytes.fromhex(digest(path)))
    # A submodule commit does not identify its dirty source. In particular,
    # DreamGPU's QEMU integration can change without advancing the donor HEAD.
    submodules = subprocess.check_output(['git', 'submodule', 'status', '--recursive'], cwd=ROOT, text=True)
    changes = {}
    for entry in submodules.splitlines():
        if entry.startswith('-'):
            continue
        _, directory, *_ = entry[1:].split(' ')
        module = ROOT / directory
        dirty = subprocess.check_output(['git', 'diff', '--name-only', '--no-renames', '-z', 'HEAD'], cwd=module)
        other = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '-z'], cwd=module)
        for name in sorted(set((dirty + other).split(b'\0')) - {b''}):
            path = module / os.fsdecode(name)
            relative = directory + '/' + os.fsdecode(name)
            if path.is_symlink():
                value = 'symlink:' + os.readlink(path)
            elif path.is_file():
                value = digest(path)
            elif not path.exists():
                value = 'deleted'
            else:
                continue  # Nested module identities are recorded separately.
            changes[relative] = value
            tree.update(relative.encode() + b'\0' + value.encode() + b'\0')
    return {'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'working_source_sha256': tree.hexdigest(), 'submodules': submodules.splitlines(),
            'submodule_changes': changes}


def acceptance_gaps(data):
    """No local test success is promoted to an installed-API conformance pass.

    Evidence ingestion is deliberately not implemented yet. Until actual runtime
    capability capture and public lifecycle checks exist, this gate fails closed.
    """
    gaps = []
    if not data['catalog_complete']:
        gaps.append('Operation/format/resource/usage/pool catalogue is incomplete')
    for item in data['contracts']:
        required = item['mandatory'] or item['advertised'] is True
        if required and item['implementation'] not in {'implemented', 'emulated'}:
            gaps.append(f'{item["id"]}: {item["implementation"]}')
        if item['advertised'] == 'unverified':
            gaps.append(f'{item["id"]}: runtime capability reporting unverified')
    for pair in data['platforms']:
        gaps.append(f'{pair["guest"]}/{pair["host"]}: installed-API capability and lifecycle evidence missing')
    return gaps


def commands(data, suite):
    if suite in ('source', 'source-i686'):
        return [(name, [sys.executable, test['path']]) for name, test in data['tests'].items()
                if test['scope'] == suite]
    return [('native-build', ['cargo', 'build', '--locked', '--release']),
            ('native-unit', ['cargo', 'test', '--locked', '-p', 'dreamgpu-host']),
            ('native-gpu', ['cargo', 'test', '--locked', '--features', 'presentation',
                            '--test', 'native_qemu', '--', '--ignored', '--test-threads=1'])]


def run(data, suite, output):
    output.mkdir(parents=True, exist_ok=False)
    report = {'schema': 1, 'suite': suite, 'scope': suite if suite.startswith('source') else 'native-transport',
              'started_utc': datetime.now(timezone.utc).isoformat(), 'host': platform.platform(),
              'inventory_sha256': digest(INVENTORY), 'source_before': identity(), 'results': [],
              'semantic_complete': False}
    env = os.environ.copy()
    env['DREAMGPU_BUILD'] = 'native'
    # QEMU must be built from this source, rather than a caller's frozen candidate.
    for key in ('DREAMGPU_QEMU_PATH', 'DREAMGPU_OUTPUT'):
        env.pop(key, None)
    try:
        for name, command in commands(data, suite):
            print(f'RUN {name}: {" ".join(command)}', flush=True)
            start = time.monotonic()
            log = output / f'{name}.log'
            with log.open('w') as stream:
                result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
            text = log.read_text(errors='replace')
            passed = result.returncode == 0
            if name == 'native-gpu':
                match = re.search(r'test result: ok\. (\d+) passed; 0 failed; 0 ignored;', text)
                passed = passed and match is not None and int(match[1]) > 0
            report['results'].append({'name': name, 'argv': command, 'exit_code': result.returncode,
                                      'passed': passed, 'seconds': round(time.monotonic() - start, 3),
                                      'log': log.name, 'log_sha256': digest(log)})
            print(f'{"PASS" if passed else "FAIL"} {name}: {log}', flush=True)
            if not passed:
                print(text[-6000:], file=sys.stderr)
                break
        binary = ROOT / 'target/qemu-build/qemu-system-x86_64'
        if suite == 'native' and binary.is_file():
            report['native_binary_sha256'] = digest(binary)
    finally:
        report['source_after'] = identity()
        report['source_unchanged'] = report['source_before'] == report['source_after']
        report['passed'] = (report['source_unchanged'] and
                            len(report['results']) == len(commands(data, suite)) and
                            all(item['passed'] for item in report['results']))
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report['passed'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('inventory', 'source', 'source-i686', 'native', 'acceptance'))
    parser.add_argument('--output', type=Path, help='New evidence directory (source/native only)')
    args = parser.parse_args()
    data = inventory()
    if args.action == 'inventory':
        print(json.dumps({'contracts': len(data['contracts']), 'catalog_complete': data['catalog_complete'],
                          'implementation': dict(Counter(item['implementation'] for item in data['contracts'])),
                          'semantic_complete': False}, indent=2))
        return 0
    if args.action == 'acceptance':
        gaps = acceptance_gaps(data)
        print(json.dumps({'complete': not gaps, 'gaps': gaps}, indent=2))
        return 1 if gaps else 0
    if args.output is None:
        parser.error('--output is required for source/native verification')
    return run(data, args.action, args.output.resolve())


if __name__ == '__main__':
    sys.exit(main())
