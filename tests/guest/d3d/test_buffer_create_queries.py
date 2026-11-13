#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0065 on actual pinned Wine source: WineD3D creates buffer objects
without glGetError round trips.

A DreamGPU glGetError waits for the host to execute everything queued before
it. Thief: Deadly Shadows creates an 84-byte vertex buffer and a 6-byte index
buffer every frame after its scene; the four queries per buffer (the clearing
loop and the checks after glGenBuffers, glBindBuffer and glBufferData) made the
guest wait for the host to finish the frame so far, once per frame. Buffer
names and bindings are guest-local in DreamGPU, so a zero name is the only
creation failure WineD3D can see synchronously; glBufferData errors stay
pending, as on a native driver.
The check fails without the patch.
"""
import importlib.util
import json
from pathlib import Path
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


with tempfile.TemporaryDirectory(prefix='dreamgpu-buffer-create-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    buffer = (work / 'wined3d/buffer.c').read_text()

    create = function(buffer, 'static void buffer_create_buffer_object(')
    assert 'p_glGetError(' not in create, 'buffer creation still queries GL errors'
    gen = create.index('GL_EXTCALL(glGenBuffers(1, &This->buffer_object));')
    check = create.index('if (!This->buffer_object)', gen)
    assert check < create.index('goto fail;', check) < create.index('GL_EXTCALL(glBindBuffer(')
    data = create.index('GL_EXTCALL(glBufferData(This->buffer_type_hint, This->resource.size,')
    assert create.index('This->buffer_object_usage = gl_usage;') > data
    # The fallback path stays reachable for a failed name and still cleans up.
    fail = create.index('fail:')
    assert create.index('delete_gl_buffer(This, gl_info);', fail) < create.index('buffer_clear_dirty_areas(This);', fail)
    # Only startup format probing queries errors elsewhere; no other buffer
    # path may reintroduce a per-creation round trip.
    assert 'p_glGetError(' not in buffer, 'buffer.c queries GL errors'

print('PASS prepared buffer creation: no glGetError round trips, zero-name fallback kept')
