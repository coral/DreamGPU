#!/usr/bin/env python3
"""Actual contiguous Wine row helpers: guards, exact bytes, bounded copy count."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
CODE = r'''

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
static unsigned calls;
static void *tracked_copy(void *d, const void *s, size_t n) {
    ++calls;
    uintptr_t a = (uintptr_t)d, b = (uintptr_t)s;
    assert(a < b ? n <= b - a : n <= a - b);
    return std::memcpy(d, s, n);
}
#define memcpy tracked_copy
#include "wine-row-copy.h"
#undef memcpy
int main() {
    for (size_t unit = 1; unit <= 4; ++unit)
        for (size_t width : {1u, 3u, 128u, 257u})
            for (size_t rows : {1u, 2u, 3u, 127u, 128u, 257u})
                for (size_t offset = 0; offset < 8; ++offset) {
                    size_t row = width * unit, total = row * rows;
                    std::vector<unsigned char> a(total + 16, 0xa5), b(total + 16, 0xcd);
                    for (size_t i = 0; i < total; ++i)
                        a[offset + i] = (unsigned char)(i * 17 + 3);
                    calls = 0;
                    assert(dg_wine_copy_rows(b.data() + offset, a.data() + offset, row, rows, row,
                                             row));
                    assert(calls == 1);
                    assert(!std::memcmp(a.data() + offset, b.data() + offset, total));
                    for (size_t i = 0; i < offset; ++i)
                        assert(b[i] == 0xcd);
                    for (size_t i = offset + total; i < b.size(); ++i)
                        assert(b[i] == 0xcd);
                    calls = 0;
                    assert(dg_wine_repeat_row(b.data() + offset, width, rows, unit, row));
                    for (size_t y = 0; y < rows; ++y)
                        assert(!std::memcmp(b.data() + offset + y * row, a.data() + offset, row));
                    assert(calls <= 9);
                    for (size_t i = 0; i < offset; ++i)
                        assert(b[i] == 0xcd);
                    for (size_t i = offset + total; i < b.size(); ++i)
                        assert(b[i] == 0xcd);
                }
    unsigned char data[128];
    std::memset(data, 0xcc, sizeof(data));
    calls = 0;
    assert(!dg_wine_copy_rows(data + 1, data, 8, 8, 8, 8));
    assert(!dg_wine_copy_rows(data, data + 1, 8, 8, 8, 8));
    assert(!dg_wine_copy_rows(data, data, 8, 8, 8, 8));
    assert(!dg_wine_copy_rows(data, data + 64, 8, 4, 16, 8));
    assert(!dg_wine_copy_rows(data, data + 64, 8, 4, 8, 16));
    assert(!dg_wine_copy_rows(data, data + 64, SIZE_MAX, 2, SIZE_MAX, SIZE_MAX));
    assert(!dg_wine_repeat_row(data, SIZE_MAX, 1, 2, SIZE_MAX));
    assert(!dg_wine_repeat_row(data, 8, SIZE_MAX, 1, 8));
    assert(!dg_wine_repeat_row(data, 8, 8, 1, 16));
    assert(!dg_wine_repeat_row(nullptr, 8, 8, 1, 8));
    assert(!dg_wine_repeat_row((void *)(UINTPTR_MAX - 2), 8, 8, 1, 8));
    assert(!dg_wine_copy_rows(data, (void *)(UINTPTR_MAX - 2), 8, 8, 8, 8));
    assert(!dg_wine_copy_rows(data, data + 64, 0, 8, 0, 0));
    assert(!dg_wine_repeat_row(data, 0, 8, 1, 0));
    assert(!dg_wine_repeat_row(data, 8, 0, 1, 8));
    assert(!dg_wine_repeat_row(data, 8, 8, 0, 0));
    assert(calls == 0);
    for (auto value : data)
        assert(value == 0xcc);
    // Exactly adjacent spans are legal and remain one copy.
    assert(dg_wine_copy_rows(data + 64, data, 8, 8, 8, 8));
    assert(calls == 1);
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-row-copy-') as tmp:
    source=Path(tmp)/'test.cpp'; source.write_text(CODE)
    compiler=os.environ.get('CXX','clang++')
    subprocess.run([compiler,'-std=c++23','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I'+str(ROOT/'guest/d3d'),str(source),'-o',str(Path(tmp)/'test')],check=True)
    subprocess.run([str(Path(tmp)/'test')],check=True)
print('actual Wine row helpers ASan/UBSan PASS')
