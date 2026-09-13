#!/usr/bin/env python3
"""Assemble one hash-checked Windows graphics package for both Juke hosts."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FILES = {
    'nt': ['dgpumini.sys', 'dgpudisp.dll', 'dreamgpu.inf', 'dginst.exe'],
    'win9x': ['dgpumini.drv', 'dgpumini.vxd', 'dg9x.inf'],
    'opengl': ['dgpugl.dll'],
    'wine': ['wined3d.dll', 'winedd.dll', 'wined8.dll', 'wined9.dll',
             'ddraw_98.dll', 'd3d8_98.dll', 'd3d9_98.dll',
             'ddraw_xp.dll', 'd3d8_xp.dll', 'd3d9_xp.dll'],
    'glide': ['glide2x.dll'],
}
LICENSES = {'nt': ['COPYING.txt'], 'win9x': ['LICENSE'], 'wine': ['LICENSE', 'LICENSE.nocrt', 'LICENSE.pthread9x', 'LICENSE.pthread9x-source-notices', 'COPYING.GPL-2.0'],
            'glide': ['COPYING.LGPL-2.1', 'COPYING.SGI-B-2.0']}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_members(component, directory):
    manifest_path = directory/'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    recorded = manifest.get('artifacts', {})
    if component == 'opengl':
        recorded = {name: entry['sha256'] for name, entry in manifest['outputs'].items()}
    elif component == 'glide':
        recorded = {'glide2x.dll': manifest['dll_sha256']}
    for name in FILES[component]:
        path = directory/name
        if path.is_symlink() or not path.is_file() or recorded.get(name) != digest(path):
            raise ValueError(f'{component}/{name}: missing file or build-manifest hash mismatch')
    for name in LICENSES.get(component, []):
        path = directory/name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'{component}/{name}: missing source license notice')
        if component == 'wine' and manifest.get('licenses', {}).get(name) != digest(path):
            raise ValueError(f'{component}/{name}: notice differs from build-manifest hash')
    return manifest



def recipe_inputs(path):
    """Pin component build manifests as well as their individual payloads."""
    recipe = json.loads(path.read_text())
    components = recipe.get('components')
    if recipe.get('schema') != 1 or not isinstance(components, dict) or set(components) != set(FILES):
        raise ValueError('Package recipe must identify exactly nt, win9x, opengl, wine and glide')
    if ('sources_lock_sha256' in recipe and
            recipe['sources_lock_sha256'] != digest(ROOT/'support/guest/sources.lock.json')):
        raise ValueError('Shared source lock differs from the package recipe')
    inputs = {}
    for name, entry in components.items():
        if not isinstance(entry, dict) or set(entry) != {'path', 'manifest_sha256'}:
            raise ValueError(f'{name}: expected path and manifest_sha256')
        directory = Path(entry['path'])
        if not directory.is_absolute():
            directory = ROOT/directory
        manifest = directory/'manifest.json'
        if manifest.is_symlink() or not manifest.is_file() or digest(manifest) != entry['manifest_sha256']:
            raise ValueError(f'{name}: component build manifest differs from the package recipe')
        inputs[name] = directory
    return inputs


def destination(component, name):
    if component in ('nt', 'win9x'):
        return Path('drivers')/('nt5' if component == 'nt' else 'win98')/name
    if name.endswith(('_98.dll', '_xp.dll')):
        # Preserve upstream switchers, without silently deploying system DLL
        # replacements or claiming an unvalidated per-game switcher policy.
        return Path('switchers')/name
    return Path('application')/name


def write_recipe(inputs, path):
    """Record freshly built components without impersonating accepted artifacts."""
    if path.exists():
        raise ValueError('Choose a new recipe path; existing recipes are not overwritten')
    if set(inputs) != set(FILES) or not all(inputs.values()):
        raise ValueError('Recipe creation requires all five component build directories')
    components = {}
    for component, directory in inputs.items():
        directory = directory.resolve()
        checked_members(component, directory)
        try:
            recorded = str(directory.relative_to(ROOT))
        except ValueError:
            recorded = str(directory)
        components[component] = {'path': recorded, 'manifest_sha256': digest(directory/'manifest.json')}
    recipe = {'schema': 1, 'components': components,
              'sources_lock_sha256': digest(ROOT/'support/guest/sources.lock.json'),
              'scope': 'Shared application DLLs and OS-specific display drivers rebuilt from source',
              'status': 'Source-build identities only; existing runtime acceptance is not transferred to new artifacts'}
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x') as stream:
        stream.write(json.dumps(recipe, indent=2)+'\n')
    return recipe


def assemble(inputs, output):
    if output.exists():
        raise ValueError('Choose a new output directory; existing packages are immutable')
    manifests = {key: checked_members(key, value) for key, value in inputs.items()}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.dreamgpu-package-', dir=output.parent) as temporary:
        stage = Path(temporary)/'package'
        stage.mkdir()
        for component, directory in inputs.items():
            for name in FILES[component]:
                target = stage/destination(component, name)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(directory/name, target)
            for name in LICENSES.get(component, []):
                target = stage/'licenses'/component/name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(directory/name, target)
            target = stage/'manifests'/f'{component}.json'
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps(manifests[component], indent=2)+'\n')
        shutil.copyfile(ROOT/'support/guest/sources.lock.json', stage/'sources.lock.json')
        shutil.copytree(ROOT/'LICENSES', stage/'licenses', dirs_exist_ok=True)
        shutil.copyfile(ROOT/'LICENSE', stage/'licenses/DREAMGPU.txt')
        shutil.copyfile(ROOT/'ATTRIBUTION.md', stage/'licenses/ATTRIBUTION.md')
        shutil.copytree(ROOT/'support/attribution', stage/'licenses/attribution')
        (stage/'README.txt').write_text(
            'DreamGPU shared Windows graphics package\n\n'
            'Use drivers/nt5 on Windows2000/XP and drivers/win98 on Windows98.\n'
            'The application/ DLLs are identical on Mac and Linux. Deploy them\n'
            'beside each supported game; do not overwrite Windows system DLLs.\n'
            'OpenGL/Glide use dgpugl.dll directly. Wine uses the same frontend.\n'
            'The switchers/ directory preserves upstream OS-specific wrappers;\n'
            'application-specific redirection must retain native DirectDraw.\n\n'
            'Package assembly validates source-build hashes, not game acceptance.\n'
            'Use the matching DreamGPU validation receipt for OS/game evidence\n'
            'and native binary identities. Receipt acceptance applies only to\n'
            'its recorded artifacts and workloads. No game media is included.\n\n'
            'Corresponding source: pinned sources.lock.json revisions plus the\n'
            'DreamGPU guest sources and build recipes identified\n'
            'by the included component manifests. Retain their license notices.\n')
        files = {str(path.relative_to(stage)): digest(path)
                 for path in sorted(stage.rglob('*')) if path.is_file()}
        identity = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
        manifest = {'schema': 1, 'package': 'dreamgpu-windows', 'identity': identity,
                    'hardware_profile_version': 1, 'native_gpu_ipc_version': 2,
                    'files': files, 'validation': 'Component build hashes; not runtime acceptance'}
        (stage/'package.json').write_text(json.dumps(manifest, indent=2)+'\n')
        stage.rename(output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for component in FILES:
        parser.add_argument('--'+component, type=Path)
    parser.add_argument('--recipe', type=Path, help='Hash-pinned component recipe; paths are repository-relative')
    result = parser.add_mutually_exclusive_group(required=True)
    result.add_argument('--output', type=Path, help='Assemble one new package directory')
    result.add_argument('--write-recipe', type=Path, help='Record new source-build identities from all five explicit component directories')
    args = parser.parse_args()
    try:
        selected = {key: getattr(args, key) for key in FILES}
        if args.write_recipe:
            if args.recipe:
                raise ValueError('New recipes require explicit source-build directories, not an old recipe')
            recipe = write_recipe(selected, args.write_recipe)
            print(json.dumps({'recipe': str(args.write_recipe), 'components': len(recipe['components'])}, indent=2))
            return
        if args.recipe:
            if any(selected.values()):
                raise ValueError('Use a recipe or explicit component directories, not both')
            selected = recipe_inputs(args.recipe)
        elif not all(selected.values()):
            raise ValueError('Supply all five component directories or --recipe')
        result = assemble(selected, args.output)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, str(error)+'\n')
    print(json.dumps({'output': str(args.output), 'identity': result['identity'],
                      'files': len(result['files'])}, indent=2))


if __name__ == '__main__':
    main()
