#!/usr/bin/env python3
"""Regenerate native GL ABI types/constants with rust-bindgen and platform headers.

The C table uses actual OpenGL declarations on Apple and epoxy declarations on
Linux. This avoids hand-declared GL function signatures. Generated Rust remains
checked in so ordinary builds need only rustc/cargo, not bindgen or libclang.
"""
from pathlib import Path
import platform
import hashlib
import subprocess

here = Path(__file__).resolve().parent
flags = []
provenance = [
    "// SPDX-License-Identifier: GPL-2.0-or-later",
    "// Generated from crates/dreamgpu-host/bindings/gl-api.h by bindings/generate.py.",
    "// GL vocabulary source: vendor/qemu-3dfx/qemu-1/hw/mesa/mglfunci.h",
    "// @5d40e054a1bc8c4b00c41c533c6b84afd9cdc30b, via guest/include/gl-funcs.h.",
    "// DreamGPU protocol sources: guest/include/{gpu,gl,cursor}.h and include/dreamgpu/transport.h.",
]
if platform.system() == 'Darwin':
    sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
    flags = ['-isysroot', sdk]
    version = subprocess.check_output(['xcrun', '--show-sdk-version'], text=True).strip()
    provenance.append(f'// Platform declarations: Apple macOS SDK {version}; OpenGL framework headers:')
    for name in ('gl.h', 'glext.h'):
        header = Path(sdk) / 'System/Library/Frameworks/OpenGL.framework/Headers' / name
        provenance.append(f'// OpenGL/{name} SHA-256: {hashlib.sha256(header.read_bytes()).hexdigest()}')
else:
    flags = subprocess.check_output(['pkg-config', '--cflags', 'epoxy'], text=True).split()
    version = subprocess.check_output(['pkg-config', '--modversion', 'epoxy'], text=True).strip()
    provenance.append(f'// Platform declarations: libepoxy {version}, epoxy/gl.h and included GL declarations.')
subprocess.run(['bindgen', str(here / 'gl-api.h'), '--use-core',
    '--allowlist-type', 'DreamGpuGlApi', '--allowlist-type', 'mglFuncEnum',
    '--allowlist-var', 'GL_.*', '--allowlist-var', 'DG_.*',
    '--no-prepend-enum-name', '--no-layout-tests', '--no-doc-comments',
    '--output', str(here.parent / 'src/gl_api.rs'),
    *[argument for line in provenance for argument in ('--raw-line', line)], '--', *flags], check=True)
