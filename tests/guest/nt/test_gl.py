#!/usr/bin/env python3
"""Run the actual NT GL validators/cache/window ownership with address and undefined sanitizers."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().parent

with tempfile.TemporaryDirectory(prefix="dreamgpu-nt-gl-") as directory:
    for name in ("gl-validate", "gl-signature", "gl-limits", "gl-registers", "window"):
        binary = Path(directory) / name
        cpp = name == "window"
        source = SOURCE / (name + (".cpp" if cpp else ".c"))
        language = ["-std=c++23", "-fno-exceptions", "-fno-rtti"] if cpp else ["-std=c11"]
        analysis = os.environ.get("DREAMGPU_STATIC_ANALYSIS") == "1"
        analyzer = ["--analyze", "-Xanalyzer", "-analyzer-output=text", "-Xanalyzer", "-analyzer-werror"] if analysis else []
        subprocess.run(shlex.split(os.environ.get("CXX" if cpp else "CC", "c++" if cpp else "cc")) + language + analyzer + [ "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(ROOT / "guest/nt/include"),
            "-I", str(ROOT / "guest/include"),
            "-I", str(ROOT / "vendor/qemu/include/standard-headers/dreamgpu"),
            str(source), "-o", str(binary),
        ], check=True)
        if not analysis:
            subprocess.run([str(binary)], check=True)
