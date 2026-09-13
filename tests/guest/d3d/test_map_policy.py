#!/usr/bin/env python3
"""Actual Wine CPU-map policy, using constants from the pinned donor headers."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
headers = '\n'.join((ROOT / p).read_text() for p in (
    'vendor/wine9x/wined3d/wined3d_private.h', 'vendor/wine9x/include/wine/wined3d.h'))
names = ['WINED3D_LOCATION_' + x for x in ('SYSMEM', 'USER_MEMORY', 'DIB', 'BUFFER')]
names += ['WINED3DUSAGE_DEPTHSTENCIL']
defines = '\n'.join(re.search(r'^#define ' + name + r'\s+0x[0-9a-fA-F]+', headers, re.M)[0] for name in names)
code = defines + r'''
#include <cassert>
#include "wine-map-policy.h"
int main() {
    for (unsigned binding = 0; binding < 512; ++binding)
        for (unsigned locations = 0; locations < 512; ++locations)
            for (unsigned usage = 0; usage < 16; ++usage) {
                bool cpu = binding == WINED3D_LOCATION_SYSMEM ||
                           binding == WINED3D_LOCATION_USER_MEMORY ||
                           binding == WINED3D_LOCATION_DIB;
                bool expected = cpu && (locations & binding) &&
                                !(usage & WINED3DUSAGE_DEPTHSTENCIL);
                assert(bool(dg_wine_cpu_map_is_current(usage, locations, binding)) == expected);
            }
    assert(!dg_wine_cpu_map_is_current(0, ~0u, ~0u));
    assert(!dg_wine_cpu_map_is_current(0, ~0u, 0x80000000u));
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-map-policy-') as tmp:
    source = Path(tmp) / 'test.cpp'; source.write_text(code)
    exe = Path(tmp) / 'test'
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++23', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(ROOT / 'guest/d3d'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS actual CPU map policy: current/stale CPU, GPU/PBO, depth/stencil, invalid bindings')
