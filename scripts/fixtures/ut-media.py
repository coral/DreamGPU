#!/usr/bin/env python3
"""Assemble immutable private UT99 fixture media; never boot or modify a guest."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('dg_package', ROOT/'scripts/fixtures/package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
HELPERS = ('DGUTSET.EXE', 'DGUT.EXE', 'DGUTD3.EXE', 'DGUTDS.EXE', 'DGUTLOG.EXE')


def digest(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def checked(path, expected):
    if path.is_symlink() or not path.is_file() or digest(path) != expected:
        raise ValueError(f'Build payload missing or changed: {path}')
    return path


def assemble(args):
    if args.output.exists() or (args.iso and args.iso.exists()):
        raise ValueError('Use new output paths; media and frozen inputs are immutable')
    iso_tool = shutil.which('xorriso') or shutil.which('hdiutil')
    if args.iso and not iso_tool:
        raise ValueError('ISO creation requires xorriso (Linux) or hdiutil (Mac)')
    manifests = {name: json.loads((getattr(args, name)/'manifest.json').read_text())
                 for name in ('helpers', 'runner', 'installer', 'opengl', 'wine', 'glide')}
    for name in ('opengl', 'wine', 'glide'):
        package.checked_members(name, getattr(args, name))
    payloads = {name: checked(args.helpers/name, manifests['helpers']['outputs'][name]['sha256'])
                for name in HELPERS}
    payloads.update({
        'DGPUBEN.EXE': checked(args.runner/'DGPUBEN.EXE', manifests['runner']['runner_sha256']),
        'DGSETUP.EXE': checked(args.installer/'DGSETUP.EXE', manifests['installer']['binary_sha256']),
        'dgpugl.dll': args.opengl/'dgpugl.dll',
        'DGWGL.EXE': checked(args.opengl/'dgwgl.exe', manifests['opengl']['outputs']['dgwgl.exe']['sha256']),
        'glide2x.dll': args.glide/'glide2x.dll',
        'DGGLIDE.EXE': checked(args.glide/'DGGLIDE.EXE', manifests['glide']['probe_sha256']),
    })
    payloads.update({name: args.wine/name for name in ('wined3d.dll', 'winedd.dll', 'wined8.dll', 'wined9.dll')})
    for required in ('UT99/DGUT.INI', 'UT99/System/UnrealTournament.exe', 'UT99/System/UCC.exe',
                     'UTD3D/DGD3D.INI', 'UTD3D/D3DDrv.dll'):
        if not (args.base/required).is_file():
            # Windows media may differ only in basename case.
            parent, name = (args.base/required).parent, Path(required).name.lower()
            if not parent.is_dir() or not any(p.name.lower() == name for p in parent.iterdir()):
                raise ValueError(f'Original prepared media lacks {required}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.dg-ut-', dir=args.output.parent) as temporary:
        stage = Path(temporary)/'media'
        stage.mkdir()
        for name in ('UT99', 'UTD3D'):
            for path in (args.base/name).rglob('*'):
                if path.is_symlink():
                    raise ValueError(f'Symlink in immutable original media: {path}')
            shutil.copytree(args.base/name, stage/name)
        for name, source in payloads.items():
            shutil.copyfile(source, stage/name)
        metadata = stage/'DG-META'
        metadata.mkdir()
        for name, manifest in manifests.items():
            (metadata/(name+'.json')).write_text(json.dumps(manifest, indent=2)+'\n')
        (stage/'README.TXT').write_text(
            'Private original UT99 fixture media. No redistribution of game assets.\n'
            'One shared Win98/NT5 setup: install DGSETUP.EXE using the owned runner replacement handshake.\n'
            'PROBE utsetup copies original assets and decompresses maps through original UCC.exe.\n'
            'PROBE utdsetup selects the fixed private D3D module and app-local Wine/DreamGPU providers.\n'
            'PROBE utglide / utd3d use fixed CityIntro, provider identity, foreground and measured phase gates.\n'
            'The helper discovers a unique optical DGUT.INI marker; D: on Win98 and E: on NT both work.\n'
            'Networking must remain disabled. No display drivers or Windows system DLLs are installed.\n')
        inventory = {str(path.relative_to(stage)): digest(path) for path in sorted(stage.rglob('*')) if path.is_file()}
        report = {'schema': 1, 'private_game_assets': True, 'files': inventory,
                  'base': str(args.base), 'runner_identity': manifests['runner']['runner_identity'],
                  'identity': hashlib.sha256(json.dumps(inventory, sort_keys=True).encode()).hexdigest(),
                  'validation': 'Source-build hashes and immutable input inventory; runtime acceptance separate'}
        (stage/'media.json').write_text(json.dumps(report, indent=2)+'\n')
        stage.rename(args.output)
    if args.iso:
        args.iso.parent.mkdir(parents=True, exist_ok=True)
        command = ([iso_tool, '-as', 'mkisofs', '-quiet', '-J', '-joliet-long', '-r', '-iso-level', '3',
                    '-V', 'DREAMGPU_UT99', '-o', str(args.iso), str(args.output)]
                   if Path(iso_tool).name == 'xorriso' else
                   [iso_tool, 'makehybrid', '-iso', '-joliet', '-default-volume-name', 'DREAMGPU_UT99',
                    '-o', str(args.iso), str(args.output)])
        subprocess.run(command, check=True)
        report['iso'] = {'path': str(args.iso), 'sha256': digest(args.iso)}
        (args.output/'iso.json').write_text(json.dumps(report['iso'], indent=2)+'\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('base', 'helpers', 'runner', 'installer', 'opengl', 'wine', 'glide', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--iso', type=Path, help='Build ISO with xorriso or hdiutil; no guest operation')
    args = parser.parse_args()
    try:
        result = assemble(args)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, str(error)+'\n')
    print(json.dumps({key: result[key] for key in ('identity', 'runner_identity')} | {'files': len(result['files'])}, indent=2))

if __name__ == '__main__':
    main()
