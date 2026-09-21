/* SPDX-License-Identifier: GPL-2.0-or-later
 * D3D6 HAL acceptance: real IDirectDraw4 / IDirect3D3 / Device3 / Viewport3.
 * Fixed CD helper protocol, 512 target + 512 presented pixels, no software device.
 */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include "entry.h"
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>
#ifdef DG_SYSTEM_D3D
#include "system-loader.h"
#ifdef DG_CAPABILITY_D3D
#define DG_LOG_PATH "C:\\DGCAP6.LOG"
#define DG_PROBE_NAME "capd3d6"
#else
#define DG_LOG_PATH "C:\\DGSYS6.LOG"
#define DG_PROBE_NAME "sysd3d6"
#endif
#else
#define DG_LOG_PATH "C:\\DGD3D6.LOG"
#define DG_PROBE_NAME "d3d6"
#endif

static HANDLE LogFile;
static IDirectDraw4 *draw;
static IDirect3D3 *d3d;
static IDirect3DDevice3 *device;
static IDirect3DViewport3 *view;
static IDirectDrawSurface4 *primary, *target;
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
#ifdef DG_CAPABILITY_D3D
#include "indexed-probe.inc"
#include "depth-probe.inc"
#endif
typedef HRESULT(WINAPI *CreateDraw)(GUID *, IDirectDraw **, IUnknown *);
struct LegacyEnumeration {
    UINT count;
    UINT cancel_after;
};
// The SDK IsEqualGUID macro calls memcmp, unavailable in this CRT-free probe.
static BOOL SameGuid(const GUID &a, const GUID &b) {
    return a.Data1 == b.Data1 && a.Data2 == b.Data2 && a.Data3 == b.Data3 &&
           a.Data4[0] == b.Data4[0] && a.Data4[1] == b.Data4[1] &&
           a.Data4[2] == b.Data4[2] && a.Data4[3] == b.Data4[3] &&
           a.Data4[4] == b.Data4[4] && a.Data4[5] == b.Data4[5] &&
           a.Data4[6] == b.Data4[6] && a.Data4[7] == b.Data4[7];
}
static HRESULT CALLBACK LegacyDevice(GUID *guid, char *, char *, D3DDEVICEDESC *hal,
                                     D3DDEVICEDESC *hel, void *context) {
    LegacyEnumeration *state = static_cast<LegacyEnumeration *>(context);
    const GUID *expected[] = {&IID_IDirect3DRampDevice, &IID_IDirect3DRGBDevice,
                              &IID_IDirect3DHALDevice};
    const UINT index = state->count++;
    const DWORD texture_flags = D3DPTEXTURECAPS_POW2 | D3DPTEXTURECAPS_NONPOW2CONDITIONAL |
                                D3DPTEXTURECAPS_PERSPECTIVE;
    const DWORD software_flags = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_DRAWPRIMITIVES2EX |
                                 D3DDEVCAPS_HWRASTERIZATION;
    if (!Check(index < 3, "FAIL legacy enumeration extra device", index) ||
        !Check(guid && SameGuid(*guid, *expected[index]),
               "FAIL legacy enumeration order RAMP/RGB/HAL", index) ||
        !Check(hal && hel, "FAIL legacy enumeration descriptors", index))
        return D3DENUMRET_CANCEL;
    // Direct3D1 descriptors end before the fields added by Direct3D2.
    if (!Check(hal->dwSize == offsetof(D3DDEVICEDESC, dwMinTextureWidth) &&
                   hel->dwSize == offsetof(D3DDEVICEDESC, dwMinTextureWidth),
               "FAIL legacy enumeration descriptor size", hal->dwSize))
        return D3DENUMRET_CANCEL;
    if (index < 2) {
        if (!Check(hal->dwFlags == 0 && hal->dcmColorModel == 0 &&
                       hel->dcmColorModel == (index ? D3DCOLOR_RGB : D3DCOLOR_MONO),
                   "FAIL legacy software descriptor identity", index) ||
            !Check(!(hal->dpcTriCaps.dwTextureCaps & texture_flags) &&
                       !(hal->dwDevCaps & software_flags) && !(hel->dwDevCaps & software_flags),
                   "FAIL legacy software descriptor capabilities", index))
            return D3DENUMRET_CANCEL;
    } else if (!Check(hal->dcmColorModel == D3DCOLOR_RGB && hel->dcmColorModel == 0 &&
                          !(hel->dpcTriCaps.dwTextureCaps & texture_flags),
                      "FAIL legacy HAL descriptor identity", index)) {
        return D3DENUMRET_CANCEL;
    }
    return state->cancel_after && state->count == state->cancel_after ? D3DENUMRET_CANCEL
                                                                    : D3DENUMRET_OK;
}
static BOOL LegacyEnumerationProbe(CreateDraw create) {
    IDirectDraw *legacy = NULL;
    IDirect3D *legacy_d3d = NULL;
    Log("STAGE legacy Direct3D1 enumeration");
    if (!HR(create(NULL, &legacy, NULL), "FAIL legacy DirectDraw creation"))
        return FALSE;
    HRESULT result = IDirectDraw_QueryInterface(legacy, IID_IDirect3D, (void **)&legacy_d3d);
    if (HR(result, "FAIL legacy Direct3D1 query")) {
        LegacyEnumeration state = {};
        if (HR(IDirect3D_EnumDevices(legacy_d3d, LegacyDevice, &state),
               "FAIL legacy full enumeration"))
            Check(state.count == 3, "FAIL legacy enumeration device count", state.count);
        for (UINT stop = 1; stop <= 3 && !failed; ++stop) {
            state = {};
            state.cancel_after = stop;
            if (HR(IDirect3D_EnumDevices(legacy_d3d, LegacyDevice, &state),
                   "FAIL legacy cancelled enumeration"))
                Check(state.count == stop, "FAIL legacy enumeration cancellation", state.count);
        }
        IDirect3D_Release(legacy_d3d);
    }
    IDirectDraw_Release(legacy);
    if (failed)
        return FALSE;
    Log("PASS legacy Direct3D1 enumeration: RAMP, RGB second, HAL; descriptor sizes and cancellation");
    return TRUE;
}
static HRESULT CALLBACK FindP8Format(DDSURFACEDESC *desc, void *argument) {
    DDSURFACEDESC *found = static_cast<DDSURFACEDESC *>(argument);
    if ((desc->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) &&
        desc->ddpfPixelFormat.dwRGBBitCount == 8) {
        *found = *desc;
        return D3DENUMRET_CANCEL;
    }
    return D3DENUMRET_OK;
}

// Exercise the original Texture1 handle/execute-buffer path, including CPU
// indices, transparent index zero, and palette edits after the first GPU draw.
static BOOL LegacyPalettedTextureProbe(IDirectDraw *legacy, IDirect3DDevice *rgb,
        IDirect3DViewport *viewport, IDirect3DMaterial *material, IDirectDrawSurface *target) {
    IDirectDrawSurface *source = NULL, *destination = NULL;
    IDirect3DTexture *source_texture = NULL, *destination_texture = NULL;
    IDirectDrawPalette *palette = NULL;
    IDirect3DExecuteBuffer *execute = NULL;
    DDSURFACEDESC format = {}, mapped = {};
    D3DEXECUTEBUFFERDESC buffer_desc = {};
    D3DEXECUTEDATA data = {};
    D3DTEXTUREHANDLE texture_handle = 0;
    PALETTEENTRY entries[256] = {};
    DDCOLORKEY key = {};
    D3DMATERIAL background = {};
    D3DRECT rect = {.x1 = 0, .y1 = 0, .x2 = 32, .y2 = 32};
    struct Vertex { float x, y, z, rhw; DWORD color, specular; float u, v; };
    struct RenderState { DWORD type, value; };
    struct Triangle { WORD a, b, c, flags; };
    struct Commands {
        Vertex vertices[4];
        D3DINSTRUCTION states;
        RenderState render[13];
        D3DINSTRUCTION process;
        D3DPROCESSVERTICES copy;
        D3DINSTRUCTION triangles;
        Triangle triangle[2];
        D3DINSTRUCTION exit;
    } commands = {};
    static_assert(sizeof(Vertex) == sizeof(D3DTLVERTEX));
    static_assert(sizeof(RenderState) == sizeof(D3DSTATE));
    static_assert(sizeof(Triangle) == sizeof(D3DTRIANGLE));
    Log("STAGE legacy P8 Texture1 palette upload, keyed draw, and palette mutation");
    do {
        if (!HR(IDirect3DDevice_EnumTextureFormats(rgb, FindP8Format, &format),
                "FAIL legacy P8 format enumeration") ||
            !Check(format.dwSize == sizeof(format) &&
                       (format.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) &&
                       format.ddpfPixelFormat.dwRGBBitCount == 8,
                   "FAIL legacy P8 format unavailable", format.ddpfPixelFormat.dwFlags))
            break;
        format.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        format.dwWidth = format.dwHeight = 2;
        format.ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY | DDSCAPS_TEXTURE | DDSCAPS_ALLOCONLOAD;
        if (!HR(IDirectDraw_CreateSurface(legacy, &format, &destination, NULL),
                "FAIL legacy P8 allocation-on-load texture"))
            break;
        format.ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY | DDSCAPS_TEXTURE;
        if (!HR(IDirectDraw_CreateSurface(legacy, &format, &source, NULL),
                "FAIL legacy P8 source texture"))
            break;
        entries[0].peRed = entries[0].peBlue = 255;
        entries[1].peRed = 255;
        entries[2].peGreen = 255;
        entries[3].peBlue = 255;
        if (!HR(IDirectDraw_CreatePalette(legacy, DDPCAPS_8BIT | DDPCAPS_ALLOW256,
                                          entries, &palette, NULL), "FAIL legacy P8 palette") ||
            !HR(IDirectDrawSurface_SetPalette(source, palette), "FAIL legacy P8 source SetPalette") ||
            !HR(IDirectDrawSurface_SetPalette(destination, palette), "FAIL legacy P8 destination SetPalette") ||
            !HR(IDirectDrawSurface_SetColorKey(source, DDCKEY_SRCBLT, &key), "FAIL legacy P8 source key") ||
            !HR(IDirectDrawSurface_SetColorKey(destination, DDCKEY_SRCBLT, &key), "FAIL legacy P8 destination key"))
            break;
        mapped.dwSize = sizeof(mapped);
        if (!HR(IDirectDrawSurface_Lock(source, NULL, &mapped, DDLOCK_WAIT, NULL),
                "FAIL legacy P8 source lock"))
            break;
        if (Check(mapped.lpSurface && mapped.lPitch >= 2 &&
                      mapped.ddpfPixelFormat.dwRGBBitCount == 8,
                  "FAIL legacy P8 source layout", mapped.lPitch)) {
            BYTE *pixels = static_cast<BYTE *>(mapped.lpSurface);
            pixels[0] = 0; pixels[1] = 1;
            pixels[mapped.lPitch] = 2; pixels[mapped.lPitch + 1] = 3;
        }
        if (!HR(IDirectDrawSurface_Unlock(source, NULL), "FAIL legacy P8 source unlock") || failed)
            break;
        if (!HR(IDirectDrawSurface_QueryInterface(source, IID_IDirect3DTexture,
                                                  (void **)&source_texture), "FAIL legacy P8 source Texture1") ||
            !HR(IDirectDrawSurface_QueryInterface(destination, IID_IDirect3DTexture,
                                                  (void **)&destination_texture), "FAIL legacy P8 destination Texture1") ||
            !HR(IDirect3DTexture_Load(destination_texture, source_texture), "FAIL legacy P8 Texture1 Load") ||
            !HR(IDirect3DTexture_GetHandle(destination_texture, rgb, &texture_handle), "FAIL legacy P8 texture handle") ||
            !Check(texture_handle != 0, "FAIL legacy P8 empty handle", 0))
            break;
        commands.vertices[0] = {0, 0, .5f, 1, 0xffffffff, 0, 0, 0};
        commands.vertices[1] = {32, 0, .5f, 1, 0xffffffff, 0, 1, 0};
        commands.vertices[2] = {32, 32, .5f, 1, 0xffffffff, 0, 1, 1};
        commands.vertices[3] = {0, 32, .5f, 1, 0xffffffff, 0, 0, 1};
        commands.states = {D3DOP_STATERENDER, sizeof(RenderState), 13};
        commands.render[0] = {D3DRENDERSTATE_TEXTUREHANDLE, texture_handle};
        commands.render[1] = {D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_DECAL};
        commands.render[2] = {D3DRENDERSTATE_ZENABLE, FALSE};
        commands.render[3] = {D3DRENDERSTATE_CULLMODE, D3DCULL_NONE};
        commands.render[4] = {D3DRENDERSTATE_DITHERENABLE, FALSE};
        commands.render[5] = {D3DRENDERSTATE_ALPHABLENDENABLE, FALSE};
        commands.render[6] = {D3DRENDERSTATE_COLORKEYENABLE, TRUE};
        commands.render[7] = {D3DRENDERSTATE_TEXTUREMIN, D3DFILTER_NEAREST};
        commands.render[8] = {D3DRENDERSTATE_TEXTUREMAG, D3DFILTER_NEAREST};
        commands.render[9] = {D3DRENDERSTATE_TEXTUREADDRESS, D3DTADDRESS_CLAMP};
        commands.render[10] = {D3DRENDERSTATE_ALPHATESTENABLE, FALSE};
        commands.render[11] = {D3DRENDERSTATE_FOGENABLE, FALSE};
        commands.render[12] = {D3DRENDERSTATE_SPECULARENABLE, FALSE};
        commands.process = {D3DOP_PROCESSVERTICES, sizeof(D3DPROCESSVERTICES), 1};
        commands.copy = {D3DPROCESSVERTICES_COPY, 0, 0, 4, 0};
        commands.triangles = {D3DOP_TRIANGLE, sizeof(Triangle), 2};
        commands.triangle[0] = {0, 1, 2, 0};
        commands.triangle[1] = {0, 2, 3, 0};
        commands.exit = {D3DOP_EXIT, 0, 0};
        buffer_desc.dwSize = sizeof(buffer_desc);
        buffer_desc.dwFlags = D3DDEB_BUFSIZE;
        buffer_desc.dwBufferSize = sizeof(commands);
        if (!HR(IDirect3DDevice_CreateExecuteBuffer(rgb, &buffer_desc, &execute, NULL),
                "FAIL legacy P8 execute buffer") ||
            !HR(IDirect3DExecuteBuffer_Lock(execute, &buffer_desc), "FAIL legacy P8 execute lock"))
            break;
        if (Check(buffer_desc.lpData != NULL, "FAIL legacy P8 execute data", 0))
            *static_cast<Commands *>(buffer_desc.lpData) = commands;
        if (!HR(IDirect3DExecuteBuffer_Unlock(execute), "FAIL legacy P8 execute unlock") || failed)
            break;
        data.dwSize = sizeof(data);
        data.dwVertexCount = 4;
        data.dwInstructionOffset = offsetof(Commands, states);
        data.dwInstructionLength = sizeof(commands) - data.dwInstructionOffset;
        if (!HR(IDirect3DExecuteBuffer_SetExecuteData(execute, &data), "FAIL legacy P8 execute metadata"))
            break;
        background.dwSize = sizeof(background);
        background.diffuse.r = background.diffuse.g = background.diffuse.b = background.diffuse.a = 1;
        if (!HR(IDirect3DMaterial_SetMaterial(material, &background), "FAIL legacy P8 background"))
            break;
        for (UINT pass = 0; pass < 2 && !failed; ++pass) {
            if (pass) {
                entries[1].peGreen = 255; // red -> yellow
                entries[2].peBlue = 255;  // green -> cyan
                if (!HR(IDirectDrawPalette_SetEntries(palette, 0, 1, 2, entries + 1),
                        "FAIL legacy P8 palette update"))
                    break;
            }
            if (!HR(IDirect3DViewport_Clear(viewport, 1, &rect, D3DCLEAR_TARGET), "FAIL legacy P8 clear") ||
                !HR(IDirect3DDevice_BeginScene(rgb), "FAIL legacy P8 begin scene"))
                break;
            HRESULT drawn = IDirect3DDevice_Execute(rgb, execute, viewport, D3DEXECUTE_UNCLIPPED);
            HRESULT ended = IDirect3DDevice_EndScene(rgb);
            if (!HR(drawn, "FAIL legacy P8 execute") || !HR(ended, "FAIL legacy P8 end scene"))
                break;
            mapped = {};
            mapped.dwSize = sizeof(mapped);
            if (!HR(IDirectDrawSurface_Lock(target, NULL, &mapped, DDLOCK_WAIT | DDLOCK_READONLY, NULL),
                    "FAIL legacy P8 render readback"))
                break;
            if (Check(mapped.lpSurface && mapped.lPitch >= 64, "FAIL legacy P8 render layout", mapped.lPitch)) {
                const WORD expected[4] = {0xffff, static_cast<WORD>(pass ? 0xffe0 : 0xf800),
                                          static_cast<WORD>(pass ? 0x07ff : 0x07e0), 0x001f};
                for (UINT quadrant = 0; quadrant < 4 && !failed; ++quadrant)
                    for (UINT y = 4; y < 12 && !failed; ++y)
                        for (UINT x = 4; x < 12; ++x) {
                            UINT xx = x + (quadrant & 1) * 16, yy = y + (quadrant >> 1) * 16;
                            WORD actual = *reinterpret_cast<WORD *>(static_cast<BYTE *>(mapped.lpSurface) +
                                                                    yy * mapped.lPitch + xx * 2);
                            if (!Check(actual == expected[quadrant], "FAIL legacy P8 exact pixel", actual)) {
                                Number("P8 pass", pass); Number("P8 quadrant", quadrant);
                                Number("P8 expected", expected[quadrant]); break;
                            }
                        }
            }
            if (!HR(IDirectDrawSurface_Unlock(target, NULL), "FAIL legacy P8 render unlock"))
                break;
            for (UINT which = 0; which < 2 && !failed; ++which) {
                IDirectDrawSurface *texture_surface = which ? destination : source;
                mapped = {}; mapped.dwSize = sizeof(mapped);
                if (!HR(IDirectDrawSurface_Lock(texture_surface, NULL, &mapped,
                                                DDLOCK_WAIT | DDLOCK_READONLY, NULL),
                        "FAIL legacy P8 index readback"))
                    break;
                if (Check(mapped.lpSurface && mapped.lPitch >= 2 && mapped.ddpfPixelFormat.dwRGBBitCount == 8,
                          "FAIL legacy P8 index layout", mapped.lPitch)) {
                    BYTE *pixels = static_cast<BYTE *>(mapped.lpSurface);
                    Check(pixels[0] == 0 && pixels[1] == 1 && pixels[mapped.lPitch] == 2 &&
                              pixels[mapped.lPitch + 1] == 3, "FAIL legacy P8 indices changed", which);
                }
                HR(IDirectDrawSurface_Unlock(texture_surface, NULL), "FAIL legacy P8 index unlock");
            }
        }
    } while (FALSE);
    if (execute) IDirect3DExecuteBuffer_Release(execute);
    if (destination_texture) IDirect3DTexture_Release(destination_texture);
    if (source_texture) IDirect3DTexture_Release(source_texture);
    if (destination) IDirectDrawSurface_Release(destination);
    if (source) IDirectDrawSurface_Release(source);
    if (palette) IDirectDrawPalette_Release(palette);
    if (failed) return FALSE;
    Log("PASS legacy P8 Texture1: enumerated format, Load/GetHandle, 512 exact keyed/palette-update pixels, unchanged CPU indices");
    return TRUE;
}

// Legacy RGB devices accept system-memory render targets. Exercise the
// viewport clear before BeginScene, including a CPU map between GPU clears.
static BOOL LegacySystemMemoryClearProbe(CreateDraw create, HWND hwnd) {
    IDirectDraw *legacy = NULL;
    IDirect3D *legacy_d3d = NULL;
    IDirectDrawSurface *color_surface = NULL, *depth_surface = NULL;
    IDirect3DDevice *rgb = NULL;
    IDirect3DViewport *viewport = NULL;
    IDirect3DMaterial *material = NULL;
    BOOL viewport_added = FALSE;
    DDSURFACEDESC color_desc = {}, depth_desc = {}, mapped = {};
    D3DVIEWPORT bounds = {};
    D3DMATERIAL background = {};
    D3DMATERIALHANDLE handle = 0;
    D3DRECT rect = {.x1 = 0, .y1 = 0, .x2 = 32, .y2 = 32};
    Log("STAGE legacy RGB system-memory target and depth clear before BeginScene");
    do {
        if (!HR(create(NULL, &legacy, NULL), "FAIL legacy clear DirectDraw") ||
            !HR(IDirectDraw_SetCooperativeLevel(legacy, hwnd, DDSCL_NORMAL),
                "FAIL legacy clear cooperative level") ||
            !HR(IDirectDraw_QueryInterface(legacy, IID_IDirect3D, (void **)&legacy_d3d),
                "FAIL legacy clear Direct3D1"))
            break;
        color_desc.dwSize = sizeof(color_desc);
        color_desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        color_desc.dwWidth = color_desc.dwHeight = 32;
        color_desc.ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY | DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
        color_desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        color_desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
        color_desc.ddpfPixelFormat.dwRGBBitCount = 16;
        color_desc.ddpfPixelFormat.dwRBitMask = 0xf800;
        color_desc.ddpfPixelFormat.dwGBitMask = 0x07e0;
        color_desc.ddpfPixelFormat.dwBBitMask = 0x001f;
        depth_desc.dwSize = sizeof(depth_desc);
        depth_desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_ZBUFFERBITDEPTH;
        depth_desc.dwWidth = depth_desc.dwHeight = 32;
        depth_desc.dwZBufferBitDepth = 16;
        depth_desc.ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY | DDSCAPS_ZBUFFER;
        if (!HR(IDirectDraw_CreateSurface(legacy, &color_desc, &color_surface, NULL),
                "FAIL legacy system-memory RGB565 surface") ||
            !HR(IDirectDraw_CreateSurface(legacy, &depth_desc, &depth_surface, NULL),
                "FAIL legacy system-memory depth surface") ||
            !HR(IDirectDrawSurface_AddAttachedSurface(color_surface, depth_surface),
                "FAIL legacy attach depth") ||
            !HR(IDirectDrawSurface_QueryInterface(color_surface, IID_IDirect3DRGBDevice,
                                                 (void **)&rgb),
                "FAIL legacy RGB device query") ||
            !HR(IDirect3D_CreateViewport(legacy_d3d, &viewport, NULL),
                "FAIL legacy viewport creation"))
            break;
        if (!HR(IDirect3DDevice_AddViewport(rgb, viewport), "FAIL legacy attach viewport"))
            break;
        viewport_added = TRUE;
        bounds.dwSize = sizeof(bounds);
        bounds.dwWidth = bounds.dwHeight = 32;
        bounds.dvScaleX = bounds.dvScaleY = 16.0f;
        bounds.dvMaxX = bounds.dvMaxY = bounds.dvMaxZ = 1.0f;
        if (!HR(IDirect3DViewport_SetViewport(viewport, &bounds), "FAIL legacy viewport bounds") ||
            !HR(IDirect3D_CreateMaterial(legacy_d3d, &material, NULL), "FAIL legacy material") ||
            !HR(IDirect3DMaterial_GetHandle(material, rgb, &handle), "FAIL legacy material handle") ||
            !HR(IDirect3DViewport_SetBackground(viewport, handle), "FAIL legacy background"))
            break;
        for (UINT pass = 0; pass < 2 && !failed; ++pass) {
            background = {};
            background.dwSize = sizeof(background);
            background.diffuse.r = pass ? 0.0f : 1.0f;
            background.diffuse.b = pass ? 1.0f : 0.0f;
            background.diffuse.a = 1.0f;
            // Preserve CPU-written green on the right while the GPU clears only the left.
            rect.x2 = pass ? 16 : 32;
            if (!HR(IDirect3DMaterial_SetMaterial(material, &background),
                    "FAIL legacy background material") ||
                !HR(IDirect3DViewport_Clear(viewport, 1, &rect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER),
                    "FAIL legacy RGB system-memory color/depth clear"))
                break;
            mapped = {};
            mapped.dwSize = sizeof(mapped);
            if (!HR(IDirectDrawSurface_Lock(color_surface, NULL, &mapped, DDLOCK_WAIT, NULL),
                    "FAIL legacy system-memory readback"))
                break;
            if (Check(mapped.lpSurface && mapped.lPitch >= 64 &&
                          mapped.ddpfPixelFormat.dwRGBBitCount == 16,
                      "FAIL legacy RGB565 readback layout", mapped.lPitch)) {
                for (UINT y = 0; y < 32 && !failed; ++y) {
                    WORD *row = reinterpret_cast<WORD *>(static_cast<BYTE *>(mapped.lpSurface) +
                                                        y * mapped.lPitch);
                    for (UINT x = 0; x < 32; ++x) {
                        const WORD expected = pass ? (x < 16 ? 0x001f : 0x07e0) : 0xf800;
                        if (!Check(row[x] == expected, "FAIL legacy clear exact RGB565 pixel", row[x])) {
                            Number("legacy clear pass", pass);
                            break;
                        }
                        row[x] = 0x07e0;
                    }
                }
            }
            if (!HR(IDirectDrawSurface_Unlock(color_surface, NULL), "FAIL legacy clear unlock"))
                break;
        }
#ifdef DG_CAPABILITY_D3D
        if (!failed)
            LegacyDepthProbe(depth_surface, viewport);
#endif
        if (!failed)
            LegacyPalettedTextureProbe(legacy, rgb, viewport, material, color_surface);
    } while (FALSE);
    if (viewport_added)
        HR(IDirect3DDevice_DeleteViewport(rgb, viewport), "FAIL legacy detach viewport");
    if (viewport)
        IDirect3DViewport_Release(viewport);
    if (material)
        IDirect3DMaterial_Release(material);
    if (rgb)
        IDirect3DDevice_Release(rgb);
    if (color_surface)
        IDirectDrawSurface_Release(color_surface);
    if (depth_surface)
        IDirectDrawSurface_Release(depth_surface);
    if (legacy_d3d)
        IDirect3D_Release(legacy_d3d);
    if (legacy)
        IDirectDraw_Release(legacy);
    if (failed)
        return FALSE;
    Log("PASS legacy RGB system-memory target: color/depth clear before BeginScene, 2048 exact RGB565 pixels with CPU map and partial-clear preservation");
    return TRUE;
}
struct WorkerDraw {
    IDirect3DDevice3 *device;
    HANDLE ready, release, thread;
    HRESULT result;
    const char *stage;
};
static WorkerDraw worker;

// Keep the worker alive until main-thread readback completes, so thread-exit
// cleanup cannot hide a missing resource-publication or context-handoff step.
static DWORD WINAPI DrawOnWorker(void *argument) {
    WorkerDraw *state = static_cast<WorkerDraw *>(argument);
    struct Vertex {
        float x, y, z, rhw;
        DWORD color;
    } vertices[3] = {{80, 60, .5f, 1, 0xffff0000},
                     {240, 60, .5f, 1, 0xffff0000},
                     {160, 180, .5f, 1, 0xffff0000}};
    state->stage = "FAIL worker begin scene";
    state->result = IDirect3DDevice3_BeginScene(state->device);
    if (SUCCEEDED(state->result)) {
        state->stage = "FAIL worker draw";
        state->result = IDirect3DDevice3_DrawPrimitive(state->device, D3DPT_TRIANGLELIST,
            D3DFVF_XYZRHW | D3DFVF_DIFFUSE, vertices, 3, 0);
        HRESULT ended = IDirect3DDevice3_EndScene(state->device);
        if (SUCCEEDED(state->result)) {
            state->stage = "FAIL worker end scene";
            state->result = ended;
        }
    }
    SetEvent(state->ready);
    if (WaitForSingleObject(state->release, INFINITE) != WAIT_OBJECT_0)
        ExitProcess(1);
    // Normal DLL_THREAD_DETACH owns Wine/WGL context cleanup.
    return 0;
}

static void WaitForWorker(HANDLE handle, const char *stage) {
    const DWORD start = GetTickCount();
    for (;;) {
        const DWORD elapsed = GetTickCount() - start;
        if (elapsed >= 30000) {
            Number(stage, WAIT_TIMEOUT);
            // The worker still owns device/stack state. Never release it or
            // forcibly kill only the thread after an incomplete operation.
            ExitProcess(1);
        }
        DWORD result = MsgWaitForMultipleObjects(1, &handle, FALSE, 30000 - elapsed,
                                                 QS_ALLINPUT);
        if (result == WAIT_OBJECT_0)
            return;
        if (result != WAIT_OBJECT_0 + 1) {
            Number(stage, result == WAIT_FAILED ? GetLastError() : result);
            ExitProcess(1);
        }
        MSG message;
        for (UINT count = 0; count < 64 && PeekMessageA(&message, NULL, 0, 0, PM_REMOVE); ++count) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
}

static BOOL StartWorkerDraw(void) {
    worker.device = device;
    worker.ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    worker.release = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!Check(worker.ready && worker.release, "FAIL worker events", GetLastError()))
        return FALSE;
    worker.thread = CreateThread(NULL, 0, DrawOnWorker, &worker, 0, NULL);
    if (!Check(worker.thread != NULL, "FAIL create drawing worker", GetLastError()))
        return FALSE;
    WaitForWorker(worker.ready, "FAIL drawing worker wait");
    return HR(worker.result, worker.stage);
}

static void FinishWorker(void) {
    if (worker.thread) {
        if (!SetEvent(worker.release)) {
            Number("FAIL release drawing worker", GetLastError());
            ExitProcess(1);
        }
        WaitForWorker(worker.thread, "FAIL drawing worker exit wait");
        CloseHandle(worker.thread);
        worker.thread = NULL;
    }
    if (worker.ready)
        CloseHandle(worker.ready);
    if (worker.release)
        CloseHandle(worker.release);
    worker.ready = worker.release = NULL;
}

static void PumpWindowMessages(void) {
    MSG message;
    for (UINT count = 0; count < 64 && PeekMessageA(&message, NULL, 0, 0, PM_REMOVE); ++count) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

static BOOL CheckTransitionPixels(DWORD background, DWORD center) {
    DDSURFACEDESC2 locked = {};
    locked.dwSize = sizeof(locked);
    if (!HR(IDirectDrawSurface4_Lock(target, NULL, &locked, DDLOCK_READONLY | DDLOCK_WAIT, NULL),
            "FAIL transition readback lock"))
        return FALSE;
    if (Check(locked.lpSurface && locked.lPitch >= 1280,
              "FAIL transition readback layout", locked.lPitch)) {
        for (UINT region = 0; region < 2 && !failed; ++region)
            for (UINT y = 0; y < 16 && !failed; ++y)
                for (UINT x = 0; x < 16; ++x) {
                    UINT xx = x + (region ? 152 : 0), yy = y + (region ? 92 : 0);
                    DWORD expected = region ? center : background;
                    DWORD actual = *(DWORD *)((BYTE *)locked.lpSurface + yy * locked.lPitch + xx * 4)
                                   & 0xffffff;
                    if (!Check(actual == expected, "FAIL exact transition pixel", actual)) {
                        Number("pixel x", xx);
                        Number("pixel y", yy);
                        Number("expected", expected);
                        break;
                    }
                }
    }
    HR(IDirectDrawSurface4_Unlock(target, NULL), "FAIL transition unlock");
    return !failed;
}

static BOOL WindowTransitionProbe(void) {
    D3DRECT rect = {.x1 = 0, .y1 = 0, .x2 = 320, .y2 = 240};
    RECT client;
    // The earlier check keeps the worker parked during readback. Now require
    // the same drawable to survive normal worker exit and a minimized HWND.
    FinishWorker();
    Log("STAGE minimize zero-client window, worker draw and exit, main readback");
    if (!HR(IDirect3DViewport3_Clear2(view, 1, &rect, D3DCLEAR_TARGET, 0xff304050, 1, 0),
            "FAIL transition initial clear"))
        return FALSE;
    ShowWindow(window, SW_MINIMIZE);
    PumpWindowMessages();
    if (!Check(IsIconic(window) && GetClientRect(window, &client) &&
               client.right == client.left && client.bottom == client.top,
               "FAIL minimized zero-client window", GetLastError()))
        return FALSE;
    if (!StartWorkerDraw())
        return FALSE;
    FinishWorker();
    // No explicit WGL call: normal thread-detach cleanup must leave the shared
    // drawable reusable by a different thread while its window stays minimized.
    if (!CheckTransitionPixels(0x304050, 0xff0000))
        return FALSE;
    Log("STAGE restore window and main-thread clear/readback after worker exit");
    ShowWindow(window, SW_RESTORE);
    PumpWindowMessages();
    if (!Check(!IsIconic(window) && GetClientRect(window, &client) &&
               client.right == 320 && client.bottom == 240,
               "FAIL restored drawable size", GetLastError()) ||
        !HR(IDirect3DViewport3_Clear2(view, 1, &rect, D3DCLEAR_TARGET, 0xff506070, 1, 0),
            "FAIL restored main-thread clear") ||
        !CheckTransitionPixels(0x506070, 0x506070))
        return FALSE;
    Log("PASS minimized/restored window: worker exit, main-thread reuse, 1024 exact target pixels");
    return TRUE;
}

static LRESULT CALLBACK WindowProc(HWND w, UINT message, WPARAM a, LPARAM b) {
    return DefWindowProcA(w, message, a, b);
}
static void Run(void) {
    IDirectDraw *legacy_draw = NULL;
    HMODULE gl, runtime;
#ifndef DG_SYSTEM_D3D
    char path[MAX_PATH];
#endif
    WNDCLASSA klass = {};
    RECT bounds = {0, 0, 320, 240}, client;
    POINT origin = {0, 0};
    DDSURFACEDESC2 desc = {}, locked = {};
    D3DVIEWPORT2 viewport = {sizeof(D3DVIEWPORT2), 0, 0, 320, 240, -1, 1, 2, 2, 0, 1};
    D3DRECT clear_rect = {.x1 = 0, .y1 = 0, .x2 = 320, .y2 = 240};
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
    const auto create = Entry<CreateDraw>(runtime, "DirectDrawCreate");
    if (!Check(create != nullptr, "FAIL DirectDraw factory", GetLastError()))
        return;
    // A separate instance keeps the legacy interface version out of the D3D3 test.
    if (!LegacyEnumerationProbe(create))
        return;
    Log("STAGE create DirectDraw4 and Direct3D3");
    if (!HR(create(NULL, &legacy_draw, NULL), "FAIL DirectDraw creation"))
        return;
    HRESULT query = IDirectDraw_QueryInterface(legacy_draw, IID_IDirectDraw4, (void **)&draw);
    IDirectDraw_Release(legacy_draw);
    if (!HR(query, "FAIL DirectDraw4 query") ||
        !HR(IDirectDraw4_QueryInterface(draw, IID_IDirect3D3, (void **)&d3d),
            "FAIL Direct3D3 query"))
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
    klass.lpszClassName = "DreamGPUD3D6Probe";
    if (!Check(RegisterClassA(&klass) != 0, "FAIL class registration", GetLastError()) ||
        !Check(AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE), "FAIL window bounds",
               GetLastError()))
        return;
    window = CreateWindowA(klass.lpszClassName, "DreamGPU D3D6 probe",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 64, 64, bounds.right - bounds.left,
                           bounds.bottom - bounds.top, NULL, NULL, klass.hInstance, NULL);
    if (!Check(window != NULL, "FAIL window creation", GetLastError()) ||
        !Check(GetClientRect(window, &client) && client.right == 320 && client.bottom == 240,
               "FAIL drawable size", 0))
        return;
    if (!LegacySystemMemoryClearProbe(create, window))
        return;
    if (!HR(IDirectDraw4_SetCooperativeLevel(draw, window, DDSCL_NORMAL), "FAIL cooperative level"))
        return;
    {
        DDSCAPS2 caps = {};
        DWORD total = 0, available = 0;
        DDSURFACEDESC2 mode = {};
        caps.dwCaps = DDSCAPS_VIDEOMEMORY;
        mode.dwSize = sizeof(mode);
        if (HR(IDirectDraw4_GetAvailableVidMem(draw, &caps, &total, &available),
               "FAIL video-memory query")) {
            Number("reported video memory total", total);
            Number("reported video memory available", available);
        }
        if (HR(IDirectDraw4_GetDisplayMode(draw, &mode), "FAIL display-mode query")) {
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
    if (!HR(IDirectDraw4_CreateSurface(draw, &desc, &primary, NULL), "FAIL primary surface") ||
        !HR(IDirectDraw4_CreateClipper(draw, 0, &clipper, NULL), "FAIL clipper") ||
        !HR(IDirectDrawClipper_SetHWnd(clipper, 0, window), "FAIL clipper window") ||
        !HR(IDirectDrawSurface4_SetClipper(primary, clipper), "FAIL primary clipper"))
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
    if (!HR(IDirectDraw4_CreateSurface(draw, &desc, &target, NULL), "FAIL render target") ||
        !HR(IDirect3D3_CreateDevice(d3d, IID_IDirect3DHALDevice, target, &device, NULL),
            "FAIL HAL device") ||
        !HR(IDirect3D3_CreateViewport(d3d, &view, NULL), "FAIL viewport creation") ||
        !HR(IDirect3DDevice3_AddViewport(device, view), "FAIL attach viewport") ||
        !HR(IDirect3DViewport3_SetViewport2(view, &viewport), "FAIL viewport2") ||
        !HR(IDirect3DDevice3_SetCurrentViewport(device, view), "FAIL current viewport"))
        return;
    if (!HR(IDirect3DDevice3_SetRenderState(device, D3DRENDERSTATE_ZENABLE, FALSE), "FAIL depth") ||
        !HR(IDirect3DDevice3_SetRenderState(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE),
            "FAIL culling") ||
        !HR(IDirect3DDevice3_SetRenderState(device, D3DRENDERSTATE_DITHERENABLE, FALSE),
            "FAIL dither") ||
        !HR(IDirect3DDevice3_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL color operation") ||
        !HR(IDirect3DDevice3_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
            "FAIL diffuse argument"))
        return;
    if (ProbeGlError)
        Number("GL error before draw", ProbeGlError());
    Log("STAGE main-thread clear and worker-thread fixed triangle");
    if (!HR(IDirect3DViewport3_Clear2(view, 1, &clear_rect, D3DCLEAR_TARGET, 0xff102030, 1, 0),
            "FAIL main-thread clear") || !StartWorkerDraw())
        return;
    if (ProbeGlError)
        Number("GL error after draw", ProbeGlError());
    locked.dwSize = sizeof(locked);
    Log("STAGE real render-target readback");
    if (!HR(IDirectDrawSurface4_Lock(target, NULL, &locked, DDLOCK_READONLY | DDLOCK_WAIT, NULL),
            "FAIL readback lock"))
        return;
    if (Check(locked.lpSurface && locked.lPitch >= 1280, "FAIL readback layout", locked.lPitch)) {
        for (region = 0; region < 2 && !failed; region++)
            for (y = 0; y < 16 && !failed; y++)
                for (x = 0; x < 16; x++) {
                    UINT xx = x + (region ? 152 : 0), yy = y + (region ? 92 : 0);
                    DWORD expected = region ? 0xff0000 : 0x102030;
                    DWORD actual =
                        *(DWORD *)((BYTE *)locked.lpSurface + yy * locked.lPitch + xx * 4) &
                        0xffffff;
                    if (!Check(actual == expected, "FAIL exact D3D6 pixel", actual)) {
                        Number("pixel x", xx);
                        Number("pixel y", yy);
                        Number("expected", expected);
                        break;
                    }
                }
    }
    if (!HR(IDirectDrawSurface4_Unlock(target, NULL), "FAIL unlock") || failed)
        return;
    if (ProbeGlError)
        Number("GL error before Blt", ProbeGlError());
    Log("STAGE clipped window presentation");
    if (!Check(ClientToScreen(window, &origin), "FAIL window origin", GetLastError()))
        return;
    OffsetRect(&client, origin.x, origin.y);
    if (!HR(IDirectDrawSurface4_Blt(primary, &client, target, NULL, DDBLT_WAIT, NULL),
            "FAIL present blit"))
        return;
    if (ProbeGlError)
        Number("GL error after Blt", ProbeGlError());
    {
        HDC dc = GetDC(window);
        if (!Check(dc != NULL, "FAIL presented window DC", GetLastError()))
            return;
        Log("STAGE actual clipped GPU front-buffer GDI readback");
        for (region = 0; region < 2 && !failed; region++)
            for (y = 0; y < 16 && !failed; y++)
                for (x = 0; x < 16; x++) {
                    UINT xx = x + (region ? 152 : 0), yy = y + (region ? 92 : 0);
                    COLORREF expected = region ? RGB(255, 0, 0) : RGB(16, 32, 48);
                    COLORREF actual = GetPixel(dc, xx, yy);
                    if (!Check(actual == expected, "FAIL exact presented D3D6 pixel", actual)) {
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
#ifdef DG_CAPABILITY_D3D
    if (!failed)
        IndexedImmediateProbe();
#endif
    if (!failed)
        WindowTransitionProbe();
}
extern "C" void WINAPI WinMainCRTStartup(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    LogFile =
        CreateFileA(DG_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    Run();
    FinishWorker();
    if (device && view)
        IDirect3DDevice3_DeleteViewport(device, view);
    if (view)
        IDirect3DViewport3_Release(view);
    if (device)
        IDirect3DDevice3_Release(device);
    if (target)
        IDirectDrawSurface4_Release(target);
    if (primary)
        IDirectDrawSurface4_Release(primary);
    if (clipper)
        IDirectDrawClipper_Release(clipper);
    if (d3d)
        IDirect3D3_Release(d3d);
    if (draw)
        IDirectDraw4_Release(draw);
    if (window)
        DestroyWindow(window);
    if (!failed)
        Log("PASS automated " DG_PROBE_NAME
            ": D3D6 HAL, main clear/worker draw/main readback, 1024 exact target/front pixels, viewport3 and "
            "independent release");
    CloseHandle(LogFile);
    ExitProcess(failed ? 1 : 0);
}
