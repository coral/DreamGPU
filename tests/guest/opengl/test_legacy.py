#!/usr/bin/env python3
"""Check actual legacy entrypoints and pixel packing with bounded mock transport."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="jgl-legacy-") as temporary:
    root = Path(temporary)
    for name in ("compatibility.cpp", "readback.cpp", "packing.h"):
        shutil.copyfile((HERE.parents[2] / "guest/opengl") / name, root / name)
    shutil.copyfile(HERE / "legacy-internal.h", root / "internal.h")
    shutil.copyfile(HERE / "texture-internal.h", root / "texture-internal.h")
    shutil.copyfile(HERE / "legacy.c", root / "test.cpp")
    command = [os.environ.get("CXX", "c++"), "-O2", "-std=c++23", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
               "-I" + str((HERE.parents[2] / "guest") / "include"),
               str(root / "test.cpp"), "-o", str(root / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(root / "test")], check=True)
