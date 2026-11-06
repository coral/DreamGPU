#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_FRAMEBUFFER_OBJECT: actual frontend contract under sanitizers
(gating, exact CALL/DATA/QUERY records within the host's shapes and admitted by
the NT validator, local validation, per-context framebuffer and shared
renderbuffer names, bindings, deletion batches, draw/read buffers, front-buffer
publication, the attribute stack, display lists, ReadPixels clipping and
DEPTH_STENCIL composition, depth textures and DEPTH_STENCIL pixels), the
exported entry points of both the private frontend and the ICD, the generated
export list, and the protocol constants shared with QEMU and the host."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"

# EXT names only, with stdcall argument sizes; no core aliases.
EXPORTS = {"glIsRenderbufferEXT": 4, "glBindRenderbufferEXT": 8, "glDeleteRenderbuffersEXT": 8,
           "glGenRenderbuffersEXT": 8, "glRenderbufferStorageEXT": 16,
           "glGetRenderbufferParameterivEXT": 12, "glIsFramebufferEXT": 4,
           "glBindFramebufferEXT": 8, "glDeleteFramebuffersEXT": 8, "glGenFramebuffersEXT": 8,
           "glCheckFramebufferStatusEXT": 4, "glFramebufferTexture1DEXT": 20,
           "glFramebufferTexture2DEXT": 20, "glFramebufferTexture3DEXT": 24,
           "glFramebufferRenderbufferEXT": 16, "glGetFramebufferAttachmentParameterivEXT": 16,
           "glGenerateMipmapEXT": 4, "glBlitFramebufferEXT": 40}
FRAMEBUFFER_NAME = re.compile(r"gl\w*(?:Framebuffer|Renderbuffer|GenerateMipmap)\w*$")
for filename in ("frontend.def", "icd.def"):
    exported = dict(re.findall(r"^\s+(\w+)=(\w+@\d+)$", (SOURCE / filename).read_text(), re.M))
    for name, size in EXPORTS.items():
        assert exported[name] == f"{name}@{size}", (filename, name)
    found = {name for name in exported if FRAMEBUFFER_NAME.match(name)}
    assert found == set(EXPORTS), (filename, found ^ set(EXPORTS))
cmake = (ROOT / "support/guest/cmake/opengl.cmake").read_text()
assert re.search(r"^set\(frontend_sources [^)]*\bframebuffer\b", cmake, re.M)
internal = (SOURCE / "internal.h").read_text()
assert "BOOL JglFramebufferObjectAvailable(void);" in internal
compatibility = (SOURCE / "compatibility.cpp").read_text()
# wglGetProcAddress gates every entry point, and only these, on the capability.
prefixes = re.findall(r'\{"(gl\w+)", JglFramebufferObjectAvailable\}', compatibility)
for name in EXPORTS:
    assert any(name.startswith(prefix) for prefix in prefixes), name
for prefix in prefixes:
    assert any(name.startswith(prefix) for name in EXPORTS), prefix
# The texture test shim mirrors the production depth texture helpers.
shim = (HERE / "texture-internal.h").read_text()
for define in ("JGL_DEPTH_STENCIL 0x84f9", "JGL_UNSIGNED_INT_24_8 0x84fa"):
    assert "#define " + define in internal and "#define " + define in shim, define
depth = re.compile(r"static inline BOOL JglDepthInternalFormat\(GLint format\) \{.*?\n\}", re.S)
assert depth.search(internal).group(0) == depth.search(shim).group(0)
# The reviewed generator reproduces the frontend export list.
with tempfile.TemporaryDirectory(prefix="dreamgpu-fbo-exports-") as directory:
    generated = Path(directory)
    generator = generated / "support/guest/opengl/generate.py"
    generator.parent.mkdir(parents=True)
    (generated / "guest/opengl").mkdir(parents=True)
    shutil.copyfile(ROOT / "support/guest/opengl/generate.py", generator)
    subprocess.run([sys.executable, str(generator)], check=True)
    for filename in ("frontend.def", "scalar.inc"):
        assert (generated / "guest/opengl" / filename).read_bytes() == \
            (SOURCE / filename).read_bytes(), filename
# The host, QEMU and frontend agree on the protocol.
protocol = (ROOT / "guest/include/gl.h").read_text()
assert "#define DG_CAP_GL_FRAMEBUFFER_OBJECT 0x2000000" in protocol
assert protocol == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_text()
bindings = (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()
for name in ("DG_CAP_GL_FRAMEBUFFER_OBJECT", "DG_GL_MAX_FRAMEBUFFERS", "DG_GL_MAX_RENDERBUFFERS",
             "DG_GL_MAX_COLOR_ATTACHMENTS", "DG_GL_MAX_RENDERBUFFER_SIZE"):
    value = int(re.search(rf"#define {name} (\w+?)U?\b", protocol).group(1), 0)
    assert f"pub const {name}: u32 = {value};" in bindings, name
qemu = (ROOT / "vendor/qemu/hw/display/dreamgpu.c").read_text()
assert "DG_CAP_GL_FRAMEBUFFER_OBJECT" in qemu
print("PASS framebuffer object exports: EXT names only, in frontend and ICD", flush=True)

UNITS = ("frontend.cpp", "arrays.cpp", "buffers.h", "query.cpp", "texture.cpp", "compressed.cpp",
         "fixed.cpp", "readback.cpp", "framebuffer.cpp", "compatibility.cpp", "icd-pixels.inc",
         "icd-images.inc", "internal.h", "packing.h", "transport.h", "scalar.inc", "names.h",
         "state.h")
with tempfile.TemporaryDirectory(prefix="dreamgpu-framebuffer-object-") as temporary:
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
    for name in UNITS:
        used |= set(re.findall(r"\bGL_[A-Z0-9_]+\b", (SOURCE / name).read_text()))
    defined = set(re.findall(r"#define (GL_\w+)", header))
    extra = sorted(name for name in used - defined if name in constants)
    header += "\n" + "".join(f"#define {name} {constants[name]}\n" for name in extra)
    (root / "GL/gl.h").write_text(header)
    shutil.copyfile(HERE / "framebuffer-object.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS framebuffer object: gating, records, host shapes, validation, names, bindings, "
      "deletion, buffers, front-buffer publication, lists, readback, depth textures and pixels")
