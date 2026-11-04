/* SPDX-License-Identifier: GPL-2.0-or-later
 * D3DCOLOR diffuse and specular arrays through the public D3D8/9 API. Included
 * by probe.cpp after vbuffer-probe.inc and vshader-probe.inc, whose frame,
 * buffer and shader helpers it reuses. Fixed function with lighting off,
 * identity transforms and SPECULARENABLE: each untransformed XYZ | DIFFUSE |
 * SPECULAR quad is exactly diffuse + specular (stage 0 selects the diffuse,
 * the specular is added after the stages). Every diffuse and specular has
 * red != blue and every sum has red != blue, so swapping red and blue in the
 * diffuse, in the specular or in both changes the pixel, as does a dropped
 * specular. Whole frames are compared as in vbuffer-probe.inc. With
 * ARB_vertex_array_bgra (DG_CAP_GL_VERTEX_ARRAY_BGRA) Wine draws these from
 * GL_BGRA arrays instead of immediate mode, and static buffers keep their
 * D3DCOLOR bytes unconverted. Cases:
 * - user memory: DrawPrimitiveUP with a tight 20-byte and a padded 24-byte
 *   stride, DrawIndexedPrimitiveUP lists and strips of both strides whose
 *   indices skip four decoy vertices (MinVertexIndex 0: this Wine copies
 *   vertices from the first one whatever MinVertexIndex says).
 * - dynamic: a DYNAMIC|WRITEONLY default-pool buffer (system memory in this
 *   Wine) written with DISCARD, then NOOVERWRITE, drawn by DrawPrimitive and
 *   by DrawIndexedPrimitive from a static index buffer with base vertices 8
 *   and 12; a decoy quad between them must not appear.
 * - static: a WRITEONLY default-pool buffer (a GL buffer object) with a
 *   padded 24-byte stride, drawn from start vertex 4 and by indexed draws
 *   with base vertices 8 and 12 and start indices 0 and 6.
 * - vertex shader: vs_1_1 (vshader-probe.inc) moves the D3DCOLOR input v1 to
 *   oD0 from a static and from a dynamic buffer, one half of the frame each
 *   (Wine's generic attribute arrays, which no longer swizzle in the shader).
 *   Below VertexShaderVersion 1.1 the case is skipped.
 */
namespace bgra_probe {

using vbuffer_probe::Area;
using vbuffer_probe::Decoy;
using vbuffer_probe::Frame;
using vbuffer_probe::Write;

struct Pair {
    DWORD diffuse, specular;
};
static constexpr DWORD Byte(DWORD color, int shift) {
    return color >> shift & 0xff;
}
/* The quad colour: each channel of diffuse plus specular. */
static constexpr DWORD Sum(Pair p) {
    return 0xff000000 | (Byte(p.diffuse, 16) + Byte(p.specular, 16)) << 16 |
           (Byte(p.diffuse, 8) + Byte(p.specular, 8)) << 8 |
           (Byte(p.diffuse, 0) + Byte(p.specular, 0));
}
/* Red differs from blue in the diffuse, the specular and their sum, and no
 * channel saturates, so any red/blue swap changes the pixel. */
static constexpr bool Telling(Pair p) {
    return Byte(p.diffuse, 16) != Byte(p.diffuse, 0) &&
           Byte(p.specular, 16) != Byte(p.specular, 0) && Byte(Sum(p), 16) != Byte(Sum(p), 0) &&
           Byte(p.diffuse, 16) + Byte(p.specular, 16) <= 255 &&
           Byte(p.diffuse, 8) + Byte(p.specular, 8) <= 255 &&
           Byte(p.diffuse, 0) + Byte(p.specular, 0) <= 255;
}
static constexpr Pair Pairs[8] = {
    {0xff602010, 0x00104080}, /* (60,20,10) + (10,40,80) = (70,60,90) */
    {0xff10a040, 0x00502008}, /* (10,a0,40) + (50,20,08) = (60,c0,48) */
    {0xffc01060, 0x00203010}, /* (c0,10,60) + (20,30,10) = (e0,40,70) */
    {0xff3070b0, 0x00401020}, /* (30,70,b0) + (40,10,20) = (70,80,d0) */
    {0xff20e080, 0x00600810}, /* (20,e0,80) + (60,08,10) = (80,e8,90) */
    {0xffa02000, 0x00106040}, /* (a0,20,00) + (10,60,40) = (b0,80,40) */
    {0xff0040a0, 0x00801020}, /* (00,40,a0) + (80,10,20) = (80,50,c0) */
    {0xff50f010, 0x00300840}, /* (50,f0,10) + (30,08,40) = (80,f8,50) */
};
static constexpr bool AllTelling() {
    for (const Pair &p : Pairs)
        if (!Telling(p))
            return false;
    return true;
}
static_assert(AllTelling());
static_assert(Sum(Pairs[0]) == 0xff706090 && Sum(Pairs[7]) == 0xff80f850);

struct Vertex {
    float x, y, z;
    DWORD diffuse, specular;
};
/* The same vertex with a 24-byte stride; the padding is never read. */
struct Padded {
    float x, y, z;
    DWORD diffuse, specular, padding;
};
static_assert(sizeof(Vertex) == 20 && sizeof(Padded) == 24);
static constexpr DWORD Fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR;
/* Top row left to right, then bottom left; the decoy takes the bottom right. */
static constexpr Area Places[4] = {
    {20, 30, 100, 110, 0}, {120, 30, 200, 110, 0}, {220, 30, 300, 110, 0}, {20, 140, 100, 220, 0}};
static constexpr Area DecoyPlace = {220, 140, 300, 220, Decoy};

/* Quad n of a frame: place n with the colour of pair p. */
static Area Quad(UINT n, UINT p) {
    Area area = Places[n];
    area.color = Sum(Pairs[p]);
    return area;
}
/* Strip-ordered clip-space corners (as vbuffer-probe.inc) with the pair's
 * colours; the decoy uses Decoy for both. */
static void Corners(Vertex *out, const Area &area, Pair pair) {
    const float left = area.left / 160.f - 1, right = area.right / 160.f - 1;
    const float top = 1 - area.top / 120.f, bottom = 1 - area.bottom / 120.f;
    out[0] = {left, top, .5f, pair.diffuse, pair.specular};
    out[1] = {right, top, .5f, pair.diffuse, pair.specular};
    out[2] = {left, bottom, .5f, pair.diffuse, pair.specular};
    out[3] = {right, bottom, .5f, pair.diffuse, pair.specular};
}
static void Corners(Padded *out, const Area &area, Pair pair) {
    Vertex tight[4];
    Corners(tight, area, pair);
    for (UINT i = 0; i < 4; ++i)
        out[i] = {tight[i].x,       tight[i].y,        tight[i].z,
                  tight[i].diffuse, tight[i].specular, 0x5aa5c33c};
}
static constexpr Pair DecoyPair = {Decoy, Decoy};

/* Fixed function (vbuffer-probe.inc) with the specular added. */
static BOOL Specular(Device *dev) {
    return vbuffer_probe::Fixed(dev, Fvf) &&
           HR(DEVICE(SetRenderState, dev, D3DRS_SPECULARENABLE, TRUE), "FAIL BGRA specular enable");
}
/* A static WRITEONLY default-pool index buffer holding these 16-bit indices. */
static BOOL Indices(Device *dev, const WORD *values, UINT bytes, IndexBuffer **out) {
    BufferPointer pointer = NULL;
    if (!HR(IBUFFER_CREATE(dev, bytes, D3DUSAGE_WRITEONLY, D3DPOOL_DEFAULT, out),
            "FAIL BGRA index buffer creation") ||
        !HR(IBUFFER(Lock, *out, 0, 0, &pointer, 0), "FAIL BGRA index buffer lock"))
        return FALSE;
    const BOOL mapped = Check(pointer != NULL, "FAIL BGRA index buffer pointer", 0);
    if (mapped)
        vbuffer_probe::Copy(pointer, values, bytes);
    return HR(IBUFFER(Unlock, *out), "FAIL BGRA index buffer unlock") && mapped;
}

static void UserCase(Device *dev, Surface *target) {
    static const WORD list[6] = {4, 5, 6, 6, 5, 7}, strip[4] = {4, 5, 6, 7};
    const Area quads[4] = {Quad(0, 0), Quad(1, 1), Quad(2, 2), Quad(3, 3)};
    Vertex tight[4], indexed[8];
    Padded padded[4], padded_indexed[8];
    Log("STAGE BGRA colour arrays user memory");
    Corners(tight, quads[0], Pairs[0]);
    Corners(padded, quads[1], Pairs[1]);
    Corners(indexed, DecoyPlace, DecoyPair);
    Corners(indexed + 4, quads[2], Pairs[2]);
    Corners(padded_indexed, DecoyPlace, DecoyPair);
    Corners(padded_indexed + 4, quads[3], Pairs[3]);
    const BOOL ok =
        Specular(dev) &&
        Frame(dev, target, "BGRA user memory: UP 20/24-byte strides, indexed UP with decoys", quads,
              4, [&]() -> BOOL {
                  return HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, tight,
                                   sizeof(Vertex)),
                            "FAIL BGRA tight UP draw") &&
                         HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, padded,
                                   sizeof(Padded)),
                            "FAIL BGRA padded UP draw") &&
                         HR(DEVICE(DrawIndexedPrimitiveUP, dev, D3DPT_TRIANGLELIST, 0, 8, 2, list,
                                   D3DFMT_INDEX16, indexed, sizeof(Vertex)),
                            "FAIL BGRA indexed UP list") &&
                         HR(DEVICE(DrawIndexedPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 0, 8, 2, strip,
                                   D3DFMT_INDEX16, padded_indexed, sizeof(Padded)),
                            "FAIL BGRA indexed UP strip");
              });
    if (ok)
        Log("PASS BGRA colour arrays user memory: DrawPrimitiveUP tight and padded, "
            "DrawIndexedPrimitiveUP list and strip, diffuse plus specular exact, decoys unseen");
    vbuffer_probe::Unbind(dev);
}

static void DynamicCase(Device *dev, Surface *target) {
    static const WORD list[6] = {4, 5, 6, 6, 5, 7};
    const Area quads[4] = {Quad(0, 4), Quad(1, 5), Quad(2, 6), Quad(3, 7)};
    VertexBuffer *vb = NULL;
    IndexBuffer *ib = NULL;
    Log("STAGE BGRA colour arrays dynamic buffer");
    /* Four-vertex slots: 0 and 1 are quads 0 and 1, 2 a decoy, 3 and 4 quads 2
     * and 3 (base vertices 8 and 12 with indices 4-7). */
    const auto write = [&](UINT slot, const Area &area, Pair pair) -> BOOL {
        Vertex quad[4];
        Corners(quad, area, pair);
        return Write(vb, slot * sizeof(quad), sizeof(quad), quad, sizeof(quad),
                     slot ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD,
                     slot ? "FAIL BGRA dynamic NOOVERWRITE lock"
                          : "FAIL BGRA dynamic DISCARD lock");
    };
    const BOOL ok =
        HR(VBUFFER_CREATE(dev, 20 * sizeof(Vertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, Fvf,
                          D3DPOOL_DEFAULT, &vb),
           "FAIL BGRA dynamic vertex buffer creation") &&
        Indices(dev, list, sizeof(list), &ib) && Specular(dev) &&
        HR(VBUFFER_STREAM(dev, vb, sizeof(Vertex)), "FAIL BGRA dynamic stream source") &&
        Frame(dev, target, "BGRA dynamic: DISCARD/NOOVERWRITE, base vertices 8 and 12", quads, 4,
              [&]() -> BOOL {
                  return write(0, quads[0], Pairs[4]) && vbuffer_probe::Strip(dev, 0) &&
                         write(1, quads[1], Pairs[5]) && vbuffer_probe::Strip(dev, 4) &&
                         write(2, DecoyPlace, DecoyPair) && write(3, quads[2], Pairs[6]) &&
                         vbuffer_probe::DrawIndexed(dev, ib, 8, 4, 4, 0, 2) &&
                         write(4, quads[3], Pairs[7]) &&
                         vbuffer_probe::DrawIndexed(dev, ib, 12, 4, 4, 0, 2);
              });
    if (ok)
        Log("PASS BGRA colour arrays dynamic buffer: DISCARD/NOOVERWRITE quads by DrawPrimitive "
            "and DrawIndexedPrimitive with base vertices 8 and 12, diffuse plus specular exact, "
            "decoy unseen");
    vbuffer_probe::Unbind(dev);
    if (ib)
        IBUFFER(Release, ib);
    if (vb)
        VBUFFER(Release, vb);
}

static void StaticCase(Device *dev, Surface *target) {
    /* Relative to base vertex 8: quads 1 and 2 from start 0, then quad 3
     * (vertices 16-19) from base vertex 12 and start index 6. */
    static const WORD list[12] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
    const Area quads[4] = {Quad(0, 1), Quad(1, 3), Quad(2, 5), Quad(3, 0)};
    Padded vertices[20];
    VertexBuffer *vb = NULL;
    IndexBuffer *ib = NULL;
    Log("STAGE BGRA colour arrays static buffer");
    Corners(vertices, DecoyPlace, DecoyPair);
    Corners(vertices + 4, quads[0], Pairs[1]);
    Corners(vertices + 8, quads[1], Pairs[3]);
    Corners(vertices + 12, quads[2], Pairs[5]);
    Corners(vertices + 16, quads[3], Pairs[0]);
    const BOOL ok =
        HR(VBUFFER_CREATE(dev, sizeof(vertices), D3DUSAGE_WRITEONLY, Fvf, D3DPOOL_DEFAULT, &vb),
           "FAIL BGRA static vertex buffer creation") &&
        Write(vb, 0, 0, vertices, sizeof(vertices), 0, "FAIL BGRA static vertex buffer lock") &&
        Indices(dev, list, sizeof(list), &ib) && Specular(dev) &&
        HR(VBUFFER_STREAM(dev, vb, sizeof(Padded)), "FAIL BGRA static stream source") &&
        Frame(dev, target, "BGRA static: 24-byte stride, start vertex 4, base vertices 8 and 12",
              quads, 4, [&]() -> BOOL {
                  return vbuffer_probe::Strip(dev, 4) &&
                         vbuffer_probe::DrawIndexed(dev, ib, 8, 0, 8, 0, 4) &&
                         vbuffer_probe::DrawIndexed(dev, ib, 12, 4, 4, 6, 2);
              });
    if (ok)
        Log("PASS BGRA colour arrays static buffer: 24-byte stride from start vertex 4, base "
            "vertices 8 and 12 and start index 6, diffuse plus specular exact, decoy unseen");
    vbuffer_probe::Unbind(dev);
    if (ib)
        IBUFFER(Release, ib);
    if (vb)
        VBUFFER(Release, vb);
}

static void ShaderCase(Device *dev, Surface *target) {
    /* Rows c0-c3 (vshader-probe.inc): the unit square onto the left or the
     * right half of the backbuffer. */
    static const float right[16] = {1, 0, 0, 0, 0, 2, 0, -1, 0, 0, 1, 0, 0, 0, 0, 1};
    static const Area halves[2] = {{0, 0, 160, 240, 0xff2080c0}, {160, 0, 320, 240, 0xffc04018}};
    vshader_probe::Vertex still[4], dynamic[4];
    VertexBuffer *still_vb = NULL, *dynamic_vb = NULL;
    VertexShader shader = VSHADER_NONE;
    void *layout = NULL;
    StageCaps caps = {};
    Log("STAGE BGRA colour arrays vertex shader");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL BGRA vertex shader caps"))
        return;
    if ((caps.VertexShaderVersion & 0xffff) < (D3DVS_VERSION(1, 1) & 0xffff)) {
        Log("SKIP BGRA colour arrays vertex shader: VertexShaderVersion below 1.1");
        return;
    }
    for (UINT i = 0; i < 4; ++i) {
        const float u = (float)(i & 1), v = (float)(i >> 1);
        still[i] = {u, v, .5f, halves[0].color, u, v};
        dynamic[i] = {u, v, .5f, halves[1].color, u, v};
    }
    const BOOL ok =
        HR(VBUFFER_CREATE(dev, sizeof(still), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &still_vb),
           "FAIL BGRA shader static buffer creation") &&
        Write(still_vb, 0, 0, still, sizeof(still), 0, "FAIL BGRA shader static buffer lock") &&
        HR(VBUFFER_CREATE(dev, sizeof(dynamic), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                          D3DPOOL_DEFAULT, &dynamic_vb),
           "FAIL BGRA shader dynamic buffer creation") &&
        vbuffer_probe::Fixed(dev, D3DFVF_XYZ | D3DFVF_DIFFUSE) && vshader_probe::Diffuse(dev) &&
        vshader_probe::Bind(dev, vshader_probe::vs11, &shader, &layout) &&
        Frame(dev, target, "BGRA vertex shader: static left half, dynamic right half", halves, 2,
              [&]() -> BOOL {
                  return Write(dynamic_vb, 0, 0, dynamic, sizeof(dynamic), D3DLOCK_DISCARD,
                               "FAIL BGRA shader dynamic buffer lock") &&
                         HR(VBUFFER_STREAM(dev, still_vb, sizeof(still[0])),
                            "FAIL BGRA shader static stream") &&
                         HR(VSHADER_CONSTANT(dev, 0, vshader_probe::half, 4),
                            "FAIL BGRA shader left rows") &&
                         vbuffer_probe::Strip(dev, 0) &&
                         HR(VBUFFER_STREAM(dev, dynamic_vb, sizeof(dynamic[0])),
                            "FAIL BGRA shader dynamic stream") &&
                         HR(VSHADER_CONSTANT(dev, 0, right, 4), "FAIL BGRA shader right rows") &&
                         vbuffer_probe::Strip(dev, 0);
              });
    if (ok)
        Log("PASS BGRA colour arrays vertex shader: vs_1_1 D3DCOLOR v1 from static and dynamic "
            "buffers to oD0 exact");
    vshader_probe::Unbind(dev, shader, layout);
    vbuffer_probe::Unbind(dev);
    if (dynamic_vb)
        VBUFFER(Release, dynamic_vb);
    if (still_vb)
        VBUFFER(Release, still_vb);
}

/* Every case runs and logs its own result; any failure fails the probe. */
static void Run(Device *dev, Surface *target) {
    Log("STAGE BGRA colour arrays");
    UserCase(dev, target);
    DynamicCase(dev, target);
    StaticCase(dev, target);
    ShaderCase(dev, target);
    DEVICE(SetRenderState, dev, D3DRS_SPECULARENABLE, FALSE);
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

} // namespace bgra_probe
