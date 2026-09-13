#!/usr/bin/env python3
"""Prepare the verified private UT99 D3D module for app-local Wine translation."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL_SHA256 = '24298f19ddf5bfe4b87a073c0dd64e24cbc02c69047023a6368891868f3284fc'


def redirect(data):
    if hashlib.sha256(data).hexdigest() != ORIGINAL_SHA256:
        raise ValueError('Expected original UT99 GOTY D3DDrv.dll; source hash differs')
    old, new = (text.encode('utf-16le') + b'\0\0' for text in ('ddraw.dll', 'dgddr.dll'))
    if len(old) != len(new) or data.count(old) != 1:
        raise ValueError('Expected one equal-length DirectDraw loader name')
    return data.replace(old, new)


def configuration(text):
    settings = {
        'FirstRun': {'FirstRun': '436'},
        'Engine.Engine': {key: 'D3DDrv.D3DRenderDevice' for key in
                          ('GameRenderDevice', 'WindowedRenderDevice', 'RenderDevice')},
        'Engine.GameEngine': {'UseSound': 'False', 'CacheSizeMegs': '32'},
        'WinDrv.WindowsClient': {
            'WindowedViewportX': '640', 'WindowedViewportY': '480',
            'WindowedColorBits': '32', 'StartupFullscreen': 'False',
            'UseDirectDraw': 'False', 'UseJoystick': 'False',
            'CaptureMouse': 'False', 'MinDesiredFrameRate': '0.0'},
        'D3DDrv.D3DRenderDevice': {
            'UseFullscreen': 'False', 'UseTripleBuffering': 'False',
            'Use32BitTextures': 'True', 'UsePrecache': 'True'},
    }
    out, section, seen = [], '', set()

    def finish():
        out.extend(f'{key}={value}' for key, value in settings.get(section, {}).items()
                   if key.lower() not in seen)

    for line in text.splitlines():
        if line.startswith('[') and line.endswith(']'):
            finish()
            section, seen = line[1:-1], set()
            out.append(line)
            continue
        key = line.split('=', 1)[0].strip() if '=' in line else ''
        if key.lower() == 'serveractors':
            continue
        replacement = next((k for k in settings.get(section, {}) if k.lower() == key.lower()), None)
        if replacement:
            if replacement.lower() not in seen:
                out.append(f'{replacement}={settings[section][replacement]}')
                seen.add(replacement.lower())
        else:
            out.append(line)
    finish()
    return ('\r\n'.join(out) + '\r\n').encode('ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'target/retro-media/ut99-original/System')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    original = args.source/'D3DDrv.dll'
    patched = redirect(original.read_bytes())
    ini = configuration((args.source/'Default.ini').read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output/'D3DDrv.dll').write_bytes(patched)
    (args.output/'DGD3D.INI').write_bytes(ini)
    manifest = {
        'schema': 1, 'source_sha256': ORIGINAL_SHA256,
        'files': {name: hashlib.sha256((args.output/name).read_bytes()).hexdigest()
                  for name in ('D3DDrv.dll', 'DGD3D.INI')},
        'adaptation': 'One equal-length UTF16 module-name replacement ddraw.dll to dgddr.dll',
        'scope': 'Private fixture copy only; original media and Windows system DLLs untouched',
        'required_modules': ['dgddr.dll', 'wined3d.dll', 'dgpugl.dll'],
        'module_alias': {'dgddr.dll': 'source-built Wine winedd.dll'},
        'acceptance': 'Prepared inputs only; runtime acceptance not inferred',
    }
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
