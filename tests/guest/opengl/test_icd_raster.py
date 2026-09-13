#!/usr/bin/env python3
"""Actual raster adapters: exact type conversion/defaults and bounded pointer reads."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
source = r'''#define JglCommandError JglSetError
#define JglCommandReady JglReady
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#define APIENTRY
using GLdouble=double; using GLfloat=float; using GLint=int32_t; using GLshort=int16_t;
constexpr unsigned FEnum_glRasterPos4d=1941, GL_INVALID_OPERATION=0x502;
static bool ready=true; static unsigned error=0, calls=0; static double result[4];
static void JglSetError(unsigned e) { error=e; }
static bool JglReady() { if (!ready) error=GL_INVALID_OPERATION; return ready; }
static void JglScalarVector(unsigned f, unsigned n, const void *v) {
 assert(f==FEnum_glRasterPos4d && n==8); ++calls; memcpy(result,v,32);
}
#include "icd-raster.inc"
int main() {
'''
for n in (2,3,4):
    for suffix,typ in (("d","GLdouble"),("f","GLfloat"),("i","GLint"),("s","GLshort")):
        values = {"d":["1.0000000000000002", "-0.0", "-0.25", "2.0"],
                  "f":["0.25f", "-0.0f", "-0.75f", "2.0f"],
                  "i":["2147483647", "(-2147483647-1)", "-1", "2"],
                  "s":["32767", "-32768", "-1", "2"]}[suffix][:n]
        expected = values + (["0.0"] if n==2 else []) + (["1.0"] if n<4 else [])
        check = "".join(f"assert(result[{i}]==static_cast<double>({v}));" for i,v in enumerate(expected))
        source += f"AliasRasterPos{n}{suffix}({','.join(values)});{check}\n"
        source += f"{{ auto *v=new {typ}[{n}]{{{','.join(values)}}}; AliasRasterPos{n}{suffix}v(v); delete[] v; {check} }}\n"
        source += f"{{unsigned before=calls; AliasRasterPos{n}{suffix}v(nullptr); assert(calls==before && error==GL_INVALID_OPERATION);}}\n"
        source += f"ready=false; {{unsigned before=calls; AliasRasterPos{n}{suffix}v(reinterpret_cast<const {typ}*>(1)); assert(calls==before);}} ready=true;\n"
source += "assert(calls==24); ready=false; AliasRasterPos4d(1,2,3,4); assert(calls==24); }"
with tempfile.TemporaryDirectory(prefix="dg-raster-") as temp:
    path=Path(temp)
    (path/"test.cpp").write_text(source)
    subprocess.run([os.environ.get("CXX","clang++"),"-std=c++23","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined","-fno-omit-frame-pointer","-I",str(ROOT/"guest/opengl"),
                    str(path/"test.cpp"),"-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True)
print("PASS: all 24 raster adapters, exact dimensions/defaults, signed extremes, double precision, null/begin guards")
