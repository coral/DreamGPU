/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed-function multi-stage texture blending through the public D3D8/9 API.
 * Included by probe.cpp after the single-triangle contract has passed.
 * Each op is drawn as a full-viewport quad with two solid 4x4 textures and
 * checked against the D3D texture-op formula. A device exposing a single
 * stage records its capabilities and skips; claimed ops must render exactly.
 */
#if DG_D3D_VERSION == 8
typedef IDirect3DTexture8 StageTexture;
typedef D3DCAPS8 StageCaps;
#define STAGE_TEXTURE(name, ...) IDirect3DTexture8_##name(__VA_ARGS__)
#define STAGE_CREATE_TEXTURE(device, out)                                                          \
    DEVICE(CreateTexture, device, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, out)
#define STAGE_SET_TEXTURE(device, stage, texture)                                                  \
    DEVICE(SetTexture, device, stage, (IDirect3DBaseTexture8 *)(texture))
#define STAGE_POINT_FILTER(device, stage)                                                          \
    (DEVICE(SetTextureStageState, device, stage, D3DTSS_MINFILTER, D3DTEXF_POINT) |                \
     DEVICE(SetTextureStageState, device, stage, D3DTSS_MAGFILTER, D3DTEXF_POINT) |                \
     DEVICE(SetTextureStageState, device, stage, D3DTSS_MIPFILTER, D3DTEXF_NONE))
#define STAGE_FVF(device, fvf) DEVICE(SetVertexShader, device, fvf)
#else
typedef IDirect3DTexture9 StageTexture;
typedef D3DCAPS9 StageCaps;
#define STAGE_TEXTURE(name, ...) IDirect3DTexture9_##name(__VA_ARGS__)
#define STAGE_CREATE_TEXTURE(device, out)                                                          \
    DEVICE(CreateTexture, device, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, out, NULL)
#define STAGE_SET_TEXTURE(device, stage, texture)                                                  \
    DEVICE(SetTexture, device, stage, (IDirect3DBaseTexture9 *)(texture))
#define STAGE_POINT_FILTER(device, stage)                                                          \
    (DEVICE(SetSamplerState, device, stage, D3DSAMP_MINFILTER, D3DTEXF_POINT) |                    \
     DEVICE(SetSamplerState, device, stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT) |                    \
     DEVICE(SetSamplerState, device, stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE))
#define STAGE_FVF(device, fvf) DEVICE(SetFVF, device, fvf)
#endif

namespace stage_probe {

struct Rgba {
    int r, g, b, a;
};
struct Vertex {
    float x, y, z, rhw;
    DWORD color;
    float u0, v0, u1, v1;
};
static int Clamp(int value) {
    return value < 0 ? 0 : value > 255 ? 255 : value;
}
static int Mul(int a, int b) {
    return (a * b + 127) / 255;
}
/* Arg1 is the stage texture, Arg2 the current color, per the D3D formulas. */
static Rgba Apply(D3DTEXTUREOP op, Rgba a, Rgba b) {
    Rgba out = b;
    int dot;
    switch (op) {
        case D3DTOP_MODULATE:
            out = {Mul(a.r, b.r), Mul(a.g, b.g), Mul(a.b, b.b), b.a};
            break;
        case D3DTOP_MODULATE2X:
            out = {Clamp(2 * Mul(a.r, b.r)), Clamp(2 * Mul(a.g, b.g)), Clamp(2 * Mul(a.b, b.b)),
                   b.a};
            break;
        case D3DTOP_ADD:
            out = {Clamp(a.r + b.r), Clamp(a.g + b.g), Clamp(a.b + b.b), b.a};
            break;
        case D3DTOP_ADDSIGNED:
            out = {Clamp(a.r + b.r - 128), Clamp(a.g + b.g - 128), Clamp(a.b + b.b - 128), b.a};
            break;
        case D3DTOP_SUBTRACT:
            out = {Clamp(a.r - b.r), Clamp(a.g - b.g), Clamp(a.b - b.b), b.a};
            break;
        case D3DTOP_BLENDTEXTUREALPHA:
            out = {Mul(a.r, a.a) + Mul(b.r, 255 - a.a), Mul(a.g, a.a) + Mul(b.g, 255 - a.a),
                   Mul(a.b, a.a) + Mul(b.b, 255 - a.a), b.a};
            break;
        case D3DTOP_ADDSMOOTH:
            out = {Clamp(a.r + b.r - Mul(a.r, b.r)), Clamp(a.g + b.g - Mul(a.g, b.g)),
                   Clamp(a.b + b.b - Mul(a.b, b.b)), b.a};
            break;
        case D3DTOP_BLENDTEXTUREALPHAPM:
            out = {Clamp(a.r + Mul(b.r, 255 - a.a)), Clamp(a.g + Mul(b.g, 255 - a.a)),
                   Clamp(a.b + Mul(b.b, 255 - a.a)), b.a};
            break;
        case D3DTOP_MODULATEALPHA_ADDCOLOR:
            out = {Clamp(a.r + Mul(a.a, b.r)), Clamp(a.g + Mul(a.a, b.g)),
                   Clamp(a.b + Mul(a.a, b.b)), b.a};
            break;
        case D3DTOP_MODULATECOLOR_ADDALPHA:
            out = {Clamp(Mul(a.r, b.r) + a.a), Clamp(Mul(a.g, b.g) + a.a),
                   Clamp(Mul(a.b, b.b) + a.a), b.a};
            break;
        case D3DTOP_MULTIPLYADD: /* Arg0 keeps its default, CURRENT (= Arg2 here). */
            out = {Clamp(b.r + Mul(a.r, b.r)), Clamp(b.g + Mul(a.g, b.g)),
                   Clamp(b.b + Mul(a.b, b.b)), b.a};
            break;
        case D3DTOP_DOTPRODUCT3:
            dot = ((2 * a.r - 255) * (2 * b.r - 255) + (2 * a.g - 255) * (2 * b.g - 255) +
                   (2 * a.b - 255) * (2 * b.b - 255)) /
                  255;
            dot = Clamp(dot);
            out = {dot, dot, dot, b.a};
            break;
        default:
            break;
    }
    return out;
}

struct Case {
    const char *name;
    D3DTEXTUREOP op;
    DWORD cap;
};

static BOOL Fill(Device *dev, StageTexture **texture, DWORD argb) {
    D3DLOCKED_RECT locked;
    if (!HR(STAGE_CREATE_TEXTURE(dev, texture), "FAIL stage texture creation"))
        return FALSE;
    if (!HR(STAGE_TEXTURE(LockRect, *texture, 0, &locked, NULL, 0), "FAIL stage texture lock"))
        return FALSE;
    for (UINT y = 0; y < 4; ++y)
        for (UINT x = 0; x < 4; ++x)
            *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4) = argb;
    return HR(STAGE_TEXTURE(UnlockRect, *texture, 0), "FAIL stage texture unlock");
}

static Rgba Unpack(DWORD argb) {
    return {(int)((argb >> 16) & 255), (int)((argb >> 8) & 255), (int)(argb & 255),
            (int)(argb >> 24)};
}

static BOOL Draw(Device *dev, Surface *target, const char *name, Rgba expected) {
    static const Vertex quad[4] = {{0, 0, .5f, 1, 0xffcc9966, 0, 0, 0, 0},
                                   {320, 0, .5f, 1, 0xffcc9966, 1, 0, 1, 0},
                                   {0, 240, .5f, 1, 0xffcc9966, 0, 1, 0, 1},
                                   {320, 240, .5f, 1, 0xffcc9966, 1, 1, 1, 1}};
    D3DLOCKED_RECT locked;
    if (!HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1, 0), "FAIL stage clear") ||
        !HR(DEVICE(BeginScene, dev), "FAIL stage begin scene") ||
        !HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
            "FAIL stage draw") ||
        !HR(DEVICE(EndScene, dev), "FAIL stage end scene"))
        return FALSE;
    if (!HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY), "FAIL stage readback"))
        return FALSE;
    BOOL passed = TRUE;
    for (UINT y = 100; y < 140 && passed; y += 13)
        for (UINT x = 140; x < 180 && passed; x += 13) {
            DWORD actual = *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4);
            Rgba got = Unpack(actual);
            int dr = got.r - expected.r, dg = got.g - expected.g, db = got.b - expected.b;
            if (dr < -3 || dr > 3 || dg < -3 || dg > 3 || db < -3 || db > 3) {
                Log(name);
                Number("FAIL stage pixel actual", actual & 0xffffff);
                Number("stage pixel expected",
                       (DWORD)((expected.r << 16) | (expected.g << 8) | expected.b));
                passed = FALSE;
                failed = TRUE;
            }
        }
    SURFACE(UnlockRect, target);
    return passed;
}

static void Run(Device *dev, Surface *target) {
    StageCaps caps = {};
    StageTexture *textures[2] = {};
    static const DWORD texels[2] = {0xff80ff40, 0x80a0be64};
    static const Case cases[] = {
        {"stage op MODULATE", D3DTOP_MODULATE, D3DTEXOPCAPS_MODULATE},
        {"stage op MODULATE2X", D3DTOP_MODULATE2X, D3DTEXOPCAPS_MODULATE2X},
        {"stage op ADD", D3DTOP_ADD, D3DTEXOPCAPS_ADD},
        {"stage op ADDSIGNED", D3DTOP_ADDSIGNED, D3DTEXOPCAPS_ADDSIGNED},
        {"stage op SUBTRACT", D3DTOP_SUBTRACT, D3DTEXOPCAPS_SUBTRACT},
        {"stage op BLENDTEXTUREALPHA", D3DTOP_BLENDTEXTUREALPHA, D3DTEXOPCAPS_BLENDTEXTUREALPHA},
        {"stage op DOTPRODUCT3", D3DTOP_DOTPRODUCT3, D3DTEXOPCAPS_DOTPRODUCT3},
        {"stage op ADDSMOOTH", D3DTOP_ADDSMOOTH, D3DTEXOPCAPS_ADDSMOOTH},
        {"stage op BLENDTEXTUREALPHAPM", D3DTOP_BLENDTEXTUREALPHAPM,
         D3DTEXOPCAPS_BLENDTEXTUREALPHAPM},
        {"stage op MODULATEALPHA_ADDCOLOR", D3DTOP_MODULATEALPHA_ADDCOLOR,
         D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR},
        {"stage op MODULATECOLOR_ADDALPHA", D3DTOP_MODULATECOLOR_ADDALPHA,
         D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA},
        {"stage op MULTIPLYADD", D3DTOP_MULTIPLYADD, D3DTEXOPCAPS_MULTIPLYADD},
    };
    unsigned passed = 0, skipped = 0;
    Log("STAGE fixed-function texture stages");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL stage device caps"))
        return;
    Number("MaxTextureBlendStages", caps.MaxTextureBlendStages);
    Number("MaxSimultaneousTextures", caps.MaxSimultaneousTextures);
    Number("TextureOpCaps", caps.TextureOpCaps);
    if (caps.MaxSimultaneousTextures < 2 || caps.MaxTextureBlendStages < 2) {
        Log("SKIP fixed-function stages: device exposes a single texture stage");
        return;
    }
    for (UINT i = 0; i < 2; ++i)
        if (!Fill(dev, &textures[i], texels[i]))
            goto done;
    if (!HR(STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2), "FAIL stage FVF") ||
        !HR(STAGE_SET_TEXTURE(dev, 0, textures[0]), "FAIL stage 0 texture") ||
        !HR(STAGE_SET_TEXTURE(dev, 1, textures[1]), "FAIL stage 1 texture") ||
        !HR(STAGE_POINT_FILTER(dev, 0), "FAIL stage 0 filter") ||
        !HR(STAGE_POINT_FILTER(dev, 1), "FAIL stage 1 filter") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE),
            "FAIL stage 0 op") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
            "FAIL stage 0 arg1") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE),
            "FAIL stage 0 arg2") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
            "FAIL stage 0 alpha op") ||
        !HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE),
            "FAIL stage 0 alpha arg") ||
        !HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLORARG1, D3DTA_TEXTURE),
            "FAIL stage 1 arg1") ||
        !HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLORARG2, D3DTA_CURRENT),
            "FAIL stage 1 arg2") ||
        !HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2),
            "FAIL stage 1 alpha op") ||
        !HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT),
            "FAIL stage 1 alpha arg") ||
        !HR(DEVICE(SetTextureStageState, dev, 2, D3DTSS_COLOROP, D3DTOP_DISABLE),
            "FAIL stage 2 disable"))
        goto done;
    {
        const Rgba diffuse = Unpack(0xffcc9966);
        const Rgba stage0 = Apply(D3DTOP_MODULATE, Unpack(texels[0]), diffuse);
        for (const Case &item : cases) {
            if (!(caps.TextureOpCaps & item.cap)) {
                ++skipped;
                continue;
            }
            if (!HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, item.op),
                    "FAIL stage 1 op"))
                goto done;
            if (!Draw(dev, target, item.name, Apply(item.op, Unpack(texels[1]), stage0)))
                goto done;
            ++passed;
        }
        /* A third stage reads only TFACTOR, proving stage chaining past two units. */
        if (caps.MaxTextureBlendStages >= 3) {
            const Rgba factor = Unpack(0xff80c0ff);
            if (!HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_ADD),
                    "FAIL stage 1 add") ||
                !HR(DEVICE(SetRenderState, dev, D3DRS_TEXTUREFACTOR, 0xff80c0ff),
                    "FAIL texture factor") ||
                !HR(DEVICE(SetTextureStageState, dev, 2, D3DTSS_COLOROP, D3DTOP_MODULATE),
                    "FAIL stage 2 op") ||
                !HR(DEVICE(SetTextureStageState, dev, 2, D3DTSS_COLORARG1, D3DTA_TFACTOR),
                    "FAIL stage 2 arg1") ||
                !HR(DEVICE(SetTextureStageState, dev, 2, D3DTSS_COLORARG2, D3DTA_CURRENT),
                    "FAIL stage 2 arg2"))
                goto done;
            if (!Draw(dev, target, "stage chain ADD then TFACTOR MODULATE",
                      Apply(D3DTOP_MODULATE, factor, Apply(D3DTOP_ADD, Unpack(texels[1]), stage0))))
                goto done;
            ++passed;
        }
    }
    Number("PASS fixed-function stage cases", passed);
    Number("skipped unclaimed stage ops", skipped);
done:
    STAGE_SET_TEXTURE(dev, 0, NULL);
    STAGE_SET_TEXTURE(dev, 1, NULL);
    for (StageTexture *texture : textures)
        if (texture)
            STAGE_TEXTURE(Release, texture);
}

} // namespace stage_probe
