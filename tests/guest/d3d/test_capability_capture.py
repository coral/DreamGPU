#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run actual D3D8/9 capability enumeration/JSON code with SDK types and fake COM."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HEADERS = ROOT / 'vendor/reactos/sdk/include'
PREAMBLE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using DWORD=uint32_t; using UINT=uint32_t; using BYTE=uint8_t;
using HRESULT=int32_t; using INT=int32_t;
struct LARGE_INTEGER { uint32_t LowPart; int32_t HighPart; };
struct GUID { uint32_t Data1; uint16_t Data2,Data3; uint8_t Data4[8]; };
#define MAX_DEVICE_IDENTIFIER_STRING 512
#define MAKEFOURCC(a,b,c,d) (uint32_t(a)|(uint32_t(b)<<8)|(uint32_t(c)<<16)|(uint32_t(d)<<24))
#define SUCCEEDED(hr) ((hr)>=0)
#define CopyMemory memcpy
'''
HARNESS = r'''
struct D3D { unsigned calls=0; unsigned scenario=0; };
static UINT fake_GetAdapterCount(D3D *d) { return d->scenario==4?0:2; }
static HRESULT fake_GetAdapterIdentifier(D3D *d,UINT adapter,DWORD flags,CaptureAdapterIdentifier *out) {
 assert(adapter<2&&!flags);
 std::memcpy(out->Driver,"driver\\name\"\n\x81",15);
 std::memset(out->Description,'x',sizeof(out->Description)); // Deliberately unterminated.
 out->DriverVersion.HighPart=-1;out->DriverVersion.LowPart=0x87654321;
 out->DeviceIdentifier.Data1=0x12345678;out->VendorId=0x1234;
 if(d->scenario==1&&adapter==0)return static_cast<HRESULT>(0x80004005u);
 return 0;
}
static HRESULT fake_GetDeviceCaps(D3D *d,UINT adapter,D3DDEVTYPE device,CaptureDeviceCaps *out) {
 assert(adapter<2&&device==D3DDEVTYPE_HAL);
 out->VertexShaderVersion=0xfffe0101;out->PixelShaderVersion=0;
 out->MaxTextureWidth=2048;out->RasterCaps=0x80000000;out->MaxVertexW=1.0f;
 if(d->scenario==2&&adapter==0)return static_cast<HRESULT>(0x8876086au);
 return 0;
}
static HRESULT fake_GetAdapterDisplayMode(D3D *d,UINT adapter,D3DDISPLAYMODE *out) {
 assert(adapter<2);out->Width=640;out->Height=480;out->RefreshRate=60;
 out->Format=adapter?D3DFMT_R5G6B5:D3DFMT_X8R8G8B8;
 if(d->scenario==3&&adapter==0)return static_cast<HRESULT>(0x8876086cu);
 return 0;
}
static HRESULT fake_CheckDeviceFormat(D3D *d,UINT adapter,D3DDEVTYPE device,D3DFORMAT display,
 DWORD usage,D3DRESOURCETYPE resource,D3DFORMAT format) {
 assert(device==D3DDEVTYPE_HAL&&display==(adapter?D3DFMT_R5G6B5:D3DFMT_X8R8G8B8));
 assert(!(resource==D3DRTYPE_VOLUMETEXTURE&&(usage==D3DUSAGE_RENDERTARGET||usage==D3DUSAGE_DEPTHSTENCIL)));
 assert(!(resource==D3DRTYPE_SURFACE&&usage==D3DUSAGE_DYNAMIC));
 ++d->calls;
 // Same format can have different usage claims. Preserve nonfailure success codes too.
 if(format==D3DFMT_P8&&usage==D3DUSAGE_RENDERTARGET)return static_cast<HRESULT>(0x8876086au);
 if(format==D3DFMT_P8&&usage==D3DUSAGE_DYNAMIC)return static_cast<HRESULT>(0x8876086cu);
 return format==D3DFMT_DXT1?1:0;
}
#define D3D_CALL(name,...) fake_##name(__VA_ARGS__)
#include "capability-json.h"
#include "capability-capture.inc"
struct Output { std::string bytes; unsigned calls=0; unsigned fail_at=0; };
static bool sink(void *pointer,const char *bytes,unsigned count) {
 auto &out=*static_cast<Output *>(pointer);++out.calls;
 if(out.fail_at&&out.calls==out.fail_at)return false;
 out.bytes.append(bytes,count);return true;
}
int main(int argc,char **argv) {
 assert(argc==2);D3D d;d.scenario=std::atoi(argv[1]);
 Output output;CapabilityJson json(sink,&output);
 json.text("{");const bool complete=CaptureAdapters(json,&d);
 json.text(",\"complete\":");json.text(complete?"true":"false");json.text("}");assert(json.flush());
 const unsigned rows=sizeof(capability_formats)/sizeof(capability_formats[0]) *
     sizeof(capability_usages)/sizeof(capability_usages[0]);
 assert(d.calls==(d.scenario==4?0:d.scenario==3?rows:2*rows));
 assert(complete==(d.scenario==0));
 assert(output.bytes.size()<65536); // Existing bounded serial result protocol.
 std::fwrite(output.bytes.data(),1,output.bytes.size(),stdout);
 // Failure on a buffered write latches permanently and never retries uncertain bytes.
 Output broken;broken.fail_at=2;CapabilityJson bad(sink,&broken);
 for(unsigned i=0;i<15000;++i)bad.text("x");
 assert(!bad.good()&&!bad.flush()&&broken.calls==2&&broken.bytes.size()==4096);
 // Number boundaries and a non-NUL-terminated bounded string do not overrun.
 Output edge;CapabilityJson bounded(sink,&edge);const char raw[]={'a','"','\\',char(255)};
 bounded.text("[");bounded.number(UINT32_MAX);bounded.text(",");bounded.hex(UINT32_MAX);
 bounded.text(",");bounded.string(raw,sizeof(raw));bounded.text("]");assert(bounded.flush());
 assert(edge.bytes=="[4294967295,\"0xffffffff\",\"a\\\"\\\\\\u00ff\"]");
}
'''


def declaration(source, name):
    return re.search(r'typedef (?:struct|enum) _' + name + r'\s*\{.*?\}\s*' + name + ';',
                     source, re.S).group(0) + '\n'


def verify(document, version, scenario):
    assert document['complete'] == (scenario == 0)
    assert document['adapter_count'] == (0 if scenario == 4 else 2)
    formats, usages = document['formats'], document['usages']
    names = {item['name'] for item in formats}
    assert {'P8', 'A8P8', 'R3G3B2', 'R8G8B8', 'D16', 'D32', 'D24S8', 'DXT1', 'DXT2',
            'DXT3', 'DXT4', 'DXT5', 'V8U8', 'UYVY', 'YUY2'} <= names
    assert ('R16F' in names) == (version == 9)
    assert ('W11V11U10' in names) == (version == 8)
    assert len(names) == len(formats)
    assert len(usages) == 13
    assert document['format_query_columns'] == ['format_index', 'usage_index', 'hresult']
    for adapter in document['adapters']:
        first = adapter['ordinal'] == 0
        if scenario == 1 and first:
            assert adapter['identity'] is None and adapter['identity_hresult'] == '0x80004005'
        else:
            identity = adapter['identity']
            assert identity['driver'] == 'driver\\name"\n\x81'
            assert identity['description'] == 'x' * 512
            assert identity['driver_version_high'] == '0xffffffff'
            assert identity['device_identifier_bytes'][:4] == [0x78, 0x56, 0x34, 0x12]
        if scenario == 2 and first:
            assert adapter['caps'] is None and adapter['caps_hresult'] == '0x8876086a'
        else:
            caps = adapter['caps']
            assert caps['vertex_shader'] == {'raw': '0xfffe0101', 'major': 1, 'minor': 1}
            assert caps['pixel_shader'] == {'raw': '0x00000000', 'major': 0, 'minor': 0}
            assert caps['reported']['MaxTextureWidth'] == 2048
            assert caps['reported']['RasterCaps'] == '0x80000000'
            assert len(caps['raw_dwords']) * 4 == caps['byte_size']
            assert '0x3f800000' in caps['raw_dwords']
        if scenario == 3 and first:
            assert adapter['display_mode'] is None and adapter['format_queries'] is None
            assert adapter['display_mode_hresult'] == '0x8876086c'
            continue
        matrix = adapter['format_queries']
        assert len(matrix) == len(formats) * len(usages)
        assert {(f, u) for f, u, _ in matrix} == {
            (f, u) for f in range(len(formats)) for u in range(len(usages))}
        for f, u, result in matrix:
            name, use = formats[f]['name'], usages[u]['usage']
            expected = '0x8876086a' if name == 'P8' and use == 'render_target' else (
                '0x8876086c' if name == 'P8' and use == 'dynamic_sampling' else (
                    '0x00000001' if name == 'DXT1' else '0x00000000'))
            assert result == expected


with tempfile.TemporaryDirectory(prefix='dreamgpu-capabilities-') as directory:
    work = Path(directory)
    for version in (8, 9):
        types = (HEADERS / f'psdk/d3d{version}types.h').read_text()
        caps = (HEADERS / f'dxsdk/d3d{version}caps.h').read_text()
        code = PREAMBLE
        for name in ('D3DDEVTYPE', 'D3DFORMAT', 'D3DRESOURCETYPE', 'D3DDISPLAYMODE',
                     f'D3DADAPTER_IDENTIFIER{version}'):
            code += declaration(types, name)
        for name in ('D3DUSAGE_RENDERTARGET', 'D3DUSAGE_DEPTHSTENCIL', 'D3DUSAGE_DYNAMIC'):
            code += re.search(r'^#define\s+' + name + r'\s+[^\n]+', types, re.M).group(0) + '\n'
        if version == 9:
            for name in ('D3DVSHADERCAPS2_0', 'D3DPSHADERCAPS2_0'):
                code += declaration(caps, name)
        code += declaration(caps, f'D3DCAPS{version}')
        code += f'using CaptureDeviceCaps=D3DCAPS{version};\n'
        code += f'using CaptureAdapterIdentifier=D3DADAPTER_IDENTIFIER{version};\n'
        (work / 'test.cpp').write_text(code + HARNESS)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        f'-DDG_D3D_VERSION={version}', '-I', str(ROOT / 'tools/d3d'),
                        str(work / 'test.cpp'), '-o', str(work / 'test')], check=True)
        for scenario in range(5):
            result = subprocess.run([str(work / 'test'), str(scenario)], check=True, capture_output=True)
            verify(json.loads(result.stdout), version, scenario)
print('PASS D3D8/9 actual capture: complete usage matrix, SDK layouts, raw HRESULTs/shader claims, '
      'bounded identity/JSON, metadata failures, output failure latch and 64-KiB serial budget')
