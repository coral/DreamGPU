#!/usr/bin/env python3
"""Exercise the actual WGL source against a strict, fault-injected transport."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="jgl-lifecycle-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "internal.h", "packing.h", "transport.h", "scalar.inc", "names.h", "state.h"):
        shutil.copyfile((HERE.parents[2] / "guest/opengl") / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "test.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    command = [os.environ.get("CXX", "c++"), "-O2", "-std=c++23", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
               "-I" + str(root), "-I" + str((HERE.parents[2] / "guest") / "include"),
               "-I" + str((HERE.parents[2] / "guest") / "nt/include"),
               str(root / "test.cpp"), "-o", str(root / "test")]
    analysis = os.environ.get("DREAMGPU_STATIC_ANALYSIS") == "1"
    if analysis:
        command += ["--analyze", "-Xanalyzer", "-analyzer-output=text", "-Xanalyzer", "-analyzer-werror"]
    subprocess.run(command, check=True)
    if not analysis:
        subprocess.run([str(root / "test")], check=True)
