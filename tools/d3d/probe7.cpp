/* SPDX-License-Identifier: GPL-2.0-or-later
 * D3D7 HAL fixed-work acceptance through the pinned Wine DirectDraw interface.
 */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include "entry.h"
#include <ddraw.h>
#include <d3d.h>
#ifdef DG_SYSTEM_D3D
#include "system-loader.h"
#define DG_LOG_PATH "C:\\DGSYS7.LOG"
#define DG_PROBE_NAME "sysd3d7"
#else
#define DG_LOG_PATH "C:\\DGD3D7.LOG"
#define DG_PROBE_NAME "d3d7"
#endif

static HANDLE LogFile;
static IDirectDraw7 *draw;
static IDirect3D7 *d3d;
static IDirect3DDevice7 *device;
static IDirectDrawSurface7 *primary, *target;
static IDirectDrawClipper *clipper;
static HWND window;
static BOOL failed;
static unsigned int(WINAPI *ProbeGlError)(void);
static void Log(const char *text) {
    DWORD written;
    WriteFile(LogFile, text, lstrlenA(text), &written, NULL);
    WriteFile(LogFile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(LogFile);
}
static void Number(const char *stage, DWORD value) {
    static const char hex[] = "0123456789abcdef";
    char message[] = "value=0x00000000";
    int i;
    Log(stage);
    for (i = 0; i < 8; i++)
        message[8 + i] = hex[(value >> (28 - i * 4)) & 15];
    Log(message);
}
static BOOL Check(BOOL condition, const char *stage, DWORD code) {
    if (condition)
        return TRUE;
    failed = TRUE;
    Number(stage, code);
    return FALSE;
}
static BOOL HR(HRESULT result, const char *stage) {
    return Check(SUCCEEDED(result), stage, (DWORD)result);
}
static LRESULT CALLBACK WindowProc(HWND w, UINT message, WPARAM a, LPARAM b) {
    return DefWindowProcA(w, message, a, b);
}
/* Vertex fog from the specular alpha (FOGVERTEXMODE and FOGTABLEMODE NONE) of
 * untransformed XYZ | DIFFUSE | SPECULAR vertices with identity transforms,
 * from a video-memory vertex buffer: DrawIndexedPrimitiveVB from start
 * vertices 4 and 8 (Wine's base vertex), DrawPrimitiveVB from vertex 12,
 * then a DrawPrimitive quad from user memory. (Wine's index buffer is sized
 * to the first draw, so both indexed draws start at index 0; the D3D8/9
 * probe covers a start index.) Each quad has its own constant alpha a, so
 * red diffuse fogged toward blue is exactly (a, 0, 255 - a); a fog coordinate
 * read from the wrong vertex (the decoy below vertex 4 has a = 255) shows as
 * another colour. Wine draws the buffer quads from arrays with a fog
 * coordinate array (Wine 0041); its former emulation gives the same pixels,
 * so this checks the result, not which path drew it. Every pixel is compared
 * except the band along each quad edge. */
static void FogBuffers(void) {
    struct FogVertex {
        float x, y, z;
        DWORD diffuse, specular;
    };
    struct Area {
        int left, top, right, bottom;
        DWORD alpha;
    };
    static const DWORD fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR, background = 0x102030;
    static const Area decoy = {20, 140, 100, 220, 0xff};
    static const Area quads[4] = {{20, 30, 100, 110, 0x40},
                                  {120, 30, 200, 110, 0xc0},
                                  {220, 30, 300, 110, 0x80},
                                  {220, 140, 300, 220, 0x20}};
    static WORD indices[6] = {0, 1, 2, 2, 1, 3};
    const auto corners = [](FogVertex *out, const Area &area) {
        const float left = area.left / 160.f - 1, right = area.right / 160.f - 1;
        const float top = 1 - area.top / 120.f, bottom = 1 - area.bottom / 120.f;
        const DWORD specular = area.alpha << 24;
        out[0] = {left, top, .5f, 0xffff0000, specular};
        out[1] = {right, top, .5f, 0xffff0000, specular};
        out[2] = {left, bottom, .5f, 0xffff0000, specular};
        out[3] = {right, bottom, .5f, 0xffff0000, specular};
    };
    D3DVERTEXBUFFERDESC desc = {};
    IDirect3DVertexBuffer7 *vb = NULL;
    FogVertex *mapped = NULL, user[4];
    D3DMATRIX identity = {};
    DDSURFACEDESC2 locked = {};
    BOOL ok;
    Log("STAGE vertex buffer fog");
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    desc.dwSize = sizeof(desc);
    desc.dwCaps = D3DVBCAPS_WRITEONLY;
    desc.dwFVF = fvf;
    desc.dwNumVertices = 16;
    corners(user, quads[3]);
    ok = HR(IDirect3D7_CreateVertexBuffer(d3d, &desc, &vb, 0), "FAIL fog vertex buffer creation") &&
         HR(IDirect3DVertexBuffer7_Lock(vb, DDLOCK_WAIT | DDLOCK_WRITEONLY, (void **)&mapped, NULL),
            "FAIL fog vertex buffer lock");
    if (ok) {
        ok = Check(mapped != NULL, "FAIL fog vertex buffer pointer", 0);
        if (ok) {
            corners(mapped, decoy);
            for (UINT i = 0; i < 3; ++i)
                corners(mapped + 4 + 4 * i, quads[i]);
        }
        ok = HR(IDirect3DVertexBuffer7_Unlock(vb), "FAIL fog vertex buffer unlock") && ok;
    }
    ok = ok &&
         HR(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_WORLD, &identity),
            "FAIL fog world") &&
         HR(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_VIEW, &identity),
            "FAIL fog view") &&
         HR(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_PROJECTION, &identity),
            "FAIL fog projection") &&
         HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_SPECULARENABLE, FALSE),
            "FAIL fog specular") &&
         HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_ALPHABLENDENABLE, FALSE),
            "FAIL fog blend") &&
         HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_FOGCOLOR, 0xff0000ff),
            "FAIL fog colour") &&
         HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_FOGTABLEMODE, D3DFOG_NONE),
            "FAIL fog table mode") &&
         HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_FOGVERTEXMODE, D3DFOG_NONE),
            "FAIL fog vertex mode") &&
         HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_FOGENABLE, TRUE),
            "FAIL fog enable") &&
         HR(IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET, 0xff000000 | background, 1, 0),
            "FAIL fog clear") &&
         HR(IDirect3DDevice7_BeginScene(device), "FAIL fog begin scene") &&
         HR(IDirect3DDevice7_DrawIndexedPrimitiveVB(device, D3DPT_TRIANGLELIST, vb, 4, 4, indices,
                                                    6, 0),
            "FAIL fog indexed draw from vertex 4") &&
         HR(IDirect3DDevice7_DrawIndexedPrimitiveVB(device, D3DPT_TRIANGLELIST, vb, 8, 4, indices,
                                                    6, 0),
            "FAIL fog indexed draw from vertex 8") &&
         HR(IDirect3DDevice7_DrawPrimitiveVB(device, D3DPT_TRIANGLESTRIP, vb, 12, 4, 0),
            "FAIL fog draw from vertex 12") &&
         HR(IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLESTRIP, fvf, user, 4, 0),
            "FAIL fog user memory draw") &&
         HR(IDirect3DDevice7_EndScene(device), "FAIL fog end scene");
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_FOGENABLE, FALSE);
    locked.dwSize = sizeof(locked);
    if (ok &&
        HR(IDirectDrawSurface7_Lock(target, NULL, &locked, DDLOCK_READONLY | DDLOCK_WAIT, NULL),
           "FAIL fog readback lock")) {
        UINT wrong = 0;
        if (Check(locked.lpSurface && locked.lPitch >= 1280, "FAIL fog readback layout",
                  locked.lPitch))
            for (int y = 0; y < 240; ++y)
                for (int x = 0; x < 320; ++x) {
                    DWORD expected = background;
                    BOOL edge = FALSE;
                    for (UINT i = 0; i < 4; ++i) {
                        const Area &a = quads[i];
                        if (x > a.left && x < a.right - 1 && y > a.top && y < a.bottom - 1)
                            expected = a.alpha << 16 | (255 - a.alpha);
                        else if (x >= a.left - 1 && x <= a.right && y >= a.top - 1 && y <= a.bottom)
                            edge = TRUE;
                    }
                    if (edge)
                        continue;
                    const DWORD actual =
                        *(DWORD *)((BYTE *)locked.lpSurface + y * locked.lPitch + x * 4) & 0xffffff;
                    if (actual != expected && !wrong++) {
                        Number("FAIL exact fog pixel", actual);
                        Number("fog pixel x", (DWORD)x);
                        Number("fog pixel y", (DWORD)y);
                        Number("fog pixel expected", expected);
                    }
                }
        if (wrong) {
            Number("fog wrong pixels", wrong);
            failed = TRUE;
        }
        HR(IDirectDrawSurface7_Unlock(target, NULL), "FAIL fog readback unlock");
        if (!failed)
            Log("PASS vertex buffer fog: specular-alpha fog of indexed, base-vertex, start-vertex "
                "and user-memory quads exact, decoy unseen");
    }
    if (vb)
        IDirect3DVertexBuffer7_Release(vb);
}
/* D3DCOLOR diffuse and specular arrays of untransformed XYZ | DIFFUSE |
 * SPECULAR vertices with lighting off, identity transforms and
 * SPECULARENABLE: each quad is exactly diffuse + specular. Every diffuse and
 * specular has red != blue, and so does every sum, so a red/blue swap in the
 * diffuse, the specular or both changes the pixel, as does a dropped specular.
 * With ARB_vertex_array_bgra (DG_CAP_GL_VERTEX_ARRAY_BGRA) Wine draws these
 * from GL_BGRA arrays instead of immediate mode. Quads: DrawPrimitive and
 * DrawIndexedPrimitive from user memory (ddraw's dynamic, system-memory
 * buffer), DrawPrimitiveVB from vertex 4 and DrawIndexedPrimitiveVB from
 * start vertex 8 of a D3DVBCAPS_SYSTEMMEMORY buffer and of a video-memory
 * one (a GL buffer object, its D3DCOLOR bytes no longer converted), and a
 * video-memory buffer locked with DDLOCK_DISCARDCONTENTS (which Wine turns
 * into a dynamic, system-memory buffer), drawn, then rewritten beyond that
 * quad with DDLOCK_NOOVERWRITE and drawn again. Decoy vertices below each
 * start vertex and between the indices must not appear. Every pixel is
 * compared except the band along each quad edge. */
static void ColorArrays(void) {
    struct ColorVertex {
        float x, y, z;
        DWORD diffuse, specular;
    };
    struct Area {
        int left, top, right, bottom;
    };
    struct Pair {
        DWORD diffuse, specular;
    };
    static_assert(sizeof(ColorVertex) == 20);
    static const DWORD fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR, background = 0x102030,
                       decoy_color = 0xffff00ff;
    /* (r, g, b) of diffuse + specular, e.g. (60,20,10) + (10,40,80) = (70,60,90). */
    static constexpr Pair pairs[8] = {{0xff602010, 0x00104080}, {0xff10a040, 0x00502008},
                                      {0xffc01060, 0x00203010}, {0xff3070b0, 0x00401020},
                                      {0xff20e080, 0x00600810}, {0xffa02000, 0x00106040},
                                      {0xff0040a0, 0x00801020}, {0xff50f010, 0x00300840}};
    constexpr auto byte = [](DWORD color, int shift) -> DWORD { return color >> shift & 0xff; };
    constexpr auto sum = [byte](Pair p) -> DWORD {
        return (byte(p.diffuse, 16) + byte(p.specular, 16)) << 16 |
               (byte(p.diffuse, 8) + byte(p.specular, 8)) << 8 |
               (byte(p.diffuse, 0) + byte(p.specular, 0));
    };
    constexpr auto telling = [byte, sum]() -> bool {
        for (const Pair &p : pairs)
            if (byte(p.diffuse, 16) == byte(p.diffuse, 0) ||
                byte(p.specular, 16) == byte(p.specular, 0) ||
                byte(sum(p), 16) == byte(sum(p), 0) ||
                byte(p.diffuse, 16) + byte(p.specular, 16) > 255 ||
                byte(p.diffuse, 8) + byte(p.specular, 8) > 255 ||
                byte(p.diffuse, 0) + byte(p.specular, 0) > 255)
                return false;
        return true;
    };
    static_assert(telling() && sum(pairs[0]) == 0x706090);
    /* Two rows of four; the decoy sits between the rows. */
    static const Area quads[8] = {{10, 20, 70, 90},     {90, 20, 150, 90},   {170, 20, 230, 90},
                                  {250, 20, 310, 90},   {10, 150, 70, 220},  {90, 150, 150, 220},
                                  {170, 150, 230, 220}, {250, 150, 310, 220}};
    static const Area decoy = {130, 105, 190, 135};
    static WORD list[6] = {4, 5, 6, 6, 5, 7}, local[6] = {0, 1, 2, 2, 1, 3};
    const auto corners = [](ColorVertex *out, const Area &area, DWORD diffuse, DWORD specular) {
        const float left = area.left / 160.f - 1, right = area.right / 160.f - 1;
        const float top = 1 - area.top / 120.f, bottom = 1 - area.bottom / 120.f;
        out[0] = {left, top, .5f, diffuse, specular};
        out[1] = {right, top, .5f, diffuse, specular};
        out[2] = {left, bottom, .5f, diffuse, specular};
        out[3] = {right, bottom, .5f, diffuse, specular};
    };
    const auto quad = [&](ColorVertex *out, UINT n) {
        corners(out, quads[n], pairs[n].diffuse, pairs[n].specular);
    };
    /* Vertices 0-3 a decoy, 4-7 quad first, 8-11 quad first + 1. */
    const auto fill = [&](ColorVertex *out, UINT first) {
        corners(out, decoy, decoy_color, decoy_color);
        quad(out + 4, first);
        quad(out + 8, first + 1);
    };
    /* Lock vb with flags and let write fill it. */
    const auto store = [](IDirect3DVertexBuffer7 *vb, DWORD flags, const char *stage,
                          auto write) -> BOOL {
        ColorVertex *mapped = NULL;
        if (!HR(IDirect3DVertexBuffer7_Lock(vb, DDLOCK_WAIT | DDLOCK_WRITEONLY | flags,
                                            (void **)&mapped, NULL),
                stage))
            return FALSE;
        const BOOL ok = Check(mapped != NULL, stage, 0);
        if (ok)
            write(mapped);
        return HR(IDirect3DVertexBuffer7_Unlock(vb), "FAIL colour vertex buffer unlock") && ok;
    };
    IDirect3DVertexBuffer7 *system = NULL, *video = NULL, *dynamic = NULL;
    ColorVertex user[4], indexed[8];
    D3DVERTEXBUFFERDESC desc = {};
    D3DMATRIX identity = {};
    DDSURFACEDESC2 locked = {};
    BOOL ok;
    Log("STAGE colour arrays");
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    quad(user, 0);
    corners(indexed, decoy, decoy_color, decoy_color);
    quad(indexed + 4, 1);
    desc.dwSize = sizeof(desc);
    desc.dwFVF = fvf;
    desc.dwNumVertices = 12;
    desc.dwCaps = D3DVBCAPS_SYSTEMMEMORY;
    ok = HR(IDirect3D7_CreateVertexBuffer(d3d, &desc, &system, 0),
            "FAIL colour system-memory vertex buffer creation");
    desc.dwCaps = D3DVBCAPS_WRITEONLY;
    ok = ok &&
         HR(IDirect3D7_CreateVertexBuffer(d3d, &desc, &video, 0),
            "FAIL colour video-memory vertex buffer creation") &&
         HR(IDirect3D7_CreateVertexBuffer(d3d, &desc, &dynamic, 0),
            "FAIL colour discarded vertex buffer creation") &&
         store(system, 0, "FAIL colour system-memory vertex buffer lock",
               [&](ColorVertex *out) { fill(out, 2); }) &&
         store(video, 0, "FAIL colour video-memory vertex buffer lock",
               [&](ColorVertex *out) { fill(out, 4); });
    ok = ok &&
         HR(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_WORLD, &identity),
            "FAIL colour world") &&
         HR(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_VIEW, &identity),
            "FAIL colour view") &&
         HR(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_PROJECTION, &identity),
            "FAIL colour projection") &&
         HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_LIGHTING, FALSE),
            "FAIL colour lighting") &&
         HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_FOGENABLE, FALSE),
            "FAIL colour fog") &&
         HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_ALPHABLENDENABLE, FALSE),
            "FAIL colour blend") &&
         HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_SPECULARENABLE, TRUE),
            "FAIL colour specular") &&
         HR(IDirect3DDevice7_SetTexture(device, 0, NULL), "FAIL colour texture") &&
         HR(IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL colour operation") &&
         HR(IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
            "FAIL colour argument") &&
         HR(IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET, 0xff000000 | background, 1, 0),
            "FAIL colour clear") &&
         HR(IDirect3DDevice7_BeginScene(device), "FAIL colour begin scene") &&
         HR(IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLESTRIP, fvf, user, 4, 0),
            "FAIL colour user-memory draw") &&
         HR(IDirect3DDevice7_DrawIndexedPrimitive(device, D3DPT_TRIANGLELIST, fvf, indexed, 8, list,
                                                  6, 0),
            "FAIL colour user-memory indexed draw") &&
         HR(IDirect3DDevice7_DrawPrimitiveVB(device, D3DPT_TRIANGLESTRIP, system, 4, 4, 0),
            "FAIL colour system-memory draw from vertex 4") &&
         HR(IDirect3DDevice7_DrawIndexedPrimitiveVB(device, D3DPT_TRIANGLELIST, system, 8, 4, local,
                                                    6, 0),
            "FAIL colour system-memory indexed draw from vertex 8") &&
         HR(IDirect3DDevice7_DrawPrimitiveVB(device, D3DPT_TRIANGLESTRIP, video, 4, 4, 0),
            "FAIL colour video-memory draw from vertex 4") &&
         HR(IDirect3DDevice7_DrawIndexedPrimitiveVB(device, D3DPT_TRIANGLELIST, video, 8, 4, local,
                                                    6, 0),
            "FAIL colour video-memory indexed draw from vertex 8") &&
         store(dynamic, DDLOCK_DISCARDCONTENTS, "FAIL colour DISCARDCONTENTS lock",
               [&](ColorVertex *out) { quad(out, 6); }) &&
         HR(IDirect3DDevice7_DrawPrimitiveVB(device, D3DPT_TRIANGLESTRIP, dynamic, 0, 4, 0),
            "FAIL colour discarded buffer draw") &&
         store(dynamic, DDLOCK_NOOVERWRITE, "FAIL colour NOOVERWRITE lock",
               [&](ColorVertex *out) {
                   corners(out + 4, decoy, decoy_color, decoy_color);
                   quad(out + 8, 7);
               }) &&
         HR(IDirect3DDevice7_DrawIndexedPrimitiveVB(device, D3DPT_TRIANGLELIST, dynamic, 8, 4,
                                                    local, 6, 0),
            "FAIL colour NOOVERWRITE indexed draw from vertex 8") &&
         HR(IDirect3DDevice7_EndScene(device), "FAIL colour end scene");
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_SPECULARENABLE, FALSE);
    locked.dwSize = sizeof(locked);
    if (ok &&
        HR(IDirectDrawSurface7_Lock(target, NULL, &locked, DDLOCK_READONLY | DDLOCK_WAIT, NULL),
           "FAIL colour readback lock")) {
        UINT wrong = 0;
        if (Check(locked.lpSurface && locked.lPitch >= 1280, "FAIL colour readback layout",
                  locked.lPitch))
            for (int y = 0; y < 240; ++y)
                for (int x = 0; x < 320; ++x) {
                    DWORD expected = background;
                    BOOL edge = FALSE;
                    for (UINT i = 0; i < 8; ++i) {
                        const Area &a = quads[i];
                        if (x > a.left && x < a.right - 1 && y > a.top && y < a.bottom - 1)
                            expected = sum(pairs[i]);
                        else if (x >= a.left - 1 && x <= a.right && y >= a.top - 1 && y <= a.bottom)
                            edge = TRUE;
                    }
                    if (edge)
                        continue;
                    const DWORD actual =
                        *(DWORD *)((BYTE *)locked.lpSurface + y * locked.lPitch + x * 4) & 0xffffff;
                    if (actual != expected && !wrong++) {
                        Number("FAIL exact colour pixel", actual);
                        Number("colour pixel x", (DWORD)x);
                        Number("colour pixel y", (DWORD)y);
                        Number("colour pixel expected", expected);
                    }
                }
        if (wrong) {
            Number("colour wrong pixels", wrong);
            failed = TRUE;
        }
        HR(IDirectDrawSurface7_Unlock(target, NULL), "FAIL colour readback unlock");
        if (!failed)
            Log("PASS colour arrays: D3DCOLOR diffuse plus specular of user-memory, "
                "system-memory, video-memory and DISCARDCONTENTS/NOOVERWRITE buffer quads exact, "
                "decoys unseen");
    }
    if (dynamic)
        IDirect3DVertexBuffer7_Release(dynamic);
    if (video)
        IDirect3DVertexBuffer7_Release(video);
    if (system)
        IDirect3DVertexBuffer7_Release(system);
}
static void Run(void) {
    typedef HRESULT(WINAPI * CreateDraw)(GUID *, void **, REFIID, IUnknown *);
    HMODULE gl, runtime;
#ifndef DG_SYSTEM_D3D
    char path[MAX_PATH];
#endif
    WNDCLASSA klass = {};
    RECT bounds = {0, 0, 320, 240}, client;
    POINT origin = {0, 0};
    DDSURFACEDESC2 desc = {}, locked = {};
    D3DVIEWPORT7 viewport = {0, 0, 320, 240, 0, 1};
    struct Vertex {
        float x, y, z, rhw;
        DWORD color;
    } vertices[3] = {{80, 60, .5f, 1, 0xffff0000},
                     {240, 60, .5f, 1, 0xffff0000},
                     {160, 180, .5f, 1, 0xffff0000}};
    const UINT region_x[5] = {0, 152, 304, 0, 304};
    const UINT region_y[5] = {0, 92, 0, 224, 224};
    const DWORD region_color[5] = {0x102030, 0xff0000, 0x00ff00, 0x00ffff, 0xffff00};
    UINT region, x, y;
#ifdef DG_SYSTEM_D3D
    Log("STAGE ordinary system DirectDraw loader");
    runtime = system_loader::load("ddraw.dll", Log);
    gl = nullptr;
    if (!Check(runtime != nullptr, "FAIL system DirectDraw loader or private neighbor",
               GetLastError()))
        return;
#else
    Log("STAGE explicit application-local DreamGPU OpenGL loader");
    gl = LoadLibraryA("C:\\SIERRA\\Half-Life\\dgpugl.dll");
    if (!Check(gl != NULL, "FAIL load DreamGPU OpenGL", GetLastError()))
        return;
    path[0] = 0;
    GetModuleFileNameA(gl, path, sizeof(path));
    Log(path);
    if (!Check(lstrcmpiA(path, "C:\\SIERRA\\Half-Life\\dgpugl.dll") == 0,
               "FAIL system OpenGL fallback is forbidden", 0))
        return;
    ProbeGlError = Entry<decltype(ProbeGlError)>(gl, "glGetError");
    runtime = LoadLibraryA("C:\\SIERRA\\Half-Life\\winedd.dll");
    if (!Check(runtime != NULL, "FAIL load Wine DirectDraw interface", GetLastError()))
        return;
    if (!Check(GetModuleHandleA("dgpugl.dll") == gl, "FAIL Wine OpenGL module identity", 0))
        return;
#endif
    const auto create = Entry<CreateDraw>(runtime, "DirectDrawCreateEx");
    if (!Check(create != nullptr, "FAIL DirectDraw factory", GetLastError()))
        return;
    Log("STAGE create DirectDraw7 and Direct3D7");
    if (!HR(create(NULL, (void **)&draw, IID_IDirectDraw7, NULL), "FAIL DirectDraw7 creation") ||
        !HR(IDirectDraw7_QueryInterface(draw, IID_IDirect3D7, (void **)&d3d),
            "FAIL Direct3D7 query"))
        return;
#ifdef DG_SYSTEM_D3D
    if (!Check(system_loader::object(draw->lpVtbl, "winedd.dll"),
               "FAIL actual system Wine DirectDraw object/dependencies", 0))
        return;
    gl = GetModuleHandleA("dgpugl.dll");
    ProbeGlError = Entry<decltype(ProbeGlError)>(gl, "glGetError");
#endif
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "DreamGPUD3D7Probe";
    if (!Check(RegisterClassA(&klass) != 0, "FAIL class registration", GetLastError()) ||
        !Check(AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE), "FAIL window bounds",
               GetLastError()))
        return;
    window = CreateWindowA(klass.lpszClassName, "DreamGPU D3D7 probe",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 64, 64, bounds.right - bounds.left,
                           bounds.bottom - bounds.top, NULL, NULL, klass.hInstance, NULL);
    if (!Check(window != NULL, "FAIL window creation", GetLastError()) ||
        !Check(GetClientRect(window, &client) && client.right == 320 && client.bottom == 240,
               "FAIL drawable size", 0))
        return;
    if (!HR(IDirectDraw7_SetCooperativeLevel(draw, window, DDSCL_NORMAL), "FAIL cooperative level"))
        return;
    {
        DDSCAPS2 caps = {};
        DWORD total = 0, available = 0;
        DDSURFACEDESC2 mode = {};
        caps.dwCaps = DDSCAPS_VIDEOMEMORY;
        mode.dwSize = sizeof(mode);
        if (HR(IDirectDraw7_GetAvailableVidMem(draw, &caps, &total, &available),
               "FAIL video-memory query")) {
            Number("reported video memory total", total);
            Number("reported video memory available", available);
        }
        if (HR(IDirectDraw7_GetDisplayMode(draw, &mode), "FAIL display-mode query")) {
            Number("desktop width", mode.dwWidth);
            Number("desktop height", mode.dwHeight);
            Number("desktop bits per pixel", mode.ddpfPixelFormat.dwRGBBitCount);
        }
        if (failed)
            return;
    }
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (!HR(IDirectDraw7_CreateSurface(draw, &desc, &primary, NULL), "FAIL primary surface") ||
        !HR(IDirectDraw7_CreateClipper(draw, 0, &clipper, NULL), "FAIL clipper") ||
        !HR(IDirectDrawClipper_SetHWnd(clipper, 0, window), "FAIL clipper window") ||
        !HR(IDirectDrawSurface7_SetClipper(primary, clipper), "FAIL primary clipper"))
        return;
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = 320;
    desc.dwHeight = 240;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
    desc.ddpfPixelFormat.dwGBitMask = 0xff00;
    desc.ddpfPixelFormat.dwBBitMask = 0xff;
    if (ProbeGlError)
        Number("GL error before HAL", ProbeGlError());
    Log("STAGE create HAL render target and device");
    if (!HR(IDirectDraw7_CreateSurface(draw, &desc, &target, NULL), "FAIL render target") ||
        !HR(IDirect3D7_CreateDevice(d3d, IID_IDirect3DHALDevice, target, &device),
            "FAIL HAL device") ||
        !HR(IDirect3DDevice7_SetViewport(device, &viewport), "FAIL viewport"))
        return;
    if (!HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_LIGHTING, FALSE),
            "FAIL lighting") ||
        !HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_ZENABLE, FALSE), "FAIL depth") ||
        !HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE),
            "FAIL culling") ||
        !HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_DITHERENABLE, FALSE),
            "FAIL dither") ||
        !HR(IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL color operation") ||
        !HR(IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
            "FAIL diffuse argument"))
        return;
    if (ProbeGlError)
        Number("GL error before draw", ProbeGlError());
    Log("STAGE clear and fixed triangle");
    if (!HR(IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET, 0xff102030, 1, 0),
            "FAIL clear") ||
        !HR(IDirect3DDevice7_BeginScene(device), "FAIL begin scene") ||
        !HR(IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST,
                                           D3DFVF_XYZRHW | D3DFVF_DIFFUSE, vertices, 3, 0),
            "FAIL draw") ||
        !HR(IDirect3DDevice7_EndScene(device), "FAIL end scene"))
        return;
    /* Distinct far-edge colors detect clipping of the desktop-sized primary
     * through a smaller window backing, including clamped-edge replication. */
    for (region = 2; region < 5; ++region) {
        D3DRECT edge = {};
        edge.x1 = (LONG)region_x[region];
        edge.y1 = (LONG)region_y[region];
        edge.x2 = edge.x1 + 16;
        edge.y2 = edge.y1 + 16;
        if (!HR(IDirect3DDevice7_Clear(device, 1, &edge, D3DCLEAR_TARGET,
                                       0xff000000 | region_color[region], 1, 0),
                "FAIL distinct far-edge clear"))
            return;
    }
    if (ProbeGlError)
        Number("GL error after draw", ProbeGlError());
    locked.dwSize = sizeof(locked);
    Log("STAGE real render-target readback");
    if (!HR(IDirectDrawSurface7_Lock(target, NULL, &locked, DDLOCK_READONLY | DDLOCK_WAIT, NULL),
            "FAIL readback lock"))
        return;
    if (Check(locked.lpSurface && locked.lPitch >= 1280, "FAIL readback layout", locked.lPitch)) {
        for (region = 0; region < 5 && !failed; region++)
            for (y = 0; y < 16 && !failed; y++)
                for (x = 0; x < 16; x++) {
                    UINT xx = x + region_x[region], yy = y + region_y[region];
                    DWORD expected = region_color[region];
                    DWORD actual =
                        *(DWORD *)((BYTE *)locked.lpSurface + yy * locked.lPitch + xx * 4) &
                        0xffffff;
                    if (!Check(actual == expected, "FAIL exact D3D7 pixel", actual)) {
                        Number("pixel x", xx);
                        Number("pixel y", yy);
                        Number("expected", expected);
                        break;
                    }
                }
    }
    if (!HR(IDirectDrawSurface7_Unlock(target, NULL), "FAIL unlock") || failed)
        return;
    if (ProbeGlError)
        Number("GL error before Blt", ProbeGlError());
    Log("STAGE clipped window presentation");
    if (!Check(ClientToScreen(window, &origin), "FAIL window origin", GetLastError()))
        return;
    OffsetRect(&client, origin.x, origin.y);
    if (!HR(IDirectDrawSurface7_Blt(primary, &client, target, NULL, DDBLT_WAIT, NULL),
            "FAIL present blit"))
        return;
    if (ProbeGlError)
        Number("GL error after Blt", ProbeGlError());
    {
        HDC dc = GetDC(window);
        if (!Check(dc != NULL, "FAIL presented window DC", GetLastError()))
            return;
        Log("STAGE actual clipped GPU front-buffer GDI readback");
        for (region = 0; region < 5 && !failed; region++)
            for (y = 0; y < 16 && !failed; y++)
                for (x = 0; x < 16; x++) {
                    UINT xx = x + region_x[region], yy = y + region_y[region];
                    DWORD color = region_color[region];
                    COLORREF expected = RGB((color >> 16) & 255, (color >> 8) & 255, color & 255);
                    COLORREF actual = GetPixel(dc, xx, yy);
                    if (!Check(actual == expected, "FAIL exact presented D3D7 pixel", actual)) {
                        Number("pixel x", xx);
                        Number("pixel y", yy);
                        Number("expected", expected);
                        {
                            typedef HDC(WINAPI * CurrentDC)(void);
                            typedef void(WINAPI * ReadBuffer)(unsigned int);
                            typedef void(WINAPI * ReadPixels)(int, int, int, int, unsigned int,
                                                              unsigned int, void *);
                            typedef unsigned int(WINAPI * GetError)(void);
                            CurrentDC current = Entry<CurrentDC>(gl, "wglGetCurrentDC");
                            ReadBuffer buffer = Entry<ReadBuffer>(gl, "glReadBuffer");
                            ReadPixels pixels = Entry<ReadPixels>(gl, "glReadPixels");
                            GetError error = Entry<GetError>(gl, "glGetError");
                            HWND fg = GetForegroundWindow(),
                                 active = current ? WindowFromDC(current()) : NULL;
                            POINT point = {(int)xx, (int)yy};
                            RECT bounds;
                            DWORD pid = 0;
                            BYTE rgba[4] = {};
                            HDC screen;
                            Number("owned HWND", (DWORD)window);
                            Number("foreground HWND", (DWORD)fg);
                            GetWindowThreadProcessId(fg, &pid);
                            Number("foreground PID", pid);
                            Number("own PID", GetCurrentProcessId());
                            Number("current GL HWND", (DWORD)active);
                            ClientToScreen(window, &point);
                            Number("screen x", point.x);
                            Number("screen y", point.y);
                            Number("WindowFromPoint", (DWORD)WindowFromPoint(point));
                            Number("visible", IsWindowVisible(window));
                            screen = GetDC(NULL);
                            Number("screen pixel before GL query",
                                   GetPixel(screen, point.x, point.y));
                            ReleaseDC(NULL, screen);
                            if (active && buffer && pixels && error &&
                                GetClientRect(active, &bounds)) {
                                Number("GL drawable width", bounds.right);
                                Number("GL drawable height", bounds.bottom);
                                buffer(0x0404);
                                pixels(xx, bounds.bottom - 1 - yy, 1, 1, 0x1908, 0x1401, rgba);
                                Number("GL FRONT RGBA", *(DWORD *)rgba);
                                Number("GL diagnostic error", error());
                                Number("window pixel after GL query", GetPixel(dc, xx, yy));
                            }
                        }
                        break;
                    }
                }
        ReleaseDC(window, dc);
    }
    if (!failed)
        FogBuffers();
    if (!failed)
        ColorArrays();
}
extern "C" void WINAPI WinMainCRTStartup(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    LogFile =
        CreateFileA(DG_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    Run();
    if (device)
        IDirect3DDevice7_Release(device);
    if (target)
        IDirectDrawSurface7_Release(target);
    if (primary)
        IDirectDrawSurface7_Release(primary);
    if (clipper)
        IDirectDrawClipper_Release(clipper);
    if (d3d)
        IDirect3D7_Release(d3d);
    if (draw)
        IDirectDraw7_Release(draw);
    if (window)
        DestroyWindow(window);
    if (!failed)
        Log("PASS automated " DG_PROBE_NAME
            ": Wine HAL triangle, 1280 exact GPU pixels, 1280 exact front "
            "pixels including far edges and "
            "clean release");
    CloseHandle(LogFile);
    ExitProcess(failed ? 1 : 0);
}
