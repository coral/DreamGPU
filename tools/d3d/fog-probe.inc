/* SPDX-License-Identifier: GPL-2.0-or-later
 * Vertex fog of pretransformed vertices through the public D3D8/9 API.
 * Included by probe.cpp after stage-probe.inc. With FOGTABLEMODE and
 * FOGVERTEXMODE NONE, D3D takes each XYZRHW vertex's fog factor from its
 * specular alpha (255 = clear, 0 = fog colour) and interpolates it. Fog applies
 * after texturing: a red texture modulating white diffuse fades from red on
 * the left to blue on the right. Blending the vertex colour toward the fog
 * colour before texturing (Wine's emulation without EXT_fog_coord) instead
 * fades to black. Claiming D3DPRASTERCAPS_FOGVERTEX requires the former.
 */
namespace fog_probe {

struct Vertex {
    float x, y, z, rhw;
    DWORD diffuse, specular;
    float u, v;
};

static BOOL Near(DWORD actual, DWORD expected, int tolerance) {
    for (UINT shift = 0; shift < 24; shift += 8) {
        int d = (int)((actual >> shift) & 255) - (int)((expected >> shift) & 255);
        if (d < -tolerance || d > tolerance)
            return FALSE;
    }
    return TRUE;
}

static void Run(Device *dev, Surface *target) {
#if DG_D3D_VERSION == 8
    D3DCAPS8 caps = {};
#else
    D3DCAPS9 caps = {};
#endif
    static const Vertex quad[4] = {{0, 0, .5f, 1, 0xffffffff, 0xff000000, 0, 0},
                                   {320, 0, .5f, 1, 0xffffffff, 0x00000000, 1, 0},
                                   {0, 240, .5f, 1, 0xffffffff, 0xff000000, 0, 1},
                                   {320, 240, .5f, 1, 0xffffffff, 0x00000000, 1, 1}};
    /* Pixel centres x + 0.5: fog factor f = 1 - (x + 0.5) / 320, colour
     * f * red + (1 - f) * blue. */
    static const struct {
        UINT x;
        DWORD expected;
    } samples[] = {
        {1, 0xfe0001}, {80, 0xbf0040}, {160, 0x7f0080}, {240, 0x4000bf}, {318, 0x0100fe}};
    StageTexture *texture = NULL;
    D3DLOCKED_RECT locked;
    BOOL ok;
    Log("STAGE pretransformed vertex fog");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL fog device caps"))
        return;
    if (!(caps.RasterCaps & D3DPRASTERCAPS_FOGVERTEX)) {
        Log("SKIP vertex fog: FOGVERTEX not claimed");
        return;
    }
    if (!stage_probe::Fill(dev, &texture, 0xffff0000))
        goto done;
    ok =
        HR(STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1),
           "FAIL fog FVF") &&
        HR(STAGE_SET_TEXTURE(dev, 0, texture), "FAIL fog texture") &&
        HR(STAGE_POINT_FILTER(dev, 0), "FAIL fog filter") &&
        HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXCOORDINDEX, 0), "FAIL fog coordinates") &&
        HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE),
           "FAIL fog color op") &&
        HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
           "FAIL fog color arg1") &&
        HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE),
           "FAIL fog color arg2") &&
        HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
           "FAIL fog alpha op") &&
        HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE),
           "FAIL fog alpha arg") &&
        HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE),
           "FAIL fog stage 1") &&
        HR(DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, FALSE), "FAIL fog blend") &&
        HR(DEVICE(SetRenderState, dev, D3DRS_SPECULARENABLE, FALSE), "FAIL fog specular") &&
        HR(DEVICE(SetRenderState, dev, D3DRS_FOGCOLOR, 0xff0000ff), "FAIL fog color") &&
        HR(DEVICE(SetRenderState, dev, D3DRS_FOGTABLEMODE, D3DFOG_NONE), "FAIL fog table") &&
        HR(DEVICE(SetRenderState, dev, D3DRS_FOGVERTEXMODE, D3DFOG_NONE), "FAIL fog vertex") &&
        HR(DEVICE(SetRenderState, dev, D3DRS_FOGENABLE, TRUE), "FAIL fog enable") &&
        HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1, 0), "FAIL fog clear") &&
        HR(DEVICE(BeginScene, dev), "FAIL fog begin scene") &&
        HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
           "FAIL fog draw") &&
        HR(DEVICE(EndScene, dev), "FAIL fog end scene");
    DEVICE(SetRenderState, dev, D3DRS_FOGENABLE, FALSE);
    if (!ok || !HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY), "FAIL fog readback"))
        goto done;
    for (const auto &sample : samples) {
        DWORD actual = *(DWORD *)((BYTE *)locked.pBits + 120 * locked.Pitch + sample.x * 4);
        if (!Near(actual & 0xffffff, sample.expected, 6)) {
            Number("FAIL fog pixel x", sample.x);
            Number("fog pixel actual", actual & 0xffffff);
            Number("fog pixel expected", sample.expected);
            failed = TRUE;
            break;
        }
    }
    SURFACE(UnlockRect, target);
    if (!failed)
        Log("PASS pretransformed vertex fog from specular alpha after texturing");
done:
    STAGE_SET_TEXTURE(dev, 0, NULL);
    if (texture)
        STAGE_TEXTURE(Release, texture);
}

} // namespace fog_probe
