#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP2_GL_CORE_1_2 (the GL 1.2 core names), the fourth feature of the
second capability word: actual frontend contract under sanitizers (GL_VERSION
1.2 only with every GL 1.2 feature negotiated and 1.1 otherwise, the LOD clamp
records and queries with the bit and local errors without it, DrawRangeElements
by lookup with 1.2 only and as the records of DrawElements, no new extension
name), the export lists, and the bit's protocol shared with QEMU and the host."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"

for filename in ("frontend.def", "icd.def"):
    text = (SOURCE / filename).read_text()
    assert "    glDrawRangeElements=glDrawRangeElements@24\n" in text, filename
    assert "glDrawRangeElementsEXT" not in text, filename
assert "exports['glDrawRangeElements']=24" in (ROOT / "support/guest/opengl/generate.py").read_text()
assert '{"glDrawRangeElements", JglCore12}' in (SOURCE / "compatibility.cpp").read_text()
assert "draw_range_elements" not in (HERE / "extension-names.h").read_text()
protocol = (ROOT / "guest/include/gl.h").read_bytes()
assert protocol == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_bytes()
assert b"#define DG_CAP2_GL_CORE_1_2 0x8\n" in protocol
bindings = (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()
assert "pub const DG_CAP2_GL_CORE_1_2: u32 = 8;" in bindings
qemu = (ROOT / "vendor/qemu/hw/display/dreamgpu.c").read_text()
assert re.search(r"case DG_GL_REG_CAPS2:\s*\n(?:\s*/\*.*\*/\n)?\s*return s->gpu_socket && "
                 r"\*s->gpu_socket\s*\?[^:;]*\bDG_CAP2_GL_CORE_1_2\b[^:;]*:\s*0;", qemu)
validation = (ROOT / "crates/dreamgpu-host/src/gl_validation.rs").read_text()
for name in ("GL_TEXTURE_MIN_LOD | GL_TEXTURE_MAX_LOD => 1", "| GL_RESCALE_NORMAL",
             "| GL_MAX_ELEMENTS_VERTICES", "| GL_MAX_ELEMENTS_INDICES"):
    assert name in validation, name

UNITS = ("frontend.cpp", "arrays.cpp", "buffers.h", "query.cpp", "texture.cpp", "compressed.cpp",
         "fixed.cpp", "readback.cpp", "framebuffer.cpp", "compatibility.cpp", "internal.h",
         "packing.h", "transport.h", "scalar.inc", "names.h", "state.h")
with tempfile.TemporaryDirectory(prefix="dreamgpu-core-1-2-") as temporary:
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
    for name in UNITS + ("core-1-2.cpp",):
        directory = HERE if name == "core-1-2.cpp" else SOURCE
        used |= set(re.findall(r"\bGL_[A-Z0-9_]+\b", (directory / name).read_text()))
    defined = set(re.findall(r"#define (GL_\w+)", header))
    extra = sorted(name for name in used - defined if name in constants)
    header += "\n" + "".join(f"#define {name} {constants[name]}\n" for name in extra)
    (root / "GL/gl.h").write_text(header)
    shutil.copyfile(HERE / "core-1-2.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS GL 1.2 core frontend: version 1.2 only with every 1.2 feature, LOD clamp records and "
      "queries with the bit and local errors without, DrawRangeElements as DrawElements by lookup "
      "with 1.2 only, no extension name and mirrored protocol")
