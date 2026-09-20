#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the prepared Wine enumeration functions with deterministic API boundaries."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("checked_patches", ROOT / "tests/guest/support/patches.py")
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
manifest = ROOT / "support/guest/d3d/patches/base/manifest.json"
PREAMBLE = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t DWORD;
typedef int32_t HRESULT;
typedef char CHAR;
typedef unsigned GUID;
#define WINAPI
#define TRACE(...) ((void)0)
#define FAILED(hr) ((hr) < 0)
#define DDERR_INVALIDPARAMS ((HRESULT)-1)
#define D3D_OK 0
#define D3DENUMRET_OK 1
#define D3DCOLOR_MONO 1
#define D3DCOLOR_RGB 2
#define D3DPTEXTURECAPS_POW2 0x2u
#define D3DPTEXTURECAPS_NONPOW2CONDITIONAL 0x100u
#define D3DPTEXTURECAPS_PERSPECTIVE 0x1u
#define D3DDEVCAPS_HWTRANSFORMANDLIGHT 0x10000u
#define D3DDEVCAPS_DRAWPRIMITIVES2EX 0x8000u
#define D3DDEVCAPS_HWRASTERIZATION 0x80000u
static const GUID IID_IDirect3DRampDevice=1, IID_IDirect3DRGBDevice=2, IID_IDirect3DHALDevice=3;
#define TEXTURE_BITS (D3DPTEXTURECAPS_POW2 | D3DPTEXTURECAPS_NONPOW2CONDITIONAL | D3DPTEXTURECAPS_PERSPECTIVE)
#define HARDWARE_BITS (D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_DRAWPRIMITIVES2EX | D3DDEVCAPS_HWRASTERIZATION)
#define UNRELATED_BIT 0x40000000u
typedef struct { DWORD dwTextureCaps; } primitive_caps;
typedef struct {
 DWORD dwSize, dwFlags, dcmColorModel, dwDevCaps;
 primitive_caps dpcLineCaps, dpcTriCaps;
 DWORD dwMinTextureWidth, dwMaxTextureRepeat, tail;
} D3DDEVICEDESC;
typedef struct { int unused; } D3DDEVICEDESC7;
struct ddraw;
typedef struct { struct ddraw *owner; } IDirect3D, IDirect3D2, IDirect3D3;
struct ddraw { IDirect3D one; IDirect3D2 two; IDirect3D3 three; unsigned d3dversion; };
typedef HRESULT (*LPD3DENUMDEVICESCALLBACK)(GUID *, char *, char *, D3DDEVICEDESC *, D3DDEVICEDESC *, void *);
static struct ddraw *impl_from_IDirect3D(IDirect3D *p) { return p->owner; }
static struct ddraw *impl_from_IDirect3D2(IDirect3D2 *p) { return p->owner; }
static struct ddraw *impl_from_IDirect3D3(IDirect3D3 *p) { return p->owner; }
static unsigned locks, unlocks, held;
static HRESULT caps_error;
static void wined3d_mutex_lock(void) { assert(!held); held=1; ++locks; }
static void wined3d_mutex_unlock(void) { assert(held); held=0; ++unlocks; }
static HRESULT ddraw_get_d3dcaps(struct ddraw *d, D3DDEVICEDESC7 *caps) {
 assert(d && caps && held); memset(caps, 0, sizeof(*caps)); return caps_error;
}
static void ddraw_d3dcaps1_from_7(D3DDEVICEDESC *out, const D3DDEVICEDESC7 *in) {
 assert(in && held); memset(out, 0, sizeof(*out));
 out->dwSize=sizeof(*out); out->dwFlags=0x1ff; out->dcmColorModel=D3DCOLOR_RGB;
 out->dwDevCaps=HARDWARE_BITS|UNRELATED_BIT;
 out->dpcLineCaps.dwTextureCaps=out->dpcTriCaps.dwTextureCaps=TEXTURE_BITS|UNRELATED_BIT;
}
'''
TEST = r'''
struct capture { unsigned count, cancel, version; GUID ids[3]; DWORD size; };
static HRESULT capture(GUID *id, char *description, char *name, D3DDEVICEDESC *hal,
                       D3DDEVICEDESC *hel, void *context) {
 struct capture *c=context;
 assert(held && id && description && name && c->count<3);
 c->ids[c->count++]=*id;
 assert(hal->dwSize==c->size && hel->dwSize==c->size);
 if (*id==IID_IDirect3DHALDevice) {
  assert(!strcmp(name,"Direct3D HAL"));
  assert(hal->dcmColorModel==D3DCOLOR_RGB && hal->dwFlags==0x1ff);
  assert(hal->dwDevCaps==(HARDWARE_BITS|UNRELATED_BIT));
  assert(hel->dcmColorModel==0 && hel->dwDevCaps==(D3DDEVCAPS_HWRASTERIZATION|UNRELATED_BIT));
  assert(hal->dpcLineCaps.dwTextureCaps==(TEXTURE_BITS|UNRELATED_BIT));
  assert(hel->dpcLineCaps.dwTextureCaps==UNRELATED_BIT);
 } else {
  assert(*id==IID_IDirect3DRGBDevice || (*id==IID_IDirect3DRampDevice && c->version<=2));
  assert(!strcmp(name,*id==IID_IDirect3DRGBDevice ? "RGB Emulation" : "Ramp Emulation"));
  assert(hal->dcmColorModel==0 && hal->dwFlags==0);
  assert(hel->dcmColorModel==(*id==IID_IDirect3DRGBDevice ? D3DCOLOR_RGB : D3DCOLOR_MONO));
  assert(hal->dwDevCaps==UNRELATED_BIT && hel->dwDevCaps==UNRELATED_BIT);
  assert(hal->dpcLineCaps.dwTextureCaps==UNRELATED_BIT);
  assert(hel->dpcLineCaps.dwTextureCaps==(TEXTURE_BITS|UNRELATED_BIT));
 }
 assert(hal->dpcLineCaps.dwTextureCaps==hal->dpcTriCaps.dwTextureCaps);
 assert(hel->dpcLineCaps.dwTextureCaps==hel->dpcTriCaps.dwTextureCaps);
 // Supported legacy callers can modify the provided string buffers.
 strcpy(name,"Modified by the application");
 if (*id!=IID_IDirect3DHALDevice) strcpy(description,"Application's custom software device description");
 return c->count==c->cancel ? 0 : D3DENUMRET_OK;
}
static HRESULT invoke(struct ddraw *d, unsigned version, LPD3DENUMDEVICESCALLBACK cb, void *ctx) {
 if(version==1) return d3d1_EnumDevices(&d->one, cb, ctx);
 if(version==2) return d3d2_EnumDevices(&d->two, cb, ctx);
 return d3d3_EnumDevices(&d->three, cb, ctx);
}
int main(void) {
 struct ddraw d={0}; d.one.owner=d.two.owner=d.three.owner=&d;
 // Enumeration version must follow the actual interface, not the last QI call.
 for(unsigned version=1; version<=3; ++version) for(unsigned last_qi=1; last_qi<=7; ++last_qi) {
  d.d3dversion=last_qi;
  unsigned total=version<=2 ? 3 : 2;
  for(unsigned cancel=0; cancel<=total; ++cancel) {
   struct capture c={0};c.cancel=cancel;c.version=version;
   c.size=version==1 ? offsetof(D3DDEVICEDESC,dwMinTextureWidth) : version==2 ? offsetof(D3DDEVICEDESC,dwMaxTextureRepeat) : sizeof(D3DDEVICEDESC);
   assert(invoke(&d,version,capture,&c)==D3D_OK && !held && locks==unlocks);
   assert(c.count==(cancel ? cancel : total));
   for(unsigned i=0; i<c.count; ++i) assert(c.ids[i]==(version<=2 ? i+1 : i+2));
   if(version==1 && c.count>=2) assert(c.ids[1]==IID_IDirect3DRGBDevice);
  }
  unsigned before=locks;
  assert(invoke(&d,version,NULL,NULL)==DDERR_INVALIDPARAMS && locks==before);
  struct capture c={0};caps_error=-123;
  assert(invoke(&d,version,capture,&c)==-123 && c.count==0 && !held && locks==unlocks);
  caps_error=0;
 }
 puts("PASS actual prepared Wine D3D1/2/3 enumeration: RAMP/RGB/HAL order, interface sizes, software/HAL caps, mutable names, cancellation and failure unlocks");
}
'''
with tempfile.TemporaryDirectory(prefix="dreamgpu-enumeration-") as directory:
    work = Path(directory)
    for name in json.loads(manifest.read_text())["files"]:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / "vendor/wine9x" / name, destination)
    patches.apply(work, manifest)
    source = (work / "ddraw/ddraw.c").read_text()
    start = source.index("static HRESULT enum_devices_d3d3(")
    last = source.index("static HRESULT WINAPI d3d1_EnumDevices(", start)
    end = source.index("\n}", last) + 2
    test = work / "test.c"
    test.write_text(PREAMBLE + source[start:end] + TEST)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(test), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
