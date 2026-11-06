#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0054 on actual pinned Wine source: d3d8.dll's ValidateVertexShader
and ValidatePixelShader with the runtime's contract.

The d3d8 runtime exports ValidatePixelShader(code, caps, return_error,
errors) and ValidateVertexShader(code, declaration, caps, return_error,
errors). D3DX8's assembler, statically linked into games such as Unreal
Tournament 2003 (D3DDrv.dll) and 3DMark2001 SE (e2_d3d8_driver_mfc.dll),
finds them with GetModuleHandle/GetProcAddress on d3d8.dll after assembling
each shader, passes a NULL caps and declaration, return_error and a pointer to
a NULL-initialized string when the caller wants errors, fails the assembly
with D3DXERR_INVALIDDATA on a failure, and frees the string with
HeapFree(GetProcessHeap(), 0, ...). Wine's stubs failed whenever the caps or
declaration was given and never wrote the string; 0054 is upstream Wine's
implementation (version token checked, against the caps when given, a
process-heap message that is empty unless return_error is set).

The prepared functions are compiled unchanged under ASan/UBSan against the
real D3DCAPS8 layout and a recording process heap, and run upstream Wine's
d3d8 test_validate_vs/test_validate_ps (including the untouched string of a
NULL pixel shader) plus every version token (VS 1.0/1.1, PS 1.0-1.4 accepted;
VS 1.2/2.0, PS 1.5/2.0/3.0 and the other shader type's tokens rejected) with
caps NULL, equal, below and above, declarations, return_error TRUE and FALSE,
errors NULL and non-NULL (each string allocated once from the process heap,
freeable with HeapFree, its text checked), a failed allocation, and D3DX8's
exact calls. The export signatures (stdcall argument bytes) are checked
against the switcher's FUNC4/FUNC5 entries and d3d8.spec. Fails without the
patch.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / 'support/guest/d3d/patches/base/manifest.json'

PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int DWORD, UINT;
typedef int BOOL, HRESULT;
typedef size_t SIZE_T;
typedef void *HANDLE;
typedef int D3DDEVTYPE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005)
#define TRACE(...) do {} while (0)
#define D3DPS_VERSION(major, minor) (0xFFFF0000 | ((major) << 8) | (minor))
#define D3DVS_VERSION(major, minor) (0xFFFE0000 | ((major) << 8) | (minor))
@CAPS@
/* The process heap: every block allocated, freed exactly once by the test. */
static char heap_identity;
static unsigned int allocations, frees, fail_next;
static void *blocks[64];
static HANDLE GetProcessHeap(void) { return &heap_identity; }
static void *HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size)
{
    unsigned int i;
    assert(heap == &heap_identity && !flags && size);
    if (fail_next)
    {
        fail_next = 0;
        return NULL;
    }
    for (i = 0; i < 64; ++i)
        if (!blocks[i])
        {
            /* Filled, so a missing terminator or copy shows. */
            blocks[i] = malloc(size);
            memset(blocks[i], 0xcc, size);
            ++allocations;
            return blocks[i];
        }
    abort();
}
static BOOL HeapFree(HANDLE heap, DWORD flags, void *block)
{
    unsigned int i;
    assert(heap == &heap_identity && !flags);
    for (i = 0; i < 64; ++i)
        if (blocks[i] && blocks[i] == block)
        {
            free(block);
            blocks[i] = NULL;
            ++frees;
            return TRUE;
        }
    printf("FAIL HeapFree of a block the process heap did not allocate: %p\n", block);
    exit(1);
}
'''

TEST = r'''
#define ok(condition, ...) do { if (!(condition)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); exit(1); } ++checks; } while (0)
static unsigned int checks;

/* One string: allocated by this call, the expected text, then freed. */
static void string(char *errors, unsigned int before, const char *expected, int line)
{
    if (allocations != before + 1 || !errors || strcmp(errors, expected))
    {
        printf("FAIL line %d: string %s, expected \"%s\" (%u allocations)\n", line,
                errors && allocations == before + 1 ? errors : "(none)", expected, allocations - before);
        exit(1);
    }
    HeapFree(GetProcessHeap(), 0, errors);
    ++checks;
}
#define STRING(call, hr_expected, text) do { unsigned int before = allocations; char *errors = (char *)0xcafeface; \
        HRESULT hr = call; ok(hr == (hr_expected), "hr %#x\n", (unsigned int)hr); string(errors, before, text, __LINE__); } while (0)

/* upstream Wine dlls/d3d8/tests/device.c test_validate_vs */
static void upstream_vs(void)
{
    static DWORD vs_code[] =
    {
        0xfffe0101,
        0x00000009, 0xc0010000, 0x90e40000, 0xa0e40000,
        0x00000009, 0xc0020000, 0x90e40000, 0xa0e40001,
        0x00000009, 0xc0040000, 0x90e40000, 0xa0e40002,
        0x00000009, 0xc0080000, 0x90e40000, 0xa0e40003,
        0x0000ffff,
    };
    static DWORD declaration_valid1[] = {0x20000000, 0x40020000, 0xffffffff};
    static DWORD declaration_valid2[] = {0x20000000, 0x40010000, 0xffffffff};
    static DWORD declaration_invalid[] = {0x20000000, 0x40020003, 0xffffffff};
    D3DCAPS8 caps;
    char *errors;
    HRESULT hr;

    hr = ValidateVertexShader(NULL, NULL, NULL, FALSE, NULL);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    hr = ValidateVertexShader(NULL, NULL, NULL, TRUE, NULL);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    hr = ValidateVertexShader(NULL, NULL, NULL, FALSE, &errors);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!*errors, "Got unexpected string \"%s\".\n", errors);
    HeapFree(GetProcessHeap(), 0, errors);
    hr = ValidateVertexShader(NULL, NULL, NULL, TRUE, &errors);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!!*errors, "Got unexpected empty string.\n");
    HeapFree(GetProcessHeap(), 0, errors);

    hr = ValidateVertexShader(vs_code, NULL, NULL, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ValidateVertexShader(vs_code, NULL, NULL, TRUE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ValidateVertexShader(vs_code, NULL, NULL, TRUE, &errors);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(!*errors, "Got unexpected string \"%s\".\n", errors);
    HeapFree(GetProcessHeap(), 0, errors);

    hr = ValidateVertexShader(vs_code, declaration_valid1, NULL, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ValidateVertexShader(vs_code, declaration_valid2, NULL, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    /* todo_wine upstream: the runtime rejects a FLOAT4 normal. */
    hr = ValidateVertexShader(vs_code, declaration_invalid, NULL, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    memset(&caps, 0, sizeof(caps));
    caps.VertexShaderVersion = D3DVS_VERSION(1, 1);
    caps.MaxVertexShaderConst = 4;
    hr = ValidateVertexShader(vs_code, NULL, &caps, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    caps.VertexShaderVersion = D3DVS_VERSION(1, 0);
    hr = ValidateVertexShader(vs_code, NULL, &caps, FALSE, NULL);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    caps.VertexShaderVersion = D3DVS_VERSION(1, 2);
    hr = ValidateVertexShader(vs_code, NULL, &caps, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    caps.VertexShaderVersion = D3DVS_VERSION(8, 8);
    hr = ValidateVertexShader(vs_code, NULL, &caps, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    /* todo_wine upstream: the runtime checks the constant count. */
    caps.VertexShaderVersion = D3DVS_VERSION(1, 1);
    caps.MaxVertexShaderConst = 3;
    hr = ValidateVertexShader(vs_code, NULL, &caps, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    *vs_code = D3DVS_VERSION(1, 0);
    hr = ValidateVertexShader(vs_code, NULL, NULL, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    *vs_code = D3DVS_VERSION(1, 2);
    hr = ValidateVertexShader(vs_code, NULL, NULL, TRUE, NULL);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    hr = ValidateVertexShader(vs_code, NULL, NULL, FALSE, &errors);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!*errors, "Got unexpected string \"%s\".\n", errors);
    HeapFree(GetProcessHeap(), 0, errors);
    hr = ValidateVertexShader(vs_code, NULL, NULL, TRUE, &errors);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!!*errors, "Got unexpected empty string.\n");
    HeapFree(GetProcessHeap(), 0, errors);
}

/* upstream Wine dlls/d3d8/tests/device.c test_validate_ps */
static void upstream_ps(void)
{
    static DWORD ps_1_1_code[] =
    {
        0xffff0101,
        0x00000001, 0x800f0001, 0xa0e40001,
        0x00000002, 0x800f0000, 0x80e40001, 0xa0e40002,
        0x0000ffff
    };
    static const DWORD ps_2_0_code[] =
    {
        0xffff0200,
        0x02000001, 0x800f0001, 0xa0e40001,
        0x03000002, 0x800f0000, 0x80e40001, 0xa0e40002,
        0x02000001, 0x800f0800, 0x80e40000,
        0x0000ffff
    };
    D3DCAPS8 caps;
    char *errors;
    HRESULT hr;

    hr = ValidatePixelShader(NULL, NULL, FALSE, NULL);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    hr = ValidatePixelShader(NULL, NULL, TRUE, NULL);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    errors = (void *)0xcafeface;
    hr = ValidatePixelShader(NULL, NULL, FALSE, &errors);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(errors == (void *)0xcafeface, "Got unexpected errors %p.\n", errors);
    errors = (void *)0xcafeface;
    hr = ValidatePixelShader(NULL, NULL, TRUE, &errors);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(errors == (void *)0xcafeface, "Got unexpected errors %p.\n", errors);

    hr = ValidatePixelShader(ps_1_1_code, NULL, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ValidatePixelShader(ps_1_1_code, NULL, TRUE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ValidatePixelShader(ps_1_1_code, NULL, TRUE, &errors);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(!*errors, "Got unexpected string \"%s\".\n", errors);
    HeapFree(GetProcessHeap(), 0, errors);

    memset(&caps, 0, sizeof(caps));
    caps.PixelShaderVersion = D3DPS_VERSION(1, 1);
    hr = ValidatePixelShader(ps_1_1_code, &caps, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    caps.PixelShaderVersion = D3DPS_VERSION(1, 0);
    hr = ValidatePixelShader(ps_1_1_code, &caps, FALSE, NULL);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    caps.PixelShaderVersion = D3DPS_VERSION(1, 2);
    hr = ValidatePixelShader(ps_1_1_code, &caps, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    caps.PixelShaderVersion = D3DPS_VERSION(8, 8);
    hr = ValidatePixelShader(ps_1_1_code, &caps, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    *ps_1_1_code = D3DPS_VERSION(1, 0);
    hr = ValidatePixelShader(ps_1_1_code, NULL, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    *ps_1_1_code = D3DPS_VERSION(1, 4);
    hr = ValidatePixelShader(ps_1_1_code, NULL, FALSE, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ValidatePixelShader(ps_2_0_code, NULL, FALSE, NULL);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    *ps_1_1_code = D3DPS_VERSION(1, 5);
    hr = ValidatePixelShader(ps_1_1_code, NULL, TRUE, NULL);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    hr = ValidatePixelShader(ps_1_1_code, NULL, FALSE, &errors);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!*errors, "Got unexpected string \"%s\".\n", errors);
    HeapFree(GetProcessHeap(), 0, errors);
    hr = ValidatePixelShader(ps_1_1_code, NULL, TRUE, &errors);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!!*errors, "Got unexpected empty string.\n");
    HeapFree(GetProcessHeap(), 0, errors);
}

/* Every version token against no caps and caps below, equal and above,
 * with every return_error/errors combination. */
static const char *expected_text(BOOL valid, BOOL caps_low, BOOL return_error)
{
    if (!return_error || (valid && !caps_low))
        return "";
    return valid ? "Shader version not supported by caps.\n" : "Unsupported shader version.\n";
}
static void tokens(void)
{
    static const DWORD vs_valid[] = {D3DVS_VERSION(1, 0), D3DVS_VERSION(1, 1)};
    static const DWORD vs_invalid[] = {D3DVS_VERSION(1, 2), D3DVS_VERSION(2, 0), D3DVS_VERSION(0, 9),
            D3DPS_VERSION(1, 1), 0, 0x0000ffff};
    static const DWORD ps_valid[] = {D3DPS_VERSION(1, 0), D3DPS_VERSION(1, 1), D3DPS_VERSION(1, 2),
            D3DPS_VERSION(1, 3), D3DPS_VERSION(1, 4)};
    static const DWORD ps_invalid[] = {D3DPS_VERSION(1, 5), D3DPS_VERSION(2, 0), D3DPS_VERSION(3, 0),
            D3DPS_VERSION(0, 9), D3DVS_VERSION(1, 1), 0};
    unsigned int i, c, r;

    for (i = 0; i < 2 + 6; ++i)
    {
        BOOL valid = i < 2;
        DWORD code[2] = {valid ? vs_valid[i] : vs_invalid[i - 2], 0x0000ffff};
        for (c = 0; c < 4; ++c)
        {
            D3DCAPS8 caps;
            const D3DCAPS8 *given = c ? &caps : NULL;
            /* 1: below the shader, 2: equal, 3: above. */
            BOOL low = c == 1 && valid;
            memset(&caps, 0xee, sizeof(caps));
            caps.VertexShaderVersion = c == 1 ? code[0] - 1 : c == 2 ? code[0] : D3DVS_VERSION(1, 1) + 1;
            caps.PixelShaderVersion = 0;
            for (r = 0; r < 2; ++r)
            {
                HRESULT expected = valid && !low ? S_OK : E_FAIL;
                ok(ValidateVertexShader(code, NULL, given, r, NULL) == expected, "vs %#x caps %u\n", code[0], c);
                STRING(ValidateVertexShader(code, NULL, given, r, &errors), expected,
                        expected_text(valid, low, r));
                STRING(ValidateVertexShader(code, code, given, r, &errors), expected,
                        expected_text(valid, low, r));
            }
        }
    }
    for (i = 0; i < 5 + 6; ++i)
    {
        BOOL valid = i < 5;
        DWORD code[2] = {valid ? ps_valid[i] : ps_invalid[i - 5], 0x0000ffff};
        for (c = 0; c < 4; ++c)
        {
            D3DCAPS8 caps;
            const D3DCAPS8 *given = c ? &caps : NULL;
            BOOL low = c == 1 && valid;
            memset(&caps, 0xee, sizeof(caps));
            caps.PixelShaderVersion = c == 1 ? code[0] - 1 : c == 2 ? code[0] : D3DPS_VERSION(1, 4) + 1;
            caps.VertexShaderVersion = 0;
            for (r = 0; r < 2; ++r)
            {
                HRESULT expected = valid && !low ? S_OK : E_FAIL;
                ok(ValidatePixelShader(code, given, r, NULL) == expected, "ps %#x caps %u\n", code[0], c);
                STRING(ValidatePixelShader(code, given, r, &errors), expected, expected_text(valid, low, r));
            }
        }
    }
    /* The vertex shader's NULL code pointer has its own message. */
    STRING(ValidateVertexShader(NULL, NULL, NULL, TRUE, &errors), E_FAIL, "Invalid code pointer.\n");
}

int main(void)
{
    /* UT2003's terrain shader as D3DX8 assembles it (version, D3DX's comment
     * block, instructions) and D3DX8's calls: caps and declaration NULL,
     * return_error and a NULL-initialized string when errors are wanted. */
    static const DWORD terrain[] = {D3DPS_VERSION(1, 4), 0x0009fffe, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0000ffff};
    static const DWORD vs[] = {D3DVS_VERSION(1, 1), 0x0009fffe, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0000ffff};
    char *errors;
    HRESULT hr;

    errors = NULL;
    hr = ValidatePixelShader(terrain, NULL, TRUE, &errors);
    ok(hr == S_OK && errors && !*errors, "D3DX8 pixel shader validation %#x\n", hr);
    HeapFree(GetProcessHeap(), 0, errors);
    ok(ValidatePixelShader(terrain, NULL, FALSE, NULL) == S_OK, "D3DX8 without errors\n");
    errors = NULL;
    hr = ValidateVertexShader(vs, NULL, NULL, TRUE, &errors);
    ok(hr == S_OK && errors && !*errors, "D3DX8 vertex shader validation %#x\n", hr);
    HeapFree(GetProcessHeap(), 0, errors);
    ok(ValidateVertexShader(vs, NULL, NULL, FALSE, NULL) == S_OK, "D3DX8 without errors\n");

    upstream_vs();
    upstream_ps();
    tokens();

    /* A failed allocation leaves a NULL string, as upstream. */
    fail_next = 1;
    errors = (char *)0xcafeface;
    ok(ValidatePixelShader(terrain, NULL, TRUE, &errors) == S_OK && !errors, "allocation failure\n");
    fail_next = 1;
    errors = (char *)0xcafeface;
    ok(ValidateVertexShader(NULL, NULL, NULL, TRUE, &errors) == E_FAIL && !errors, "allocation failure\n");

    ok(allocations == frees, "%u strings allocated, %u freed\n", allocations, frees);
    printf("PASS prepared d3d8 shader validation: %u checks; upstream Wine's test_validate_vs/ps, every version "
            "token against caps below/equal/above and none, %u process-heap strings, each freed by HeapFree\n",
            checks, allocations);
    return 0;
}
'''


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-shader-validation-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    main = (work / 'd3d8/d3d8_main.c').read_text()
    wine = ROOT / 'vendor/wine9x'
    caps_header = (wine / 'msc/d3d8caps.h').read_text()

    # Export signatures: stdcall argument bytes agree between Wine's
    # definition, its spec and the switcher's trampoline table (FUNCn: n
    # DWORD arguments), so either path returns to the caller with a balanced
    # stack.
    switcher = (wine / 'switcher/d3d8_list.h').read_text()
    spec_text = (wine / 'd3d8/d3d8.spec').read_text()
    for name, count in (('ValidatePixelShader', 4), ('ValidateVertexShader', 5)):
        assert 'FUNC%d(%s, E_FAIL)' % (count, name) in switcher
        arguments = re.search(r'@ stdcall %s\(([^)]*)\)' % name, spec_text)[1].split()
        assert len(arguments) == count, (name, arguments)
        signature = re.search(r'HRESULT WINAPI %s\(([^)]*)\)' % name, main)[1]
        assert len(signature.split(',')) == count, (name, signature)
    assert 'HRESULT WINAPI ValidatePixelShader(const DWORD *ps_code,\n        const D3DCAPS8 *caps, BOOL return_error, char **errors)' in main
    assert 'HRESULT WINAPI ValidateVertexShader(const DWORD *vs_code, const DWORD *declaration,\n        const D3DCAPS8 *caps, BOOL return_error, char **errors)' in main
    for name in ('ValidatePixelShader', 'ValidateVertexShader'):
        assert re.search(r'^\s*%s$' % name, (wine / 'd3d8/d3d8.def').read_text(), re.M)

    caps = re.search(r'typedef struct _D3DCAPS8\s*\{.*?\}\s*D3DCAPS8;', caps_header, re.S)[0]
    validation = main[main.index('/* DreamGPU: ValidateVertexShader and ValidatePixelShader'):
                      main.index('void d3d8_resource_cleanup(')]
    body = PREAMBLE.replace('@CAPS@', caps) + validation + TEST
    (work / 'test.c').write_text(body)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-Wno-unused-parameter', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', str(work / 'test.c'),
                    '-o', str(work / 'test')], check=True)
    result = subprocess.run([str(work / 'test')], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    sys.exit(result.returncode)
