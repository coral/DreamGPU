/* SPDX-License-Identifier: GPL-2.0-or-later
 * Cube textures through the public D3D8/9 API. Included by probe.cpp. A MANAGED
 * 4x4 A8R8G8B8 cube texture gets one solid colour per face through LockRect;
 * six quads with constant 3D texture coordinates along each axis must sample
 * exactly their face. A device that does not claim D3DPTEXTURECAPS_CUBEMAP
 * records that and skips.
 */
#if DG_D3D_VERSION == 8
typedef IDirect3DCubeTexture8 CubeTexture;
#define CUBE_CREATE(device, out)                                                                   \
    DEVICE(CreateCubeTexture, device, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, out)
#define CUBE_TEXTURE(name, ...) IDirect3DCubeTexture8_##name(__VA_ARGS__)
#define CUBE_SET(device, texture) DEVICE(SetTexture, device, 0, (IDirect3DBaseTexture8 *)(texture))
#else
typedef IDirect3DCubeTexture9 CubeTexture;
#define CUBE_CREATE(device, out)                                                                   \
    DEVICE(CreateCubeTexture, device, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, out, NULL)
#define CUBE_TEXTURE(name, ...) IDirect3DCubeTexture9_##name(__VA_ARGS__)
#define CUBE_SET(device, texture) DEVICE(SetTexture, device, 0, (IDirect3DBaseTexture9 *)(texture))
#endif

namespace cube_probe {

struct Vertex {
    float x, y, z, rhw;
    float s, t, r;
};

static void Run(Device *dev, Surface *target) {
#if DG_D3D_VERSION == 8
    D3DCAPS8 caps = {};
#else
    D3DCAPS9 caps = {};
#endif
    /* +X red, -X green, +Y blue, -Y yellow, +Z magenta, -Z cyan. */
    static const DWORD colors[6] = {0xffff0000, 0xff00ff00, 0xff0000ff,
                                    0xffffff00, 0xffff00ff, 0xff00ffff};
    static const float axes[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    CubeTexture *texture = NULL;
    D3DLOCKED_RECT locked;
    BOOL ok = TRUE;
    Log("STAGE cube textures");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL cube device caps"))
        return;
    Number("TextureCaps", caps.TextureCaps);
    if (!(caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP)) {
        Log("SKIP cube textures: CUBEMAP not claimed");
        return;
    }
    if (!HR(CUBE_CREATE(dev, &texture), "FAIL cube texture creation"))
        return;
    for (UINT face = 0; face < 6 && ok; ++face) {
        ok = HR(CUBE_TEXTURE(LockRect, texture, (D3DCUBEMAP_FACES)face, 0, &locked, NULL, 0),
                "FAIL cube face lock");
        if (!ok)
            break;
        for (UINT y = 0; y < 4; ++y)
            for (UINT x = 0; x < 4; ++x)
                *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4) = colors[face];
        ok = HR(CUBE_TEXTURE(UnlockRect, texture, (D3DCUBEMAP_FACES)face, 0), "FAIL cube unlock");
    }
    ok = ok &&
         HR(STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0)),
            "FAIL cube FVF") &&
         HR(CUBE_SET(dev, texture), "FAIL cube bind") &&
         HR(STAGE_POINT_FILTER(dev, 0), "FAIL cube filter") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXCOORDINDEX, 0),
            "FAIL cube coordinates") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL cube color op") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
            "FAIL cube color arg") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
            "FAIL cube alpha op") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
            "FAIL cube alpha arg") &&
         HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE),
            "FAIL cube stage 1") &&
         HR(DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, FALSE), "FAIL cube blend") &&
         HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1, 0), "FAIL cube clear") &&
         HR(DEVICE(BeginScene, dev), "FAIL cube begin scene");
    /* Six 40x40 quads in a row, each sampling one face. */
    for (UINT face = 0; face < 6 && ok; ++face) {
        float x0 = 10.f + face * 50.f, x1 = x0 + 40.f;
        const float *a = axes[face];
        Vertex quad[4] = {{x0, 100, .5f, 1, a[0], a[1], a[2]},
                          {x1, 100, .5f, 1, a[0], a[1], a[2]},
                          {x0, 140, .5f, 1, a[0], a[1], a[2]},
                          {x1, 140, .5f, 1, a[0], a[1], a[2]}};
        ok = HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
                "FAIL cube draw");
    }
    ok = ok && HR(DEVICE(EndScene, dev), "FAIL cube end scene") &&
         HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY), "FAIL cube readback");
    if (ok) {
        for (UINT face = 0; face < 6; ++face) {
            DWORD actual =
                *(DWORD *)((BYTE *)locked.pBits + 120 * locked.Pitch + (30 + face * 50) * 4);
            if ((actual & 0xffffff) != (colors[face] & 0xffffff)) {
                Number("FAIL cube face", face);
                Number("cube pixel actual", actual & 0xffffff);
                Number("cube pixel expected", colors[face] & 0xffffff);
                failed = TRUE;
                break;
            }
        }
        SURFACE(UnlockRect, target);
        if (!failed)
            Log("PASS cube textures: six faces sampled exactly");
    } else {
        failed = TRUE;
    }
    CUBE_SET(dev, NULL);
    CUBE_TEXTURE(Release, texture);
}

} // namespace cube_probe
