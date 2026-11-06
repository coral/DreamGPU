#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0055 on actual pinned Wine source: which provider each switcher
export reaches.

The d3d8, d3d9 and ddraw switchers (installed as the system DLLs) dispatch
each export through a user list (the Wine provider: wined8, wined9, winedd)
and a system list (msd3d8/msd3d9, ddsys or the native DLL). With
unkToSystemDll (patch 0003) d3d8 and ddraw fill the user list only from their
*_list_safe.h, d3d9 likewise whenever msd3d9 is loaded; everything else goes to
the system list, whose slots stay the switcher's own stubs when there is no
system DLL. d3d8's safe list held Direct3DCreate8 alone, so on a Win2000 or
XP image without msd3d8.dll ValidatePixelShader and ValidateVertexShader
reached stubs that return E_FAIL without writing the error string: D3DX8,
statically linked into Unreal Tournament 2003 and 3DMark2001 SE, then fails
every shader assembly with D3DXERR_INVALIDDATA.

Each switcher's prepared sources (the *_sw.c loaders, the *_stubs.c lists and
stubs, switcher.c's switcher_init, tryLoad and path helpers, and the real
list headers) are built natively under ASan/UBSan against a mock loader whose
modules export exactly what the Wine .def files and the switcher's own
native .def files (gendef of the Microsoft DLLs) list. The trampoline of
switcher.inc is checked in the source and executed in C. For every export,
with and without a system DLL and for allowed and blacklisted callers, the
reached provider must follow the policy: an export Wine implements reaches
Wine, except the few whose system implementation must win (d3d9's
Direct3DShaderValidatorCreate9, which Wine returns NULL from, when Microsoft's
d3d9 is present; ddraw's thread and surface lock helpers, which native
components call on native objects and whose Wine aliases take no arguments);
anything else reaches the system DLL, or a stub. Blacklisted callers always
take the system list. Stubs return values that are safe for their
signatures (no error code as an interface pointer, "not profiling" for
D3DPERF queries, a failing DllGetClassObject), and every stub's argument count
equals Wine's stdcall argument count for that export, so either path leaves a
balanced stack. The same d3d8 build with the lists before 0055 reproduces the
failure: ValidatePixelShader reaches the E_FAIL stub.
"""
import importlib.util
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
WINE = ROOT / 'vendor/wine9x'

WINDOWS_H = r'''
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef int BOOL;
typedef uint32_t DWORD;
typedef int32_t LONG, LSTATUS;
typedef unsigned char BYTE;
typedef void *HANDLE, *HMODULE, *HKEY, *HINSTANCE, *LPVOID, *FARPROC;
#define WINAPI
#define __stdcall
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define PATH_MAX 260
#define S_FALSE ((LONG)1)
#define E_FAIL ((LONG)0x80004005)
#define DDERR_GENERIC E_FAIL
#define DDERR_INVALIDPARAMS ((LONG)0x80070057)
#define CLASS_E_CLASSNOTAVAILABLE ((LONG)0x80040111)
#define HKEY_LOCAL_MACHINE ((HKEY)1)
#define KEY_READ 1
#define REG_SZ 1
#define ERROR_SUCCESS 0
#define stricmp strcasecmp
HMODULE LoadLibraryA(const char *path);
FARPROC GetProcAddress(HMODULE module, const char *name);
BOOL FreeLibrary(HMODULE module);
DWORD GetModuleFileNameA(HMODULE module, char *out, DWORD size);
DWORD GetSystemDirectoryA(char *out, DWORD size);
LONG RegOpenKeyEx(HKEY key, const char *name, DWORD options, DWORD access, HKEY *out);
LONG RegQueryValueExA(HKEY key, const char *name, DWORD *reserved, DWORD *type, BYTE *data, DWORD *size);
LONG RegCloseKey(HKEY key);
BOOL isWindows95(void);
BOOL isWindows98me(void);
BOOL isWindows2k(void);
'''

# Each switcher's system DLL names (tried in order) and Wine provider.
DLLS = {
    'd3d8': {'wine': 'wined8.dll', 'system': 'msd3d8.dll', 'native': 'd3d8.dll', 'def': 'd3d8/d3d8.def',
             'spec': 'd3d8/d3d8.spec', 'native_def': 'switcher/d3d8_xp.def'},
    'd3d9': {'wine': 'wined9.dll', 'system': 'msd3d9.dll', 'native': 'd3d9.dll', 'def': 'd3d9/d3d9.def',
             'spec': 'd3d9/d3d9.spec', 'native_def': 'switcher/d3d9_xp.def'},
    'ddraw': {'wine': 'winedd.dll', 'system': 'ddsys.dll', 'native': 'ddraw.dll', 'def': 'ddraw/ddraw.def',
              'spec': 'ddraw/ddraw.spec', 'native_def': 'switcher/ddraw_xp.def'},
}
# Exports Wine has but whose system implementation is preferred when there is
# one, and exports never routed to Wine (see the module docstring).
SYSTEM_PREFERRED = {
    'd3d8': set(),
    'd3d9': {'Direct3DShaderValidatorCreate9'},
    'ddraw': set(),
}
SYSTEM_ONLY = {
    'd3d8': set(),
    'd3d9': set(),
    'ddraw': {'AcquireDDThreadLock', 'ReleaseDDThreadLock', 'DDInternalLock', 'DDInternalUnlock',
              'InternalLock', 'InternalUnlock'},
}
# Safe stub return values for exports whose result is a pointer or a query.
STUB_RETURNS = {
    ('d3d8', 'Direct3DCreate8'): '0', ('d3d9', 'Direct3DCreate9'): '0',
    ('d3d9', 'Direct3DShaderValidatorCreate9'): '0', ('d3d9', 'D3DPERF_GetStatus'): '0',
    ('d3d9', 'D3DPERF_QueryRepeatFrame'): '0', ('ddraw', 'DllGetClassObject'): 'CLASS_E_CLASSNOTAVAILABLE',
    ('d3d8', 'ValidatePixelShader'): 'E_FAIL', ('d3d8', 'ValidateVertexShader'): 'E_FAIL',
}


def def_exports(path):
    names = []
    for line in path.read_text().splitlines():
        line = line.split(';')[0].strip()
        if not line or line.startswith(('LIBRARY', 'EXPORTS')):
            continue
        names.append(line.split()[0].split('=')[0].strip())
    return names


def list_entries(path):
    return [(int(n), name, ret) for n, name, ret in
            re.findall(r'^FUNC(\d+)\((\w+), ([^)]+)\)\s*$', path.read_text(), re.M)]


def function(source, name):
    start = re.search(r'^[A-Za-z][^\n]*\b' + name + r'\([^\n]*\)\s*\n\{', source, re.M).start()
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def program(dll, switcher, names, wine_exports, native_exports, scenarios):
    """The C test of one switcher: scenarios (system DLL present?, caller
    allowed?, expected providers per export) against the real loaders."""
    source = (switcher / 'switcher.c').read_text()
    unknown = re.search(r'^BOOL unkToSystemDll = (TRUE|FALSE);', source, re.M)[1]
    helpers = ''.join(function(source, name) for name in
                      ('getSysPath', 'getExeName', 'readSetting', 'getSettings', 'tryLoad', 'switcher_init'))
    c_names = ',\n'.join('"%s"' % name for name in names)
    wine = ',\n'.join('"%s"' % name for name in wine_exports)
    native = ',\n'.join('"%s"' % name for name in native_exports)
    cases = ',\n'.join('{%d, %d, %d, "%s"}' % s for s in scenarios)
    return r'''
#include <windows.h>
#include "switcher.h"
/* The list type, as @DLL@_stubs.c and @DLL@_sw.c (separate units) define it. */
#include "switcher_mf_struct.h"
#pragma pack(push)
#pragma pack(1)
typedef struct _fnlistStruct {
#include "@DLL@_list.h"
} fnlistStruct;
#pragma pack(pop)
#include "switcher_mf_clean.h"
extern fnlistStruct fnlist, fnlist_sys;
extern const fnlistStruct fnlist_stubs;
extern const BOOL requireSystemLib;
BOOL unkToSystemDll = @UNKNOWN@;
static HMODULE thisDLL = (HMODULE)0x9000;
static HMODULE userlib = NULL, systemlib = NULL;
static const char *blacklist_exe[] = {"iexplore.exe", "explorer.exe", NULL};
const char *fn_names[] = {NULL};
BOOL isWindows95(void) { return FALSE; }
BOOL isWindows98me(void) { return FALSE; }
BOOL isWindows2k(void) { return TRUE; }
BOOL getVMDISP9xFlags(DWORD *flags) { *flags = 0; return FALSE; }
BOOL isNineSafe(DWORD flags) { (void)flags; return FALSE; }
@HELPERS@

/* The mock loader: the Wine provider, the system DLL (when present) and
 * the native-named DLL in the system directory, which is the switcher
 * itself. Each export of a module is a distinct address. */
static const char *names[] = {@NAMES@};
#define COUNT (sizeof(names) / sizeof(*names))
static const char *wine_exports[] = {@WINE@};
static const char *native_exports[] = {@NATIVE@};
static char wine_marks[COUNT], native_marks[COUNT];
static BOOL system_present;
static const char *loaded_name[3];
static unsigned int loads, frees;
static int find(const char *const *list, unsigned int count, const char *name)
{
    unsigned int i;
    for (i = 0; i < count; ++i)
        if (!strcmp(list[i], name))
            return 1;
    return 0;
}
static int index_of(const char *name)
{
    unsigned int i;
    for (i = 0; i < COUNT; ++i)
        if (!strcmp(names[i], name))
            return (int)i;
    return -1;
}
#define WINE_MODULE ((HMODULE)0x1000)
#define SYSTEM_MODULE ((HMODULE)0x2000)
static const char system_dir[] = "C:\\WINNT\\system32";
DWORD GetSystemDirectoryA(char *out, DWORD size)
{
    if (size <= strlen(system_dir)) abort();
    strcpy(out, system_dir);
    return (DWORD)strlen(system_dir);
}
static char paths[3][MAX_PATH];
HMODULE LoadLibraryA(const char *path)
{
    static const char *const files[3] = {"@WINEDLL@", "@SYSTEMDLL@", "@NATIVEDLL@"};
    unsigned int i;
    for (i = 0; i < 3; ++i)
    {
        snprintf(paths[i], MAX_PATH, "%s\\%s", system_dir, files[i]);
        if (strcasecmp(path, paths[i]))
            continue;
        if (i == 1 && !system_present)
            return NULL;
        ++loads;
        return i == 0 ? WINE_MODULE : i == 1 ? SYSTEM_MODULE : thisDLL;
    }
    return NULL;
}
DWORD GetModuleFileNameA(HMODULE module, char *out, DWORD size)
{
    const char *path = module == WINE_MODULE ? paths[0] : module == SYSTEM_MODULE ? paths[1]
            : module == thisDLL ? paths[2] : "C:\\GAME\\GAME.EXE";
    if (strlen(path) >= size) abort();
    strcpy(out, path);
    return (DWORD)strlen(path);
}
FARPROC GetProcAddress(HMODULE module, const char *name)
{
    int i = index_of(name);
    if (module == WINE_MODULE && find(wine_exports, sizeof(wine_exports) / sizeof(*wine_exports), name))
        return i < 0 ? (FARPROC)wine_marks : (FARPROC)&wine_marks[i];
    if (module == SYSTEM_MODULE && find(native_exports, sizeof(native_exports) / sizeof(*native_exports), name))
        return i < 0 ? (FARPROC)native_marks : (FARPROC)&native_marks[i];
    if (module == thisDLL)
        return (FARPROC)&loads; /* the switcher exports everything */
    return NULL;
}
BOOL FreeLibrary(HMODULE module) { (void)module; ++frees; return TRUE; }
LONG RegOpenKeyEx(HKEY key, const char *name, DWORD options, DWORD access, HKEY *out)
{
    (void)key; (void)name; (void)options; (void)access; (void)out;
    return 2; /* no switcher settings */
}
LONG RegQueryValueExA(HKEY key, const char *name, DWORD *reserved, DWORD *type, BYTE *data, DWORD *size)
{
    (void)key; (void)name; (void)reserved; (void)type; (void)data; (void)size;
    abort();
}
LONG RegCloseKey(HKEY key) { (void)key; abort(); }

/* switcher.inc's trampoline once initialized: an allowed caller takes the
 * user slot unless it is NULL, everyone else the system slot. */
static void *dispatch(unsigned int i, BOOL allowed)
{
    void *user = ((void **)&fnlist)[i];
    if (allowed && user)
        return user;
    return ((void **)&fnlist_sys)[i];
}

typedef DWORD (*Stub0)(void);
typedef DWORD (*Stub1)(DWORD);
typedef DWORD (*Stub2)(DWORD, DWORD);
typedef DWORD (*Stub3)(DWORD, DWORD, DWORD);
typedef DWORD (*Stub4)(DWORD, DWORD, DWORD, DWORD);
typedef DWORD (*Stub5)(DWORD, DWORD, DWORD, DWORD, DWORD);

struct scenario { int system, allowed, ready; const char *expected; };
static const struct scenario scenarios[] = {@CASES@};

int main(void)
{
    fnlistStruct initial = fnlist, initial_sys = fnlist_sys;
    unsigned int s, i, reached[3] = {0, 0, 0};

    if (sizeof(fnlistStruct) != COUNT * sizeof(void *)) abort();
    for (s = 0; s < sizeof(scenarios) / sizeof(*scenarios); ++s)
    {
        char actual[COUNT + 1];
        fnlist = initial;
        fnlist_sys = initial_sys;
        system_present = scenarios[s].system;
        userlib = systemlib = NULL;
        /* Not ready (a required system DLL is missing): the trampoline
         * takes fnlist_stubs. */
        if (switcher_init() != scenarios[s].ready)
        {
            printf("FAIL @DLL@ switcher_init, scenario %u (user %p, system %p)\n", s, userlib, systemlib);
            return 1;
        }
        if (userlib != WINE_MODULE || systemlib != (system_present ? SYSTEM_MODULE : NULL))
        {
            printf("FAIL @DLL@ providers %p %p, scenario %u\n", userlib, systemlib, s);
            return 1;
        }
        for (i = 0; i < COUNT; ++i)
        {
            void *p = scenarios[s].ready ? dispatch(i, scenarios[s].allowed) : ((void *const *)&fnlist_stubs)[i];
            void *stub = ((void *const *)&fnlist_stubs)[i];
            actual[i] = p == &wine_marks[i] ? 'W' : p == &native_marks[i] ? 'S' : p == stub ? 'X' : '?';
            reached[actual[i] == 'W' ? 0 : actual[i] == 'S' ? 1 : 2]++;
        }
        actual[COUNT] = 0;
        if (strcmp(actual, scenarios[s].expected))
        {
            printf("FAIL @DLL@ scenario %u (system %d, allowed %d): reached %s, expected %s\n", s,
                    scenarios[s].system, scenarios[s].allowed, actual, scenarios[s].expected);
            for (i = 0; i < COUNT; ++i)
                if (actual[i] != scenarios[s].expected[i])
                    printf("  %s: %c, expected %c\n", names[i], actual[i], scenarios[s].expected[i]);
            return 1;
        }
    }
    /* What the stubs return. */
    for (i = 0; i < COUNT; ++i)
        printf("stub %s %#x\n", names[i], (unsigned int)((Stub0)((void *const *)&fnlist_stubs)[i])());
    printf("dispatch %u wine %u system %u stub\n", reached[0], reached[1], reached[2]);
    return 0;
}
'''.replace('@UNKNOWN@', unknown).replace('@DLL@', dll).replace('@HELPERS@', helpers) \
        .replace('@NAMES@', c_names).replace('@WINE@', wine).replace('@NATIVE@', native) \
        .replace('@CASES@', cases).replace('@WINEDLL@', DLLS[dll]['wine']) \
        .replace('@SYSTEMDLL@', DLLS[dll]['system']).replace('@NATIVEDLL@', DLLS[dll]['native'])


def compile_units(mock, switcher, dll, test, binary):
    """The switcher's own units (stubs and lists, loader) and the test."""
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-variable',
                    '-Wno-old-style-declaration', '-Wno-unused-but-set-variable',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(mock), '-I', str(switcher), str(switcher / (dll + '_stubs.c')),
                    str(switcher / (dll + '_sw.c')), str(test), '-o', str(binary)], check=True)


def expected(dll, names, wine_exports, native_exports, system, allowed):
    """The policy, independently of the list headers."""
    out = ''
    for name in names:
        wine_has = name in wine_exports
        system_has = system and name in native_exports
        if allowed and wine_has and name not in SYSTEM_ONLY[dll] \
                and not (system_has and name in SYSTEM_PREFERRED[dll]):
            out += 'W'
        elif system_has:
            out += 'S'
        else:
            out += 'X'
    return out


def stdcall_arguments(spec_text, name):
    m = re.search(r'^@ stdcall (?:-private )?%s\(([^)]*)\)' % name, spec_text, re.M)
    return None if not m else len(m[1].split())


def build(work, dll, switcher, scenarios_policy=True):
    names = [name for _, name, _ in list_entries(switcher / (dll + '_list.h'))]
    wine_exports = def_exports(WINE / DLLS[dll]['def'])
    native_exports = def_exports(WINE / DLLS[dll]['native_def'])
    scenarios = []
    for system in (0, 1):
        for allowed in (1, 0):
            # ddraw requires its system DLL (requireSystemLib); without it the
            # switcher is never ready and every call takes a stub.
            ready = int(bool(system) or dll != 'ddraw')
            scenarios.append((system, allowed, ready,
                              expected(dll, names, wine_exports, native_exports, system, allowed)
                              if ready else 'X' * len(names)))
    mock = work / ('mock-' + dll)
    mock.mkdir()
    (mock / 'windows.h').write_text(WINDOWS_H)
    (mock / 'ddraw.h').write_text('#pragma once\n')
    (mock / 'nocrt.h').write_text('#pragma once\n')
    test = switcher / ('test-' + dll + '.c')
    test.write_text(program(dll, switcher, names, wine_exports, native_exports, scenarios))
    binary = work / ('test-' + dll)
    compile_units(mock, switcher, dll, test, binary)
    return subprocess.run([str(binary)], capture_output=True, text=True), names, wine_exports


with tempfile.TemporaryDirectory(prefix='dreamgpu-switcher-dispatch-') as temporary:
    work = Path(temporary)
    donor = work / 'wine'
    shutil.copytree(WINE, donor, ignore=shutil.ignore_patterns('.git'))
    patches.apply(donor, MANIFEST)
    switcher = donor / 'switcher'

    # The trampoline the C dispatch() mirrors: not ready -> stubs; fncheck
    # FALSE -> system; a NULL user slot -> system.
    trampoline = (switcher / 'switcher.inc').read_text()
    assert re.search(r'call _switcher_reinit\s+test eax, eax\s+jz %1 %\+ _not_ready', trampoline)
    assert re.search(r'call _fncheck\s+add esp,8\s+test eax, eax\s+jz %1 %\+ _sys', trampoline)
    assert re.search(r'mov eax,_fnlist\s+mov eax,\[eax \+ fnlistStruct\.p %\+ %1 \]\s+test eax, eax\s+'
                     r'jz %1 %\+ _sys\s+jmp eax', trampoline)
    assert re.search(r'_not_ready:\s+mov eax,_fnlist_stubs', trampoline)

    summary = []
    for dll in DLLS:
        entries = list_entries(switcher / (dll + '_list.h'))
        safe = list_entries(switcher / (dll + '_list_safe.h'))
        spec_text = (WINE / DLLS[dll]['spec']).read_text()
        wine_exports = def_exports(WINE / DLLS[dll]['def'])
        # The safe list is a subset of the list with the same arities.
        assert all(entry in entries for entry in safe), dll
        for n, name, ret in entries:
            # Every stub's stdcall argument count is Wine's (when Wine
            # declares one), so a stub balances the stack as Wine would.
            count = stdcall_arguments(spec_text, name)
            if count is not None:
                assert n == count, (dll, name, n, count)
            if (dll, name) in STUB_RETURNS:
                assert ret == STUB_RETURNS[(dll, name)], (dll, name, ret)
        # Wine's DDInternalLock/Unlock and InternalLock/Unlock are aliases
        # of the argument-less thread lock: never routed to Wine.
        if dll == 'ddraw':
            assert {'DDInternalLock', 'DDInternalUnlock', 'InternalLock', 'InternalUnlock'} \
                <= {name for name in wine_exports}
            assert not {'DDInternalLock', 'DDInternalUnlock', 'InternalLock', 'InternalUnlock',
                        'AcquireDDThreadLock', 'ReleaseDDThreadLock'} & {name for _, name, _ in safe}
        result, names, _ = build(work, dll, switcher)
        sys.stdout.write(result.stdout if result.returncode else '')
        sys.stderr.write(result.stderr)
        if result.returncode:
            sys.exit(result.returncode)
        stubs = dict(re.findall(r'^stub (\w+) (0x[0-9a-f]+|0)$', result.stdout, re.M))
        for (owner, name), value in STUB_RETURNS.items():
            if owner == dll:
                want = {'0': 0, 'E_FAIL': 0x80004005, 'CLASS_E_CLASSNOTAVAILABLE': 0x80040111}[value]
                assert int(stubs[name], 16) == want, (dll, name, stubs[name])
        counts = re.search(r'^dispatch (\d+) wine (\d+) system (\d+) stub$', result.stdout, re.M)
        summary.append('%s %s/%s/%s' % (dll, counts[1], counts[2], counts[3]))

    # d3d8 before 0055: on an image without msd3d8.dll ValidatePixelShader
    # and ValidateVertexShader reached the E_FAIL stubs.
    before = work / 'before'
    shutil.copytree(switcher, before)
    for name in ('d3d8_list.h', 'd3d8_list_safe.h'):
        shutil.copyfile(WINE / 'switcher' / name, before / name)
    names = [name for _, name, _ in list_entries(before / 'd3d8_list.h')]
    old = program('d3d8', before, names, def_exports(WINE / 'd3d8/d3d8.def'),
                  def_exports(WINE / 'switcher/d3d8_xp.def'),
                  [(0, 1, 1, ''.join('W' if name == 'Direct3DCreate8' else 'X' for name in names))])
    (before / 'test-d3d8.c').write_text(old)
    mock = work / 'mock-before'
    mock.mkdir()
    (mock / 'windows.h').write_text(WINDOWS_H)
    (mock / 'ddraw.h').write_text('#pragma once\n')
    (mock / 'nocrt.h').write_text('#pragma once\n')
    compile_units(mock, before, 'd3d8', before / 'test-d3d8.c', work / 'before-d3d8')
    old_result = subprocess.run([str(work / 'before-d3d8')], capture_output=True, text=True)
    assert old_result.returncode == 0, old_result.stdout + old_result.stderr
    assert re.search(r'^stub ValidatePixelShader 0x80004005$', old_result.stdout, re.M)

print('PASS prepared switchers: every export reaches the provider the policy names, with and without a '
      'system DLL and for allowed and blacklisted callers (%s: wine/system/stub dispatches); stub arities '
      'match Wine\'s stdcall signatures and pointer/query stubs return safe values; before 0055 d3d8\'s '
      'ValidatePixelShader/ValidateVertexShader reached E_FAIL stubs without msd3d8.dll' % ', '.join(summary))
