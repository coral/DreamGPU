/* SPDX-License-Identifier: GPL-2.0-or-later
 * D3DRS_POINTSPRITEENABLE left on while fixed-function triangles draw,
 * through the public D3D8/9 API (Wine 0058). Included by probe.cpp after
 * partial-lock-probe.inc; reuses vbuffer-probe.inc's frames and the textures
 * of stage-probe.inc. Point sprites replace the texture coordinates of points
 * only. Wine kept GL_POINT_SPRITE enabled from the render state alone (with
 * COORD_REPLACE on every unit), and Mesa's fixed-function vertex processing
 * then loses those coordinates for every primitive: each textured triangle
 * samples one texel. Halo turns sprites on at device creation and copies its
 * scene to the back buffer with a pretransformed indexed fan, so every frame
 * was one colour. Each frame here draws, in this order:
 * - Halo's shape: an XYZRHW, D3DCOLOR and one-coordinate-set quad (Halo's
 *   28-byte layout) as a DrawIndexedPrimitiveUP triangle fan;
 * - one 16-pixel XYZRHW point whose own coordinate selects the top-right
 *   texel (when MaxPointSize reaches 16);
 * - an untransformed XYZ quad (identity transforms) as a DrawPrimitiveUP
 *   strip, after the point, so the switch back from points is covered.
 * The texture is point-sampled and clamped, one colour per quadrant, so both
 * quads must show the four quadrants exactly. The frame runs with sprites on
 * and again with them off. With sprites off the point is the top-right
 * colour throughout. With them on and GL_ARB_point_sprite in dgpugl.dll's
 * extensions (DreamGPU: DG_CAP_GL_FIXED_STATE, asked in a context of the
 * probe's own as probe.cpp asks for RGB565) the point shows the four
 * quadrants upright; without the extension Wine draws no sprites, the point
 * keeps its colour and that check is skipped (logged). Without point sizes of
 * 16 the point is not drawn and the quads are still checked.
 */
namespace point_sprite_probe {

using vbuffer_probe::Area;
using vbuffer_probe::Frame;

/* Halo's layout: POSITIONT, D3DCOLOR, one coordinate set (28 bytes). */
struct Transformed {
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};
struct Untransformed {
    float x, y, z, u, v;
};
static_assert(sizeof(Transformed) == 28 && sizeof(Untransformed) == 20);
static constexpr DWORD TransformedFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static constexpr DWORD UntransformedFvf = D3DFVF_XYZ | D3DFVF_TEX1;
/* Texture quadrants: top-left, top-right, bottom-left, bottom-right. */
static constexpr DWORD Texels[4] = {0xffc02040, 0xff40c020, 0xff2040c0, 0xffe0e0a0};
static constexpr int FanLeft = 16, FanTop = 16, FanRight = 144, FanBottom = 112;
static constexpr int StripLeft = 16, StripTop = 128, StripRight = 144, StripBottom = 224;
static constexpr int PointX = 232, PointY = 64, PointSize = 16;

/* A float render state or cap as its bits. */
static DWORD Bits(float value) {
    DWORD bits;
    static_assert(sizeof(bits) == sizeof(value));
    CopyMemory(&bits, &value, sizeof(bits));
    return bits;
}

/* GL_ARB_point_sprite in dgpugl.dll's extension string, asked in a context of
 * the probe's own on a hidden popup (Wine releases its context after every
 * call); the previous context, if any, is restored. */
static BOOL SpritesClaimed(HMODULE gl) {
    typedef int(WINAPI * ChooseFormat)(HDC, const PIXELFORMATDESCRIPTOR *);
    typedef BOOL(WINAPI * SetFormat)(HDC, int, const PIXELFORMATDESCRIPTOR *);
    typedef HGLRC(WINAPI * CreateContext)(HDC);
    typedef BOOL(WINAPI * MakeCurrent)(HDC, HGLRC);
    typedef BOOL(WINAPI * DeleteContext)(HGLRC);
    typedef HGLRC(WINAPI * CurrentContext)(void);
    typedef HDC(WINAPI * CurrentDC)(void);
    typedef const BYTE *(WINAPI * GetString)(DWORD);
    static const char wanted[] = "GL_ARB_point_sprite";
    if (!gl) {
        Log("point sprite extension: dgpugl.dll not loaded");
        return FALSE;
    }
    const auto choose = Entry<ChooseFormat>(gl, "wglChoosePixelFormat");
    const auto set = Entry<SetFormat>(gl, "wglSetPixelFormat");
    const auto create = Entry<CreateContext>(gl, "wglCreateContext");
    const auto make_current = Entry<MakeCurrent>(gl, "wglMakeCurrent");
    const auto destroy = Entry<DeleteContext>(gl, "wglDeleteContext");
    const auto current_context = Entry<CurrentContext>(gl, "wglGetCurrentContext");
    const auto current_dc = Entry<CurrentDC>(gl, "wglGetCurrentDC");
    const auto string = Entry<GetString>(gl, "glGetString");
    if (!choose || !set || !create || !make_current || !destroy || !current_context ||
        !current_dc || !string) {
        Log("point sprite extension: dgpugl.dll export missing");
        return FALSE;
    }
    const HGLRC previous = current_context();
    const HDC previous_dc = previous ? current_dc() : NULL;
    WNDCLASSA klass = {};
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "DreamGPUPointSpriteProbe";
    const BOOL registered = RegisterClassA(&klass) != 0;
    const HWND hidden =
        registered ? CreateWindowA(klass.lpszClassName, "DreamGPU point sprite probe", WS_POPUP, 0,
                                   0, 32, 32, NULL, NULL, klass.hInstance, NULL)
                   : NULL;
    const HDC dc = hidden ? GetDC(hidden) : NULL;
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = dc ? choose(dc, &pfd) : 0;
    const HGLRC context = format && set(dc, format, &pfd) ? create(dc) : NULL;
    BOOL claimed = FALSE, asked = FALSE;
    if (context && make_current(dc, context)) {
        const char *list = (const char *)string(0x1f03 /* EXTENSIONS */);
        asked = list != NULL;
        /* Whole space-separated tokens only. */
        for (const char *p = list; p && *p && !claimed;) {
            while (*p == ' ')
                ++p;
            const char *end = p;
            while (*end && *end != ' ')
                ++end;
            UINT i = 0;
            while (wanted[i] && p + i < end && p[i] == wanted[i])
                ++i;
            claimed = !wanted[i] && p + i == end;
            p = end;
        }
        make_current(NULL, NULL);
    }
    if (context)
        destroy(context);
    if (dc)
        ReleaseDC(hidden, dc);
    if (hidden)
        DestroyWindow(hidden);
    if (registered)
        UnregisterClassA(klass.lpszClassName, klass.hInstance);
    if (previous)
        make_current(previous_dc, previous);
    if (!asked)
        Log("point sprite extension: no extension string from a context of the probe's own");
    return claimed;
}

/* A 4x4 texture of four 2x2 quadrants, point-sampled and clamped on stage 0,
 * selected as the colour; stage 1 off. */
static BOOL Texture(Device *dev, StageTexture **texture) {
    D3DLOCKED_RECT locked;
    if (!HR(STAGE_CREATE_TEXTURE(dev, texture), "FAIL point sprite texture creation") ||
        !HR(STAGE_TEXTURE(LockRect, *texture, 0, &locked, NULL, 0),
            "FAIL point sprite texture lock"))
        return FALSE;
    for (UINT y = 0; y < 4; ++y)
        for (UINT x = 0; x < 4; ++x)
            *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4) =
                Texels[(y >= 2) * 2 + (x >= 2)];
    if (!HR(STAGE_TEXTURE(UnlockRect, *texture, 0), "FAIL point sprite texture unlock"))
        return FALSE;
#if DG_D3D_VERSION == 8
    const BOOL clamped =
        HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP) |
               DEVICE(SetTextureStageState, dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP),
           "FAIL point sprite texture addressing");
#else
    const BOOL clamped =
        HR(DEVICE(SetSamplerState, dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP) |
               DEVICE(SetSamplerState, dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP),
           "FAIL point sprite texture addressing");
#endif
    return clamped && vbuffer_probe::Fixed(dev, TransformedFvf) &&
           HR(STAGE_SET_TEXTURE(dev, 0, *texture), "FAIL point sprite texture bind") &&
           HR(STAGE_POINT_FILTER(dev, 0), "FAIL point sprite point filter") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXCOORDINDEX, 0),
              "FAIL point sprite coordinate set") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
              "FAIL point sprite colour argument");
}

/* The four quadrants of [left, right) x [top, bottom). */
static void Quadrants(Area *out, int left, int top, int right, int bottom) {
    const int x = (left + right) / 2, y = (top + bottom) / 2;
    out[0] = {left, top, x, y, Texels[0]};
    out[1] = {x, top, right, y, Texels[1]};
    out[2] = {left, y, x, bottom, Texels[2]};
    out[3] = {x, y, right, bottom, Texels[3]};
}

static void Run(Device *dev, Surface *target) {
    static const Transformed fan[4] = {{FanLeft, FanTop, .5f, 1, 0xffffffff, 0, 0},
                                       {FanRight, FanTop, .5f, 1, 0xffffffff, 1, 0},
                                       {FanRight, FanBottom, .5f, 1, 0xffffffff, 1, 1},
                                       {FanLeft, FanBottom, .5f, 1, 0xffffffff, 0, 1}};
    static const WORD fan_indices[4] = {0, 1, 2, 3};
    /* The point's own coordinate is in the top-right quadrant. */
    static const Transformed point = {PointX, PointY, .5f, 1, 0xffffffff, .75f, .25f};
    /* Clip space for identity transforms: x / 160 - 1, 1 - y / 120. */
    static const Untransformed strip[4] = {
        {StripLeft / 160.f - 1, 1 - StripTop / 120.f, .5f, 0, 0},
        {StripRight / 160.f - 1, 1 - StripTop / 120.f, .5f, 1, 0},
        {StripLeft / 160.f - 1, 1 - StripBottom / 120.f, .5f, 0, 1},
        {StripRight / 160.f - 1, 1 - StripBottom / 120.f, .5f, 1, 1}};
    static const float size = (float)PointSize, one = 1;
    StageCaps caps = {};
    StageTexture *texture = NULL;
    Log("STAGE point sprites on fixed-function triangles");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL point sprite device caps"))
        return;
    const BOOL points = caps.MaxPointSize >= size;
    const BOOL claimed = SpritesClaimed(gl_module);
    Number("MaxPointSize bits", Bits(caps.MaxPointSize));
    Log(claimed ? "point sprite extension: GL_ARB_point_sprite" : "point sprite extension: absent");
    if (!points)
        Log("SKIP point sprite point: MaxPointSize below 16, quads only");
    else if (!claimed)
        Log("SKIP point sprite rasterization: GL_ARB_point_sprite absent, the point keeps its "
            "own coordinate");
    BOOL ok = Texture(dev, &texture) && HR(DEVICE(SetRenderState, dev, D3DRS_POINTSIZE, Bits(size)),
                                           "FAIL point sprite point size");
    for (int sprites = 1; sprites >= 0 && ok; --sprites) {
        Area areas[12];
        UINT count = 0;
        Quadrants(areas, FanLeft, FanTop, FanRight, FanBottom);
        Quadrants(areas + 4, StripLeft, StripTop, StripRight, StripBottom);
        count = 8;
        if (points && sprites && claimed) {
            Quadrants(areas + 8, PointX - PointSize / 2, PointY - PointSize / 2,
                      PointX + PointSize / 2, PointY + PointSize / 2);
            count = 12;
        } else if (points) {
            areas[8] = {PointX - PointSize / 2, PointY - PointSize / 2, PointX + PointSize / 2,
                        PointY + PointSize / 2, Texels[1]};
            count = 9;
        }
        ok = HR(DEVICE(SetRenderState, dev, D3DRS_POINTSPRITEENABLE, sprites),
                "FAIL point sprite enable") &&
             Frame(dev, target,
                   sprites ? "point sprites on: XYZRHW indexed fan, point, XYZ strip"
                           : "point sprites off: XYZRHW indexed fan, point, XYZ strip",
                   areas, count, [&]() -> BOOL {
                       return HR(STAGE_FVF(dev, TransformedFvf), "FAIL point sprite FVF") &&
                              HR(DEVICE(DrawIndexedPrimitiveUP, dev, D3DPT_TRIANGLEFAN, 0, 4, 2,
                                        fan_indices, D3DFMT_INDEX16, fan, sizeof(fan[0])),
                                 "FAIL point sprite fan draw") &&
                              (!points || HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_POINTLIST, 1,
                                                    &point, sizeof(point)),
                                             "FAIL point sprite point draw")) &&
                              HR(STAGE_FVF(dev, UntransformedFvf), "FAIL point sprite FVF") &&
                              HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, strip,
                                        sizeof(strip[0])),
                                 "FAIL point sprite strip draw");
                   });
    }
    if (ok)
        Log(points && claimed
                ? "PASS point sprites: XYZRHW fan and XYZ strip exact with sprites on and off, "
                  "16-pixel point sprite upright, point colour without sprites"
            : points ? "PASS point sprites: XYZRHW fan and XYZ strip exact with sprites on and "
                       "off, point keeps its colour (no sprites claimed)"
                     : "PASS point sprites: XYZRHW fan and XYZ strip exact with sprites on and "
                       "off (no point drawn)");
    DEVICE(SetRenderState, dev, D3DRS_POINTSPRITEENABLE, FALSE);
    DEVICE(SetRenderState, dev, D3DRS_POINTSIZE, Bits(one));
    STAGE_SET_TEXTURE(dev, 0, NULL);
#if DG_D3D_VERSION == 8
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
#else
    DEVICE(SetSamplerState, dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    DEVICE(SetSamplerState, dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
#endif
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    if (texture)
        STAGE_TEXTURE(Release, texture);
}

} // namespace point_sprite_probe
