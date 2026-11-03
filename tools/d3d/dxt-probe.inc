/* SPDX-License-Identifier: GPL-2.0-or-later
 * S3TC (DXTn) textures through the public D3D8/9 API. Included by probe.cpp
 * after the stage probe. Each claimed format is created MANAGED, written by
 * LockRect as two raw 4x4 blocks (red | green), sampled point-filtered across
 * the viewport and compared exactly. DXT5 carries half alpha and is blended
 * over white, checking its interpolated alpha block. A format the device does
 * not claim through CheckDeviceFormat is recorded and skipped.
 */
#if DG_D3D_VERSION == 8
typedef IDirect3DTexture8 DxtTexture;
#define DXT_CREATE(device, format, out)                                                            \
    DEVICE(CreateTexture, device, 8, 4, 1, 0, format, D3DPOOL_MANAGED, out)
#define DXT_SET(device, texture) DEVICE(SetTexture, device, 0, (IDirect3DBaseTexture8 *)(texture))
#define DXT_TEXTURE(name, ...) IDirect3DTexture8_##name(__VA_ARGS__)
#define DXT_POINT(device)                                                                          \
    (DEVICE(SetTextureStageState, device, 0, D3DTSS_MINFILTER, D3DTEXF_POINT) |                    \
     DEVICE(SetTextureStageState, device, 0, D3DTSS_MAGFILTER, D3DTEXF_POINT) |                    \
     DEVICE(SetTextureStageState, device, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE))
#define DXT_FVF(device, fvf) DEVICE(SetVertexShader, device, fvf)
#else
typedef IDirect3DTexture9 DxtTexture;
#define DXT_CREATE(device, format, out)                                                            \
    DEVICE(CreateTexture, device, 8, 4, 1, 0, format, D3DPOOL_MANAGED, out, NULL)
#define DXT_SET(device, texture) DEVICE(SetTexture, device, 0, (IDirect3DBaseTexture9 *)(texture))
#define DXT_TEXTURE(name, ...) IDirect3DTexture9_##name(__VA_ARGS__)
#define DXT_POINT(device)                                                                          \
    (DEVICE(SetSamplerState, device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT) |                        \
     DEVICE(SetSamplerState, device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT) |                        \
     DEVICE(SetSamplerState, device, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE))
#define DXT_FVF(device, fvf) DEVICE(SetFVF, device, fvf)
#endif

namespace dxt_probe {

struct Vertex {
    float x, y, z, rhw;
    float u, v;
};

/* One 4x4 block of a single RGB565 colour: color0 > color1, all indices 0.
 * DXT3 prefixes 64 explicit alpha bits, DXT5 an interpolated alpha block whose
 * indices all select alpha0. */
static UINT Block(BYTE *out, D3DFORMAT format, WORD color, BYTE alpha) {
    UINT at = 0;
    if (format == D3DFMT_DXT3) {
        for (UINT i = 0; i < 8; ++i)
            out[at++] = (BYTE)((alpha >> 4) * 0x11);
    } else if (format == D3DFMT_DXT5) {
        out[at++] = alpha;
        out[at++] = 0;
        for (UINT i = 0; i < 6; ++i)
            out[at++] = 0;
    }
    out[at++] = (BYTE)color;
    out[at++] = (BYTE)(color >> 8);
    for (UINT i = 0; i < 6; ++i)
        out[at++] = 0;
    return at;
}

static BOOL Near(DWORD actual, DWORD expected) {
    for (UINT shift = 0; shift < 24; shift += 8) {
        int d = (int)((actual >> shift) & 255) - (int)((expected >> shift) & 255);
        if (d < -2 || d > 2)
            return FALSE;
    }
    return TRUE;
}

static BOOL Draw(Device *dev, Surface *target, const char *name, DWORD background, DWORD left,
                 DWORD right) {
    static const Vertex quad[4] = {{0, 0, .5f, 1, 0, 0},
                                   {320, 0, .5f, 1, 1, 0},
                                   {0, 240, .5f, 1, 0, 1},
                                   {320, 240, .5f, 1, 1, 1}};
    D3DLOCKED_RECT locked;
    if (!HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, background, 1, 0), "FAIL DXT clear") ||
        !HR(DEVICE(BeginScene, dev), "FAIL DXT begin scene") ||
        !HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
            "FAIL DXT draw") ||
        !HR(DEVICE(EndScene, dev), "FAIL DXT end scene"))
        return FALSE;
    if (!HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY), "FAIL DXT readback"))
        return FALSE;
    BOOL passed = TRUE;
    static const UINT xs[4] = {20, 140, 180, 300};
    for (UINT y = 30; y < 240 && passed; y += 90)
        for (UINT i = 0; i < 4 && passed; ++i) {
            DWORD actual = *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + xs[i] * 4);
            DWORD expected = xs[i] < 160 ? left : right;
            if (!Near(actual & 0xffffff, expected)) {
                Log(name);
                Number("FAIL DXT pixel actual", actual & 0xffffff);
                Number("DXT pixel expected", expected);
                passed = FALSE;
                failed = TRUE;
            }
        }
    SURFACE(UnlockRect, target);
    return passed;
}

static void Run(D3D *d3d, Device *dev, Surface *target) {
    static const struct {
        const char *name;
        D3DFORMAT format;
        BYTE alpha;
    } cases[] = {{"DXT1 blocks", D3DFMT_DXT1, 255},
                 {"DXT3 blocks", D3DFMT_DXT3, 255},
                 {"DXT5 half-alpha blocks", D3DFMT_DXT5, 128}};
    unsigned passed = 0, skipped = 0;
    Log("STAGE DXT compressed textures");
    if (!HR(DXT_FVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1), "FAIL DXT FVF") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL DXT color op") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
            "FAIL DXT color arg") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
            "FAIL DXT alpha op") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
            "FAIL DXT alpha arg") ||
        !HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE),
            "FAIL DXT stage 1 disable") ||
        !HR(DEVICE(SetRenderState, dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA), "FAIL DXT src blend") ||
        !HR(DEVICE(SetRenderState, dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA),
            "FAIL DXT dest blend") ||
        !HR(DXT_POINT(dev), "FAIL DXT filter"))
        return;
    for (const auto &item : cases) {
        DxtTexture *texture = NULL;
        D3DLOCKED_RECT locked;
        if (FAILED(D3D_CALL(CheckDeviceFormat, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                            D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, item.format))) {
            ++skipped;
            continue;
        }
        if (!HR(DXT_CREATE(dev, item.format, &texture), "FAIL DXT texture creation"))
            return;
        if (!HR(DXT_TEXTURE(LockRect, texture, 0, &locked, NULL, 0), "FAIL DXT lock")) {
            DXT_TEXTURE(Release, texture);
            return;
        }
        UINT at = Block((BYTE *)locked.pBits, item.format, 0xf800, item.alpha);
        Block((BYTE *)locked.pBits + at, item.format, 0x07e0, item.alpha);
        BOOL blend = item.alpha != 255;
        BOOL ok =
            HR(DXT_TEXTURE(UnlockRect, texture, 0), "FAIL DXT unlock") &&
            HR(DXT_SET(dev, texture), "FAIL DXT bind") &&
            HR(DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, blend), "FAIL DXT blend state");
        /* Half alpha over white: colour/2 + 127 per channel. */
        ok = ok && (blend ? Draw(dev, target, item.name, 0xffffffff, 0xff7f7f, 0x7fff7f)
                          : Draw(dev, target, item.name, 0xff000000, 0xff0000, 0x00ff00));
        DXT_SET(dev, NULL);
        DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, FALSE);
        DXT_TEXTURE(Release, texture);
        if (!ok)
            return;
        ++passed;
    }
    Number("PASS DXT formats", passed);
    Number("skipped unclaimed DXT formats", skipped);
}

} // namespace dxt_probe
