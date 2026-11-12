#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP2_GL_RGB565 (exact R5G6B5 storage), the second feature of the second
capability word: actual frontend contract under sanitizers (negotiation from
the CAPABILITIES reply and the unchanged frontend without it, no extension
name or ARB_ES2_compatibility entry point, exact RGB565 image, proxy, copy and
renderbuffer records within the host's rules and admitted by the NT validator,
local errors without the bit and display lists), and the bit's protocol shared
with QEMU and the host."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"

# No new entry points: the existing records carry the format.
for filename in ("frontend.def", "icd.def"):
    text = (SOURCE / filename).read_text()
    for name in ("ReleaseShaderCompiler", "ShaderBinary", "ShaderPrecision", "DepthRangef",
                 "ClearDepthf"):
        assert name not in text, (filename, name)
internal = (SOURCE / "internal.h").read_text()
assert "BOOL JglRgb565Available(void);" in internal
assert "BOOL JglRgb565Available(void);" in (HERE / "texture-internal.h").read_text()
protocol = (ROOT / "guest/include/gl.h").read_bytes()
assert protocol == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_bytes()
for define in (b"#define DG_CAP2_GL_RGB565 0x2\n", b"#define DG_GL_RGB565 0x8d62\n"):
    assert define in protocol, define
bindings = (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()
for line in ("pub const DG_CAP2_GL_RGB565: u32 = 2;", "pub const DG_GL_RGB565: u32 = 36194;"):
    assert line in bindings, line
qemu = (ROOT / "vendor/qemu/hw/display/dreamgpu.c").read_text()
assert re.search(r"case DG_GL_REG_CAPS2:\s*\n(?:\s*/\*.*\*/\n)?\s*return s->gpu_socket && "
                 r"\*s->gpu_socket\s*\?\s*DG_CAP2_GL_DEPTH_TEXTURE \| DG_CAP2_GL_RGB565\s*:\s*0;",
                 qemu)
# No extension name names the capability.
assert "565" not in (HERE / "extension-names.h").read_text()

UNITS = ("frontend.cpp", "arrays.cpp", "buffers.h", "query.cpp", "texture.cpp", "compressed.cpp",
         "fixed.cpp", "readback.cpp", "framebuffer.cpp", "compatibility.cpp", "internal.h",
         "packing.h", "transport.h", "scalar.inc", "names.h", "state.h")
with tempfile.TemporaryDirectory(prefix="dreamgpu-rgb565-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in UNITS:
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "extension-names.h", root / "extension-names.h")
    header = (HERE / "frontend-gl.h").read_text()
    constants = dict(re.findall(r"pub const (GL_\w+): [^=]+ = (\d+);", bindings))
    used = set()
    for name in UNITS + ("rgb565.cpp",):
        directory = HERE if name == "rgb565.cpp" else SOURCE
        used |= set(re.findall(r"\bGL_[A-Z0-9_]+\b", (directory / name).read_text()))
    defined = set(re.findall(r"#define (GL_\w+)", header))
    extra = sorted(name for name in used - defined if name in constants)
    header += "\n" + "".join(f"#define {name} {constants[name]}\n" for name in extra)
    (root / "GL/gl.h").write_text(header)
    shutil.copyfile(HERE / "rgb565.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS RGB565 frontend: second-word negotiation, unchanged without it, no extension name, "
      "image/proxy/copy/renderbuffer records within the host's rules, list capture and mirrored "
      "protocol")
