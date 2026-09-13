#!/usr/bin/env python3
"""Prepare fixed UT99 ISO contents from the verified original CD outside Git."""
import argparse,hashlib,json,os,shutil
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
ISO_SHA='e184984ca88f001c5ddd52035d76cd64e266e26c74975161b5ed72366c74704f'
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def config(text):
    settings={
        'FirstRun':{'FirstRun':'436'},
        'Engine.Engine':{'GameRenderDevice':'GlideDrv.GlideRenderDevice','WindowedRenderDevice':'GlideDrv.GlideRenderDevice','RenderDevice':'GlideDrv.GlideRenderDevice'},
        'Engine.GameEngine':{'UseSound':'False','CacheSizeMegs':'32'},
        'WinDrv.WindowsClient':{'WindowedViewportX':'640','WindowedViewportY':'480','WindowedColorBits':'32','FullscreenViewportX':'640','FullscreenViewportY':'480','StartupFullscreen':'True','UseDirectDraw':'False','UseJoystick':'False','CaptureMouse':'False','MinDesiredFrameRate':'0.0'},
    }
    out=[];section='';done=set()
    def finish():
        if section in settings:
            for k,v in settings[section].items():
                if k.lower() not in done:out.append(f'{k}={v}')
    for line in text.splitlines():
        if line.startswith('[') and line.endswith(']'):
            finish();section=line[1:-1];done=set();out.append(line);continue
        key=line.split('=',1)[0].strip() if '=' in line else ''
        if key.lower()=='serveractors':continue
        match=next((k for k in settings.get(section,{}) if k.lower()==key.lower()),None)
        if match:
            if match.lower() not in done:out.append(f'{match}={settings[section][match]}');done.add(match.lower())
        else:out.append(line)
    finish();return '\r\n'.join(out)+'\r\n'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,default=ROOT/'target/retro-media/ut99-original');p.add_argument('--iso',type=Path,default=ROOT/'target/retro-media/UT_GOTY_CD1.ISO');p.add_argument('--output',type=Path,default=ROOT/'target/retro-media/ut99-dreamgpu');a=p.parse_args()
    if digest(a.iso)!=ISO_SHA:p.error('Original CD digest mismatch')
    if a.output.exists():p.error('Use a new output directory; prepared media is immutable')
    destination=a.output/'UT99';destination.mkdir(parents=True);files={}
    for name in ['System','Textures','Sounds','Music','Maps','Help']:
        target=destination/name;target.mkdir()
        for source in sorted((a.source/name).iterdir()):
            if not source.is_file():continue
            if source.is_symlink():p.error(f'Symlink rejected: {source}')
            output=target/source.name
            try:os.link(source,output)
            except OSError:shutil.copyfile(source,output)
            files[str(output.relative_to(a.output))]={'bytes':source.stat().st_size,'sha256':digest(source)}
    ini=destination/'DGUT.INI';ini.write_bytes(config((a.source/'System/Default.ini').read_text()).encode('ascii'))
    manifest={'schema':1,'original_iso':str(a.iso),'original_iso_sha256':ISO_SHA,'files':files,'config_sha256':digest(ini),'bytes':sum(f['bytes'] for f in files.values()),'purpose':'Private original-game Glide acceptance; media and generated config remain outside Git','copy_reuse':'Guest skips same-size pinned original assets only; no guest hash readback claim'}
    (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({'root':str(a.output),'files':len(files),'bytes':manifest['bytes'],'config':str(ini)},indent=2))
if __name__=='__main__':main()
