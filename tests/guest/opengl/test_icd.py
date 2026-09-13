#!/usr/bin/env python3
"""Compile the actual ICD adapter; exercise dispatch and lifecycle without a GPU."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"
slots = re.findall(r"DG_ICD_SLOT\(\s*(\d+),\s*(.+?),\s*(\w+),\s*\((.*?)\)\)",
                   (SOURCE / "icd-slots.inc").read_text(), re.S)
assert len(slots) == 336 and [int(x[0]) for x in slots] == list(range(336))
coverage = json.loads((SOURCE / "icd-coverage.json").read_text())
implementations = re.findall(r"\b(gl\w+|Alias\w+|Missing\w+)\s*,",
                             (SOURCE / "icd-table.inc").read_text(), re.M)
assert len(implementations) == 336
assert coverage["unimplemented"] == [
    {"slot": int(i), "name": "gl" + name}
    for (i, _, name, _), impl in zip(slots, implementations) if impl.startswith("Missing")]
assert coverage["existing"] == sum(n.startswith("gl") for n in implementations)
assert coverage["aliases"] == sum(n.startswith("Alias") for n in implementations)
assert not coverage["production_registration_ready"]
assert [(item["slot"], item["name"]) for item in coverage["partial"]] == [(248, "glPixelTransferi"), (257, "glDrawPixels"), (306, "glArrayElement"), (323, "glCopyTexImage1D"), (325, "glCopyTexSubImage1D")]
assert all(item["limitation"] for item in coverage["partial"])
# Compare the checked-in ABI against the actual pinned donor, not our own initializer.
donor = subprocess.check_output([
    "git", "-C", str(ROOT / "vendor/reactos"), "show",
    "22fb3bb2c1d8196cf501edbb49b3814739f7b016:dll/opengl/opengl32/icd.h"], text=True)
original = re.findall(r"^\s*(.+?)\s*\(GLAPIENTRY \* (\w+)\)\((.*?)\);", donor, re.M)
normalize = lambda s: "".join(s.split())
assert [(normalize(r), n, normalize(a)) for _, r, n, a in slots] == [
    (normalize(r), n, normalize(a)) for r, n, a in original]

public = set(re.findall(r"^\s+(\w+)=", (SOURCE / "frontend.def").read_text(), re.M))
icd = set(re.findall(r"^\s+(\w+)=", (SOURCE / "icd.def").read_text(), re.M))
assert public <= icd

with tempfile.TemporaryDirectory(prefix="dreamgpu-icd-") as temporary:
    root = Path(temporary)
    for name in ("icd.cpp", "icd-trace.h", "icd-numeric.inc", "icd-state.inc", "icd-raster.inc", "icd-client.inc", "icd-pixels.inc", "icd-images.inc", "icd-textures.inc", "icd-functions.inc", "icd-slots.inc", "icd-table.inc"):
        shutil.copyfile(SOURCE / name, root / name)
    declarations = []
    definitions = []
    types = {"GLvoid": "void", "GLboolean": "unsigned char", "GLbyte": "signed char",
             "GLubyte": "unsigned char", "GLshort": "short", "GLushort": "unsigned short",
             "GLint": "int", "GLuint": "unsigned", "GLsizei": "int", "GLenum": "unsigned",
             "GLbitfield": "unsigned", "GLfloat": "float", "GLclampf": "float",
             "GLdouble": "double", "GLclampd": "double"}
    for i, result, name, args in slots:
        declarations.append(f'{result} gl{name}({args});')
        if "gl" + name not in public:
            continue
        if name in ("Vertex3f", "TexCoord2f", "Rotatef"):
            count = {"Vertex3f": 3, "TexCoord2f": 2, "Rotatef": 4}[name]
            params = ", ".join(f"GLfloat a{n}" for n in range(count))
            body = " ".join(f"Numbers[{n}]=a{n};" for n in range(count))
            definitions.append(f'void gl{name}({params}) {{ LastCall={i}; {body} }}')
        else:
            definitions.append(f'{result} gl{name}({args}) {{ LastCall={i};' +
                               (' return {};' if result != "void" else '') + '}')
    header = r'''
#include <cstddef>
#include <cstdint>
#include <cstring>
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define ZeroMemory(d,n) memset(d,0,n)
#define WINAPI
#define APIENTRY
using ULONG_PTR=uintptr_t; using ULONG=uint32_t; using BYTE=uint8_t;
using DWORD=uint32_t; using UINT=uint32_t; using LONG=int32_t; using BOOL=int;
using HANDLE=void*; using HDC=void*; using HGLRC=void*; using LPCSTR=const char*; using COLORREF=uint32_t;
using PROC=void(*)(); using LPLAYERPLANEDESCRIPTOR=void*;
struct PIXELFORMATDESCRIPTOR { int marker; };
#define TRUE 1
#define FALSE 0
#define GL_INVALID_OPERATION 0x0502
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_REVISION_MISMATCH 1306
#define ERROR_INVALID_PIXEL_FORMAT 2000
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INVALID_HANDLE 6
#define WGL_SWAP_MAIN_PLANE 1
#define GENERIC_WRITE 1
#define FILE_SHARE_READ 1
#define CREATE_ALWAYS 2
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_END 2
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
extern int LastCall, ErrorValue, LastError; extern float Numbers[4];
inline LONG InterlockedExchange(volatile LONG*p,LONG v) { LONG old=*p; *p=v; return old; }
inline LONG InterlockedCompareExchange(volatile LONG*p,LONG v,LONG old) { LONG got=*p; if(got==old)*p=v; return got; }
inline void SetLastError(DWORD e) { LastError=e; }
inline void JglSetError(unsigned e) { ErrorValue=e; }
inline DWORD GetLastError() { return LastError; }
inline LONG InterlockedIncrement(volatile LONG*p) { LONG next=*p+1;*p=next;return next; }
inline HANDLE CreateFileA(const char*,DWORD,DWORD,void*,DWORD,DWORD,void*) { return INVALID_HANDLE_VALUE; }
inline DWORD SetFilePointer(HANDLE,LONG,void*,DWORD) { return 0; }
inline BOOL WriteFile(HANDLE,const void*,DWORD,DWORD*,void*) { return FALSE; }
inline BOOL CloseHandle(HANDLE) { return TRUE; }
extern "C" {
BOOL JglReady();
void JglScalarVector(unsigned, unsigned, const void*);
int GetPixelFormat(HDC);
HGLRC wglCreateContext(HDC); BOOL wglDeleteContext(HGLRC); BOOL wglMakeCurrent(HDC,HGLRC);
HGLRC wglGetCurrentContext(); BOOL wglShareLists(HGLRC,HGLRC); PROC wglGetProcAddress(LPCSTR);
}
'''
    existing_macros = set(re.findall(r"#define\s+(\w+)", header))
    macros = re.findall(r"^#define (GL_\w+) ([^\n]+)", (HERE / "frontend-gl.h").read_text(), re.M)
    header += "\n" + "\n".join(f"#define {name} {value}" for name,value in macros if name not in existing_macros)
    enum_constants = dict(re.findall(r"pub const (GL_\w+): [^=]+ = (\d+);", (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()))
    all_macros = set(re.findall(r"#define\s+(\w+)", header))
    required_constants = set(re.findall(r"\bGL_[A-Z0-9_]+\b", ((SOURCE / "icd-pixels.inc").read_text() + (SOURCE / "icd-images.inc").read_text() + (SOURCE / "icd-textures.inc").read_text())))
    header += "\n" + "\n".join(f"#define {name} {enum_constants[name]}" for name in sorted(required_constants-all_macros))
    header += '\n#include "gl-funcs.h"\n#include "gl.h"\n' 
    header += '\n'.join(f'using {name}={value};' for name, value in types.items())
    header += '\n' + '\n'.join(declarations)
    actual_internal = (SOURCE / "internal.h").read_text()
    header += "\n" + actual_internal[actual_internal.index("typedef struct {"):actual_internal.index("#ifdef __cplusplus")]
    header += r"""
extern "C" {
JGL_ARRAY_STATE* JglArrays(); JGL_UNPACK* JglPack(); JGL_UNPACK* JglUnpack();
void JglArrayElement(GLint);void JglInterleavedArrays(GLenum,GLsizei,const void*);
ULONG JglNextImageId(); ULONG JglMaxDataBytes(ULONG);
BOOL JglData(ULONG,const ULONG*,ULONG,const void*,ULONG);
BOOL JglQuery(ULONG,const ULONG*,ULONG,void*,ULONG,ULONG*);
}
"""

    (root / "internal.h").write_text(header)
    test = r'''
#include "icd.cpp"
#include <cassert>
int LastCall=-1, ErrorValue=0, LastError=0; float Numbers[4];
static HGLRC Current=nullptr; static int SystemPixel=0, Pixel=0, Swaps=0, Callbacks=0; static bool Fail=false;
extern "C" {
HGLRC wglCreateContext(HDC) { return reinterpret_cast<HGLRC>(42); }
BOOL wglDeleteContext(HGLRC c) { return c==reinterpret_cast<HGLRC>(42); }
BOOL wglMakeCurrent(HDC,HGLRC c) { if(Fail)return FALSE; Current=c; return TRUE; }
HGLRC wglGetCurrentContext() { return Current; }
BOOL wglShareLists(HGLRC,HGLRC) { return TRUE; }
PROC wglGetProcAddress(LPCSTR) { return nullptr; }
int wglDescribePixelFormat(HDC,int,UINT,PIXELFORMATDESCRIPTOR*p) { if(p)p->marker=7;return 1; }
JGL_ARRAY_STATE* JglArrays(){static JGL_ARRAY_STATE state{};return &state;}
JGL_UNPACK* JglPack(){static JGL_UNPACK state{};return &state;}
JGL_UNPACK* JglUnpack(){static JGL_UNPACK state{};return &state;}
void JglArrayElement(GLint){} void JglInterleavedArrays(GLenum,GLsizei,const void*){}
ULONG JglNextImageId(){return 1;} ULONG JglMaxDataBytes(ULONG){return 65536;}
BOOL JglData(ULONG,const ULONG*,ULONG,const void*,ULONG){return FALSE;}
BOOL JglQuery(ULONG,const ULONG*,ULONG,void*,ULONG,ULONG*){return FALSE;}
BOOL JglReady(){return TRUE;}
void JglScalarVector(unsigned,unsigned,const void*){}
int GetPixelFormat(HDC) { return SystemPixel; }
int wglGetPixelFormat(HDC) { return Pixel; }
BOOL wglSetPixelFormat(HDC,int p,const PIXELFORMATDESCRIPTOR*) { Pixel=p;return TRUE; }
BOOL wglSwapBuffers(HDC) { ++Swaps;return TRUE; }
}
static void Set(const ClientTable* t) { assert(t==&Table);++Callbacks; }
'''
    test += '\n'.join(definitions)
    test += r'''
int main() {
    HDC dc=reinterpret_cast<HDC>(5);
    assert(!DrvValidateVersion(2)); assert(DrvValidateVersion(1));
    assert(!DrvCreateContext(dc)); assert(!DrvSetPixelFormat(dc,2));
    assert(DrvSetPixelFormat(dc,1)); assert(DrvSetPixelFormat(dc,1));
    Pixel=0;SystemPixel=1;HGLRC c=DrvCreateContext(dc);assert(c&&Pixel==1);assert(!DrvCreateLayerContext(dc,1));
    Fail=true;assert(!DrvSetContext(dc,c,Set));assert(Callbacks==0);
    Fail=false;auto*t=DrvSetContext(dc,c,Set);assert(t&&t->Count==336&&Callbacks==1);
    t->Functions.Finish();assert(LastCall==216);
    t->Functions.Vertex3d(1.25,-2.5,3.75);assert(Numbers[0]==1.25f&&Numbers[1]==-2.5f&&Numbers[2]==3.75f);
    const GLshort v[]={12,-7};t->Functions.TexCoord2sv(v);assert(Numbers[0]==12&&Numbers[1]==-7);
    t->Functions.Rotated(90,1,0,0);assert(Numbers[0]==90&&Numbers[1]==1);
    t->Functions.NewList(1,0);assert(ErrorValue==GL_INVALID_OPERATION);
    assert(DgIcdUnsupportedSlot()==0&&LastError==ERROR_CALL_NOT_IMPLEMENTED);
    assert(t->Functions.GenLists(1)==0);assert(DgIcdUnsupportedSlot()==5);
    assert(!DrvSwapLayerBuffers(dc,2));assert(Swaps==0);
    assert(DrvSwapLayerBuffers(dc,1));assert(Swaps==1);
    assert(!DrvReleaseContext(reinterpret_cast<HGLRC>(6)));assert(Current==c);
    assert(DrvReleaseContext(c));assert(!Current);assert(DrvDeleteContext(c));
    LastError=777;for(unsigned n=0;n<100;++n)IcdTrace("test",n,0);
    assert(IcdTraceRecords==64&&LastError==777);
}
'''
    # TexCoord2sv already exists, so test an actually added pointer alias.
    test = test.replace('const GLshort v[]={12,-7};t->Functions.TexCoord2sv(v);',
                        'const GLint v[]={12,-7};t->Functions.TexCoord2iv(v);')
    (root / "test.cpp").write_text(test)
    command = [os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
               "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined", "-g",
               "-I" + str(ROOT / "guest/include"), str(root / "test.cpp"), "-o", str(root / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(root / "test")], check=True)
print(f"PASS ICD336 typed slots; {coverage['aliases']} real aliases; {len(coverage['unimplemented'])} explicit gaps; {len(coverage.get('partial', []))} partial slot")
