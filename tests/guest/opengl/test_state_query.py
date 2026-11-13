#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Local IsEnabled/Get* answers (guest/opengl/state.h) through the actual
frontend against a native-GL reference model, under sanitizers. Every
invalidation is load-bearing: each mutation below must make the suite fail."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"
NAMES = ("frontend.cpp", "arrays.cpp", "buffers.h", "query.cpp", "icd-lists.inc", "internal.h",
         "packing.h", "transport.h", "scalar.inc", "names.h", "state.h")

# (file, original, replacement): each removes one rule the cache depends on.
MUTATIONS = {
    "pop-attrib-keeps-state": ("frontend.cpp", """    --c->Arrays.ServerDepth;
    /* Pop may restore any subset of these values. Invalidate conservatively
     * instead of shadowing another server attribute stack. */
    ZeroMemory(&c->State, sizeof(c->State));""", """    --c->Arrays.ServerDepth;"""),
    "list-pop-attrib-keeps-state": ("frontend.cpp", """        if (c->Arrays.ListMode != GL_COMPILE) {
            ZeroMemory(&c->State, sizeof(c->State));""", """        if (c->Arrays.ListMode != GL_COMPILE) {"""),
    "call-list-keeps-state": ("frontend.cpp", """        return;
    ZeroMemory(&c->State, sizeof(c->State));
    /* A list may begin queries""", """        return;
    /* A list may begin queries"""),
    "list-mode-keeps-state": ("frontend.cpp", """    if (!mode)
        ZeroMemory(&c->State, sizeof(c->State));""", ""),
    "active-texture-keeps-unit-enables": ("state.h", """        cache->EnableKnown[0] &= ~JGL_UNIT_ENABLES;""", ""),
    "initial-dither-disabled": ("state.h", """    cache->EnableValue[0] = JGL_DITHER_ENABLE;""", ""),
    "initial-color-sum-without-extension": ("frontend.cpp", """    StateInitial(&c->State, SecondarySupported);""",
                                            """    StateInitial(&c->State, TRUE);"""),
    "viewport-unclamped": ("state.h", """    const LONG limit = DG_GL_MAX_DIMENSION;""", """    const LONG limit = 0x7fffffff;"""),
    "viewport-not-tracked": ("state.h", """        cache->ViewportKnown = ViewportExact((const LONG *)arguments);""",
                             """        (void)ViewportExact;"""),
    "deferred-fault-answered": ("frontend.cpp", """c->InBegin || c->DeferredFault || c->Arrays.ListMode)""",
                                """c->InBegin || c->Arrays.ListMode)"""),
}


def build(root, expect_pass):
    executable = root / "test"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g", "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"), "-o", str(executable)],
                   check=True)
    result = subprocess.run([str(executable)], capture_output=not expect_pass, text=True)
    if expect_pass:
        result.check_returncode()
        return None
    assert result.returncode, "mutation not rejected"
    lines = (result.stdout + result.stderr).strip().splitlines()
    return next((line for line in lines if "Assertion" in line), lines[-1] if lines else "(abort)")


with tempfile.TemporaryDirectory(prefix="dreamgpu-state-query-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in NAMES:
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "state-query.cpp", root / "test.cpp")
    build(root, True)
    for name, (file, old, new) in MUTATIONS.items():
        text = (SOURCE / file).read_text()
        assert text.count(old) == 1, name
        (root / file).write_text(text.replace(old, new))
        print(f"{name} rejected: {build(root, False)}")
        shutil.copyfile(SOURCE / file, root / file)
    print(f"PASS all {len(MUTATIONS)} state-query mutations are rejected")
