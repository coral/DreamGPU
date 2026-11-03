#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the glide3x.dll export table against the prototypes that implement it.

A wrong stdcall @N corrupts the caller's stack, so three sources must agree:
the checked .def, the dg3_ C++ prototypes (compiled for i686 Windows with
static_asserts over their real parameter types), and an independent table of
the 3dfx Glide 3.x SDK prototypes (glide.h/glideutl.h argument counts plus the
hidden exports real glide3x.dll also carries). The guest link then uses
--disable-stdcall-fixup, so every _grX@N must resolve to dg3_grX@N exactly."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
GLIDE = ROOT / 'guest/glide'
# Public Glide 3.x API: argument bytes from the 3dfx SDK prototypes.
SDK = {
    'grAADrawTriangle': 24, 'grAlphaBlendFunction': 16, 'grAlphaCombine': 20,
    'grAlphaControlsITRGBLighting': 4, 'grAlphaTestFunction': 4, 'grAlphaTestReferenceValue': 4,
    'grBufferClear': 12, 'grBufferSwap': 4, 'grChromakeyMode': 4, 'grChromakeyValue': 4,
    'grClipWindow': 16, 'grColorCombine': 20, 'grColorMask': 8, 'grConstantColorValue': 4,
    'grCoordinateSpace': 4, 'grCullMode': 4, 'grDepthBiasLevel': 4, 'grDepthBufferFunction': 4,
    'grDepthBufferMode': 4, 'grDepthMask': 4, 'grDepthRange': 8, 'grDisable': 4,
    'grDisableAllEffects': 0, 'grDitherMode': 4, 'grDrawLine': 8, 'grDrawPoint': 4,
    'grDrawTriangle': 12, 'grDrawVertexArray': 12, 'grDrawVertexArrayContiguous': 16,
    'grEnable': 4, 'grErrorSetCallback': 4, 'grFinish': 0, 'grFlush': 0, 'grFogColorValue': 4,
    'grFogMode': 4, 'grFogTable': 4, 'grGet': 12, 'grGetProcAddress': 4, 'grGetString': 4,
    'grGlideGetState': 4, 'grGlideGetVertexLayout': 4, 'grGlideInit': 0, 'grGlideSetState': 4,
    'grGlideSetVertexLayout': 4, 'grGlideShutdown': 0, 'grLfbConstantAlpha': 4,
    'grLfbConstantDepth': 4, 'grLfbLock': 24, 'grLfbReadRegion': 28, 'grLfbUnlock': 8,
    'grLfbWriteColorFormat': 4, 'grLfbWriteColorSwizzle': 8, 'grLfbWriteRegion': 36,
    'grLoadGammaTable': 16, 'grQueryResolutions': 8, 'grRenderBuffer': 4, 'grReset': 4,
    'grSelectContext': 4, 'grSplash': 20, 'grSstOrigin': 4, 'grSstSelect': 4, 'grSstWinClose': 4,
    'grSstWinOpen': 28, 'grStippleMode': 4, 'grStipplePattern': 4, 'grTexCalcMemRequired': 16,
    'grTexClampMode': 12, 'grTexCombine': 28, 'grTexDetailControl': 16, 'grTexDownloadMipMap': 16,
    'grTexDownloadMipMapLevel': 32, 'grTexDownloadMipMapLevelPartial': 40,
    'grTexDownloadTable': 8, 'grTexDownloadTablePartial': 16, 'grTexFilterMode': 12,
    'grTexLodBiasValue': 8, 'grTexMaxAddress': 4, 'grTexMinAddress': 4, 'grTexMipMapMode': 12,
    'grTexMultibase': 8, 'grTexMultibaseAddress': 20, 'grTexNCCTable': 4, 'grTexSource': 16,
    'grTexTextureMemRequired': 8, 'grVertexLayout': 12, 'grViewport': 16, 'gu3dfGetInfo': 8,
    'gu3dfLoad': 8, 'guFogGenerateExp': 8, 'guFogGenerateExp2': 8, 'guFogGenerateLinear': 12,
    'guFogTableIndexToW': 4, 'guGammaCorrectionRGB': 12,
}
# Exported by the real Voodoo glide3x.dll but absent from glide.h.
HIDDEN = {'grCheckForRoom': 4, 'grGlideGetVersion': 4, 'grSetNumPendingBuffers': 4,
          'grSstConfigPipeline': 12}
EXPECTED = SDK | HIDDEN
assert len(SDK) == 93 and len(EXPECTED) == 97


def definition():
    lines = [line.strip() for line in (GLIDE / 'glide3x.def').read_text().splitlines()]
    assert lines[:2] == ['LIBRARY glide3x.dll', 'EXPORTS'], lines[:2]
    exports = {}
    for line in lines[2:]:
        if not line:
            continue
        m = re.fullmatch(r'_(\w+)@(\d+) = dg3_(\w+)@(\d+)', line)
        assert m, f'malformed export: {line}'
        name, bytes_, internal, internal_bytes = m.groups()
        assert name == internal and bytes_ == internal_bytes, line
        assert name not in exports, f'duplicate export {name}'
        exports[name] = int(bytes_)
    assert list(exports) == sorted(exports, key=str.lower), 'exports must stay sorted'
    return exports


def prototypes():
    text = (GLIDE / 'glide3-api.h').read_text()
    found = {}
    for m in re.finditer(r'^DG3_API\((.+?)\) dg3_(\w+)\((.*?)\);$', text, re.M):
        _, name, arguments = m.groups()
        parameters = [] if arguments == 'void' else [a.strip() for a in arguments.split(',')]
        for parameter in parameters:
            assert not re.search(r'\b(double|FxDouble|long long|FxU64|FxI64)\b', parameter), \
                f'{name}: 8-byte parameter {parameter}'
        assert name not in found, f'duplicate prototype {name}'
        found[name] = 4 * len(parameters)
    assert text.count('DG3_API(') - 2 == len(found), 'every DG3_API line must be one prototype'
    return found


exports = definition()
declared = prototypes()
assert exports == EXPECTED, {
    'missing': sorted(set(EXPECTED) - set(exports)),
    'extra': sorted(set(exports) - set(EXPECTED)),
    'bytes': {k: (exports[k], EXPECTED[k]) for k in set(exports) & set(EXPECTED)
              if exports[k] != EXPECTED[k]},
}
assert declared == exports, {k: (declared.get(k), exports.get(k))
                             for k in set(declared) | set(exports)
                             if declared.get(k) != exports.get(k)}

# The C++ types themselves, compiled for the 32-bit Windows ABI.
asserts = '\n'.join(f'static_assert(stdcall_bytes(&dg3_{n}) == {b}, "{n}");'
                    for n, b in exports.items())
harness = f'''#include "glide3-api.h"
template <class R, class... A> constexpr unsigned stdcall_bytes(R(__stdcall *)(A...)) {{
    return (0u + ... + ((sizeof(A) + 3u) & ~3u));
}}
static_assert(sizeof(void *) == 4 && sizeof(Dg3TexInfo) == 20 && sizeof(Dg3LfbInfo) == 20 &&
              sizeof(Dg3Resolution) == 16 && sizeof(Dg3Gu3dfInfo) == 1056, "Glide 3 structures");
{asserts}
'''
compiler = os.environ.get('CXX') or shutil.which('clang++')
if not compiler:
    raise SystemExit('clang++ (or CXX) is required for the i686 prototype gate')
with tempfile.TemporaryDirectory(prefix='dg-glide3-exports-') as temporary:
    work = Path(temporary)
    unix = (ROOT / 'vendor/qemu-xtra/openglide/sdk2_unix.h.in').read_text()
    for name, value in (('UNSIGNED_CHAR', 1), ('UNSIGNED_SHORT', 2), ('UNSIGNED_INT', 4),
                        ('UNSIGNED_LONG', 4), ('UNSIGNED_LONG_LONG', 8), ('INT_P', 4)):
        unix = unix.replace(f'@SIZEOF_{name}@', str(value))
    (work / 'sdk2_unix.h').write_text(unix)
    shutil.copyfile(ROOT / 'vendor/qemu-xtra/openglide/sdk2_3dfx.h', work / 'sdk2_3dfx.h')
    (work / 'test.cpp').write_text(harness)
    subprocess.run([compiler, '--target=i686-w64-windows-gnu', '-std=c++20', '-fsyntax-only',
                    '-Wall', '-Wextra', '-Werror', '-I', str(work), '-I', str(GLIDE),
                    str(work / 'test.cpp')], check=True)

# Packaging keeps the providers separate: glide2x retains its exact export
# table and source list; glide3x links the DreamGPU front end, not g3wrap.
cmake = (ROOT / 'support/guest/cmake/glide.cmake').read_text()
assert 'add_library(glide2x SHARED ${glide_sources} "${DREAMGPU_GLIDE_SOURCE}/Glide2x.def"' in cmake
assert 'list(REMOVE_ITEM glide3_sources "${DREAMGPU_GLIDE_SOURCE}/g3wrap.cpp")' in cmake
assert '"${DREAMGPU_GLIDE_SOURCE}/dg-glide3x.def"' in cmake and '--disable-stdcall-fixup' in cmake
print(f'PASS Glide 3 export ABI: {len(exports)} decorated exports match the 3dfx SDK argument '
      'bytes, the dg3_ prototypes (i686 static_asserts) and the link-exact .def')
