#!/usr/bin/env python3
"""Create a fixed-runner update ISO from an already-built Cargo guest tool.

This never invokes a compiler. The tool must match the supplied OS package's
payload manifest. It is renamed DGDRV.EXE only on the generated optical media.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil


def package_tool(package, member):
    path = PurePosixPath(member)
    if not path.parts or path.is_absolute() or '..' in path.parts or path.parts[0] != 'tools':
        raise ValueError('Select a relative tools/ member from the Cargo package')
    manifest = json.loads((package/'package.json').read_text())
    if manifest.get('schema') != 2 or manifest.get('identity_scheme') != 'sha256-json-utf8-sorted-compact':
        raise ValueError('Expected a Cargo OS package')
    files = manifest['files']
    encoded = json.dumps(files, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
    if hashlib.sha256(encoded).hexdigest() != manifest['identity']:
        raise ValueError('Package identity mismatch')
    source = package
    for part in path.parts:
        source = source/part
        if source.is_symlink():
            raise ValueError('Package member must not traverse symlinks')
    if not source.is_file() or files.get(member) != hashlib.sha256(source.read_bytes()).hexdigest():
        raise ValueError('Tool missing or its payload hash differs')
    return source, manifest['identity']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--tool', required=True, help='For example tools/common/DGDMAQ.EXE')
    parser.add_argument('--output', type=Path, required=True, help='Fresh output directory')
    args = parser.parse_args()
    source, identity = package_tool(args.package.resolve(), args.tool)
    import pycdlib
    args.output.mkdir(parents=True, exist_ok=False)
    executable = args.output/'DGDRV.EXE'
    shutil.copyfile(source, executable)
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, vol_ident='DGTOOLS')
    iso.add_file(str(executable), iso_path='/DGDRV.EXE;1')
    media = args.output/'tool.iso'
    iso.write(str(media))
    iso.close()
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    (args.output/'manifest.json').write_text(json.dumps({
        'schema': 1, 'source_package_identity': identity, 'source_member': args.tool,
        'executable_sha256': digest(executable), 'iso_sha256': digest(media),
        'scope': 'Prebuilt fixed-runner helper media; no compiler, VM or driver installation invoked',
    }, indent=2)+'\n')
    print(media)


if __name__ == '__main__':
    main()
