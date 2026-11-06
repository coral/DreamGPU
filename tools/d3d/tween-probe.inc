/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed-function vertex tweening (D3DRS_VERTEXBLEND D3DVBF_TWEENING,
 * D3DRS_TWEENFACTOR) through the public D3D8/9 API. Included by probe.cpp
 * after vertex-blend-probe.inc; reuses the state, areas, buffers and indexed
 * draws of vbuffer-probe.inc. Direct3D defines POSITION = POSITION1 * (1 -
 * TWEENFACTOR) + POSITION2 * TWEENFACTOR, normals likewise, before lighting;
 * the second position and normal are D3D9 declaration elements of usage
 * index 1 and D3D8 shader declaration registers D3DVSDE_POSITION2 and
 * NORMAL2. A GeForce FX claims D3DVTXPCAPS_TWEENING (D3D9
 * VertexProcessingCaps 0x17b); WineD3D did not, and drew the first position
 * (Wine 0053 tweens on the CPU). The caps are logged; without TWEENING the
 * stage skips. Identity transforms put positions in clip space, so each 32x32
 * quad of known key frames lands on a known rectangle and whole frames are
 * compared as in vbuffer-probe.inc. Cases:
 * - positions (unlit, tween factors 0, 1/2 and 1): the second key frame is
 *   64 pixels right and 48 down. One quad by DrawPrimitiveUP (the second
 *   position interleaved), one from a managed vertex buffer by DrawPrimitive
 *   from vertex 4 (a decoy quad below it), one from two streams (key frame 2
 *   in stream 1) by DrawIndexedPrimitive with base vertex 4 and start index
 *   6 (decoy vertices and indices below them), and one whose right edge alone
 *   moves, which stretches it from 32 to 96 pixels. Tweening disabled draws
 *   the first key frame whatever the factor.
 * - lit normals (one directional light travelling +z, white material, the
 *   second normal alone in the declaration, so positions stay put): normals
 *   (1, 0, 0) and (0, 0, -1) at factors 0, 1/2 and 1 in one frame are lit 0,
 *   1/2 and 1 without NORMALIZENORMALS and 0, 0.707 and 1 with it (within 3
 *   of 255 times that, grey); an untweened normal stays black.
 */
#if DG_D3D_VERSION == 8
typedef DWORD TweenLayout;
typedef DWORD TweenElement;
#define TWEEN_LAYOUT_NONE 0
#else
typedef IDirect3DVertexDeclaration9 *TweenLayout;
typedef D3DVERTEXELEMENT9 TweenElement;
#define TWEEN_LAYOUT_NONE NULL
#endif

namespace tween_probe {

using vbuffer_probe::Area;
using vbuffer_probe::Background;
using vbuffer_probe::Decoy;
using vbuffer_probe::ScreenHeight;
using vbuffer_probe::ScreenWidth;
using vbuffer_probe::Unchecked;

static constexpr int Size = 32, MoveX = 64, MoveY = 48;
static constexpr float Factors[3] = {0, .5f, 1};

/* Interleaved: both key frames and the colour. */
struct Vertex {
    float x, y, z;
    DWORD color;
    float x2, y2, z2;
};
/* Two streams: the first key frame with the colour, the second alone. */
struct First {
    float x, y, z;
    DWORD color;
};
struct Second {
    float x, y, z;
};
struct Lit {
    float x, y, z, nx, ny, nz, nx2, ny2, nz2;
};
static_assert(sizeof(Vertex) == 28 && sizeof(First) == 16 && sizeof(Second) == 12 &&
              sizeof(Lit) == 36);

static float ClipX(int x) {
    return x / 160.f - 1;
}
static float ClipY(int y) {
    return 1 - y / 120.f;
}

/* A quad's first key frame (top-left pixel); the second moves it by
 * MoveX, MoveY, or (stretch) moves only its right edge by MoveX. */
struct Quad {
    int left, top;
    BOOL stretch;
    DWORD color;
};
/* Where a quad lands at factor t (a multiple of 1/2: whole pixels). */
static Area Expected(const Quad &q, float t) {
    const int dx = (int)(MoveX * t), dy = q.stretch ? 0 : (int)(MoveY * t);
    return q.stretch
               ? Area{q.left, q.top, q.left + Size + dx, q.top + Size, q.color}
               : Area{q.left + dx, q.top + dy, q.left + dx + Size, q.top + dy + Size, q.color};
}
/* Strip-ordered corners of both key frames. */
static void Corners(Vertex *out, const Quad &q) {
    for (int v = 0; v < 4; ++v) {
        const int x = q.left + ((v & 1) ? Size : 0), y = q.top + ((v & 2) ? Size : 0);
        const int x2 = x + (q.stretch ? ((v & 1) ? MoveX : 0) : MoveX),
                  y2 = y + (q.stretch ? 0 : MoveY);
        out[v] = {ClipX(x), ClipY(y), .5f, q.color, ClipX(x2), ClipY(y2), .5f};
    }
}

/* Compare the whole backbuffer with the areas; tolerance per channel. */
static BOOL Verify(Surface *target, const char *name, const Area *areas, UINT count,
                   int tolerance) {
    D3DLOCKED_RECT locked;
    UINT wrong = 0;
    int first_x = 0, first_y = 0;
    DWORD first_actual = 0, first_expected = 0;
    if (!HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY),
            "FAIL vertex tween readback")) {
        Log(name);
        return FALSE;
    }
    if (!locked.pBits || locked.Pitch < ScreenWidth * 4) {
        SURFACE(UnlockRect, target);
        Log(name);
        return Check(FALSE, "FAIL vertex tween readback layout", (DWORD)locked.Pitch);
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
        Log("FAIL vertex tween pixels");
        Log(name);
        Number("vertex tween pixel actual", first_actual);
        Number("vertex tween pixel expected", first_expected);
        Number("vertex tween pixel x", (DWORD)first_x);
        Number("vertex tween pixel y", (DWORD)first_y);
        Number("vertex tween wrong pixels", wrong);
        for (UINT i = 0; i < count; ++i) {
            Number("vertex tween quad centre x", (DWORD)((areas[i].left + areas[i].right) / 2));
            Number("vertex tween quad centre y", (DWORD)((areas[i].top + areas[i].bottom) / 2));
            Number("vertex tween quad expected", areas[i].color & 0xffffff);
            Number("vertex tween quad actual", pixel((areas[i].left + areas[i].right) / 2,
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
            "FAIL vertex tween clear") ||
        !HR(DEVICE(BeginScene, dev), "FAIL vertex tween begin scene")) {
        Log(name);
        return FALSE;
    }
    const BOOL drawn = draw();
    if (!HR(DEVICE(EndScene, dev), "FAIL vertex tween end scene") || !drawn) {
        Log(name);
        return FALSE;
    }
    return Verify(target, name, areas, count, tolerance);
}

static BOOL Factor(Device *dev, float t) {
    return HR(DEVICE(SetRenderState, dev, D3DRS_TWEENFACTOR, __builtin_bit_cast(DWORD, t)),
              "FAIL vertex tween factor");
}

/* Fixed function with a declaration: on D3D8 a shader declaration without
 * a function, on D3D9 a vertex declaration (vbuffer_probe::Fixed unbinds
 * any vertex shader). */
static BOOL Bind(Device *dev, TweenLayout layout) {
#if DG_D3D_VERSION == 8
    return HR(DEVICE(SetVertexShader, dev, layout), "FAIL vertex tween declaration bind");
#else
    return HR(DEVICE(SetVertexDeclaration, dev, layout), "FAIL vertex tween declaration bind");
#endif
}
static BOOL Layout(Device *dev, const TweenElement *declaration, TweenLayout *out) {
#if DG_D3D_VERSION == 8
    return HR(DEVICE(CreateVertexShader, dev, declaration, NULL, out, 0),
              "FAIL vertex tween shader declaration") &&
           Bind(dev, *out);
#else
    return HR(DEVICE(CreateVertexDeclaration, dev, declaration, out),
              "FAIL vertex tween declaration") &&
           Bind(dev, *out);
#endif
}
static void Release(Device *dev, TweenLayout layout) {
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    if (!layout)
        return;
#if DG_D3D_VERSION == 8
    DEVICE(DeleteVertexShader, dev, layout);
#else
    IDirect3DVertexDeclaration9_Release(layout);
#endif
}

#if DG_D3D_VERSION == 8
/* D3D8 shader declarations: D3DVSDE_POSITION2 and NORMAL2. */
static const DWORD Interleaved[] = {D3DVSD_STREAM(0), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3),
                                    D3DVSD_REG(D3DVSDE_DIFFUSE, D3DVSDT_D3DCOLOR),
                                    D3DVSD_REG(D3DVSDE_POSITION2, D3DVSDT_FLOAT3), D3DVSD_END()};
static const DWORD Streams[] = {D3DVSD_STREAM(0),
                                D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3),
                                D3DVSD_REG(D3DVSDE_DIFFUSE, D3DVSDT_D3DCOLOR),
                                D3DVSD_STREAM(1),
                                D3DVSD_REG(D3DVSDE_POSITION2, D3DVSDT_FLOAT3),
                                D3DVSD_END()};
static const DWORD Normals[] = {D3DVSD_STREAM(0), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3),
                                D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_FLOAT3),
                                D3DVSD_REG(D3DVSDE_NORMAL2, D3DVSDT_FLOAT3), D3DVSD_END()};
#else
/* D3D9 vertex declarations: POSITION1 and NORMAL1. */
static const D3DVERTEXELEMENT9 Interleaved[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
    {0, 16, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
    D3DDECL_END()};
static const D3DVERTEXELEMENT9 Streams[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
    {1, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
    D3DDECL_END()};
static const D3DVERTEXELEMENT9 Normals[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
    {0, 24, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 1},
    D3DDECL_END()};
#endif

static BOOL Stream1(Device *dev, VertexBuffer *buffer, UINT stride) {
#if DG_D3D_VERSION == 8
    return HR(DEVICE(SetStreamSource, dev, 1, buffer, stride), "FAIL vertex tween stream 1");
#else
    return HR(DEVICE(SetStreamSource, dev, 1, buffer, 0, stride), "FAIL vertex tween stream 1");
#endif
}

static BOOL Fill(VertexBuffer **buffer, Device *dev, const void *data, UINT size,
                 const char *stage) {
    return HR(VBUFFER_CREATE(dev, size, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, buffer), stage) &&
           vbuffer_probe::Write(*buffer, 0, 0, data, size, 0, stage);
}

static constexpr DWORD Red = 0xffd04010, Cyan = 0xff10a0f0, Green = 0xff60e030, Yellow = 0xfff0d020;

/* Four quads, one frame per factor, and one with tweening disabled. */
static void Positions(Device *dev, Surface *target) {
    static const Quad user = {16, 16, FALSE, Red}, buffered = {160, 16, FALSE, Cyan},
                      streamed = {16, 140, FALSE, Green}, stretched = {160, 140, TRUE, Yellow};
    static const Quad decoy = {272, 100, FALSE, Decoy};
    /* Relative to base vertex 4: six decoy indices (vertices 8-11, drawn if
     * the start index were ignored), then the quad (vertices 4-7; 0-3 if the
     * base vertex were ignored). */
    static const WORD indices[12] = {4, 5, 6, 6, 5, 7, 0, 1, 2, 2, 1, 3};
    Vertex interleaved[4], stretch[4], listed[8];
    First firsts[12];
    Second seconds[12];
    VertexBuffer *vb = NULL, *keys = NULL, *frames = NULL;
    IndexBuffer *ib = NULL;
    TweenLayout single = TWEEN_LAYOUT_NONE, split = TWEEN_LAYOUT_NONE;
    BufferPointer pointer = NULL;
    Log("STAGE vertex tween positions: POSITION1/POSITION2, factors 0, 1/2, 1");
    Corners(interleaved, user);
    Corners(stretch, stretched);
    Corners(listed, decoy);
    Corners(listed + 4, buffered);
    /* Vertices 0-3 and 8-11: the decoy quad; 4-7: the streamed quad. Each
     * vertex's second key frame is in stream 1 at the same vertex. */
    {
        Vertex all[12];
        Corners(all, decoy);
        Corners(all + 4, streamed);
        Corners(all + 8, decoy);
        for (int v = 0; v < 12; ++v) {
            firsts[v] = {all[v].x, all[v].y, all[v].z, all[v].color};
            seconds[v] = {all[v].x2, all[v].y2, all[v].z2};
        }
    }
    BOOL ok =
        Fill(&vb, dev, listed, sizeof(listed), "FAIL vertex tween vertex buffer") &&
        Fill(&keys, dev, firsts, sizeof(firsts), "FAIL vertex tween key frame 1 buffer") &&
        Fill(&frames, dev, seconds, sizeof(seconds), "FAIL vertex tween key frame 2 buffer") &&
        HR(IBUFFER_CREATE(dev, sizeof(indices), D3DUSAGE_WRITEONLY, D3DPOOL_MANAGED, &ib),
           "FAIL vertex tween index buffer creation") &&
        HR(IBUFFER(Lock, ib, 0, 0, &pointer, 0), "FAIL vertex tween index buffer lock");
    if (ok) {
        const BOOL mapped = Check(pointer != NULL, "FAIL vertex tween index buffer pointer", 0);
        if (mapped)
            vbuffer_probe::Copy(pointer, indices, sizeof(indices));
        ok = HR(IBUFFER(Unlock, ib), "FAIL vertex tween index buffer unlock") && mapped;
    }
    ok = ok && vbuffer_probe::Fixed(dev, vbuffer_probe::XyzDiffuse) &&
         Layout(dev, Interleaved, &single) && Layout(dev, Streams, &split) &&
         HR(DEVICE(SetRenderState, dev, D3DRS_VERTEXBLEND, D3DVBF_TWEENING),
            "FAIL vertex tween state");
    static const char *const names[4] = {
        "positions: factor 0 (first key frame)", "positions: factor 1/2 (half way)",
        "positions: factor 1 (second key frame)",
        "positions: tweening disabled, factor 1 (first key frame)"};
    for (int f = 0; ok && f < 4; ++f) {
        const float t = Factors[f < 3 ? f : 2], at = f < 3 ? t : 0;
        const Area areas[4] = {Expected(user, at), Expected(buffered, at), Expected(streamed, at),
                               Expected(stretched, at)};
        ok = Factor(dev, t) &&
             (f < 3 || HR(DEVICE(SetRenderState, dev, D3DRS_VERTEXBLEND, D3DVBF_DISABLE),
                          "FAIL vertex tween disable")) &&
             Frame(dev, target, names[f], areas, 4, 0, [&]() -> BOOL {
                 if (!Bind(dev, single) ||
                     !HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, interleaved,
                                sizeof(Vertex)),
                         "FAIL vertex tween user draw") ||
                     !HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, stretch,
                                sizeof(Vertex)),
                         "FAIL vertex tween stretched draw") ||
                     !HR(VBUFFER_STREAM(dev, vb, sizeof(Vertex)), "FAIL vertex tween stream") ||
                     !vbuffer_probe::Strip(dev, 4))
                     return FALSE;
                 return Bind(dev, split) &&
                        HR(VBUFFER_STREAM(dev, keys, sizeof(First)), "FAIL vertex tween stream") &&
                        Stream1(dev, frames, sizeof(Second)) &&
                        vbuffer_probe::DrawIndexed(dev, ib, 4, 0, 4, 6, 2);
             });
    }
    if (ok)
        Log("PASS vertex tween positions: user memory, a vertex buffer from vertex 4, two "
            "streams indexed from base vertex 4 and a stretched quad at factors 0, 1/2 and 1 "
            "exact, untweened when disabled, decoys unseen");
    DEVICE(SetRenderState, dev, D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    vbuffer_probe::Unbind(dev);
    Stream1(dev, NULL, 0);
    Release(dev, single);
    Release(dev, split);
    if (ib)
        IBUFFER(Release, ib);
    if (frames)
        VBUFFER(Release, frames);
    if (keys)
        VBUFFER(Release, keys);
    if (vb)
        VBUFFER(Release, vb);
}

/* Lit quads at factors 0, 1/2 and 1 in one frame: the second normal alone
 * tweens. */
static void Lighting(Device *dev, Surface *target) {
    static const int lefts[3] = {16, 112, 208};
    /* 255 times N.L for the normal (1 - t, 0, -t) and light direction +z. */
    static const DWORD plain[3] = {0, 128, 255}, normalized[3] = {0, 180, 255};
    BlendLight light = {};
    BlendMaterial material = {};
    Lit vertices[3][4];
    Area areas[3];
    TweenLayout layout = TWEEN_LAYOUT_NONE;
    Log("STAGE vertex tween lit: NORMAL1/NORMAL2 alone, with and without NORMALIZENORMALS");
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = light.Diffuse.a = 1;
    light.Direction.z = 1;
    material.Diffuse.r = material.Diffuse.g = material.Diffuse.b = material.Diffuse.a = 1;
    for (int q = 0; q < 3; ++q) {
        const float left = ClipX(lefts[q]), right = ClipX(lefts[q] + 64);
        const float top = ClipY(64), bottom = ClipY(128);
        const float xs[4] = {left, right, left, right}, ys[4] = {top, top, bottom, bottom};
        for (int v = 0; v < 4; ++v)
            vertices[q][v] = {xs[v], ys[v], .5f, 1, 0, 0, 0, 0, -1};
    }
    BOOL ok = vbuffer_probe::Fixed(dev, D3DFVF_XYZ | D3DFVF_NORMAL) &&
              Layout(dev, Normals, &layout) &&
              HR(DEVICE(SetLight, dev, 0, &light), "FAIL vertex tween light") &&
              HR(DEVICE(LightEnable, dev, 0, TRUE), "FAIL vertex tween light enable") &&
              HR(DEVICE(SetMaterial, dev, &material), "FAIL vertex tween material") &&
              HR(DEVICE(SetRenderState, dev, D3DRS_AMBIENT, 0), "FAIL vertex tween ambient") &&
              HR(DEVICE(SetRenderState, dev, D3DRS_LIGHTING, TRUE), "FAIL vertex tween lighting") &&
              HR(DEVICE(SetRenderState, dev, D3DRS_VERTEXBLEND, D3DVBF_TWEENING),
                 "FAIL vertex tween state");
    for (int normalize = 0; ok && normalize < 2; ++normalize) {
        for (int q = 0; q < 3; ++q)
            areas[q] = {lefts[q], 64, lefts[q] + 64, 128,
                        (normalize ? normalized[q] : plain[q]) * 0x010101u};
        ok = HR(DEVICE(SetRenderState, dev, D3DRS_NORMALIZENORMALS, (DWORD)normalize),
                "FAIL vertex tween normalize") &&
             Frame(dev, target,
                   normalize ? "lit, NORMALIZENORMALS: grey 0, 180, 255" : "lit: grey 0, 128, 255",
                   areas, 3, 3, [&]() -> BOOL {
                       for (int q = 0; q < 3; ++q)
                           if (!Factor(dev, Factors[q]) ||
                               !HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, vertices[q],
                                          sizeof(Lit)),
                                   "FAIL vertex tween lit draw"))
                               return FALSE;
                       return TRUE;
                   });
    }
    if (ok)
        Log("PASS vertex tween lit: tweened normals at factors 0, 1/2 and 1, with and without "
            "NORMALIZENORMALS");
    DEVICE(SetRenderState, dev, D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    DEVICE(SetRenderState, dev, D3DRS_NORMALIZENORMALS, FALSE);
    DEVICE(SetRenderState, dev, D3DRS_LIGHTING, FALSE);
    DEVICE(LightEnable, dev, 0, FALSE);
    Release(dev, layout);
}

static void Run(Device *dev, Surface *backbuffer) {
    StageCaps caps = {};
    Log("STAGE vertex tweening (D3DVBF_TWEENING)");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL vertex tween device caps"))
        return;
    Number("VertexProcessingCaps", caps.VertexProcessingCaps);
    if (!(caps.VertexProcessingCaps & D3DVTXPCAPS_TWEENING)) {
        Log("SKIP vertex tweening: D3DVTXPCAPS_TWEENING not claimed (no Wine 0053)");
        return;
    }
    Log("PASS vertex tween caps: D3DVTXPCAPS_TWEENING");
    Positions(dev, backbuffer);
    if (!failed)
        Lighting(dev, backbuffer);
    /* Back to the state the other stages expect. */
    Factor(dev, 0);
    DEVICE(SetRenderState, dev, D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    DEVICE(SetRenderState, dev, D3DRS_LIGHTING, FALSE);
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

} // namespace tween_probe
