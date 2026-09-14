"""Prepare pinned WineD3D translation sources using reviewed exact patches."""
import importlib.util
import json
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PATCHES = ROOT / "support/guest/d3d/patches"
_spec = importlib.util.spec_from_file_location('dreamgpu_checked_patches', HERE / 'patches.py')
patches = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(patches)


def prepare(work, output, copy_diagnostics=False):
    work, output = Path(work), Path(output)
    base = PATCHES / 'base/manifest.json'
    evidence = {'base': patches.apply(work, base)}
    shutil.copy2(ROOT / 'guest/d3d/wine-diagnostics.h', work / 'ddraw/dg-wine-diagnostics.h')
    shutil.copy2(ROOT / 'guest/d3d/wine-blit-usage.h', work / 'wined3d/dg-wine-blit-usage.h')
    for name in ('row-copy', 'map-policy', 'vertex-pack', 'vertex-array'):
        shutil.copy2(ROOT / ('guest/d3d/wine-' + name + '.h'),
                     work / ('wined3d/dg-wine-' + name + '.h'))
    for directory in ('ddraw', 'wined3d'):
        shutil.copy2(ROOT / 'guest/d3d/wine-display-timing.h', work / directory / 'dg-wine-display-timing.h')
        for header in ('display-timing.h', 'gpu.h'):
            shutil.copy2(ROOT / 'guest/include' / header, work / directory / header)
    shutil.copyfile(work / 'ddraw/surface.c', output / 'surface-diagnostic.c')
    sources = {}
    if copy_diagnostics:
        diagnostic = PATCHES / 'diagnostic/manifest.json'
        evidence['diagnostic'] = patches.apply(work, diagnostic)
        sources = json.loads(diagnostic.read_text())['source_ids']
        shutil.copy2(ROOT / 'guest/d3d/wine-copy-diagnostics.h', work / 'wined3d/dg-wine-copy-diagnostics.h')
        for path in sorted((work / 'wined3d').glob('*.c')):
            if 'dg_surface_load_location(' in path.read_text():
                shutil.copyfile(path, output / ('copy-diagnostic-' + path.name))
    return {
        'patches': evidence,
        'copy_sources': sources,
        'inputs': {str(path.relative_to(ROOT)): patches.digest(path)
                   for path in [Path(__file__), HERE / 'patches.py',
                                *sorted(PATCHES.rglob('*.json')),
                                *sorted(PATCHES.rglob('*.patch'))]},
    }
