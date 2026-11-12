/* SPDX-License-Identifier: GPL-2.0-or-later
 * Table fog of a vertex shader that writes oFog, through the public D3D8/9
 * API. Included by probe.cpp after vshader-probe.inc, whose quad, rows,
 * declarations and binding helpers it reuses. vs_1_1 transforms the quad
 * (z 0.5, w 1), passes the green diffuse colour to oD0 and writes c4.x to
 * oFog.x; the fog colour is magenta. With FOGTABLEMODE LINEAR, FOGSTART 0 and
 * FOGEND 1, the GeForce FX and ATI drivers of 2003-2004 ran the oFog value
 * through the table equation, f = (end - oFog) / (end - start) (Wine patch
 * 0062): oFog 0 is the diffuse colour, oFog 1 the fog colour, oFog 0.25 three
 * quarters diffuse, and with FOGEND 4 oFog 2 is half fogged (an unclamped
 * coordinate). Depth fog (z 0.5 or w 1) would give half fog or full fog in
 * every case. With FOGTABLEMODE NONE oFog is the vertex fog factor instead:
 * oFog 1 is the diffuse colour and oFog 0 the fog colour. The table cases run
 * with fixed-function fragments and with a ps_1_1 that moves v0 to r0.
 */
namespace table_fog_probe {

using vshader_probe::Dst;
using vshader_probe::Instruction;
using vshader_probe::RegisterConst;
using vshader_probe::RegisterRasterOut;
using vshader_probe::Src;

/* vs_1_1: dp4 oPos.x..w, v0, c0..c3; mov oD0, v1; mov oFog.x, c4.x. */
static constexpr DWORD vs11_fog[] = {
    0xfffe0101, /* vs_1_1 */
#if DG_D3D_VERSION == 9
    0x0000001f, /* dcl */
    0x80000000, /*   D3DDECLUSAGE_POSITION (0), index 0 */
    0x900f0000, /*   v0 */
    0x0000001f, /* dcl */
    0x8000000a, /*   D3DDECLUSAGE_COLOR (10), index 0 */
    0x900f0001, /*   v1 */
#endif
    0x00000009, /* dp4 */
    0xc0010000, /*   oPos.x */
    0x90e40000, /*   v0 */
    0xa0e40000, /*   c0 */
    0x00000009, /* dp4 */
    0xc0020000, /*   oPos.y */
    0x90e40000, /*   v0 */
    0xa0e40001, /*   c1 */
    0x00000009, /* dp4 */
    0xc0040000, /*   oPos.z */
    0x90e40000, /*   v0 */
    0xa0e40002, /*   c2 */
    0x00000009, /* dp4 */
    0xc0080000, /*   oPos.w */
    0x90e40000, /*   v0 */
    0xa0e40003, /*   c3 */
    0x00000001, /* mov */
    0xd00f0000, /*   oD0 */
    0x90e40001, /*   v1 */
    0x00000001, /* mov */
    0xc0010001, /*   oFog.x: rasterizer output 1, mask x */
    0xa0000004, /*   c4.x: swizzle xxxx */
    0x0000ffff, /* end */
};
static constexpr DWORD fog_body = DG_D3D_VERSION == 9 ? 7 : 1;
static_assert(vs11_fog[0] == D3DVS_VERSION(1, 1) &&
              vs11_fog[sizeof(vs11_fog) / 4 - 1] == D3DVS_END());
static_assert(vs11_fog[fog_body + 19] == Instruction(D3DSIO_MOV, 0) &&
              vs11_fog[fog_body + 20] == Dst(RegisterRasterOut, 1, 1) &&
              vs11_fog[fog_body + 21] == (Src(RegisterConst, 4) & ~(0xffu << 16)) &&
              vs11_fog[fog_body + 22] == D3DVS_END());

/* ps_1_1: mov r0, v0. */
static constexpr DWORD ps11_diffuse[] = {0xffff0101, 0x00000001, 0x800f0000, 0x90e40000,
                                         0x0000ffff};

static const DWORD diffuse = 0xff00ff00, fog_color = 0xffff00ff;

struct Case {
    const char *name;
    DWORD table;
    float end, fog;
    float diffuse_share; /* f: the share of the diffuse colour */
};

static const Case cases[] = {
    {"table LINEAR 0..1, oFog 0: diffuse", D3DFOG_LINEAR, 1, 0, 1},
    {"table LINEAR 0..1, oFog 1: fog colour", D3DFOG_LINEAR, 1, 1, 0},
    {"table LINEAR 0..1, oFog 0.25: three quarters diffuse", D3DFOG_LINEAR, 1, .25f, .75f},
    {"table LINEAR 0..4, oFog 2 (unclamped): half", D3DFOG_LINEAR, 4, 2, .5f},
    {"vertex fog, oFog 1: diffuse", D3DFOG_NONE, 1, 1, 1},
    {"vertex fog, oFog 0: fog colour", D3DFOG_NONE, 1, 0, 0},
};

static BOOL Near(DWORD actual, DWORD expected, int tolerance) {
    for (UINT shift = 0; shift < 24; shift += 8) {
        int d = (int)((actual >> shift) & 255) - (int)((expected >> shift) & 255);
        if (d < -tolerance || d > tolerance)
            return FALSE;
    }
    return TRUE;
}

static DWORD Expected(float share) {
    const DWORD green = (DWORD)(share * 255 + .5f), other = (DWORD)((1 - share) * 255 + .5f);
    return other << 16 | green << 8 | other;
}

static BOOL Draw(Device *dev, Surface *target, const Case &check, const char *fragments) {
    const vshader_probe::Vertex quad[4] = {{0, 0, .5f, diffuse, 0, 0},
                                           {1, 0, .5f, diffuse, 1, 0},
                                           {0, 1, .5f, diffuse, 0, 1},
                                           {1, 1, .5f, diffuse, 1, 1}};
    const float c4[4] = {check.fog, 0, 0, 0};
    const DWORD expected = Expected(check.diffuse_share);
    D3DLOCKED_RECT locked;
    BOOL passed = TRUE;
    if (!HR(VSHADER_CONSTANT(dev, 0, vshader_probe::whole, 4), "FAIL table fog rows") ||
        !HR(VSHADER_CONSTANT(dev, 4, c4, 1), "FAIL table fog c4") ||
        !HR(DEVICE(SetRenderState, dev, D3DRS_FOGTABLEMODE, check.table), "FAIL table fog mode") ||
        !HR(DEVICE(SetRenderState, dev, D3DRS_FOGSTART, shader_probe::Bits(0)),
            "FAIL table fog start") ||
        !HR(DEVICE(SetRenderState, dev, D3DRS_FOGEND, shader_probe::Bits(check.end)),
            "FAIL table fog end") ||
        !HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1, 0), "FAIL table fog clear") ||
        !HR(DEVICE(BeginScene, dev), "FAIL table fog begin scene") ||
        !HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
            "FAIL table fog draw") ||
        !HR(DEVICE(EndScene, dev), "FAIL table fog end scene") ||
        !HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY), "FAIL table fog readback"))
        return FALSE;
    for (UINT y = 20; y < 240 && passed; y += 100)
        for (UINT x : vshader_probe::columns) {
            DWORD actual = *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4) & 0x00ffffff;
            if (!Near(actual, expected, 3)) {
                Log(fragments);
                Log(check.name);
                Number("FAIL table fog pixel actual", actual);
                Number("table fog pixel expected", expected);
                Number("table fog pixel x", x);
                Number("table fog pixel y", y);
                passed = FALSE;
                failed = TRUE;
                break;
            }
        }
    SURFACE(UnlockRect, target);
    return passed;
}

static void Run(Device *dev, Surface *target) {
    StageCaps caps = {};
    VertexShader shader = VSHADER_NONE;
    PixelShader pixel = SHADER_NONE;
    void *layout = NULL;
    BOOL ok;
    Log("STAGE table fog from a vertex shader's oFog");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL table fog device caps"))
        return;
    if ((caps.VertexShaderVersion & 0xffff) < (D3DVS_VERSION(1, 1) & 0xffff)) {
        Log("SKIP table fog: VertexShaderVersion below 1.1");
        return;
    }
    if (!(caps.RasterCaps & D3DPRASTERCAPS_FOGTABLE)) {
        Log("SKIP table fog: FOGTABLE not claimed");
        return;
    }
    ok = vshader_probe::Diffuse(dev) &&
         HR(DEVICE(SetRenderState, dev, D3DRS_FOGCOLOR, fog_color), "FAIL table fog color") &&
         HR(DEVICE(SetRenderState, dev, D3DRS_FOGVERTEXMODE, D3DFOG_NONE),
            "FAIL table fog vertex mode") &&
         HR(DEVICE(SetRenderState, dev, D3DRS_FOGENABLE, TRUE), "FAIL table fog enable") &&
         vshader_probe::Bind(dev, vs11_fog, &shader, &layout);
    for (const Case &check : cases)
        ok = ok && Draw(dev, target, check, "fixed-function fragments");
    if (ok)
        Log("PASS table fog: vs_1_1 oFog through LINEAR table fog unclamped (0 clear, 1 fogged, "
            "0.25, 2 of 4) and as the vertex fog factor with fixed-function fragments");
    if (ok && (caps.PixelShaderVersion & 0xffff) >= (D3DPS_VERSION(1, 1) & 0xffff)) {
        ok = HR(SHADER_CREATE(dev, ps11_diffuse, &pixel), "FAIL table fog ps creation") &&
             HR(SHADER_SET(dev, pixel), "FAIL table fog ps bind");
        for (const Case &check : cases)
            if (check.table != D3DFOG_NONE)
                ok = ok && Draw(dev, target, check, "ps_1_1 fragments");
        if (ok)
            Log("PASS table fog: vs_1_1 oFog through LINEAR table fog after ps_1_1");
        SHADER_SET(dev, SHADER_NONE);
        if (pixel != SHADER_NONE)
            SHADER_DELETE(dev, pixel);
    } else if (ok) {
        Log("SKIP table fog after a pixel shader: PixelShaderVersion below 1.1");
    }
    DEVICE(SetRenderState, dev, D3DRS_FOGENABLE, FALSE);
    DEVICE(SetRenderState, dev, D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    vshader_probe::Unbind(dev, shader, layout);
}

} // namespace table_fog_probe
