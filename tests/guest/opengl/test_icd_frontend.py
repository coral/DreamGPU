#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Join actual Drv* entry points to actual frontend ownership/transport code."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"
with tempfile.TemporaryDirectory(prefix="dreamgpu-icd-frontend-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "internal.h", "packing.h", "transport.h", "scalar.inc", "names.h", "state.h", "icd-slots.inc"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-test.cpp")
    shutil.copyfile(HERE / "icd-frontend.cpp", root / "test.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    source = (SOURCE / "icd.cpp").read_text()
    # Compile unchanged production types and entire Drv* bodies. The separate
    # 336-slot test checks every dispatch initializer; this test substitutes
    # only its GL function contents to focus on the real context boundary.
    types = source[source.index("struct Dispatch final {"):source.index("static void APIENTRY glIcdFinish")]
    callback = source[source.index("using SetTable ="):source.index("using SetTable =") + len("using SetTable = void(APIENTRY *)(const ClientTable *);")]
    assert callback.endswith(";")
    body = source[source.index("BOOL WINAPI DrvValidateVersion"):]
    (root / "actual-icd-context.inc").write_text(
        types + "\nstatic const ClientTable Table = {336, {}};\n" + callback + '\nextern "C" {\n' + body)
    command = [os.environ.get("CXX", "c++"), "-O2", "-std=c++23", "-fno-exceptions", "-fno-rtti",
               "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
               "-I" + str(root), "-I" + str(ROOT / "guest/include"), "-I" + str(ROOT / "guest/nt/include"),
               str(root / "test.cpp"), "-o", str(root / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(root / "test")], check=True)
