/* SPDX-License-Identifier: GPL-2.0-or-later
 * Conditional non-power-of-two textures through the public D3D8/9 API.
 * Included by probe.cpp after rtt-probe.inc, whose fixed-function state and
 * target helpers it reuses. With ARB_texture_rectangle
 * (DG_CAP_GL_TEXTURE_RECTANGLE) WineD3D reports what the GeForce FX 5800 it
 * names reports: D3DPTEXTURECAPS_POW2 with NONPOW2CONDITIONAL, never
 * SQUAREONLY, and CUBEMAP_POW2/VOLUMEMAP_POW2 with cube and volume maps. An
 * older host reports POW2 alone: the caps are recorded and the stage skipped.
 * Cases:
 * - caps: the NPOT bits above, exactly; TextureCaps is logged whole.
 * - texture: a MANAGED 640x480 X8R8G8B8 texture of one level, Lock-filled so
 *   every texel differs (Pattern), CLAMP addressing, POINT filtering, no mip
 *   filter. Four 320x240 1:1 views of its quarters cover every texel,
 *   including the last row and column, and a fifth view from (480, 360) runs
 *   160 columns and 120 rows past the edges, which CLAMP repeats.
 * - target: a DEFAULT 640x480 X8R8G8B8 render-target texture (an attachment of
 *   its own size, no longer a padded 1024x512 one) cleared with rectangles at
 *   its first texels, last eight rows and columns and last corner, plus a
 *   drawn quad, sampled back 1:1 in four views and, in D3D9, read whole with
 *   GetRenderTargetData. Without framebuffer objects (see rtt_probe::Run) the
 *   case is recorded and skipped.
 * - restrictions, as D3D reports them on POW2|NONPOW2CONDITIONAL hardware:
 *   mipmapped NPOT textures (all levels, or two) fail with
 *   D3DERR_INVALIDCALL; an NPOT DXT1 texture (block-aligned 640x480) cannot be
 *   created either (any failure; its HRESULT is logged); CheckDeviceFormat of
 *   DXT1 textures is logged and decides whether that DXT case applies.
 */
#if DG_D3D_VERSION == 8
#define NPOT_TEXTURE(device, width, height, levels, usage, format, pool, out)                      \
    DEVICE(CreateTexture, device, width, height, levels, usage, format, pool, out)
#else
#define NPOT_TEXTURE(device, width, height, levels, usage, format, pool, out)                      \
    DEVICE(CreateTexture, device, width, height, levels, usage, format, pool, out, NULL)
#endif

namespace npot_probe {

static constexpr int Width = 640, Height = 480, ScreenWidth = 320, ScreenHeight = 240;
static constexpr DWORD Background = 0x00102030, Unchecked = 0xffffffff;
static constexpr DWORD Npot = D3DPTEXTURECAPS_POW2 | D3DPTEXTURECAPS_NONPOW2CONDITIONAL;

/* Every texel of the 640x480 texture differs: x and y low bytes in red and
 * green, their high bits in blue. */
static DWORD Pattern(int x, int y) {
    return (DWORD)(x & 255) << 16 | (DWORD)(y & 255) << 8 | (DWORD)(x >> 8 | (y >> 8) << 4);
}

/* The render target: base, a drawn quad, then clears of the first texels,
 * the last eight columns and rows and the last corner. */
static constexpr DWORD TargetBase = 0x00203040, TargetQuad = 0xffc04080;
static constexpr int QuadLeft = 300, QuadTop = 200, QuadRight = 340, QuadBottom = 240;
struct Fill {
    int left, top, right, bottom;
    DWORD color;
};
static constexpr Fill Fills[] = {
    {0, 0, 4, 4, 0xfff02060},
    {Width - 8, 0, Width, Height, 0xff40c020},
    {0, Height - 8, Width, Height, 0xff2080e0},
    {Width - 4, Height - 4, Width, Height, 0xffe0e020},
};
static DWORD Target(int x, int y) {
    for (UINT i = sizeof(Fills) / sizeof(Fills[0]); i-- > 0;)
        if (x >= Fills[i].left && x < Fills[i].right && y >= Fills[i].top && y < Fills[i].bottom)
            return Fills[i].color & 0xffffff;
    /* A drawn edge may go either way; its interior and outside are exact. */
    if (x >= QuadLeft - 1 && x <= QuadRight && y >= QuadTop - 1 && y <= QuadBottom &&
        (x <= QuadLeft || x >= QuadRight - 1 || y <= QuadTop || y >= QuadBottom - 1))
        return Unchecked;
    if (x >= QuadLeft && x < QuadRight && y >= QuadTop && y < QuadBottom)
        return TargetQuad & 0xffffff;
    return TargetBase;
}

typedef DWORD (*Model)(int x, int y);
struct Seen {
    UINT checked, last_row, last_column, corner;
};

/* Compare a width x height locked image whose pixel (x, y) shows the source
 * texel (min(sx + x, Width - 1), min(sy + y, Height - 1)) of model; log the
 * first wrong pixel and the counts. */
static BOOL Compare(const char *name, const D3DLOCKED_RECT &locked, int width, int height, int sx,
                    int sy, Model model, Seen *seen) {
    UINT wrong = 0;
    int first_x = 0, first_y = 0;
    DWORD first_actual = 0, first_expected = 0;
    if (!locked.pBits || locked.Pitch < width * 4) {
        Log(name);
        return Check(FALSE, "FAIL NPOT readback layout", (DWORD)locked.Pitch);
    }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int tx = sx + x < Width ? sx + x : Width - 1;
            const int ty = sy + y < Height ? sy + y : Height - 1;
            const DWORD expected = model(tx, ty);
            if (expected == Unchecked)
                continue;
            const DWORD actual =
                *(const DWORD *)((const BYTE *)locked.pBits + y * locked.Pitch + x * 4) & 0xffffff;
            ++seen->checked;
            seen->last_row += ty == Height - 1;
            seen->last_column += tx == Width - 1;
            seen->corner += tx == Width - 1 && ty == Height - 1;
            if (actual != expected && !wrong++) {
                first_x = x;
                first_y = y;
                first_actual = actual;
                first_expected = expected;
            }
        }
    if (!wrong)
        return TRUE;
    Log("FAIL NPOT pixels");
    Log(name);
    Number("NPOT pixel actual", first_actual);
    Number("NPOT pixel expected", first_expected);
    Number("NPOT pixel x", (DWORD)first_x);
    Number("NPOT pixel y", (DWORD)first_y);
    Number("NPOT source texel x", (DWORD)(sx + first_x < Width ? sx + first_x : Width - 1));
    Number("NPOT source texel y", (DWORD)(sy + first_y < Height ? sy + first_y : Height - 1));
    Number("NPOT wrong pixels", wrong);
    failed = TRUE;
    return FALSE;
}

/* The backbuffer, without a depth-stencil surface (this device has none). */
static BOOL Back(Device *dev, Surface *backbuffer) {
    return rtt_probe::Target(dev, backbuffer, NULL, ScreenWidth, ScreenHeight);
}

/* Fill the backbuffer with a 320x240 1:1 point-sampled view of texture from
 * texel (sx, sy); half-pixel corners put pixel centres on texel centres, and
 * coordinates past 1 clamp. Then compare it with model. */
static BOOL View(Device *dev, Surface *backbuffer, StageTexture *texture, int sx, int sy,
                 Model model, const char *name, Seen *seen) {
    const float x0 = -.5f, x1 = ScreenWidth - .5f, y0 = -.5f, y1 = ScreenHeight - .5f;
    const float u0 = (float)sx / Width, u1 = (float)(sx + ScreenWidth) / Width;
    const float v0 = (float)sy / Height, v1 = (float)(sy + ScreenHeight) / Height;
    const rtt_probe::Textured quad[4] = {{x0, y0, .5f, 1, u0, v0},
                                         {x1, y0, .5f, 1, u1, v0},
                                         {x0, y1, .5f, 1, u0, v1},
                                         {x1, y1, .5f, 1, u1, v1}};
    D3DLOCKED_RECT locked;
    if (!HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000 | Background, 1, 0),
            "FAIL NPOT view clear") ||
        !HR(STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1), "FAIL NPOT view FVF") ||
        !HR(STAGE_SET_TEXTURE(dev, 0, texture), "FAIL NPOT texture bind") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
            "FAIL NPOT texture arg") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
            "FAIL NPOT texture alpha arg") ||
        !HR(DEVICE(BeginScene, dev), "FAIL NPOT view begin scene"))
        return FALSE;
    const BOOL drawn =
        HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
           "FAIL NPOT view draw");
    if (!HR(DEVICE(EndScene, dev), "FAIL NPOT view end scene") || !drawn)
        return FALSE;
    if (!HR(SURFACE(LockRect, backbuffer, &locked, NULL, D3DLOCK_READONLY),
            "FAIL NPOT backbuffer readback"))
        return FALSE;
    const BOOL ok = Compare(name, locked, ScreenWidth, ScreenHeight, sx, sy, model, seen);
    SURFACE(UnlockRect, backbuffer);
    return ok;
}

/* The four quarters, which cover every texel of a 640x480 source once. */
static BOOL Quarters(Device *dev, Surface *backbuffer, StageTexture *texture, Model model,
                     const char *name, Seen *seen) {
    for (int quarter = 0; quarter < 4; ++quarter)
        if (!View(dev, backbuffer, texture, quarter & 1 ? ScreenWidth : 0,
                  quarter & 2 ? ScreenHeight : 0, model, name, seen)) {
            Number("NPOT view quarter", (DWORD)quarter);
            return FALSE;
        }
    return TRUE;
}

/* Every texel, and CLAMP beyond the last row and column. */
static void TextureCase(Device *dev, Surface *backbuffer) {
    StageTexture *texture = NULL;
    D3DLOCKED_RECT locked;
    D3DSURFACE_DESC desc = {};
    Seen seen = {}, clamped = {};
    Log("STAGE NPOT texture 640x480 X8R8G8B8, CLAMP, POINT");
    BOOL ok = HR(NPOT_TEXTURE(dev, Width, Height, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &texture),
                 "FAIL NPOT 640x480 texture creation (one level)") &&
              HR(STAGE_TEXTURE(GetLevelDesc, texture, 0, &desc), "FAIL NPOT level description") &&
              Check(desc.Width == (UINT)Width && desc.Height == (UINT)Height,
                    "FAIL NPOT level size", desc.Width << 16 | desc.Height) &&
              Check(STAGE_TEXTURE(GetLevelCount, texture) == 1, "FAIL NPOT level count",
                    STAGE_TEXTURE(GetLevelCount, texture)) &&
              HR(STAGE_TEXTURE(LockRect, texture, 0, &locked, NULL, 0), "FAIL NPOT texture lock");
    if (ok) {
        const BOOL mapped = Check(locked.pBits && locked.Pitch >= Width * 4,
                                  "FAIL NPOT texture lock layout", (DWORD)locked.Pitch);
        if (mapped)
            for (int y = 0; y < Height; ++y)
                for (int x = 0; x < Width; ++x)
                    *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4) =
                        0xff000000 | Pattern(x, y);
        ok = HR(STAGE_TEXTURE(UnlockRect, texture, 0), "FAIL NPOT texture unlock") && mapped;
    }
    ok = ok && Quarters(dev, backbuffer, texture, Pattern, "texture: 640x480 quarters 1:1", &seen);
    ok = ok && Check(seen.last_row == Width && seen.last_column == Height && seen.corner == 1,
                     "FAIL NPOT texture edges not covered", seen.last_row);
    ok = ok && View(dev, backbuffer, texture, 480, 360, Pattern,
                    "texture: CLAMP past the last row and column", &clamped);
    if (ok) {
        Number("NPOT texture texels checked", seen.checked);
        Number("NPOT clamped view pixels on the last column", clamped.last_column);
        Number("NPOT clamped view pixels on the last row", clamped.last_row);
        Log("PASS NPOT texture: 640x480 X8R8G8B8 sampled 1:1, all 307200 texels exact including "
            "the last row and column; CLAMP repeats them past the edges");
    }
    STAGE_SET_TEXTURE(dev, 0, NULL);
    if (texture)
        STAGE_TEXTURE(Release, texture);
}

/* A 640x480 render-target texture, cleared and drawn, sampled back. */
static void TargetCase(D3D *d3d, Device *dev, Surface *backbuffer) {
    StageTexture *texture = NULL;
    Surface *level = NULL;
    D3DSURFACE_DESC desc = {};
    Seen seen = {};
    Log("STAGE NPOT render target 640x480");
    /* As rtt_probe::Run: only framebuffer objects render a target beyond the
     * 320x240 window, and only they claim a target in the other depth. */
    D3DDISPLAYMODE mode = {};
    if (!HR(D3D_CALL(GetAdapterDisplayMode, d3d, D3DADAPTER_DEFAULT, &mode),
            "FAIL NPOT adapter display mode"))
        return;
    const D3DFORMAT other = mode.Format == D3DFMT_R5G6B5 ? D3DFMT_X8R8G8B8 : D3DFMT_R5G6B5;
    if (FAILED(D3D_CALL(CheckDeviceFormat, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format,
                        D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, other))) {
        Log("SKIP NPOT render target: offscreen targets render through the backbuffer (no "
            "framebuffer objects)");
        return;
    }
    BOOL ok =
        HR(NPOT_TEXTURE(dev, Width, Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                        D3DPOOL_DEFAULT, &texture),
           "FAIL NPOT render-target texture creation") &&
        HR(STAGE_TEXTURE(GetSurfaceLevel, texture, 0, &level), "FAIL NPOT render-target level") &&
        HR(SURFACE(GetDesc, level, &desc), "FAIL NPOT render-target description") &&
        Check(desc.Width == (UINT)Width && desc.Height == (UINT)Height,
              "FAIL NPOT render-target size", desc.Width << 16 | desc.Height) &&
        rtt_probe::Target(dev, level, NULL, Width, Height) &&
        HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000 | TargetBase, 1, 0),
           "FAIL NPOT render-target clear") &&
        HR(DEVICE(BeginScene, dev), "FAIL NPOT render-target begin scene");
    if (ok) {
        BOOL drawn = rtt_probe::Solid(
            dev, rtt_probe::Quad(QuadLeft, QuadTop, QuadRight, QuadBottom, TargetQuad), .5f);
        for (const Fill &fill : Fills) {
            const D3DRECT rect = {fill.left, fill.top, fill.right, fill.bottom};
            drawn = drawn && HR(DEVICE(Clear, dev, 1, &rect, D3DCLEAR_TARGET, fill.color, 1, 0),
                                "FAIL NPOT render-target rectangle clear");
        }
        ok = HR(DEVICE(EndScene, dev), "FAIL NPOT render-target end scene") && drawn;
    }
    ok = ok && Back(dev, backbuffer) &&
         Quarters(dev, backbuffer, texture, Target, "target: 640x480 quarters sampled 1:1", &seen);
    ok = ok && Check(seen.last_row == Width && seen.last_column == Height && seen.corner == 1,
                     "FAIL NPOT render-target edges not covered", seen.last_row);
#if DG_D3D_VERSION == 9
    Surface *copy = NULL;
    Seen read = {};
    if (ok) {
        D3DLOCKED_RECT locked;
        ok = HR(DEVICE(CreateOffscreenPlainSurface, dev, Width, Height, D3DFMT_X8R8G8B8,
                       D3DPOOL_SYSTEMMEM, &copy, NULL),
                "FAIL NPOT system-memory surface creation") &&
             HR(DEVICE(GetRenderTargetData, dev, level, copy), "FAIL NPOT GetRenderTargetData") &&
             HR(SURFACE(LockRect, copy, &locked, NULL, D3DLOCK_READONLY),
                "FAIL NPOT GetRenderTargetData lock");
        if (ok) {
            ok = Compare("target: GetRenderTargetData", locked, Width, Height, 0, 0, Target, &read);
            SURFACE(UnlockRect, copy);
        }
    }
    if (copy)
        SURFACE(Release, copy);
#endif
    if (ok)
        Log("PASS NPOT render target: 640x480 cleared and drawn to its last row and column, "
            "sampled back 1:1"
#if DG_D3D_VERSION == 9
            " and read whole by GetRenderTargetData"
#endif
            " exact");
    STAGE_SET_TEXTURE(dev, 0, NULL);
    Back(dev, backbuffer);
    if (level)
        SURFACE(Release, level);
    if (texture)
        STAGE_TEXTURE(Release, texture);
}

/* One 640x480 MANAGED creation that must fail with expected (S_OK: with any
 * failure); a texture that was created anyway is released. */
static BOOL Refused(Device *dev, UINT levels, D3DFORMAT format, HRESULT expected,
                    const char *name) {
    StageTexture *texture = NULL;
    const HRESULT hr =
        NPOT_TEXTURE(dev, Width, Height, levels, 0, format, D3DPOOL_MANAGED, &texture);
    Number(name, (DWORD)hr);
    if (texture)
        STAGE_TEXTURE(Release, texture);
    if (expected != S_OK)
        return Check(hr == expected, "FAIL NPOT creation answer", (DWORD)hr);
    return Check(FAILED(hr), "FAIL NPOT creation succeeded", (DWORD)hr);
}

static void RestrictionCase(D3D *d3d, Device *dev) {
    Log("STAGE NPOT conditional restrictions");
    const HRESULT dxt = D3D_CALL(CheckDeviceFormat, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                 D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT1);
    Number("DXT1 texture CheckDeviceFormat", (DWORD)dxt);
    BOOL ok = Refused(dev, 0, D3DFMT_X8R8G8B8, D3DERR_INVALIDCALL,
                      "CreateTexture 640x480 X8R8G8B8, all levels") &&
              Refused(dev, 2, D3DFMT_X8R8G8B8, D3DERR_INVALIDCALL,
                      "CreateTexture 640x480 X8R8G8B8, two levels");
    if (FAILED(dxt))
        Log("SKIP NPOT DXT1 texture: DXT1 textures not claimed");
    else
        ok = ok && Refused(dev, 1, D3DFMT_DXT1, S_OK, "CreateTexture 640x480 DXT1, one level");
    if (ok)
        Log(SUCCEEDED(dxt) ? "PASS NPOT restrictions: mipmapped NPOT textures D3DERR_INVALIDCALL, "
                             "NPOT DXT1 texture refused"
                           : "PASS NPOT restrictions: mipmapped NPOT textures D3DERR_INVALIDCALL");
}

static void Run(D3D *d3d, Device *dev, Surface *backbuffer) {
    StageCaps caps = {};
    Log("STAGE NPOT textures (conditional non-power-of-two)");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL NPOT device caps"))
        return;
    const DWORD bits = caps.TextureCaps;
    Number("TextureCaps", bits);
    Number("TextureAddressCaps", caps.TextureAddressCaps);
    Number("MaxTextureWidth", caps.MaxTextureWidth);
    Number("MaxTextureHeight", caps.MaxTextureHeight);
    if ((bits & Npot) == D3DPTEXTURECAPS_POW2) {
        Log("SKIP NPOT textures: POW2 without NONPOW2CONDITIONAL (no texture rectangles)");
        return;
    }
    /* The GeForce FX 5800's answer: conditional NPOT 2D textures, square or
     * not; cube and volume maps powers of two. */
    if (!Check((bits & Npot) == Npot, "FAIL TextureCaps POW2|NONPOW2CONDITIONAL", bits) ||
        !Check(!(bits & D3DPTEXTURECAPS_SQUAREONLY), "FAIL TextureCaps SQUAREONLY", bits) ||
        !Check(!(bits & D3DPTEXTURECAPS_CUBEMAP) || (bits & D3DPTEXTURECAPS_CUBEMAP_POW2),
               "FAIL TextureCaps CUBEMAP without CUBEMAP_POW2", bits) ||
        !Check(!(bits & D3DPTEXTURECAPS_VOLUMEMAP) || (bits & D3DPTEXTURECAPS_VOLUMEMAP_POW2),
               "FAIL TextureCaps VOLUMEMAP without VOLUMEMAP_POW2", bits) ||
        !Check(caps.MaxTextureWidth >= (DWORD)Width && caps.MaxTextureHeight >= (DWORD)Height,
               "FAIL NPOT textures need 640x480", caps.MaxTextureWidth))
        return;
    Log("PASS NPOT caps: POW2|NONPOW2CONDITIONAL, not SQUAREONLY, cube and volume maps POW2");
    if (rtt_probe::State(dev)) {
        TextureCase(dev, backbuffer);
        TargetCase(d3d, dev, backbuffer);
        RestrictionCase(d3d, dev);
    }
    STAGE_SET_TEXTURE(dev, 0, NULL);
    (void)RTT_ADDRESS(dev, D3DTADDRESS_WRAP);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

} // namespace npot_probe
