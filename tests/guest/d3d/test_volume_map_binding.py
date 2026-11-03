#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0036 on actual pinned Wine source: volumes get a SYSMEM map binding.

This wine9x tree comments out resource_init's default map binding; surfaces
set their own, volumes did not. With a zero binding, wined3d_volume_map
invalidated WINED3D_LOCATION_SYSMEM along with every other location, so the
data an application wrote through LockBox was lost and the next texture load
found no valid location ("Implement texture loading from 0") and sampled an
undefined (black) texture. Fails without the patch.
"""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / 'support/guest/d3d/patches/base/manifest.json'


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]


with tempfile.TemporaryDirectory(prefix='dreamgpu-volume-binding-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    resource = (work / 'wined3d/resource.c').read_text()
    volume = (work / 'wined3d/volume.c').read_text()

    # The precondition the patch exists for: resource_init sets no default binding.
    init = function(resource, 'HRESULT resource_init(')
    assert not re.search(r'^\s*resource->map_binding\s*=', init, re.M), 'resource_init now sets a binding'

    vinit = function(volume, 'static HRESULT volume_init(')
    sysmem = vinit.index('volume->resource.map_binding = WINED3D_LOCATION_SYSMEM;')
    assert vinit.index('resource_init(') < sysmem, 'binding must be set after resource_init'
    # The dynamic default-pool PBO override still wins afterwards.
    assert sysmem < vinit.index('volume->resource.map_binding = WINED3D_LOCATION_BUFFER;')

    # Map invalidates everything but the binding, so the binding must be SYSMEM
    # for the system-memory branch to keep the data it just exposed.
    vmap = function(volume, 'HRESULT wined3d_volume_map(')
    assert 'wined3d_volume_invalidate_location(volume, ~volume->resource.map_binding);' in vmap

print('PASS prepared volume map binding: SYSMEM set after resource_init, PBO override preserved')
