#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the prepared ARB shader-constant dirtying under ASan/UBSan.

Patch 0012 clears Wine's current context at the outermost release, so an
application's SetPixelShaderConstantF/SetVertexShaderConstantF between draws
runs with no context current. The ARB backend's dirty arrays belong to the
device; marking them only when a context was current dropped those updates and
left the previous constants loaded. With patch 0030 removed, this test fails.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / 'support/guest/d3d/patches/base/manifest.json'
PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int UINT;
typedef unsigned int DWORD;
typedef int BOOL;
#define max(a, b) ((a) > (b) ? (a) : (b))
#define WINED3D_SHADER_CONST_VS_F 0x1
#define WINED3D_SHADER_CONST_PS_F 0x2
struct wined3d_device;
struct wined3d_swapchain { struct wined3d_device *device; };
struct wined3d_context { struct wined3d_swapchain *swapchain; DWORD constant_update_mask; };
struct shader_arb_priv {
    char vshader_const_dirty[256], pshader_const_dirty[256];
    UINT highest_dirty_vs_const, highest_dirty_ps_const;
};
struct wined3d_device {
    struct shader_arb_priv *shader_priv;
    struct wined3d_context **contexts;
    UINT context_count;
};
static struct wined3d_context *current;
static struct wined3d_context *context_get_current(void) { return current; }
'''
TEST = r'''
int main(void)
{
    struct shader_arb_priv priv;
    struct wined3d_device device, other;
    struct wined3d_swapchain swapchain = {&device}, other_swapchain = {&other};
    struct wined3d_context context = {&swapchain, 0}, foreign = {&other_swapchain, 0};
    struct wined3d_context *contexts[1] = {&context};
    (void)context_get_current;
    memset(&priv, 0, sizeof(priv));
    device.shader_priv = &priv;
    device.contexts = contexts;
    device.context_count = 1;
    /* No context current (after the outermost release): still dirty. */
    current = NULL;
    shader_arb_update_float_pixel_constants(&device, 3, 2);
    assert(priv.pshader_const_dirty[3] && priv.pshader_const_dirty[4] && !priv.pshader_const_dirty[5]);
    assert(priv.highest_dirty_ps_const == 5 && (context.constant_update_mask & WINED3D_SHADER_CONST_PS_F));
    shader_arb_update_float_vertex_constants(&device, 10, 1);
    assert(priv.vshader_const_dirty[10] && priv.highest_dirty_vs_const == 11);
    assert(context.constant_update_mask & WINED3D_SHADER_CONST_VS_F);
    /* Another device's context current: this device's arrays still dirty. */
    current = &foreign;
    shader_arb_update_float_pixel_constants(&device, 0, 1);
    assert(priv.pshader_const_dirty[0] && priv.highest_dirty_ps_const == 5);
    /* Own context current: unchanged behaviour. */
    current = &context;
    shader_arb_update_float_pixel_constants(&device, 20, 4);
    assert(priv.pshader_const_dirty[23] && priv.highest_dirty_ps_const == 24);
    puts("PASS prepared ARB constant dirtying: marked with no context, a foreign context or the own context current");
    return 0;
}
'''


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-arb-constants-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    source = (work / 'wined3d/arb_program_shader.c').read_text()
    body = function(source, 'static void shader_arb_update_float_vertex_constants(') + \
        function(source, 'static void shader_arb_update_float_pixel_constants(')
    (work / 'test.c').write_text(PREAMBLE + body + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    sys.exit(subprocess.run([str(work / 'test')]).returncode)
