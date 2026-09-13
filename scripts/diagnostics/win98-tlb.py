#!/usr/bin/env python3
"""Apply the pinned Win98 VMM TLB fix to a private copy, retaining original bytes."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = 'e30e6bf872dd9b7c354772b9b16169b2b9731b0e707233d415d256cbfe35fcb0'
PATCHED = 'c43a285ae46dc7feb252c7143f237a4b526abe12781fe844aeb54556b396627d'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-vmm', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=ROOT/'vendor/patcher9x')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    lock = json.loads((ROOT/'support/guest/sources.lock.json').read_text())
    entry = lock['sources']['patcher9x'] if 'sources' in lock else lock['patcher9x']
    revision = entry.get('commit', entry.get('revision'))
    actual = subprocess.check_output(['git','-C',str(args.source),'rev-parse','HEAD'],text=True).strip()
    if actual != revision:
        raise SystemExit('Patcher source does not match sources.lock.json')
    for name, expected in entry.get('submodules', {}).items():
        found = subprocess.check_output(['git','-C',str(args.source/name),'rev-parse','HEAD'],text=True).strip()
        if found != expected:
            raise SystemExit('Patcher submodule does not match sources.lock.json: '+name)
    original = args.input_vmm.read_bytes()
    digest = lambda data: hashlib.sha256(data).hexdigest()
    if digest(original) != ORIGINAL:
        raise SystemExit('This recipe only accepts the validated Win98 SE VMM override; input preserved')
    patcher = args.source/'patcher9x'
    if not patcher.is_file():
        raise SystemExit('Build the pinned native patcher first; see tools/win9x/TLB.md')
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output/'VMM.ORIGINAL.VXD').write_bytes(original)
    target = args.output/'VMM.VXD'
    shutil.copyfile(args.input_vmm,target)
    result = subprocess.run([str(patcher.resolve()),'--patch','tlb',str(target.resolve())],capture_output=True)
    (args.output/'patch.log').write_bytes(result.stdout+result.stderr)
    patched = target.read_bytes()
    changed = sum(a != b for a,b in zip(original,patched))
    manifest = {'source_commit':actual,'patcher_sha256':digest(patcher.read_bytes()),
                'original_sha256':digest(original),'patched_sha256':digest(patched),
                'original_bytes':len(original),'patched_bytes':len(patched),
                'changed_bytes':changed,'exit_code':result.returncode,
                'guest_target':'WINDOWS/SYSTEM/VMM32/VMM.VXD'}
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if result.returncode or len(patched)!=len(original) or changed!=58 or digest(patched)!=PATCHED:
        raise SystemExit('Patch output differs from the validated binary; original and evidence retained; do not install')
    print(json.dumps(manifest,indent=2))


if __name__ == '__main__':
    main()
