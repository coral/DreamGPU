#!/usr/bin/env python3
"""Actual NT5 loader reply layout, bounds, padding, and aliasing contract."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="dreamgpu-icd-info-") as temporary:
    root = Path(temporary)
    source = r'''
#include <cstdint>
#include <cstring>
#include <cassert>
using ULONG=uint32_t; using WCHAR=uint16_t;
#include "icd-info.h"
int main() {
    alignas(4) unsigned char bytes[540];memset(bytes,0xa5,sizeof(bytes));
    ULONG query=0;
    assert(!DgIcdGetInfo(4,&query,519,bytes));assert(bytes[0]==0xa5);
    assert(!DgIcdGetInfo(4,&query,528,bytes));assert(bytes[0]==0xa5);
    assert(!DgIcdGetInfo(3,&query,520,bytes));assert(!DgIcdGetInfo(4,nullptr,520,bytes));
    query=1;assert(!DgIcdGetInfo(4,&query,520,bytes));
    query=0;memcpy(bytes,&query,4);assert(DgIcdGetInfo(4,bytes,520,bytes));
    DgIcdInformation result{};memcpy(&result,bytes,520);
    assert(result.Version==2&&result.DriverVersion==1);
    const char* name="DGPUICD";for(unsigned n=0;n<7;++n)assert(result.DriverName[n]==name[n]);
    for(unsigned n=7;n<256;++n)assert(!result.DriverName[n]);
    for(unsigned n=520;n<sizeof(bytes);++n)assert(bytes[n]==0xa5);
    assert(DgIcdGetInfo(0,nullptr,520,bytes));
    assert(DgIcdGetInfo(0,nullptr,532,bytes));
    memcpy(&result,bytes,sizeof(result));
    for(unsigned n=7;n<262;++n)assert(!result.DriverName[n]);
    for(unsigned n=532;n<sizeof(bytes);++n)assert(bytes[n]==0xa5);
}
'''
    (root / "test.cpp").write_text(source)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I"+str(ROOT / "guest/nt/display"),
                    str(root / "test.cpp"), "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS NT ICD520/532-byte replies; bounds, padding, overlapping input/output")
