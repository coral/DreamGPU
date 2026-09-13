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
assert coverage["production_registration_ready"]
assert not coverage["unimplemented"]
assert [(item["slot"], item["name"]) for item in coverage["partial"]] == [(248, "glPixelTransferi"), (257, "glDrawPixels"), (323, "glCopyTexImage1D"), (325, "glCopyTexSubImage1D")]
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
    for name in ("icd.cpp", "icd-trace.h", "icd-numeric.inc", "icd-state.inc", "icd-raster.inc", "icd-client.inc", "icd-pixels.inc", "icd-images.inc", "icd-textures.inc", "icd-fixed.inc", "icd-evaluator.inc", "icd-selection.inc", "icd-lists.inc", "icd-functions.inc", "icd-slots.inc", "icd-table.inc"):
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
        if "gl" + name not in public and name not in ("IndexPointer", "EdgeFlagPointer"):
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
#define JglCommandError JglSetError
#define JglCommandReady JglReady
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
BOOL JglListMode(ULONG); BOOL JglCompiling(); void JglCallList(ULONG); BOOL JglCallLists(ULONG,const ULONG*);
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
    required_constants = set(re.findall(r"\bGL_[A-Z0-9_]+\b", ((SOURCE / "icd-pixels.inc").read_text() + (SOURCE / "icd-images.inc").read_text() + (SOURCE / "icd-textures.inc").read_text() + (SOURCE / "icd-fixed.inc").read_text() + (SOURCE / "icd-evaluator.inc").read_text() + (SOURCE / "icd-selection.inc").read_text() + (SOURCE / "icd-lists.inc").read_text())))
    required_constants.update(("GL_MAP1_VERTEX_3","GL_MAP2_VERTEX_3"))
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
static ULONG ListReply=0,ListCount=0,ListBytes=0;static bool ListShort=false;
static ULONG CaptureMode=GL_RENDER,CaptureReply[3]={},CaptureQueries=0,CaptureShortAt=~0u;static bool CaptureShortHeader=false;
static unsigned ScalarFn,ScalarWords;static BYTE ScalarArgs[40],Pattern[128],MapPayload[2080];static unsigned ReplyBytes=128,MapFunction,MapBytes,MapArgs[3];static bool MapShort=false;static bool FixedReady=true;
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
ULONG JglNextImageId(){return 1;} ULONG JglMaxDataBytes(ULONG){return ListBytes?ListBytes:65536;}
BOOL JglData(ULONG fn,const ULONG*a,ULONG words,const void*p,ULONG bytes){
 if(fn==FEnum_glMap1d||fn==FEnum_glMap1f||fn==FEnum_glMap2d||fn==FEnum_glMap2f){assert(words==2||words==3);assert(bytes<=sizeof(MapPayload));MapFunction=fn;MapBytes=bytes;memcpy(MapArgs,a,words*4);memcpy(MapPayload,p,bytes);return TRUE;}
 assert(fn==FEnum_glPolygonStipple&&words==0&&bytes==128);memcpy(Pattern,p,128);return TRUE;}
BOOL JglQuery(ULONG fn,const ULONG*a,ULONG type,void*p,ULONG cap,ULONG*bytes){
 if(fn==FEnum_glNewList||fn==FEnum_glEndList||fn==FEnum_glDeleteLists||fn==FEnum_glGenLists){++ListCount;assert(cap==(fn==FEnum_glGenLists?8:4));((ULONG*)p)[0]=ListReply;if(cap==8)((ULONG*)p)[1]=123;*bytes=cap-(ListShort?4:0);return TRUE;}
 if(fn==FEnum_glIsList){++ListCount;assert(cap==1);*(BYTE*)p=1;*bytes=ListShort?0:1;return TRUE;}
 if(fn==FEnum_glSelectBuffer||fn==FEnum_glFeedbackBuffer){assert(cap==4);*(ULONG*)p=0;*bytes=4;++CaptureQueries;return TRUE;}
 if(fn==FEnum_glRenderMode){++CaptureQueries;if(a[0]){assert(cap==12);memcpy(p,CaptureReply,12);*bytes=CaptureShortHeader?8:12;CaptureMode=a[0];return TRUE;}assert(cap==a[2]*4);for(ULONG i=0;i<a[2];++i)((ULONG*)p)[i]=0x12340000+a[1]+i;*bytes=cap-(a[1]==CaptureShortAt?4:0);return TRUE;}

 if(fn==FEnum_glGetIntegerv){assert(a[0]==GL_MAX_EVAL_ORDER&&cap==4);*(GLint*)p=8;*bytes=4;return TRUE;}
 if(fn==FEnum_glGetMapiv||fn==FEnum_glGetMapfv||fn==FEnum_glGetMapdv){
 if(a[1]==GL_ORDER){assert(cap==a[2]*4);GLint order[]={2,3};memcpy(p,order,cap);*bytes=cap;return TRUE;}
 assert(a[1]==GL_COEFF&&a[2]==18&&cap==18*4);for(unsigned i=0;i<18;++i)((GLuint*)p)[i]=0x1020300+i;*bytes=cap-(MapShort?4:0);return TRUE;}
 assert(fn==FEnum_glGetPolygonStipple&&!a[0]&&!a[1]&&!a[2]&&type==DG_GL_RESULT_INT&&cap==128);memcpy(p,Pattern,128);*bytes=ReplyBytes;return TRUE;}
BOOL JglReady(){return FixedReady;}
BOOL JglListMode(ULONG mode){JglArrays()->ListMode=mode;return TRUE;}
BOOL JglCompiling(){return JglArrays()->ListMode!=0;}
void JglCallList(ULONG name){ScalarFn=FEnum_glCallList;ScalarWords=1;memcpy(ScalarArgs,&name,4);}
BOOL JglCallLists(ULONG count,const ULONG*data){MapFunction=FEnum_glCallLists;MapBytes=count*4;assert(MapBytes<=sizeof(MapPayload));memcpy(MapPayload,data,MapBytes);return TRUE;}

void JglScalarVector(unsigned fn,unsigned words,const void*p){assert(words<=10);ScalarFn=fn;ScalarWords=words;memcpy(ScalarArgs,p,words*4);}
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

    t->Functions.Indexi(2147483647);GLdouble index;memcpy(&index,ScalarArgs,8);assert(ScalarFn==FEnum_glIndexd&&ScalarWords==2&&index==2147483647.0);
    GLboolean edge=7;t->Functions.EdgeFlagv(&edge);assert(ScalarFn==FEnum_glEdgeFlag&&ScalarArgs[0]==1);
    t->Functions.ClearAccum(-2,.25f,.5f,2);assert(ScalarFn==FEnum_glClearAccum&&ScalarWords==4);
    BYTE input[280];memset(input,0x69,sizeof(input));*JglUnpack()={8,40,1,3,0,1};
    t->Functions.PolygonStipple(input);for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)assert(bool(Pattern[y*4+x/8]&(1<<(7-x%8)))==bool(input[8+y*8+(3+x)/8]&(1<<((3+x)%8))));
    BYTE output[280],expected[280];memset(output,0xa5,sizeof(output));memcpy(expected,output,sizeof(output));*JglPack()={8,40,1,3,0,1};
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){BYTE &v=expected[8+y*8+(3+x)/8];BYTE bit=1<<((3+x)%8);if(Pattern[y*4+x/8]&(1<<(7-x%8)))v|=bit;else v&=~bit;}
    t->Functions.GetPolygonStipple(output);assert(!memcmp(output,expected,sizeof(output)));
    memset(output,0xa5,sizeof(output));ReplyBytes=127;t->Functions.GetPolygonStipple(output);for(BYTE v:output)assert(v==0xa5);assert(ErrorValue==GL_INVALID_OPERATION);ReplyBytes=128;
    FixedReady=false;t->Functions.PolygonStipple((BYTE*)1);t->Functions.GetPolygonStipple((BYTE*)1);FixedReady=true;

    GLfloat control[32];for(unsigned n=0;n<32;++n)control[n]=n+.25f;
    t->Functions.Map2f(GL_MAP2_VERTEX_3,0,1,17,2,2,4,5,3,control);assert(MapFunction==FEnum_glMap2f&&MapArgs[0]==GL_MAP2_VERTEX_3&&MapArgs[1]==2&&MapArgs[2]==3&&MapBytes==(4+18)*4);
    GLfloat packed[22];memcpy(packed,MapPayload,MapBytes);assert(packed[0]==0&&packed[1]==1&&packed[2]==2&&packed[3]==4);
    for(unsigned u=0;u<2;++u)for(unsigned v=0;v<3;++v)for(unsigned c=0;c<3;++c)assert(packed[4+(u*3+v)*3+c]==control[u*17+v*5+c]);
    const unsigned previous=MapBytes;ErrorValue=0;t->Functions.Map2d(GL_MAP2_VERTEX_4,0,1,4,9,0,1,4,1,(GLdouble*)1);assert(ErrorValue==GL_INVALID_VALUE&&MapBytes==previous);
    ErrorValue=0;t->Functions.Map1f(GL_MAP1_VERTEX_3,0,1,2,2,(GLfloat*)1);assert(ErrorValue==GL_INVALID_VALUE&&MapBytes==previous);
    ErrorValue=0;t->Functions.Map1d(GL_MAP1_VERTEX_3,0,1,3,2,(GLdouble*)(~(ULONG_PTR)0-1));assert(ErrorValue==GL_INVALID_VALUE&&MapBytes==previous);
    GLuint coeff[20];for(auto&v:coeff)v=0xa5a5a5a5;t->Functions.GetMapfv(GL_MAP2_VERTEX_3,GL_COEFF,(GLfloat*)coeff);for(unsigned i=0;i<18;++i)assert(coeff[i]==0x1020300+i);assert(coeff[18]==0xa5a5a5a5);
    MapShort=true;for(auto&v:coeff)v=0xa5a5a5a5;t->Functions.GetMapfv(GL_MAP2_VERTEX_3,GL_COEFF,(GLfloat*)coeff);for(auto v:coeff)assert(v==0xa5a5a5a5);MapShort=false;
    t->Functions.MapGrid2d(7,-.25,.75,11,-1.5,2.5);assert(ScalarFn==FEnum_glMapGrid2d&&ScalarWords==10);GLdouble domain;memcpy(&domain,ScalarArgs+24,8);assert(domain==-1.5);
    ErrorValue=0;t->Functions.EvalMesh2(GL_POINT,0,65536,0,65536);assert(ErrorValue==GL_OUT_OF_MEMORY);
    ScalarFn=0xdead;t->Functions.EvalMesh2(GL_LINE,1,0,(-2147483647-1),2147483647);assert(ScalarFn==0xdead);
    t->Functions.EvalMesh1(GL_POINT,2147483647,2147483647);assert(ScalarFn==FEnum_glEvalMesh1);
    t->Functions.EvalCoord2f(.25f,.75f);assert(ScalarFn==FEnum_glEvalCoord2d&&ScalarWords==4);memcpy(&domain,ScalarArgs+8,8);assert(domain==.75);
    ULONG capture[514];for(auto&v:capture)v=0xa5a5a5a5;
    t->Functions.SelectBuffer(513,capture);assert(JglArrays()->SelectPointer==capture&&JglArrays()->SelectSize==513);
    t->Functions.RenderMode(GL_SELECT);assert(JglArrays()->CaptureMode==GL_SELECT);
    CaptureReply[1]=7;CaptureReply[2]=513;assert(t->Functions.RenderMode(GL_RENDER)==7);for(ULONG i=0;i<513;++i)assert(capture[i]==0x12340000+i);assert(capture[513]==0xa5a5a5a5);
    CaptureReply[1]=0;CaptureReply[2]=0;t->Functions.RenderMode(GL_SELECT);CaptureReply[1]=7;CaptureReply[2]=513;CaptureShortAt=256;for(auto&v:capture)v=0xa5a5a5a5;
    assert(t->Functions.RenderMode(GL_RENDER)==0);for(auto v:capture)assert(v==0xa5a5a5a5);CaptureShortAt=~0u;
    const ULONG cq=CaptureQueries;ErrorValue=0;t->Functions.FeedbackBuffer(-1,GL_2D,(GLfloat*)1);assert(ErrorValue==GL_INVALID_VALUE&&CaptureQueries==cq);
    ErrorValue=0;t->Functions.SelectBuffer(DG_GL_MAX_CAPTURE_VALUES+1,(GLuint*)1);assert(ErrorValue==GL_OUT_OF_MEMORY&&CaptureQueries==cq);
    ErrorValue=0;t->Functions.FeedbackBuffer(1,0,(GLfloat*)1);assert(ErrorValue==GL_INVALID_ENUM&&CaptureQueries==cq);
    FixedReady=false;t->Functions.SelectBuffer(1,(GLuint*)1);assert(!t->Functions.RenderMode(GL_SELECT));assert(CaptureQueries==cq);FixedReady=true;
    t->Functions.PushName(0xf1234567);assert(ScalarFn==FEnum_glPushName&&ScalarWords==1);ULONG exactname;memcpy(&exactname,ScalarArgs,4);assert(exactname==0xf1234567);
    t->Functions.PassThrough(-.25f);assert(ScalarFn==FEnum_glPassThrough);GLfloat token;memcpy(&token,ScalarArgs,4);assert(token==-.25f);
    t->Functions.NewList(1,0);assert(ErrorValue==GL_INVALID_ENUM);
    ErrorValue=0;t->Functions.NewList(7,GL_COMPILE);assert(JglCompiling());t->Functions.NewList(8,GL_COMPILE);assert(ErrorValue==GL_INVALID_OPERATION);
    t->Functions.EndList();assert(!JglCompiling());assert(t->Functions.GenLists(3)==123);assert(t->Functions.IsList(123));
    ListShort=true;ErrorValue=0;assert(!t->Functions.IsList(123)&&ErrorValue==GL_INVALID_OPERATION);ListShort=false;
    ErrorValue=0;auto listCount=ListCount;t->Functions.DeleteLists(1,-1);assert(ErrorValue==GL_INVALID_VALUE&&ListCount==listCount);
    const GLbyte signedNames[]={-1,2};t->Functions.CallLists(2,GL_BYTE,signedNames);assert(MapBytes==8);ULONG listNames[2];memcpy(listNames,MapPayload,8);assert(listNames[0]==0xffffffff&&listNames[1]==2);
    const BYTE packedNames[]={1,2,3,4,5,6};t->Functions.CallLists(2,GL_3_BYTES,packedNames);memcpy(listNames,MapPayload,8);assert(listNames[0]==0x010203&&listNames[1]==0x040506);
    const GLfloat floatNames[]={-2.75f,3.75f};t->Functions.CallLists(2,GL_FLOAT,floatNames);memcpy(listNames,MapPayload,8);assert(listNames[0]==0xfffffffe&&listNames[1]==3);
    ListBytes=4;ErrorValue=0;const auto previousListBytes=MapBytes;t->Functions.CallLists(2,GL_INT,(GLvoid*)1);assert(ErrorValue==GL_OUT_OF_MEMORY&&MapBytes==previousListBytes);ListBytes=0;
    ErrorValue=0;t->Functions.CallLists(0,GL_UNSIGNED_INT,nullptr);assert(!ErrorValue&&MapBytes==previousListBytes);

    assert(DgIcdUnsupportedSlot()==-1);

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
