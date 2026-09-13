#!/usr/bin/env python3
"""Compile the actual frontend with mock transport and sanitizers, then test packing."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="jgl-texture-") as temporary:
    root = Path(temporary)
    # A local test shim replaces only platform/transport declarations; packing
    # compiles from the same unmodified source used in the Windows frontend.
    shutil.copyfile((HERE.parents[2] / "guest/opengl") / "texture.cpp", root / "texture.cpp")
    shutil.copyfile(HERE / "texture-internal.h", root / "internal.h")
    shutil.copyfile(HERE / "texture.cpp", root / "test.cpp")
    command = [os.environ.get("CXX", "c++"), "-O2", "-std=c++23", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
               "-I" + str((HERE.parents[2] / "guest") / "include"),
               str(root / "texture.cpp"), str(root / "test.cpp"), "-o", str(root / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(root / "test")], check=True)
