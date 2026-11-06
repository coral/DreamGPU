#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP2_GL_DEPTH_TEXTURE (ARB_depth_texture, ARB_shadow and, with
ARB_fragment_program, ARB_fragment_program_shadow), the first feature of the
second capability word: actual frontend contract under sanitizers (negotiation
from the CAPABILITIES reply and the unchanged frontend without it, local
validation of the compare and depth texture mode values, exact 1D, 2D and
rectangle depth image, proxy, subimage and copy records within the host's rules
and admitted by the NT validator, depth readback and deferred list errors), the
shared test shim, and the second word's protocol shared with QEMU, the kernels
and the host. The extensions add no entry points."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"

# No new entry points: the existing records carry depth images and parameters.
for filename in ("frontend.def", "icd.def"):
    text = (SOURCE / filename).read_text()
    assert "Shadow" not in text and "DepthTexture" not in text, filename
internal = (SOURCE / "internal.h").read_text()
assert "BOOL JglDepthTextureAvailable(void);" in internal
assert "BOOL JglDepthTextureAvailable(void);" in (HERE / "texture-internal.h").read_text()
# The host, QEMU, both kernels and the frontend agree on the second word.
protocol = (ROOT / "guest/include/gl.h").read_bytes()
assert protocol == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_bytes()
for define in (b"#define DG_GL_REG_CAPS2 0x1194\n", b"#define DG_CAP2_GL_DEPTH_TEXTURE 0x1\n",
               b"#define DG_GL_TEXTURE_READ_DEPTH 0x4000\n",
               b"#define DG_GL_TEXTURE_READ_LEVEL_MASK 0x3fff\n"):
    assert define in protocol, define
registers = [int(v, 16) for v in re.findall(rb"#define DG_GL_REG_\w+ (0x[0-9a-f]+)\n", protocol)]
assert len(registers) == len(set(registers)), "GL registers overlap"
bindings = (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()
for line in ("pub const DG_GL_REG_CAPS2: u32 = 4500;", "pub const DG_CAP2_GL_DEPTH_TEXTURE: u32 = 1;",
             "pub const DG_GL_TEXTURE_READ_DEPTH: u32 = 16384;",
             "pub const DG_GL_TEXTURE_READ_LEVEL_MASK: u32 = 16383;"):
    assert line in bindings, line
qemu = (ROOT / "vendor/qemu/hw/display/dreamgpu.c").read_text()
assert re.search(r"case DG_GL_REG_CAPS2:\s*\n(?:\s*/\*.*\*/\n)?\s*return s->gpu_socket && \*s->gpu_socket \? DG_CAP2_GL_DEPTH_TEXTURE : 0;", qemu)
assert "reply.DeviceError = t->GlRegisters.Caps2;" in (ROOT / "guest/nt/miniport/transport.cpp").read_text()
assert "reply.DeviceError = dg_gl_caps2;" in (ROOT / "guest/win9x/channel32.cpp").read_text()
# Every second-word bit stays distinct; no first-word bit moved.
caps2 = [int(v, 16) for v in re.findall(rb"#define DG_CAP2_GL_\w+ (0x[0-9a-f]+)\n", protocol)]
assert caps2 and len(caps2) == len(set(caps2)) and all(c & (c - 1) == 0 for c in caps2), caps2
caps = [int(v, 16) for v in re.findall(rb"#define DG_CAP_GL_\w+ (0x[0-9a-f]+)\n", protocol)]
assert max(caps) == 0x10000000 and len(caps) == len(set(caps)), caps

UNITS = ("frontend.cpp", "arrays.cpp", "buffers.h", "query.cpp", "texture.cpp", "compressed.cpp",
         "fixed.cpp", "readback.cpp", "framebuffer.cpp", "compatibility.cpp", "internal.h",
         "packing.h", "transport.h", "scalar.inc", "names.h", "state.h")
with tempfile.TemporaryDirectory(prefix="dreamgpu-depth-texture-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in UNITS:
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "extension-names.h", root / "extension-names.h")
    # The fixture's GL header gains the GL 1.1 names these sources use, with
    # the host's values.
    header = (HERE / "frontend-gl.h").read_text()
    constants = dict(re.findall(r"pub const (GL_\w+): [^=]+ = (\d+);", bindings))
    used = set()
    for name in UNITS + ("depth-texture.cpp",):
        directory = HERE if name == "depth-texture.cpp" else SOURCE
        used |= set(re.findall(r"\bGL_[A-Z0-9_]+\b", (directory / name).read_text()))
    defined = set(re.findall(r"#define (GL_\w+)", header))
    extra = sorted(name for name in used - defined if name in constants)
    header += "\n" + "".join(f"#define {name} {constants[name]}\n" for name in extra)
    (root / "GL/gl.h").write_text(header)
    shutil.copyfile(HERE / "depth-texture.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS depth texture frontend: second-word negotiation, unchanged without it, parameter "
      "values, depth image/proxy/copy records within the host's rules, readback, list errors and "
      "mirrored protocol")
