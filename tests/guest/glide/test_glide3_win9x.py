#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Windows 98 constraints for the Glide 3 provider and its DGSYSG3 oracle.

Win9x lacks several NT-only entry points (an implicit import of one makes the
loader refuse the image), rejects CreateThread without a thread-id
destination, and pays for every full-frame LFB read through its VxD path. A
probe the runner kills on Win9x can leave the Win16 lock held, so its budget
must cover the slow path rather than rely on termination."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
SOURCES = [ROOT / name for name in ('tools/glide/probe3.cpp', 'tools/glide/provider3.h', 'tools/glide/display.h',
                                    'tools/glide/provider.h', 'tools/common/provider-clean.h',
                                    'guest/glide/glide3.cpp', 'guest/glide/glide3-core.h')]
# Absent from Windows 95/98/Me KERNEL32/USER32 (or NT-only behaviour).
NT_ONLY = ('GetModuleHandleExA', 'GetModuleHandleExW', 'SetDllDirectoryA', 'SetDllDirectoryW',
           'GetFileAttributesExW', 'GetTickCount64', 'AllowSetForegroundWindow',
           'IsProcessorFeaturePresent', 'CreateFileW', 'LoadLibraryW', 'LoadLibraryExW',
           'GetModuleFileNameW', 'GetSystemDirectoryW', 'CreateWindowExW', 'RegisterClassW',
           'wsprintfW', 'GetFileSizeEx', 'SetFilePointerEx', 'InterlockedCompareExchange64')
for path in SOURCES:
    text = re.sub(r'//[^\n]*|/\*.*?\*/', '', path.read_text(), flags=re.S)
    for name in NT_ONLY:
        assert not re.search(rf'\b{name}\b', text), f'{path.name}: NT-only {name}'
    # Win9x fails CreateThread when lpThreadId is NULL.
    for call in re.finditer(r'\bCreateThread\s*\(([^;]*)\);', text, re.S):
        assert not re.search(r',\s*(NULL|nullptr|0)\s*$', call.group(1)), \
            f'{path.name}: CreateThread without a thread-id destination'

probe = (ROOT / 'tools/glide/probe3.cpp').read_text()
# Every pixel stage reads the frame once; the shared core reads the whole
# buffer per call, so per-region reads multiplied Win98's transfer cost.
reads = len(re.findall(r'\bsnapshot\(\)', probe)) + len(re.findall(r'\bregion\(\d', probe)) - 1
assert reads <= 10, f'{reads} full-frame reads'
assert 'pgrLfbReadRegion(kBack, 0, 0, 640, 480, 1280, frame)' in probe
# The first lines are logged before any provider is loaded, so a missing log
# means the process never ran, not that the provider failed.
entry = probe[probe.index('WinMainCRTStartup'):]
assert entry.index('CreateFileA("C:\\\\DGSYSG3.LOG"') < entry.index('Run()')
run = probe[probe.index('static bool Run()'):]
assert run.index('Record("START automated sysglide3")') < run.index('LoadLibraryA("glide3x.dll")')

runner = (ROOT / 'tools/benchmark/runner.cpp').read_text()
budget = {name: int(ms) for name, ms in
          re.findall(r'\{"(sysglide3?)", "C:\\\\DGSYSG[R3]\.EXE", "C:\\\\DGSYSG[R3]\.LOG", "", (\d+)\}',
                     runner)}
assert set(budget) == {'sysglide', 'sysglide3'}, budget
assert budget['sysglide3'] >= 3 * budget['sysglide'], budget
host = (ROOT / 'scripts/benchmarks/halflife.py').read_text()
default_result = int(re.search(r'def run_demo\([^)]*\btimeout=(\d+)', host).group(1))
assert budget['sysglide3'] < default_result * 1000, 'guest budget must end before the host gives up'
print(f'PASS Glide 3 Win9x constraints: no NT-only entry points or NULL thread ids, '
      f'{reads} full-frame reads, sysglide3 budget {budget["sysglide3"] // 1000} s')
