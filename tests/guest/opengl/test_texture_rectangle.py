#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_TEXTURE_RECTANGLE (ARB_texture_rectangle, NV_texture_rectangle):
actual frontend contract under sanitizers (gating, exact CALL/DATA/QUERY
records within the host's rectangle rules and admitted by the NT validator,
local validation of level, border, size, compressed and depth formats and
GENERATE_MIPMAP, tiles, readback, level queries and attachments), the shared
test shim, and the protocol constants shared with QEMU and the host. The
extension adds no entry points."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"

# No new entry points: the existing 2D records carry the new target.
for filename in ("frontend.def", "icd.def"):
    assert "Rectangle" not in (SOURCE / filename).read_text(), filename
internal = (SOURCE / "internal.h").read_text()
assert "BOOL JglTextureRectangleAvailable(void);" in internal
# The texture test shim mirrors the production rectangle helpers.
shim = (HERE / "texture-internal.h").read_text()
for define in ("JGL_TEXTURE_RECTANGLE 0x84f5", "JGL_PROXY_TEXTURE_RECTANGLE 0x84f7"):
    assert "#define " + define in internal and "#define " + define in shim, define
helper = re.compile(r"static inline BOOL JglRectangleTarget\(GLenum target\) \{.*?\n\}", re.S)
assert helper.search(internal).group(0) == helper.search(shim).group(0)
# The host, QEMU and frontend agree on the protocol.
protocol = (ROOT / "guest/include/gl.h").read_bytes()
assert b"#define DG_CAP_GL_TEXTURE_RECTANGLE 0x10000000\n" in protocol
assert protocol == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_bytes()
bindings = (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()
assert "pub const DG_CAP_GL_TEXTURE_RECTANGLE: u32 = 268435456;" in bindings
qemu = (ROOT / "vendor/qemu/hw/display/dreamgpu.c").read_text()
assert "DG_CAP_GL_TEXTURE_RECTANGLE" in qemu
# Every capability bit stays distinct.
caps = [int(v, 16) for v in re.findall(rb"#define DG_CAP_GL_\w+ (0x[0-9a-f]+)\n", protocol)]
assert len(caps) == len(set(caps)) and all(c & (c - 1) == 0 for c in caps), caps

UNITS = ("frontend.cpp", "arrays.cpp", "buffers.h", "query.cpp", "texture.cpp", "compressed.cpp",
         "fixed.cpp", "readback.cpp", "framebuffer.cpp", "compatibility.cpp", "internal.h",
         "packing.h", "transport.h", "scalar.inc", "names.h", "state.h")
with tempfile.TemporaryDirectory(prefix="dreamgpu-texture-rectangle-") as temporary:
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
    for name in UNITS + ("texture-rectangle.cpp",):
        directory = HERE if name == "texture-rectangle.cpp" else SOURCE
        used |= set(re.findall(r"\bGL_[A-Z0-9_]+\b", (directory / name).read_text()))
    defined = set(re.findall(r"#define (GL_\w+)", header))
    extra = sorted(name for name in used - defined if name in constants)
    header += "\n" + "".join(f"#define {name} {constants[name]}\n" for name in extra)
    (root / "GL/gl.h").write_text(header)
    shutil.copyfile(HERE / "texture-rectangle.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS rectangle texture frontend: gating, records within the host's rules, local "
      "validation, tiles, readback, level queries, attachments and mirrored protocol")
