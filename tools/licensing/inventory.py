#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Refresh attribution metadata; optional documentation tool, never a build step.

Cargo's declared license strings are preserved, not interpreted as a legal
compatibility verdict. All features/targets include optional and development
packages, not just the contents of one shipped binary.
"""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'support/attribution'


def run(*args):
    return subprocess.check_output([str(arg) for arg in args], cwd=ROOT, text=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def preserve_notice(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    destination = OUTPUT / 'notices' / digest
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    return digest


def write(name, value):
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT/name).write_text(json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False)+'\n')


def cargo_inventory(metadata):
    members = set(metadata['workspace_members'])
    resolved = {node['id']: node for node in metadata['resolve']['nodes']}
    packages = {p['id']: p for p in metadata['packages']}
    lock = tomllib.loads((ROOT/'Cargo.lock').read_text())
    checksums = {(p['name'], p['version'], p.get('source')): p.get('checksum')
                 for p in lock['package']}
    direct = []
    for package in metadata['packages']:
        if package['id'] not in members:
            continue
        for dependency in package['dependencies']:
            direct.append({'workspace': package['name'],
                           **{key: dependency[key] for key in
                              ('name', 'rename', 'req', 'kind', 'target', 'optional', 'features')}})
    entries = []
    for package in metadata['packages']:
        if package['id'] in members:
            continue
        directory = Path(package['manifest_path']).parent
        notices = set()
        for pattern in ('LICENSE*', 'LICENCE*', 'COPYING*', 'NOTICE*', 'UNLICENSE*', 'license*'):
            notices.update(p for p in directory.glob(pattern) if p.is_file())
        if package.get('license_file'):
            notices.add(directory/package['license_file'])
        vcs = directory/'.cargo_vcs_info.json'
        vcs_data = json.loads(vcs.read_text()) if vcs.is_file() else None
        dependencies = sorted({f"{packages[dep]['name']}@{packages[dep]['version']}"
                               for dep in resolved[package['id']]['dependencies']})
        entries.append({'name': package['name'], 'version': package['version'],
                        'repository': package['repository'], 'homepage': package['homepage'],
                        'declared_license': package['license'], 'authors': package['authors'],
                        'source': package['source'],
                        'crate_checksum': checksums.get((package['name'], package['version'], package['source'])),
                        'crate_vcs': vcs_data,
                        'notice_files': {p.relative_to(directory).as_posix(): preserve_notice(p)
                                         for p in sorted(notices) if p.is_file() and p.is_relative_to(directory)},
                        'resolved_features': resolved[package['id']]['features'],
                        'dependencies': dependencies})
    write('cargo.json', {'schema': 1, 'cargo_lock_sha256': sha(ROOT/'Cargo.lock'),
                        'command': 'cargo metadata --locked --all-features --format-version 1',
                        'scope': 'Resolved all-target, all-feature normal/build/dev dependency superset; not a binary bill of materials or compatibility determination',
                        'direct_dependencies': sorted(direct, key=lambda p: (p['workspace'], p['name'], str(p['kind']), str(p['target']))),
                        'packages': sorted(entries, key=lambda p: (p['name'], p['version']))})


def repository_inventory():
    entries = []
    def walk(directory):
        config = directory/'.gitmodules'
        if not config.is_file():
            return
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(config)
        for section in parser.sections():
            relative, url = parser[section]['path'], parser[section]['url']
            path = directory/relative
            row = run('git', '-C', directory, 'ls-tree', 'HEAD', '--', relative).strip()
            pin = row.split()[2] if row else None
            initialized = (path/'.git').exists()
            notices = {}
            if initialized:
                for pattern in ('LICENSE*', 'LICENCE*', 'COPYING*', 'licence*'):
                    for file in sorted(path.glob(pattern)):
                        if file.is_file():
                            notices[file.relative_to(path).as_posix()] = preserve_notice(file)
            entries.append({'path': path.relative_to(ROOT).as_posix(), 'repository': url,
                            'declared_gitlink': pin, 'initialized': initialized,
                            'checkout_revision': run('git', '-C', path, 'rev-parse', 'HEAD').strip() if initialized else None,
                            'notice_files': notices,
                            'scope': 'Declared source dependency; initialization does not prove it is linked or shipped. See ATTRIBUTION.md for reviewed license scope.'})
            if initialized:
                walk(path)
    walk(ROOT)
    wraps = []
    for path in sorted((ROOT/'vendor/qemu/subprojects').glob('*.wrap')):
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(path)
        wraps.append({'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(path),
                      'configuration': {name: dict(parser[name]) for name in parser.sections()},
                      'scope': 'QEMU-declared optional source fallback; not necessarily selected by this build'})
    write('repositories.json', {'schema': 1, 'guest_source_lock_sha256': sha(ROOT/'support/guest/sources.lock.json'),
                                'submodules': entries, 'qemu_meson_wraps': wraps})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, help='Reuse a previously captured cargo metadata JSON')
    args = parser.parse_args()
    metadata = json.loads(args.metadata.read_text() if args.metadata else run('cargo', 'metadata', '--locked', '--all-features', '--format-version', '1'))
    cargo_inventory(metadata)
    repository_inventory()
    print(OUTPUT)


if __name__ == '__main__':
    main()
