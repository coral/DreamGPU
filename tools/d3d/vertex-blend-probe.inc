/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed-function vertex blending (D3DRS_VERTEXBLEND, D3DTS_WORLD..WORLD3)
 * through the public D3D8/9 API. Included by probe.cpp after
 * depth-texture-probe.inc; reuses the state, areas, buffers and indexed draws
 * of vbuffer-probe.inc. A GeForce FX 5800 blends four world matrices and has
 * no indexed blending (MaxVertexBlendMatrices 4, MaxVertexBlendMatrixIndex
 * 0); WineD3D without GL_ARB_vertex_blend claimed one matrix and drew blended
 * meshes unblended. With Wine 0052 it blends on the CPU. The caps are logged
 * (VertexProcessingCaps too); fewer than two matrices skips the stage, any
 * other answer than the FX's fails. View and projection are identity, so
 * positions are clip coordinates. Each blended vertex lands at sum w_i
 * (v WORLDi), the last weight being 1 minus the others; the world matrices
 * are translations (WORLD none, WORLD1 64 pixels right, WORLD2 48 down,
 * WORLD3 both), so each 32x32 quad of known weights lands on a known
 * rectangle, and its bind pose position, where an unblended draw would put
 * it, stays clear. Whole frames are compared as in vbuffer-probe.inc. Cases:
 * - two matrices (D3DFVF_XYZB1, 1WEIGHTS, DrawPrimitiveUP): weights 1, 0 and
 *   1/2 move a quad by 0, 64 and 32 pixels; a quad whose left vertices weigh
 *   WORLD fully and right vertices WORLD1 fully stretches to 96 pixels.
 * - three matrices (D3DFVF_XYZB2, 2WEIGHTS, DrawPrimitiveUP): WORLD, WORLD1,
 *   the implicit WORLD2 and the mixture (1/2, 1/4, 1/4).
 * - four matrices (D3DFVF_XYZB3, 3WEIGHTS): a managed vertex buffer and a
 *   16-bit index buffer drawn by DrawIndexedPrimitive with base vertex 4 and
 *   start index 6 (a decoy quad below the base vertex and decoy indices
 *   below the start, which name a fifth quad, must not appear): the
 *   implicit WORLD3, WORLD2, (1/4, 1/4, 1/4, 1/4) and WORLD.
 * - lit (D3DFVF_XYZB1 | D3DFVF_NORMAL, one directional light travelling +z,
 *   white material): WORLD is identity and WORLD1 keeps every point of the
 *   z = 0.5 plane in place but turns the normal (1, 0, 0) into (1, 0, -1)
 *   through its inverse transpose. Quads of weights 1, 0 and 1/2 are lit
 *   0, 1 and 1/2 without NORMALIZENORMALS and 0, 0.707 and 0.447 with it
 *   (within 3 of 255 times that, grey); a normal transformed by the matrix
 *   itself, or not at all, stays black.
 */
#if DG_D3D_VERSION == 8
typedef D3DLIGHT8 BlendLight;
typedef D3DMATERIAL8 BlendMaterial;
#else
typedef D3DLIGHT9 BlendLight;
typedef D3DMATERIAL9 BlendMaterial;
#endif

namespace vertex_blend_probe {

using vbuffer_probe::Area;
using vbuffer_probe::Background;
using vbuffer_probe::Decoy;
using vbuffer_probe::ScreenHeight;
using vbuffer_probe::ScreenWidth;
using vbuffer_probe::Unchecked;

/* World matrix offsets in pixels: WORLD, WORLD1, WORLD2, WORLD3. */
static constexpr int OffsetX[4] = {0, 64, 0, 64}, OffsetY[4] = {0, 0, 48, 48};
static constexpr int Size = 32;

/* A quad's bind pose (top-left pixel), weights, colour and where it must
 * land. stretch: the left vertices weigh WORLD fully, the right ones the
 * last matrix. */
struct Quad {
    int left, top;
    float weights[3];
    BOOL stretch;
    Area expected;
};

static void Matrix(D3DMATRIX *m, float x, float y) {
    *m = D3DMATRIX{};
    m->_11 = m->_22 = m->_33 = m->_44 = 1;
    m->_41 = x;
    m->_42 = y;
}

/* WORLD..WORLD3 as translations by the pixel offsets, in clip units. */
static BOOL Worlds(Device *dev) {
    for (int i = 0; i < 4; ++i) {
        D3DMATRIX m;
        Matrix(&m, OffsetX[i] / 160.f, -OffsetY[i] / 120.f);
        if (!HR(DEVICE(SetTransform, dev, D3DTS_WORLDMATRIX(i), &m), "FAIL vertex blend world"))
            return FALSE;
    }
    return TRUE;
}

/* Strip-ordered corners of a quad as floats: x, y, z, weights, then the
 * colour's bits. count weights per vertex. */
static UINT Corners(float *out, const Quad &q, UINT count) {
    const float left = q.left / 160.f - 1, right = (q.left + Size) / 160.f - 1;
    const float top = 1 - q.top / 120.f, bottom = 1 - (q.top + Size) / 120.f;
    const float xs[4] = {left, right, left, right}, ys[4] = {top, top, bottom, bottom};
    const UINT stride = 3 + count + 1;
    for (UINT v = 0; v < 4; ++v) {
        float *vertex = out + v * stride;
        vertex[0] = xs[v];
        vertex[1] = ys[v];
        vertex[2] = .5f;
        for (UINT i = 0; i < count; ++i)
            vertex[3 + i] = q.stretch ? ((v & 1) ? 0.f : (i ? 0.f : 1.f)) : q.weights[i];
        vertex[3 + count] = __builtin_bit_cast(float, q.expected.color);
    }
    return 4 * stride;
}

/* Compare the whole backbuffer with the areas; tolerance per channel. */
static BOOL Verify(Surface *target, const char *name, const Area *areas, UINT count,
                   int tolerance) {
    D3DLOCKED_RECT locked;
    UINT wrong = 0;
    int first_x = 0, first_y = 0;
    DWORD first_actual = 0, first_expected = 0;
    if (!HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY),
            "FAIL vertex blend readback")) {
        Log(name);
        return FALSE;
    }
    if (!locked.pBits || locked.Pitch < ScreenWidth * 4) {
        SURFACE(UnlockRect, target);
        Log(name);
        return Check(FALSE, "FAIL vertex blend readback layout", (DWORD)locked.Pitch);
    }
    const auto pixel = [&](int x, int y) -> DWORD {
        return *(const DWORD *)((const BYTE *)locked.pBits + y * locked.Pitch + x * 4) & 0xffffff;
    };
    for (int y = 0; y < ScreenHeight; ++y)
        for (int x = 0; x < ScreenWidth; ++x) {
            const DWORD expected = vbuffer_probe::Expected(areas, count, x, y);
            if (expected == Unchecked)
                continue;
            const DWORD actual = pixel(x, y);
            BOOL same = TRUE;
            for (int shift = 0; shift < 24; shift += 8) {
                const int a = (int)((actual >> shift) & 0xff),
                          e = (int)((expected >> shift) & 0xff);
                same &= a - e <= tolerance && e - a <= tolerance;
            }
            if (!same && !wrong++) {
                first_x = x;
                first_y = y;
                first_actual = actual;
                first_expected = expected;
            }
        }
    if (wrong) {
        Log("FAIL vertex blend pixels");
        Log(name);
        Number("vertex blend pixel actual", first_actual);
        Number("vertex blend pixel expected", first_expected);
        Number("vertex blend pixel x", (DWORD)first_x);
        Number("vertex blend pixel y", (DWORD)first_y);
        Number("vertex blend wrong pixels", wrong);
        for (UINT i = 0; i < count; ++i) {
            Number("vertex blend quad centre x", (DWORD)((areas[i].left + areas[i].right) / 2));
            Number("vertex blend quad centre y", (DWORD)((areas[i].top + areas[i].bottom) / 2));
            Number("vertex blend quad expected", areas[i].color & 0xffffff);
            Number("vertex blend quad actual", pixel((areas[i].left + areas[i].right) / 2,
                                                     (areas[i].top + areas[i].bottom) / 2));
        }
        failed = TRUE;
    }
    SURFACE(UnlockRect, target);
    return !wrong;
}

/* Clear, draw inside a scene, compare. */
template <typename Draw>
static BOOL Frame(Device *dev, Surface *target, const char *name, const Area *areas, UINT count,
                  int tolerance, Draw draw) {
    if (!HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, Background, 1, 0),
            "FAIL vertex blend clear") ||
        !HR(DEVICE(BeginScene, dev), "FAIL vertex blend begin scene")) {
        Log(name);
        return FALSE;
    }
    const BOOL drawn = draw();
    if (!HR(DEVICE(EndScene, dev), "FAIL vertex blend end scene") || !drawn) {
        Log(name);
        return FALSE;
    }
    return Verify(target, name, areas, count, tolerance);
}

static DWORD BlendFvf(UINT count) {
    return count == 1 ? D3DFVF_XYZB1 : count == 2 ? D3DFVF_XYZB2 : D3DFVF_XYZB3;
}

/* Quads of count weights, each drawn by DrawPrimitiveUP. */
static BOOL UserCase(Device *dev, Surface *target, UINT count, const Quad *quads, UINT n,
                     const char *name) {
    const DWORD fvf = BlendFvf(count) | D3DFVF_DIFFUSE;
    const UINT stride = (3 + count + 1) * sizeof(float);
    Area areas[8];
    for (UINT i = 0; i < n; ++i)
        areas[i] = quads[i].expected;
    return vbuffer_probe::Fixed(dev, fvf) && Worlds(dev) &&
           HR(DEVICE(SetRenderState, dev, D3DRS_VERTEXBLEND, (DWORD)count),
              "FAIL vertex blend state") &&
           Frame(dev, target, name, areas, n, 0, [&]() -> BOOL {
               for (UINT i = 0; i < n; ++i) {
                   float vertices[4 * 8];
                   Corners(vertices, quads[i], count);
                   if (!HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, vertices, stride),
                           "FAIL vertex blend draw"))
                       return FALSE;
               }
               return TRUE;
           });
}

static constexpr DWORD Red = 0xffd04010, Cyan = 0xff10a0f0, Green = 0xff60e030, Yellow = 0xfff0d020,
                       Violet = 0xff9040e0;

static void TwoMatrices(Device *dev, Surface *target) {
    static const Quad quads[4] = {
        {16, 16, {1, 0, 0}, FALSE, {16, 16, 48, 48, Red}},
        {16, 120, {0, 0, 0}, FALSE, {80, 120, 112, 152, Cyan}},
        {160, 16, {.5f, 0, 0}, FALSE, {192, 16, 224, 48, Green}},
        {160, 120, {0, 0, 0}, TRUE, {160, 120, 256, 152, Yellow}},
    };
    Log("STAGE vertex blend two matrices: D3DFVF_XYZB1, 1WEIGHTS");
    if (UserCase(dev, target, 1, quads, 4, "two matrices: weights 1, 0, 1/2 and a stretched quad"))
        Log("PASS vertex blend two matrices: WORLD, WORLD1, half of each and a quad stretched "
            "between them exact");
}

static void ThreeMatrices(Device *dev, Surface *target) {
    static const Quad quads[4] = {
        {16, 16, {1, 0, 0}, FALSE, {16, 16, 48, 48, Red}},
        {16, 64, {0, 1, 0}, FALSE, {80, 64, 112, 96, Cyan}},
        {160, 16, {0, 0, 0}, FALSE, {160, 64, 192, 96, Green}},
        {240, 140, {.5f, .25f, 0}, FALSE, {256, 152, 288, 184, Yellow}},
    };
    Log("STAGE vertex blend three matrices: D3DFVF_XYZB2, 2WEIGHTS");
    if (UserCase(dev, target, 2, quads, 4,
                 "three matrices: WORLD, WORLD1, implicit WORLD2, (1/2, 1/4, 1/4)"))
        Log("PASS vertex blend three matrices: WORLD, WORLD1, the implicit WORLD2 and a mixture "
            "exact");
}

/* Four matrices from vertex and index buffers: base vertex 4, start index 6. */
static void FourMatrices(Device *dev, Surface *target) {
    static const Quad decoy = {120, 196, {1, 0, 0}, FALSE, {120, 196, 152, 228, Decoy}};
    static const Quad quads[5] = {
        {16, 16, {0, 0, 0}, FALSE, {80, 64, 112, 96, Red}},
        {160, 16, {0, 0, 1}, FALSE, {160, 64, 192, 96, Cyan}},
        {16, 140, {.25f, .25f, .25f}, FALSE, {48, 164, 80, 196, Green}},
        {240, 16, {1, 0, 0}, FALSE, {240, 16, 272, 48, Yellow}},
        /* WORLD1; named only by the decoy indices. */
        {200, 150, {0, 1, 0}, FALSE, {264, 150, 296, 182, Violet}},
    };
    /* Relative to base vertex 4: six decoy indices (quad 4, which is never
     * drawn), then quads 0-3. Ignoring the start index draws quad 4 instead
     * of quad 3, ignoring the base vertex the decoy and shifted quads. */
    static const WORD indices[30] = {
        16, 17, 18, 18, 17, 19, 0,  1, 2,  2,  1,  3,  4,  5,  6,
        6,  5,  7,  8,  9,  10, 10, 9, 11, 12, 13, 14, 14, 13, 15,
    };
    const DWORD fvf = D3DFVF_XYZB3 | D3DFVF_DIFFUSE;
    const UINT stride = 7 * sizeof(float);
    float vertices[24 * 7];
    Area areas[4];
    VertexBuffer *vb = NULL;
    IndexBuffer *ib = NULL;
    BufferPointer pointer = NULL;
    Log("STAGE vertex blend four matrices: D3DFVF_XYZB3, 3WEIGHTS, indexed buffers");
    Corners(vertices, decoy, 3);
    for (UINT i = 0; i < 5; ++i)
        Corners(vertices + (4 + 4 * i) * 7, quads[i], 3);
    for (UINT i = 0; i < 4; ++i)
        areas[i] = quads[i].expected;
    BOOL ok =
        HR(VBUFFER_CREATE(dev, sizeof(vertices), D3DUSAGE_WRITEONLY, fvf, D3DPOOL_MANAGED, &vb),
           "FAIL vertex blend vertex buffer creation") &&
        HR(IBUFFER_CREATE(dev, sizeof(indices), D3DUSAGE_WRITEONLY, D3DPOOL_MANAGED, &ib),
           "FAIL vertex blend index buffer creation") &&
        vbuffer_probe::Write(vb, 0, 0, vertices, sizeof(vertices), 0,
                             "FAIL vertex blend vertex buffer lock") &&
        HR(IBUFFER(Lock, ib, 0, 0, &pointer, 0), "FAIL vertex blend index buffer lock");
    if (ok) {
        const BOOL mapped = Check(pointer != NULL, "FAIL vertex blend index buffer pointer", 0);
        if (mapped)
            vbuffer_probe::Copy(pointer, indices, sizeof(indices));
        ok = HR(IBUFFER(Unlock, ib), "FAIL vertex blend index buffer unlock") && mapped;
    }
    ok = ok && vbuffer_probe::Fixed(dev, fvf) && Worlds(dev) &&
         HR(DEVICE(SetRenderState, dev, D3DRS_VERTEXBLEND, D3DVBF_3WEIGHTS),
            "FAIL vertex blend state") &&
         HR(VBUFFER_STREAM(dev, vb, stride), "FAIL vertex blend stream source") &&
         Frame(dev, target, "four matrices: base vertex 4, start index 6", areas, 4, 0,
               [&]() -> BOOL { return vbuffer_probe::DrawIndexed(dev, ib, 4, 0, 16, 6, 8); });
    if (ok)
        Log("PASS vertex blend four matrices: the implicit WORLD3, WORLD2, a mixture of all four "
            "and WORLD from indexed buffers exact, decoys unseen");
    vbuffer_probe::Unbind(dev);
    if (ib)
        IBUFFER(Release, ib);
    if (vb)
        VBUFFER(Release, vb);
}

struct Lit {
    float x, y, z, weight, nx, ny, nz;
};
static_assert(sizeof(Lit) == 28);

/* Lit quads: WORLD identity, WORLD1 fixes the z = 0.5 plane and turns the
 * normal (1, 0, 0) into (1, 0, -1). */
static void Lighting(Device *dev, Surface *target) {
    static const float weights[3] = {1, 0, .5f};
    static const int lefts[3] = {16, 112, 208};
    /* 255 times N.L for the blended normal (1, 0, -(1 - w)). */
    static const DWORD plain[3] = {0, 255, 128}, normalized[3] = {0, 180, 114};
    BlendLight light = {};
    BlendMaterial material = {};
    D3DMATRIX world1;
    Lit vertices[3][4];
    Area areas[3];
    Log("STAGE vertex blend lit: D3DFVF_XYZB1 | D3DFVF_NORMAL, normals through each inverse "
        "transpose");
    Matrix(&world1, -.5f, 0);
    world1._31 = 1;
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = light.Diffuse.a = 1;
    light.Direction.z = 1;
    material.Diffuse.r = material.Diffuse.g = material.Diffuse.b = material.Diffuse.a = 1;
    for (int q = 0; q < 3; ++q) {
        const float left = lefts[q] / 160.f - 1, right = (lefts[q] + 64) / 160.f - 1;
        const float top = 1 - 64 / 120.f, bottom = 1 - 128 / 120.f;
        const float xs[4] = {left, right, left, right}, ys[4] = {top, top, bottom, bottom};
        for (int v = 0; v < 4; ++v)
            vertices[q][v] = {xs[v], ys[v], .5f, weights[q], 1, 0, 0};
    }
    D3DMATRIX identity;
    Matrix(&identity, 0, 0);
    BOOL ok = vbuffer_probe::Fixed(dev, D3DFVF_XYZB1 | D3DFVF_NORMAL) &&
              HR(DEVICE(SetTransform, dev, D3DTS_WORLDMATRIX(0), &identity),
                 "FAIL vertex blend lit world") &&
              HR(DEVICE(SetTransform, dev, D3DTS_WORLDMATRIX(1), &world1),
                 "FAIL vertex blend lit world1") &&
              HR(DEVICE(SetLight, dev, 0, &light), "FAIL vertex blend light") &&
              HR(DEVICE(LightEnable, dev, 0, TRUE), "FAIL vertex blend light enable") &&
              HR(DEVICE(SetMaterial, dev, &material), "FAIL vertex blend material") &&
              HR(DEVICE(SetRenderState, dev, D3DRS_AMBIENT, 0), "FAIL vertex blend ambient") &&
              HR(DEVICE(SetRenderState, dev, D3DRS_LIGHTING, TRUE), "FAIL vertex blend lighting") &&
              HR(DEVICE(SetRenderState, dev, D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS),
                 "FAIL vertex blend state");
    for (int normalize = 0; ok && normalize < 2; ++normalize) {
        for (int q = 0; q < 3; ++q)
            areas[q] = {lefts[q], 64, lefts[q] + 64, 128,
                        (normalize ? normalized[q] : plain[q]) * 0x010101u};
        ok = HR(DEVICE(SetRenderState, dev, D3DRS_NORMALIZENORMALS, (DWORD)normalize),
                "FAIL vertex blend normalize") &&
             Frame(dev, target,
                   normalize ? "lit, NORMALIZENORMALS: grey 0, 180, 114" : "lit: grey 0, 255, 128",
                   areas, 3, 3, [&]() -> BOOL {
                       for (int q = 0; q < 3; ++q)
                           if (!HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, vertices[q],
                                          sizeof(Lit)),
                                   "FAIL vertex blend lit draw"))
                               return FALSE;
                       return TRUE;
                   });
    }
    if (ok)
        Log("PASS vertex blend lit: blended normals through each world matrix's inverse "
            "transpose, with and without NORMALIZENORMALS");
    DEVICE(SetRenderState, dev, D3DRS_NORMALIZENORMALS, FALSE);
    DEVICE(SetRenderState, dev, D3DRS_LIGHTING, FALSE);
    DEVICE(LightEnable, dev, 0, FALSE);
}

static void Run(Device *dev, Surface *backbuffer) {
    StageCaps caps = {};
    Log("STAGE vertex blending (WORLD..WORLD3)");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL vertex blend device caps"))
        return;
    Number("MaxVertexBlendMatrices", caps.MaxVertexBlendMatrices);
    Number("MaxVertexBlendMatrixIndex", caps.MaxVertexBlendMatrixIndex);
    Number("VertexProcessingCaps", caps.VertexProcessingCaps);
    if (caps.MaxVertexBlendMatrices < 2) {
        Log("SKIP vertex blending: one blend matrix claimed (no GL_ARB_vertex_blend, no Wine "
            "0052)");
        return;
    }
    if (!Check(caps.MaxVertexBlendMatrices == 4 && caps.MaxVertexBlendMatrixIndex == 0,
               "FAIL vertex blend caps: a GeForce FX claims 4 matrices and no indexed blending",
               caps.MaxVertexBlendMatrices << 16 | caps.MaxVertexBlendMatrixIndex))
        return;
    Log("PASS vertex blend caps: MaxVertexBlendMatrices 4, MaxVertexBlendMatrixIndex 0");
    TwoMatrices(dev, backbuffer);
    if (!failed)
        ThreeMatrices(dev, backbuffer);
    if (!failed)
        FourMatrices(dev, backbuffer);
    if (!failed)
        Lighting(dev, backbuffer);
    /* Back to the state the other stages expect. */
    D3DMATRIX identity;
    Matrix(&identity, 0, 0);
    for (int i = 0; i < 4; ++i)
        DEVICE(SetTransform, dev, D3DTS_WORLDMATRIX(i), &identity);
    DEVICE(SetRenderState, dev, D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    DEVICE(SetRenderState, dev, D3DRS_LIGHTING, FALSE);
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

} // namespace vertex_blend_probe
