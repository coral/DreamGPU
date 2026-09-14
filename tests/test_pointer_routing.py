#!/usr/bin/env python3
"""Exercise actual shared-memory routing and QEMU handler dispatch together."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
def function(source, name):
    match = re.search(r'(?:static\s+)?(?:[\w*]+\s*\n?\s+)+\b' + name + r'\([^;]*?\)\s*\{', source)
    assert match, name
    start = match.start(); end = match.end(); depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}'); end += 1
    return source[start:end] + '\n'
qemu = (ROOT/'vendor/qemu/ui/input.c').read_text()
shmem = (ROOT/'vendor/qemu/ui/dreamgpu-shmem.c').read_text()
parts = [(ROOT/'tests/native_input/routing-prefix.c').read_text()]
for name in ('qemu_input_handler_activate','qemu_input_find_handler','qemu_input_event_send_impl','qemu_input_queue_btn','qemu_input_is_absolute','qemu_input_select_pointer','qemu_input_scale_axis','qemu_input_queue_rel','qemu_input_queue_abs'):
    parts.append(function(qemu, name))
for name in ('dreamgpu_shmem_release_buttons','dreamgpu_shmem_pointer_mode','dreamgpu_shmem_release_input','dreamgpu_shmem_process_event'):
    parts.append(function(shmem, name))
parts.append((ROOT/'tests/native_input/routing-cases.c').read_text())
with tempfile.TemporaryDirectory(prefix='dg-input-route-') as directory:
    p=Path(directory); source=p/'routing.c';binary=p/'routing';source.write_text('\n'.join(parts))
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-O1','-fsanitize=address,undefined','-I'+str(ROOT/'vendor/qemu/include'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
