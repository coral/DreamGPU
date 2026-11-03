/* SPDX-License-Identifier: GPL-2.0-or-later
 * Volume textures through the public D3D8/9 API. Included by probe.cpp. A
 * MANAGED 4x4x4 A8R8G8B8 volume texture gets one solid colour per slice through
 * LockBox; four quads with constant 3D texture coordinates at each slice's
 * centre must sample exactly that slice. A device that does not claim
 * D3DPTEXTURECAPS_VOLUMEMAP records that and skips.
 */
#if DG_D3D_VERSION == 8
typedef IDirect3DVolumeTexture8 VolumeTexture;
#define VOLUME_CREATE(device, out)                                                                 \
    DEVICE(CreateVolumeTexture, device, 4, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, out)
#define VOLUME_TEXTURE(name, ...) IDirect3DVolumeTexture8_##name(__VA_ARGS__)
#define VOLUME_SET(device, texture)                                                                \
    DEVICE(SetTexture, device, 0, (IDirect3DBaseTexture8 *)(texture))
#else
typedef IDirect3DVolumeTexture9 VolumeTexture;
#define VOLUME_CREATE(device, out)                                                                 \
    DEVICE(CreateVolumeTexture, device, 4, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, out, NULL)
#define VOLUME_TEXTURE(name, ...) IDirect3DVolumeTexture9_##name(__VA_ARGS__)
#define VOLUME_SET(device, texture)                                                                \
    DEVICE(SetTexture, device, 0, (IDirect3DBaseTexture9 *)(texture))
#endif

namespace volume_probe {

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
    /* Slice 0 red, 1 green, 2 blue, 3 magenta. */
    static const DWORD colors[4] = {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffff00ff};
    VolumeTexture *texture = NULL;
    D3DLOCKED_BOX box;
    D3DLOCKED_RECT locked;
    BOOL ok;
    Log("STAGE volume textures");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL volume device caps"))
        return;
    Number("TextureCaps", caps.TextureCaps);
    Number("VolumeTextureFilterCaps", caps.VolumeTextureFilterCaps);
    Number("VolumeTextureAddressCaps", caps.VolumeTextureAddressCaps);
    Number("MaxVolumeExtent", caps.MaxVolumeExtent);
    if (!(caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP)) {
        Log("SKIP volume textures: VOLUMEMAP not claimed");
        return;
    }
    if (!HR(VOLUME_CREATE(dev, &texture), "FAIL volume texture creation"))
        return;
    ok = HR(VOLUME_TEXTURE(LockBox, texture, 0, &box, NULL, 0), "FAIL volume lock");
    if (ok) {
        for (UINT z = 0; z < 4; ++z)
            for (UINT y = 0; y < 4; ++y)
                for (UINT x = 0; x < 4; ++x)
                    *(DWORD *)((BYTE *)box.pBits + z * box.SlicePitch + y * box.RowPitch + x * 4) =
                        colors[z];
        ok = HR(VOLUME_TEXTURE(UnlockBox, texture, 0), "FAIL volume unlock");
    }
    ok = ok &&
         HR(STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0)),
            "FAIL volume FVF") &&
         HR(VOLUME_SET(dev, texture), "FAIL volume bind") &&
         HR(STAGE_POINT_FILTER(dev, 0), "FAIL volume filter") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXCOORDINDEX, 0),
            "FAIL volume coordinates") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL volume color op") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
            "FAIL volume color arg") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
            "FAIL volume alpha op") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
            "FAIL volume alpha arg") &&
         HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE),
            "FAIL volume stage 1") &&
         HR(DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, FALSE), "FAIL volume blend") &&
         HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1, 0), "FAIL volume clear") &&
         HR(DEVICE(BeginScene, dev), "FAIL volume begin scene");
    /* Four 40x40 quads in a row, each sampling one slice's centre. */
    for (UINT slice = 0; slice < 4 && ok; ++slice) {
        float x0 = 10.f + slice * 50.f, x1 = x0 + 40.f, r = (slice + .5f) / 4.f;
        Vertex quad[4] = {{x0, 100, .5f, 1, .5f, .5f, r},
                          {x1, 100, .5f, 1, .5f, .5f, r},
                          {x0, 140, .5f, 1, .5f, .5f, r},
                          {x1, 140, .5f, 1, .5f, .5f, r}};
        ok = HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
                "FAIL volume draw");
    }
    ok = ok && HR(DEVICE(EndScene, dev), "FAIL volume end scene") &&
         HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY), "FAIL volume readback");
    if (ok) {
        for (UINT slice = 0; slice < 4; ++slice) {
            DWORD actual =
                *(DWORD *)((BYTE *)locked.pBits + 120 * locked.Pitch + (30 + slice * 50) * 4);
            if ((actual & 0xffffff) != (colors[slice] & 0xffffff)) {
                Number("FAIL volume slice", slice);
                Number("volume pixel actual", actual & 0xffffff);
                Number("volume pixel expected", colors[slice] & 0xffffff);
                failed = TRUE;
                break;
            }
        }
        SURFACE(UnlockRect, target);
        if (!failed)
            Log("PASS volume textures: four slices sampled exactly");
    } else {
        failed = TRUE;
    }
    VOLUME_SET(dev, NULL);
    VOLUME_TEXTURE(Release, texture);
}

} // namespace volume_probe
