#!/usr/bin/env python3
"""Check the public WGL probe's unscaled GPU screenshot, without cursor/shader."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import importlib.util
import json
from pathlib import Path
spec=importlib.util.spec_from_file_location('dg_cursor_pixels',(Path(__file__).resolve().parents[2] / 'scripts/diagnostics/cursor-check.py'))
pixels=importlib.util.module_from_spec(spec);spec.loader.exec_module(pixels)

def texture_color(x, y):
    if 120 <= x < 136 and 120 <= y < 136:
        return (255, 255, 255)
    return ((255, 0, 0) if x < 128 else (0, 255, 0)) if y < 128 else ((0, 0, 255) if x < 128 else (255, 255, 0))


def texture_samples(x, y):
    # Pixel centers mapped to a 256-square nearest-filtered texture. Only an
    # exact integer boundary admits the lower adjacent texel: GL interpolation
    # may round to either side. No color or general coordinate tolerance.
    xn, xd = (2 * x + 1) * 256, 640
    yn, yd = (479 - 2 * y) * 256, 480
    tx, ty = xn // xd, yn // yd
    xs = (tx - 1, tx) if xn % xd == 0 and tx else (tx,)
    ys = (ty - 1, ty) if yn % yd == 0 and ty else (ty,)
    return texture_color(tx, ty), {texture_color(a, b) for a in xs for b in ys}


def verify(png, closed_second=False, textured=False, front_stage=None):
    if front_stage not in (None, 'flush', 'swap') or (front_stage and textured):
        raise ValueError('choose one valid probe stage')
    count=bad=boundary=0;samples=[]
    for y,row,channels in pixels.png_rows(png):
        if not 64<=y<304:continue
        for x in range(64,736):
            pane=0 if 64<=x<384 else 1 if 416<=x<736 and not closed_second else None
            if pane is None:continue
            if (x+1)*channels>len(row):raise ValueError('capture is narrower than the probe')
            split=224 if pane==0 else 576
            expected=(255,255 if pane else 0,0) if x<split else (0,255 if pane else 0,255)
            allowed = {expected}
            if textured and pane == 0:
                expected, allowed = texture_samples(x - 64, y - 64)
            if front_stage and pane == 0:
                expected = (255, 0, 0) if front_stage == 'flush' else (0, 255, 0)
                allowed = {expected}
            actual=tuple(row[x*channels:x*channels+3]);count+=1
            if actual != expected and actual in allowed:
                boundary += 1
            if actual not in allowed:
                bad+=1
                if len(samples)<12:samples.append({'x':x,'y':y,'actual':actual,'expected':expected})
    if count!=(1 if closed_second else 2)*320*240:raise ValueError('incomplete probe capture')
    return {'verified_pixels':count,'mismatches':bad,'sampling_boundary_pixels':boundary,'samples':samples,'pass':bad==0,'closed_second':closed_second,'textured':textured,'front_stage':front_stage}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('image',type=Path);p.add_argument('--closed-second',action='store_true');p.add_argument('--textured',action='store_true');p.add_argument('--front-stage',choices=['flush','swap']);p.add_argument('--output',type=Path);a=p.parse_args()
    result=verify(a.image.read_bytes(),a.closed_second,a.textured,a.front_stage);result['image']=str(a.image);text=json.dumps(result,indent=2)+'\n'
    if a.output:a.output.write_text(text)
    print(text,end='');raise SystemExit(0 if result['pass'] else 1)
if __name__=='__main__':main()
