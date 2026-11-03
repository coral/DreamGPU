/* SPDX-License-Identifier: GPL-2.0-or-later
 * Vertex and index buffers through the public D3D8/9 API. Included by
 * probe.cpp after volume-probe.inc. Fixed function with lighting off and
 * identity transforms: untransformed XYZ corners are given in clip space, so
 * each quad covers a known pixel rectangle. Diffuse is a D3DCOLOR whose red and
 * blue differ, so a byte-order conversion error shows. Every frame is read back
 * whole: one pixel inside each quad's edges must be its colour, one pixel
 * outside every quad the clear colour, so missing, stale and misplaced
 * geometry all fail. Cases:
 * - static: WRITEONLY default-pool vertex and 16-bit index buffers, drawn by
 *   DrawIndexedPrimitive with base vertex 4 (D3D8: SetIndices), start indices
 *   6 and 18 and MinIndex 0 and 8. A decoy quad below the base vertex and
 *   decoy indices below the first start index must not appear.
 * - pretransformed: a managed WRITEONLY XYZRHW buffer with rhw 2 drawn from
 *   start vertex 4. Without GLSL WineD3D rewrites XYZRHW positions in buffer
 *   objects (x, y, z divided by rhw, w = 1/rhw); the quad must not move.
 * - dynamic: a DYNAMIC|WRITEONLY buffer used as a ring. Frame 1 writes quad 0
 *   with DISCARD and quads 1-3 at later offsets with NOOVERWRITE, each drawn
 *   after its write. Frame 2 appends two quads with NOOVERWRITE, then wraps
 *   with DISCARD to offset 0 after those draws.
 * - update: a managed WRITEONLY buffer drawn, checked and presented, then
 *   rewritten whole by Lock(0, 0, 0) and in part by a Lock at vertex 4 with
 *   flags 0. The next frame shows each change, and no earlier quad remains.
 */
#if DG_D3D_VERSION == 8
typedef IDirect3DVertexBuffer8 VertexBuffer;
typedef IDirect3DIndexBuffer8 IndexBuffer;
typedef D3DVIEWPORT8 BufferViewport;
typedef BYTE *BufferPointer;
#define VBUFFER(name, ...) IDirect3DVertexBuffer8_##name(__VA_ARGS__)
#define IBUFFER(name, ...) IDirect3DIndexBuffer8_##name(__VA_ARGS__)
#define VBUFFER_CREATE(device, size, usage, fvf, pool, out)                                        \
    DEVICE(CreateVertexBuffer, device, size, usage, fvf, pool, out)
#define IBUFFER_CREATE(device, size, usage, pool, out)                                             \
    DEVICE(CreateIndexBuffer, device, size, usage, D3DFMT_INDEX16, pool, out)
#define VBUFFER_STREAM(device, buffer, stride) DEVICE(SetStreamSource, device, 0, buffer, stride)
#else
typedef IDirect3DVertexBuffer9 VertexBuffer;
typedef IDirect3DIndexBuffer9 IndexBuffer;
typedef D3DVIEWPORT9 BufferViewport;
typedef void *BufferPointer;
#define VBUFFER(name, ...) IDirect3DVertexBuffer9_##name(__VA_ARGS__)
#define IBUFFER(name, ...) IDirect3DIndexBuffer9_##name(__VA_ARGS__)
#define VBUFFER_CREATE(device, size, usage, fvf, pool, out)                                        \
    DEVICE(CreateVertexBuffer, device, size, usage, fvf, pool, out, NULL)
#define IBUFFER_CREATE(device, size, usage, pool, out)                                             \
    DEVICE(CreateIndexBuffer, device, size, usage, D3DFMT_INDEX16, pool, out, NULL)
#define VBUFFER_STREAM(device, buffer, stride) DEVICE(SetStreamSource, device, 0, buffer, 0, stride)
#endif

namespace vbuffer_probe {

struct Vertex {
    float x, y, z;
    DWORD color;
};
struct Transformed {
    float x, y, z, rhw;
    DWORD color;
};
static_assert(sizeof(Vertex) == 16 && sizeof(Transformed) == 20);
static constexpr DWORD XyzDiffuse = D3DFVF_XYZ | D3DFVF_DIFFUSE;
static constexpr DWORD RhwDiffuse = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
static constexpr DWORD Background = 0xff102030;
static constexpr DWORD Unchecked = 0xffffffff;
static constexpr int ScreenWidth = 320, ScreenHeight = 240;
static constexpr DWORD Decoy = 0xffff00ff;

/* A quad's pixels [left, right) x [top, bottom) and its colour. */
struct Area {
    int left, top, right, bottom;
    DWORD color;
};

/* Strip-ordered corners in clip space: pixel x is x / 160 - 1, y 1 - y / 120. */
static void Corners(Vertex *out, const Area &area) {
    const float left = area.left / 160.f - 1, right = area.right / 160.f - 1;
    const float top = 1 - area.top / 120.f, bottom = 1 - area.bottom / 120.f;
    out[0] = {left, top, .5f, area.color};
    out[1] = {right, top, .5f, area.color};
    out[2] = {left, bottom, .5f, area.color};
    out[3] = {right, bottom, .5f, area.color};
}
/* Strip-ordered screen corners with rhw 2. */
static void Corners(Transformed *out, const Area &area) {
    const float left = (float)area.left, right = (float)area.right;
    const float top = (float)area.top, bottom = (float)area.bottom;
    out[0] = {left, top, .5f, 2.f, area.color};
    out[1] = {right, top, .5f, 2.f, area.color};
    out[2] = {left, bottom, .5f, 2.f, area.color};
    out[3] = {right, bottom, .5f, 2.f, area.color};
}

/* The colour expected at (x, y), or Unchecked on the two-pixel band around
 * an edge (the last pixel row or column on either side). */
static DWORD Expected(const Area *areas, UINT count, int x, int y) {
    DWORD color = Background & 0xffffff;
    for (UINT i = 0; i < count; ++i) {
        const Area &a = areas[i];
        if (x > a.left && x < a.right - 1 && y > a.top && y < a.bottom - 1)
            color = a.color & 0xffffff;
        else if (x >= a.left - 1 && x <= a.right && y >= a.top - 1 && y <= a.bottom)
            return Unchecked;
    }
    return color;
}

/* Compare the whole backbuffer; on a mismatch log the first wrong pixel, the
 * counts and every quad's centre. */
static BOOL Verify(Surface *target, const char *name, const Area *areas, UINT count) {
    D3DLOCKED_RECT locked;
    UINT wrong = 0, checked = 0;
    int first_x = 0, first_y = 0;
    DWORD first_actual = 0, first_expected = 0;
    if (!HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY),
            "FAIL vertex buffer readback")) {
        Log(name);
        return FALSE;
    }
    if (!locked.pBits || locked.Pitch < ScreenWidth * 4) {
        SURFACE(UnlockRect, target);
        Log(name);
        return Check(FALSE, "FAIL vertex buffer readback layout", (DWORD)locked.Pitch);
    }
    const auto pixel = [&](int x, int y) -> DWORD {
        return *(const DWORD *)((const BYTE *)locked.pBits + y * locked.Pitch + x * 4) & 0xffffff;
    };
    for (int y = 0; y < ScreenHeight; ++y)
        for (int x = 0; x < ScreenWidth; ++x) {
            const DWORD expected = Expected(areas, count, x, y);
            if (expected == Unchecked)
                continue;
            ++checked;
            const DWORD actual = pixel(x, y);
            if (actual != expected && !wrong++) {
                first_x = x;
                first_y = y;
                first_actual = actual;
                first_expected = expected;
            }
        }
    if (wrong) {
        Log("FAIL vertex buffer pixels");
        Log(name);
        Number("vertex buffer pixel actual", first_actual);
        Number("vertex buffer pixel expected", first_expected);
        Number("vertex buffer pixel x", (DWORD)first_x);
        Number("vertex buffer pixel y", (DWORD)first_y);
        Number("vertex buffer wrong pixels", wrong);
        Number("vertex buffer checked pixels", checked);
        for (UINT i = 0; i < count; ++i) {
            const int x = (areas[i].left + areas[i].right) / 2;
            const int y = (areas[i].top + areas[i].bottom) / 2;
            Number("vertex buffer quad centre x", (DWORD)x);
            Number("vertex buffer quad centre y", (DWORD)y);
            Number("vertex buffer quad expected", areas[i].color & 0xffffff);
            Number("vertex buffer quad actual", pixel(x, y));
        }
        failed = TRUE;
    }
    SURFACE(UnlockRect, target);
    return !wrong;
}

static void Copy(void *to, const void *from, UINT size) {
    BYTE *out = (BYTE *)to;
    const BYTE *in = (const BYTE *)from;
    for (UINT i = 0; i < size; ++i)
        out[i] = in[i];
}
/* Lock [offset, offset + size) (size 0: to the end) with flags, copy bytes
 * there and unlock. */
static BOOL Write(VertexBuffer *buffer, UINT offset, UINT size, const void *data, UINT bytes,
                  DWORD flags, const char *stage) {
    BufferPointer pointer = NULL;
    if (!HR(VBUFFER(Lock, buffer, offset, size, &pointer, flags), stage))
        return FALSE;
    const BOOL mapped = Check(pointer != NULL, stage, 0);
    if (mapped)
        Copy(pointer, data, bytes);
    return HR(VBUFFER(Unlock, buffer), "FAIL vertex buffer unlock") && mapped;
}

/* Fixed function, diffuse only, identity transforms, the whole viewport. */
static BOOL Fixed(Device *dev, DWORD fvf) {
    D3DMATRIX identity = {};
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    BufferViewport viewport = {0, 0, ScreenWidth, ScreenHeight, 0, 1};
#if DG_D3D_VERSION == 9
    if (!HR(DEVICE(SetVertexShader, dev, NULL), "FAIL vertex buffer vertex shader unbind"))
        return FALSE;
#endif
    return HR(SHADER_SET(dev, SHADER_NONE), "FAIL vertex buffer pixel shader unbind") &&
           HR(STAGE_FVF(dev, fvf), "FAIL vertex buffer FVF") &&
           HR(DEVICE(SetTransform, dev, D3DTS_WORLD, &identity), "FAIL vertex buffer world") &&
           HR(DEVICE(SetTransform, dev, D3DTS_VIEW, &identity), "FAIL vertex buffer view") &&
           HR(DEVICE(SetTransform, dev, D3DTS_PROJECTION, &identity),
              "FAIL vertex buffer projection") &&
           HR(DEVICE(SetViewport, dev, &viewport), "FAIL vertex buffer viewport") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_LIGHTING, FALSE), "FAIL vertex buffer lighting") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ZENABLE, FALSE), "FAIL vertex buffer depth") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_CULLMODE, D3DCULL_NONE),
              "FAIL vertex buffer culling") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_FILLMODE, D3DFILL_SOLID),
              "FAIL vertex buffer fill mode") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, FALSE),
              "FAIL vertex buffer blend") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ALPHATESTENABLE, FALSE),
              "FAIL vertex buffer alpha test") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_FOGENABLE, FALSE), "FAIL vertex buffer fog") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_SPECULARENABLE, FALSE),
              "FAIL vertex buffer specular") &&
           HR(STAGE_SET_TEXTURE(dev, 0, NULL), "FAIL vertex buffer texture unbind") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
              "FAIL vertex buffer color op") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
              "FAIL vertex buffer color arg") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
              "FAIL vertex buffer alpha op") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE),
              "FAIL vertex buffer alpha arg") &&
           HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE),
              "FAIL vertex buffer stage 1") &&
           HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE),
              "FAIL vertex buffer stage 1 alpha");
}

/* Clear, run draw inside a scene, then compare the frame. */
template <typename Draw>
static BOOL Frame(Device *dev, Surface *target, const char *name, const Area *areas, UINT count,
                  Draw draw) {
    if (!HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, Background, 1, 0),
            "FAIL vertex buffer clear") ||
        !HR(DEVICE(BeginScene, dev), "FAIL vertex buffer begin scene")) {
        Log(name);
        return FALSE;
    }
    const BOOL drawn = draw();
    if (!HR(DEVICE(EndScene, dev), "FAIL vertex buffer end scene") || !drawn) {
        Log(name);
        return FALSE;
    }
    return Verify(target, name, areas, count);
}
static BOOL Present(Device *dev) {
    return HR(DEVICE(Present, dev, NULL, NULL, NULL, NULL), "FAIL vertex buffer present");
}
/* One quad from the bound stream: two strip triangles from vertex first. */
static BOOL Strip(Device *dev, UINT first) {
    return HR(DEVICE(DrawPrimitive, dev, D3DPT_TRIANGLESTRIP, first, 2), "FAIL vertex buffer draw");
}
static BOOL DrawIndexed(Device *dev, IndexBuffer *indices, UINT base, UINT min, UINT count,
                        UINT start, UINT triangles) {
#if DG_D3D_VERSION == 8
    return HR(DEVICE(SetIndices, dev, indices, base), "FAIL vertex buffer indices") &&
           HR(DEVICE(DrawIndexedPrimitive, dev, D3DPT_TRIANGLELIST, min, count, start, triangles),
              "FAIL vertex buffer indexed draw");
#else
    return HR(DEVICE(SetIndices, dev, indices), "FAIL vertex buffer indices") &&
           HR(DEVICE(DrawIndexedPrimitive, dev, D3DPT_TRIANGLELIST, (INT)base, min, count, start,
                     triangles),
              "FAIL vertex buffer indexed draw");
#endif
}
static void Unbind(Device *dev) {
    VBUFFER_STREAM(dev, NULL, 0);
#if DG_D3D_VERSION == 8
    DEVICE(SetIndices, dev, NULL, 0);
#else
    DEVICE(SetIndices, dev, NULL);
#endif
}

static void StaticCase(Device *dev, Surface *target) {
    /* Vertices 0-3: a decoy below base vertex 4; then quads 0, 1 and 2. */
    static const Area decoy = {120, 140, 200, 220, Decoy};
    static const Area quads[3] = {{20, 30, 100, 110, 0xffd04010},
                                  {120, 30, 200, 110, 0xff10a0f0},
                                  {220, 30, 300, 110, 0xff60e030}};
    /* Relative to the base vertex. Draw 1: start 6, quads 0 and 1 (MinIndex 0,
     * eight vertices). Draw 2: start 18, quad 2 (MinIndex 8, four vertices). */
    static const WORD indices[24] = {
        8, 9, 10, 10, 9, 11, /* decoy: ignoring start 6 draws quad 2, not 1 */
        0, 1, 2,  2,  1, 3,  /* quad 0 */
        4, 5, 6,  6,  5, 7,  /* quad 1 */
        8, 9, 10, 10, 9, 11, /* quad 2 */
    };
    Vertex vertices[16];
    VertexBuffer *vb = NULL;
    IndexBuffer *ib = NULL;
    BufferPointer pointer = NULL;
    Log("STAGE vertex buffer static indexed");
    Corners(vertices, decoy);
    for (UINT i = 0; i < 3; ++i)
        Corners(vertices + 4 + 4 * i, quads[i]);
    BOOL ok = HR(VBUFFER_CREATE(dev, sizeof(vertices), D3DUSAGE_WRITEONLY, XyzDiffuse,
                                D3DPOOL_DEFAULT, &vb),
                 "FAIL static vertex buffer creation") &&
              HR(IBUFFER_CREATE(dev, sizeof(indices), D3DUSAGE_WRITEONLY, D3DPOOL_DEFAULT, &ib),
                 "FAIL static index buffer creation") &&
              Write(vb, 0, 0, vertices, sizeof(vertices), 0, "FAIL static vertex buffer lock") &&
              HR(IBUFFER(Lock, ib, 0, 0, &pointer, 0), "FAIL static index buffer lock");
    if (ok) {
        const BOOL mapped = Check(pointer != NULL, "FAIL static index buffer pointer", 0);
        if (mapped)
            Copy(pointer, indices, sizeof(indices));
        ok = HR(IBUFFER(Unlock, ib), "FAIL static index buffer unlock") && mapped;
    }
    ok = ok && Fixed(dev, XyzDiffuse) &&
         HR(VBUFFER_STREAM(dev, vb, sizeof(Vertex)), "FAIL static stream source") &&
         Frame(dev, target, "static indexed: base vertex 4, start indices 6 and 18", quads, 3,
               [&]() -> BOOL {
                   return DrawIndexed(dev, ib, 4, 0, 8, 6, 4) &&
                          DrawIndexed(dev, ib, 4, 8, 4, 18, 2);
               });
    if (ok)
        Log("PASS vertex buffer static indexed: default-pool WRITEONLY vertices and 16-bit "
            "indices, base vertex 4, start indices 6/18, three quads exact, decoys unseen");
    Unbind(dev);
    if (ib)
        IBUFFER(Release, ib);
    if (vb)
        VBUFFER(Release, vb);
}

static void TransformedCase(Device *dev, Surface *target) {
    /* Vertices 0-3: a decoy below start vertex 4. */
    static const Area decoy = {20, 140, 100, 220, Decoy};
    static const Area quad = {200, 130, 300, 210, 0xfff030c0};
    Transformed vertices[8];
    VertexBuffer *vb = NULL;
    Log("STAGE vertex buffer pretransformed");
    Corners(vertices, decoy);
    Corners(vertices + 4, quad);
    const BOOL ok =
        HR(VBUFFER_CREATE(dev, sizeof(vertices), D3DUSAGE_WRITEONLY, RhwDiffuse, D3DPOOL_MANAGED,
                          &vb),
           "FAIL pretransformed vertex buffer creation") &&
        Write(vb, 0, 0, vertices, sizeof(vertices), 0, "FAIL pretransformed vertex buffer lock") &&
        Fixed(dev, RhwDiffuse) &&
        HR(VBUFFER_STREAM(dev, vb, sizeof(Transformed)), "FAIL pretransformed stream source") &&
        Frame(dev, target, "pretransformed: rhw 2 from start vertex 4", &quad, 1,
              [&]() -> BOOL { return Strip(dev, 4); });
    if (ok)
        Log("PASS vertex buffer pretransformed: managed XYZRHW rhw 2 from start vertex 4 exact, "
            "decoy unseen");
    Unbind(dev);
    if (vb)
        VBUFFER(Release, vb);
}

static void DynamicCase(Device *dev, Surface *target) {
    /* Six quad slots of four vertices. */
    static const Area first[4] = {{20, 20, 80, 80, 0xff8020f0},
                                  {95, 20, 155, 80, 0xfff0e020},
                                  {170, 20, 230, 80, 0xff20f0a0},
                                  {245, 20, 305, 80, 0xffa05020}};
    static const Area second[3] = {{40, 150, 110, 210, 0xff3090d0},
                                   {130, 150, 200, 210, 0xffd03090},
                                   {220, 150, 290, 210, 0xff90d040}};
    static const UINT second_slots[3] = {4, 5, 0};
    VertexBuffer *vb = NULL;
    Log("STAGE vertex buffer dynamic");
    /* Write one quad into slot with flags (DISCARD at slot 0, otherwise
     * NOOVERWRITE) and draw it. */
    const auto append = [&](const Area &area, UINT slot) -> BOOL {
        Vertex quad[4];
        Corners(quad, area);
        const DWORD flags = slot ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD;
        return Write(vb, slot * sizeof(quad), sizeof(quad), quad, sizeof(quad), flags,
                     slot ? "FAIL dynamic NOOVERWRITE lock" : "FAIL dynamic DISCARD lock") &&
               Strip(dev, slot * 4);
    };
    const BOOL ok =
        HR(VBUFFER_CREATE(dev, 24 * sizeof(Vertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                          XyzDiffuse, D3DPOOL_DEFAULT, &vb),
           "FAIL dynamic vertex buffer creation") &&
        Fixed(dev, XyzDiffuse) &&
        HR(VBUFFER_STREAM(dev, vb, sizeof(Vertex)), "FAIL dynamic stream source") &&
        Frame(dev, target, "dynamic frame 1: DISCARD at 0, NOOVERWRITE at vertices 4, 8, 12", first,
              4,
              [&]() -> BOOL {
                  BOOL drawn = TRUE;
                  for (UINT i = 0; i < 4 && drawn; ++i)
                      drawn = append(first[i], i);
                  return drawn;
              }) &&
        Present(dev) &&
        Frame(dev, target, "dynamic frame 2: NOOVERWRITE at vertices 16, 20, DISCARD wrap to 0",
              second, 3, [&]() -> BOOL {
                  BOOL drawn = TRUE;
                  for (UINT i = 0; i < 3 && drawn; ++i)
                      drawn = append(second[i], second_slots[i]);
                  return drawn;
              });
    if (ok)
        Log("PASS vertex buffer dynamic: DISCARD/NOOVERWRITE ring, seven quads over two frames "
            "exact, first frame gone");
    Unbind(dev);
    if (vb)
        VBUFFER(Release, vb);
}

static void UpdateCase(Device *dev, Surface *target) {
    /* Frame 2 moves and recolours quad 0 by a whole-buffer lock; frame 3 moves
     * and recolours quad 1 by a lock at vertex 4 only. */
    static const Area frame1[2] = {{20, 20, 120, 100, 0xffc06020},
                                   {180, 130, 300, 220, 0xff2060c0}};
    static const Area frame2[2] = {{40, 130, 140, 220, 0xff60c0a0},
                                   {180, 130, 300, 220, 0xff2060c0}};
    static const Area frame3[2] = {{40, 130, 140, 220, 0xff60c0a0},
                                   {190, 20, 300, 100, 0xffe0a040}};
    Vertex vertices[8];
    VertexBuffer *vb = NULL;
    Log("STAGE vertex buffer static update");
    const auto fill = [&](const Area *areas) {
        Corners(vertices, areas[0]);
        Corners(vertices + 4, areas[1]);
    };
    const auto draw = [&]() -> BOOL { return Strip(dev, 0) && Strip(dev, 4); };
    fill(frame1);
    BOOL ok = HR(VBUFFER_CREATE(dev, sizeof(vertices), D3DUSAGE_WRITEONLY, XyzDiffuse,
                                D3DPOOL_MANAGED, &vb),
                 "FAIL update vertex buffer creation") &&
              Write(vb, 0, 0, vertices, sizeof(vertices), 0, "FAIL update initial lock") &&
              Fixed(dev, XyzDiffuse) &&
              HR(VBUFFER_STREAM(dev, vb, sizeof(Vertex)), "FAIL update stream source") &&
              Frame(dev, target, "update frame 1: initial contents", frame1, 2, draw) &&
              Present(dev);
    fill(frame2);
    ok = ok && Write(vb, 0, 0, vertices, sizeof(vertices), 0, "FAIL update whole-buffer lock") &&
         Frame(dev, target, "update frame 2: Lock(0, 0, 0) moved and recoloured quad 0", frame2, 2,
               draw) &&
         Present(dev);
    fill(frame3);
    ok = ok &&
         Write(vb, 4 * sizeof(Vertex), 4 * sizeof(Vertex), vertices + 4, 4 * sizeof(Vertex), 0,
               "FAIL update partial lock") &&
         Frame(dev, target, "update frame 3: Lock at vertex 4 moved and recoloured quad 1 only",
               frame3, 2, draw) &&
         Present(dev);
    if (ok)
        Log("PASS vertex buffer static update: whole and partial flags-0 locks between presented "
            "frames exact, earlier quads gone");
    Unbind(dev);
    if (vb)
        VBUFFER(Release, vb);
}

/* Every case runs and logs its own result; any failure fails the probe. */
static void Run(Device *dev, Surface *target) {
    StageCaps caps = {};
    Log("STAGE vertex buffers");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL vertex buffer device caps"))
        return;
    Number("MaxPrimitiveCount", caps.MaxPrimitiveCount);
    Number("MaxVertexIndex", caps.MaxVertexIndex);
    Number("MaxStreams", caps.MaxStreams);
    Number("MaxStreamStride", caps.MaxStreamStride);
#if DG_D3D_VERSION == 9
    Number("DevCaps2", caps.DevCaps2);
#endif
    StaticCase(dev, target);
    TransformedCase(dev, target);
    DynamicCase(dev, target);
    UpdateCase(dev, target);
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

} // namespace vbuffer_probe
