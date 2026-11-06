/* SPDX-License-Identifier: GPL-2.0-or-later
 * Depth textures ("hardware shadow maps") through the public D3D8/9 API.
 * Included by probe.cpp after npot-probe.inc; reuses the render-target
 * helpers of rtt-probe.inc and the shader token helpers of shader-probe.inc.
 * With ARB_depth_texture and ARB_shadow (DG_CAP2_GL_DEPTH_TEXTURE, the second
 * capability word) WineD3D answers CheckDeviceFormat(D3DUSAGE_DEPTHSTENCIL,
 * D3DRTYPE_TEXTURE) as the GeForce FX 5800 it names: D16, D24X8 and D24S8 and
 * nothing else (Wine 0050). Sampling such a texture compares the texture
 * coordinate's r (r/q when projected) with the stored depth, 1 when r <= depth,
 * else 0, in every channel; LINEAR filtering averages the comparisons of its
 * texels (percentage-closer filtering). An older host claims no depth texture:
 * the answers are recorded and the stage skipped.
 * Cases:
 * - formats: every depth format's answer is logged; exactly the FX's three
 *   are claimed.
 * - render: a 256x256 D24S8 texture is the depth-stencil surface of a 256x256
 *   X8R8G8B8 render-target texture (framebuffer objects; without them the case
 *   is recorded and skipped). Cleared to 1, its left half takes depth 0.25 and
 *   its top-right quarter 0.75 from quads drawn with LESSEQUAL depth writes.
 * - sample: the depth texture on stage 0 (POINT, CLAMP, COUNT4 | PROJECTED
 *   coordinates (u q, v q, r q, q) with q = 2) fills the backbuffer's
 *   256x240 view 1:1 for references 0.5, 0.8 and 0.1: every pixel is exactly
 *   white or black by the comparison with its texel, and pixels beyond the
 *   view keep the clear.
 * - filter: with LINEAR and coordinates a quarter texel to the right, the
 *   column before the 0.25 | 0.75 boundary blends one failing and one passing
 *   texel: strictly between black and white and the same in every row (its
 *   value is logged); every other pixel stays exact.
 * - shaders: the same projected lookup from ps_1_1 (tex t0 with the stage's
 *   projected flag) and, in D3D9, ps_2_0 (texldp), exact as above. Unclaimed
 *   pixel shader versions are recorded and skipped.
 */
#if DG_D3D_VERSION == 8
#define DEPTH_TEXTURE(device, size, format, out)                                                   \
    DEVICE(CreateTexture, device, size, size, 1, D3DUSAGE_DEPTHSTENCIL, format, D3DPOOL_DEFAULT,   \
           out)
#define DEPTH_LINEAR_FILTER(device)                                                                \
    (DEVICE(SetTextureStageState, device, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR) |                   \
     DEVICE(SetTextureStageState, device, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR))
#else
#define DEPTH_TEXTURE(device, size, format, out)                                                   \
    DEVICE(CreateTexture, device, size, size, 1, D3DUSAGE_DEPTHSTENCIL, format, D3DPOOL_DEFAULT,   \
           out, NULL)
#define DEPTH_LINEAR_FILTER(device)                                                                \
    (DEVICE(SetSamplerState, device, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR) |                       \
     DEVICE(SetSamplerState, device, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR))
#endif

namespace depth_texture_probe {

static constexpr int Size = 256, Half = 128, ViewWidth = 256, ViewHeight = 240;
static constexpr int ScreenWidth = 320, ScreenHeight = 240;
static constexpr DWORD Background = 0x00102030, Pass = 0x00ffffff, Fail = 0x00000000;
static constexpr DWORD Unchecked = 0xffffffff;
static constexpr float Left = .25f, TopRight = .75f, Cleared = 1.f;

/* The depth written at texel (x, y). */
static float Depth(int x, int y) {
    return x < Half ? Left : y < Half ? TopRight : Cleared;
}

struct Format {
    D3DFORMAT format;
    const char *name;
    BOOL fx;
};
static const Format Formats[] = {
    {D3DFMT_D16, "D16 depth texture CheckDeviceFormat", TRUE},
    {D3DFMT_D24X8, "D24X8 depth texture CheckDeviceFormat", TRUE},
    {D3DFMT_D24S8, "D24S8 depth texture CheckDeviceFormat", TRUE},
    {D3DFMT_D16_LOCKABLE, "D16_LOCKABLE depth texture CheckDeviceFormat", FALSE},
    {D3DFMT_D32, "D32 depth texture CheckDeviceFormat", FALSE},
    {D3DFMT_D15S1, "D15S1 depth texture CheckDeviceFormat", FALSE},
    {D3DFMT_D24X4S4, "D24X4S4 depth texture CheckDeviceFormat", FALSE},
#if DG_D3D_VERSION == 9
    {D3DFMT_D32F_LOCKABLE, "D32F_LOCKABLE depth texture CheckDeviceFormat", FALSE},
    {D3DFMT_D24FS8, "D24FS8 depth texture CheckDeviceFormat", FALSE},
#endif
    {(D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'), "INTZ depth texture CheckDeviceFormat", FALSE},
};

/* Log every answer; FALSE when no depth texture is claimed (an older host).
 * Otherwise any answer other than the FX's fails the probe. */
static BOOL Claims(D3D *d3d, D3DFORMAT display, BOOL *claimed) {
    BOOL exact = TRUE;
    *claimed = FALSE;
    for (const Format &f : Formats) {
        const HRESULT hr = D3D_CALL(CheckDeviceFormat, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                    display, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, f.format);
        Number(f.name, (DWORD)hr);
        *claimed |= SUCCEEDED(hr);
        exact &= SUCCEEDED(hr) == f.fx;
    }
    return exact;
}

struct Vertex {
    float x, y, z, rhw;
    float s, t, r, q;
};

/* The backbuffer's 256x240 view of the depth texture: pixel (x, y) samples
 * texel (x + shift, y) 1:1, projected coordinates (s q, t q, r q, q). */
static BOOL View(Device *dev, StageTexture *texture, float reference, float shift) {
    const float x0 = -.5f, x1 = ViewWidth - .5f, y0 = -.5f, y1 = ViewHeight - .5f;
    const float s0 = shift / Size, s1 = (ViewWidth + shift) / Size, t1 = (float)ViewHeight / Size;
    const float q = 2;
    const Vertex quad[4] = {{x0, y0, .5f, 1, s0 * q, 0, reference * q, q},
                            {x1, y0, .5f, 1, s1 * q, 0, reference * q, q},
                            {x0, y1, .5f, 1, s0 * q, t1 * q, reference * q, q},
                            {x1, y1, .5f, 1, s1 * q, t1 * q, reference * q, q}};
    if (!HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000 | Background, 1, 0),
            "FAIL depth texture view clear") ||
        !HR(STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE4(0)),
            "FAIL depth texture view FVF") ||
        !HR(STAGE_SET_TEXTURE(dev, 0, texture), "FAIL depth texture bind") ||
        !HR(DEVICE(BeginScene, dev), "FAIL depth texture view begin scene"))
        return FALSE;
    const BOOL drawn =
        HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
           "FAIL depth texture view draw");
    return HR(DEVICE(EndScene, dev), "FAIL depth texture view end scene") && drawn;
}

/* Compare the backbuffer with the comparison of every texel against
 * reference. With linear, the column before the boundary (texel 127, rows
 * where 127 and 128 differ) is checked strictly between black and white and
 * equal in every row; its value is logged. */
static BOOL Verify(Surface *backbuffer, float reference, BOOL linear, const char *name) {
    D3DLOCKED_RECT locked;
    UINT wrong = 0;
    int first_x = 0, first_y = 0;
    DWORD first_actual = 0, first_expected = 0, blend = Unchecked;
    if (!HR(SURFACE(LockRect, backbuffer, &locked, NULL, D3DLOCK_READONLY),
            "FAIL depth texture readback"))
        return FALSE;
    if (!locked.pBits || locked.Pitch < ScreenWidth * 4) {
        SURFACE(UnlockRect, backbuffer);
        return Check(FALSE, "FAIL depth texture readback layout", (DWORD)locked.Pitch);
    }
    for (int y = 0; y < ScreenHeight; ++y)
        for (int x = 0; x < ScreenWidth; ++x) {
            const DWORD actual =
                *(const DWORD *)((const BYTE *)locked.pBits + y * locked.Pitch + x * 4) & 0xffffff;
            DWORD expected;
            if (x >= ViewWidth || y >= ViewHeight)
                expected = Background;
            else if (linear && x == Half - 1 &&
                     (reference <= Depth(x, y)) != (reference <= Depth(x + 1, y))) {
                /* Percentage-closer: one grey for the whole column. */
                const DWORD grey = actual & 0xff;
                const BOOL ok = actual == (grey * 0x010101u) && grey > 0 && grey < 255 &&
                                (blend == Unchecked || blend == actual);
                if (blend == Unchecked)
                    blend = actual;
                expected = ok ? actual : Unchecked;
            } else
                expected = reference <= Depth(x, y) ? Pass : Fail;
            if (actual != expected && !wrong++) {
                first_x = x;
                first_y = y;
                first_actual = actual;
                first_expected = expected;
            }
        }
    SURFACE(UnlockRect, backbuffer);
    if (linear)
        Number("depth texture LINEAR boundary column value", blend);
    if (!wrong)
        return TRUE;
    Log("FAIL depth texture pixels");
    Log(name);
    Number("depth texture pixel actual", first_actual);
    Number("depth texture pixel expected (0xffffffff: between black and white)", first_expected);
    Number("depth texture pixel x", (DWORD)first_x);
    Number("depth texture pixel y", (DWORD)first_y);
    Number("depth texture wrong pixels", wrong);
    failed = TRUE;
    return FALSE;
}

/* Render the depth: a 256x256 colour target with the depth texture's level as
 * its depth-stencil surface, cleared to 1, the left half drawn at 0.25 and
 * the top-right quarter at 0.75. */
static BOOL Render(Device *dev, Surface *color, Surface *depth) {
    BOOL ok = rtt_probe::Target(dev, color, depth, Size, Size) &&
              HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                        0xff000000, Cleared, 0),
                 "FAIL depth texture clear") &&
              HR(DEVICE(SetRenderState, dev, D3DRS_ZENABLE, D3DZB_TRUE),
                 "FAIL depth texture depth test") &&
              HR(DEVICE(BeginScene, dev), "FAIL depth texture begin scene");
    if (!ok)
        return FALSE;
    const BOOL drawn =
        rtt_probe::Solid(dev, rtt_probe::Quad(0, 0, Half, Size, 0xff4080c0), Left) &&
        rtt_probe::Solid(dev, rtt_probe::Quad(Half, 0, Size, Half, 0xffc08040), TopRight);
    ok = HR(DEVICE(EndScene, dev), "FAIL depth texture end scene") && drawn;
    return HR(DEVICE(SetRenderState, dev, D3DRS_ZENABLE, D3DZB_FALSE),
              "FAIL depth texture depth test off") &&
           ok;
}

/* Sampling state of stage 0 for the views: the texture's comparison in every
 * channel, projected four-component coordinates, POINT and CLAMP. */
static BOOL Sampling(Device *dev) {
    return HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
              "FAIL depth texture colour argument") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
              "FAIL depth texture alpha argument") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS,
                     D3DTTFF_COUNT4 | D3DTTFF_PROJECTED),
              "FAIL depth texture projected coordinates") &&
           HR(STAGE_POINT_FILTER(dev, 0), "FAIL depth texture point filter") &&
           HR(RTT_ADDRESS(dev, D3DTADDRESS_CLAMP), "FAIL depth texture clamp");
}

/* The three references through POINT views. */
static BOOL References(Device *dev, Surface *backbuffer, StageTexture *texture, const char *how) {
    static const float references[] = {.5f, .8f, .1f};
    for (float reference : references)
        if (!View(dev, texture, reference, 0) || !Verify(backbuffer, reference, FALSE, how)) {
            Number("depth texture reference (float bits)", __builtin_bit_cast(DWORD, reference));
            return FALSE;
        }
    return TRUE;
}

/* ps_1_1: tex t0; mov r0, t0. The stage's projected flag applies to tex. */
static constexpr DWORD ps11[] = {
    0xffff0101,
    shader_probe::Instruction(D3DSIO_TEX, 0),
    shader_probe::Dst(shader_probe::RegisterTexture, 0, 0xf),
    shader_probe::Instruction(D3DSIO_MOV, 0),
    shader_probe::Dst(shader_probe::RegisterTemp, 0, 0xf),
    shader_probe::Src(shader_probe::RegisterTexture, 0),
    0x0000ffff,
};
static_assert(ps11[0] == D3DPS_VERSION(1, 1) && ps11[6] == D3DPS_END());
#if DG_D3D_VERSION == 9
/* ps_2_0: dcl t0; dcl_2d s0; texldp r0, t0, s0; mov oC0, r0. */
static constexpr DWORD ps20[] = {
    0xffff0200,
    shader_probe::Instruction(D3DSIO_DCL, 2),
    0x80000000,
    shader_probe::Dst(shader_probe::RegisterTexture, 0, 0xf),
    shader_probe::Instruction(D3DSIO_DCL, 2),
    0x80000000u | (DWORD)D3DSTT_2D,
    shader_probe::Dst(shader_probe::RegisterSampler, 0, 0xf),
    shader_probe::Instruction(D3DSIO_TEX, 3) | D3DSI_TEXLD_PROJECT,
    shader_probe::Dst(shader_probe::RegisterTemp, 0, 0xf),
    shader_probe::Src(shader_probe::RegisterTexture, 0),
    shader_probe::Src(shader_probe::RegisterSampler, 0),
    shader_probe::Instruction(D3DSIO_MOV, 2),
    shader_probe::Dst(shader_probe::RegisterColorOut, 0, 0xf),
    shader_probe::Src(shader_probe::RegisterTemp, 0),
    0x0000ffff,
};
static_assert(ps20[0] == D3DPS_VERSION(2, 0) && ps20[14] == D3DPS_END());
static_assert(D3DSI_TEXLD_PROJECT == 1u << 16);
#endif

/* One pixel shader's projected lookups, exact for the three references. */
static void Shader(Device *dev, Surface *backbuffer, StageTexture *texture, const DWORD *tokens,
                   const char *stage, const char *how, const char *pass) {
    PixelShader shader = SHADER_NONE;
    Log(stage);
    const BOOL ok = HR(SHADER_CREATE(dev, tokens, &shader), "FAIL depth texture pixel shader") &&
                    HR(SHADER_SET(dev, shader), "FAIL depth texture pixel shader bind") &&
                    References(dev, backbuffer, texture, how);
    SHADER_SET(dev, SHADER_NONE);
    if (shader != SHADER_NONE)
        SHADER_DELETE(dev, shader);
    if (ok)
        Log(pass);
}

static void RenderCase(D3D *d3d, Device *dev, Surface *backbuffer, const StageCaps &caps) {
    StageTexture *color = NULL, *depth = NULL;
    Surface *color_level = NULL, *depth_level = NULL;
    D3DSURFACE_DESC desc = {};
    Log("STAGE depth texture D24S8 256x256: render depth, sample with comparison");
    /* As rtt_probe::Run: only framebuffer objects render to a target with its
     * own depth-stencil surface beyond the 320x240 window. */
    D3DDISPLAYMODE mode = {};
    if (!HR(D3D_CALL(GetAdapterDisplayMode, d3d, D3DADAPTER_DEFAULT, &mode),
            "FAIL depth texture adapter display mode"))
        return;
    const D3DFORMAT other = mode.Format == D3DFMT_R5G6B5 ? D3DFMT_X8R8G8B8 : D3DFMT_R5G6B5;
    if (FAILED(D3D_CALL(CheckDeviceFormat, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format,
                        D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, other))) {
        Log("SKIP depth texture rendering: offscreen targets render through the backbuffer (no "
            "framebuffer objects)");
        return;
    }
    BOOL ok =
        rtt_probe::Create(dev, Size, Size, D3DFMT_X8R8G8B8, &color, &color_level) &&
        HR(DEPTH_TEXTURE(dev, Size, D3DFMT_D24S8, &depth), "FAIL D24S8 depth texture creation") &&
        HR(STAGE_TEXTURE(GetSurfaceLevel, depth, 0, &depth_level), "FAIL depth texture level") &&
        HR(SURFACE(GetDesc, depth_level, &desc), "FAIL depth texture description") &&
        Check(desc.Width == (UINT)Size && desc.Height == (UINT)Size &&
                  desc.Format == D3DFMT_D24S8 && (desc.Usage & D3DUSAGE_DEPTHSTENCIL) &&
                  desc.Pool == D3DPOOL_DEFAULT,
              "FAIL depth texture level description", desc.Width << 16 | desc.Height) &&
        Render(dev, color_level, depth_level) &&
        rtt_probe::Target(dev, backbuffer, NULL, ScreenWidth, ScreenHeight) && Sampling(dev) &&
        References(dev, backbuffer, depth, "fixed function, POINT");
    if (ok)
        Log("PASS depth texture: D24S8 rendered as a depth-stencil surface, sampled with "
            "projected comparisons (r/q 0.5, 0.8, 0.1) exactly white or black per texel");
    if (ok) {
        ok = HR(DEPTH_LINEAR_FILTER(dev), "FAIL depth texture linear filter") &&
             View(dev, depth, .5f, .25f) &&
             Verify(backbuffer, .5f, TRUE, "fixed function, LINEAR a quarter texel right");
        if (ok)
            Log("PASS depth texture filter: LINEAR averages the comparisons at the depth "
                "boundary (percentage-closer), exact elsewhere");
        ok = ok && HR(STAGE_POINT_FILTER(dev, 0), "FAIL depth texture point filter");
    }
    const DWORD version = caps.PixelShaderVersion & 0xffff;
    Number("PixelShaderVersion", caps.PixelShaderVersion);
    if (ok && version >= 0x0101)
        Shader(dev, backbuffer, depth, ps11, "STAGE depth texture ps_1_1 tex t0 (projected)",
               "ps_1_1 tex, POINT", "PASS depth texture ps_1_1: projected tex comparisons exact");
    else if (ok)
        Log("SKIP depth texture ps_1_1: pixel shaders not claimed");
#if DG_D3D_VERSION == 9
    if (ok && !failed && version >= 0x0200)
        Shader(dev, backbuffer, depth, ps20, "STAGE depth texture ps_2_0 texldp",
               "ps_2_0 texldp, POINT", "PASS depth texture ps_2_0: texldp comparisons exact");
    else if (ok && !failed)
        Log("SKIP depth texture ps_2_0: ps_2_0 not claimed");
#endif
    STAGE_SET_TEXTURE(dev, 0, NULL);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    rtt_probe::Target(dev, backbuffer, NULL, ScreenWidth, ScreenHeight);
    if (depth_level)
        SURFACE(Release, depth_level);
    if (depth)
        STAGE_TEXTURE(Release, depth);
    rtt_probe::Release(color, color_level);
}

static void Run(D3D *d3d, Device *dev, Surface *backbuffer) {
    StageCaps caps = {};
    D3DDISPLAYMODE mode = {};
    BOOL claimed;
    Log("STAGE depth textures (hardware shadow maps)");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL depth texture device caps") ||
        !HR(D3D_CALL(GetAdapterDisplayMode, d3d, D3DADAPTER_DEFAULT, &mode),
            "FAIL depth texture display mode"))
        return;
    const BOOL exact = Claims(d3d, mode.Format, &claimed);
    if (!claimed) {
        Log("SKIP depth textures: no depth format claimed as a texture (no ARB_depth_texture)");
        return;
    }
    if (!Check(exact, "FAIL depth texture formats: the GeForce FX takes D16, D24X8 and D24S8 only",
               0))
        return;
    Log("PASS depth texture formats: D16, D24X8 and D24S8 textures, no other depth format");
    if (rtt_probe::State(dev))
        RenderCase(d3d, dev, backbuffer, caps);
    STAGE_SET_TEXTURE(dev, 0, NULL);
    (void)RTT_ADDRESS(dev, D3DTADDRESS_WRAP);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

} // namespace depth_texture_probe
