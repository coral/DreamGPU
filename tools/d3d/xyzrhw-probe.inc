/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pretransformed (XYZRHW) vertices outside buffer objects through the public
 * D3D8/9 API. Included by probe.cpp after bgra-probe.inc; reuses the frames,
 * buffers and indexed draws of vbuffer-probe.inc and the textures of
 * stage-probe.inc. Wine drew every such draw in immediate mode; with Wine 0048
 * they are array draws whose positions are converted as immediate mode
 * converted them (x, y, z and 1 over rhw). Every case has rhw other than 1, so
 * unconverted positions or a wrong 1/rhw show. Whole frames are compared as
 * in vbuffer-probe.inc. Cases:
 * - batch: one 312-vertex DrawPrimitiveUP triangle list (larger than the
 *   64 vertices of 0006's packed path) of 52 cells with rhw from 0.25 to 2
 *   per vertex, red diffuse plus red specular (SPECULARENABLE) fogged toward
 *   blue by the specular alpha (FOGVERTEXMODE and FOGTABLEMODE NONE). Each
 *   cell has its own alpha a, so it is exactly (a, 0, 255 - a); a dropped
 *   specular, a fog coordinate from another vertex or a moved cell shows.
 * - indexed: DrawIndexedPrimitiveUP with four decoy vertices before the quad,
 *   a DYNAMIC (system memory in this Wine) and a SYSTEMMEM vertex buffer
 *   drawn by DrawIndexedPrimitive from a static index buffer with base
 *   vertices 4, 8 and 4 and start indices 6 and 0. Ignoring the base vertex
 *   or the start index draws a decoy or misplaces a quad.
 * - mixed streams (D3D9): POSITIONT from a static buffer (a GL buffer object)
 *   and D3DCOLOR from a SYSTEMMEM buffer through a vertex declaration, from
 *   start vertex 4.
 * - perspective: a 322-vertex DrawPrimitiveUP strip of 160 slices forming one
 *   quad whose rhw grows from 1 on the left to 3 on the right, with two
 *   point-sampled 4x4 textures of two colour columns on two stages (stage 0
 *   selects texture 0 by coordinate set 0, stage 1 adds texture 1 by set 1,
 *   which runs the other way). The perspective-correct texel boundary is a
 *   quarter of the way across (u = 3s / (1 + 2s) = 1/2); an affine or
 *   inverted 1/rhw puts it at a half or three quarters, a coordinate set on
 *   the wrong unit swaps a texture's colours. Below two stages or without
 *   D3DTOP_ADD the case is skipped.
 */
namespace xyzrhw_probe {

using vbuffer_probe::Area;
using vbuffer_probe::Decoy;
using vbuffer_probe::Frame;
using vbuffer_probe::Write;

struct Vertex {
    float x, y, z, rhw;
    DWORD diffuse, specular;
};
struct Textured {
    float x, y, z, rhw;
    DWORD diffuse;
    float u0, v0, u1, v1;
};
static_assert(sizeof(Vertex) == 24 && sizeof(Textured) == 36);
static constexpr DWORD Fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR;
static constexpr DWORD TexturedFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2;

/* Strip-ordered screen corners of area with four different rhw. */
static void Corners(Vertex *out, const Area &area, DWORD diffuse, DWORD specular) {
    const float left = (float)area.left, right = (float)area.right;
    const float top = (float)area.top, bottom = (float)area.bottom;
    out[0] = {left, top, .5f, .5f, diffuse, specular};
    out[1] = {right, top, .5f, 2.f, diffuse, specular};
    out[2] = {left, bottom, .5f, 4.f, diffuse, specular};
    out[3] = {right, bottom, .5f, .25f, diffuse, specular};
}

/* Linear fog from 255 (clear) to 0 (fog colour) of red diffuse 0x80 plus red
 * specular 0x7f toward blue: f = a / 255 of red 255 and 1 - f of blue. */
static constexpr DWORD Fogged(DWORD alpha) {
    return 0xff000000 | alpha << 16 | (255 - alpha);
}
/* Each cell's distinct alpha (79 is odd). */
static constexpr DWORD CellAlpha(UINT cell) {
    return (cell * 79 + 13) & 0xff;
}
static Area Cell(UINT cell) {
    const int column = (int)(cell % 13), row = (int)(cell / 13);
    return {4 + 24 * column, 10 + 58 * row, 24 + 24 * column, 50 + 58 * row, Fogged(CellAlpha(cell))};
}

static void BatchCase(Device *dev, Surface *target) {
    static Vertex vertices[312];
    Area cells[52];
    Log("STAGE XYZRHW arrays batch");
    for (UINT i = 0; i < 52; ++i) {
        static const UINT order[6] = {0, 1, 2, 2, 1, 3};
        Vertex corners[4];
        cells[i] = Cell(i);
        Corners(corners, cells[i], 0xff800000, CellAlpha(i) << 24 | 0x007f0000);
        for (UINT k = 0; k < 6; ++k) {
            vertices[6 * i + k] = corners[order[k]];
            /* rhw 0.25 to 2, 1 among them. */
            vertices[6 * i + k].rhw = .25f * (float)(1 + (i + k) % 8);
        }
    }
    const BOOL ok =
        vbuffer_probe::Fixed(dev, Fvf) &&
        HR(DEVICE(SetRenderState, dev, D3DRS_SPECULARENABLE, TRUE), "FAIL XYZRHW specular") &&
        HR(DEVICE(SetRenderState, dev, D3DRS_FOGCOLOR, 0xff0000ff), "FAIL XYZRHW fog colour") &&
        HR(DEVICE(SetRenderState, dev, D3DRS_FOGTABLEMODE, D3DFOG_NONE), "FAIL XYZRHW table fog") &&
        HR(DEVICE(SetRenderState, dev, D3DRS_FOGVERTEXMODE, D3DFOG_NONE),
           "FAIL XYZRHW vertex fog") &&
        HR(DEVICE(SetRenderState, dev, D3DRS_FOGENABLE, TRUE), "FAIL XYZRHW fog enable") &&
        Frame(dev, target, "XYZRHW batch: 312 vertices, fogged diffuse plus specular", cells, 52,
              [&]() -> BOOL {
                  return HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLELIST, 104, vertices,
                                   sizeof(Vertex)),
                            "FAIL XYZRHW batch draw");
              });
    DEVICE(SetRenderState, dev, D3DRS_FOGENABLE, FALSE);
    DEVICE(SetRenderState, dev, D3DRS_SPECULARENABLE, FALSE);
    if (ok)
        Log("PASS XYZRHW arrays batch: 312-vertex DrawPrimitiveUP list of 52 cells, rhw 0.25 to "
            "2, specular plus specular-alpha fog exact per cell");
}

/* Quads 0-3 and the decoy's place. */
static constexpr Area Places[4] = {{20, 30, 90, 110, 0xff60c020},
                                   {110, 30, 180, 110, 0xff2080e0},
                                   {200, 30, 270, 110, 0xffe04080},
                                   {20, 140, 90, 220, 0xff40e0a0}};
static constexpr Area DecoyPlace = {200, 140, 300, 220, Decoy};

/* A static WRITEONLY default-pool index buffer of these 16-bit indices. */
static BOOL Indices(Device *dev, const WORD *values, UINT bytes, IndexBuffer **out) {
    BufferPointer pointer = NULL;
    if (!HR(IBUFFER_CREATE(dev, bytes, D3DUSAGE_WRITEONLY, D3DPOOL_DEFAULT, out),
            "FAIL XYZRHW index buffer creation") ||
        !HR(IBUFFER(Lock, *out, 0, 0, &pointer, 0), "FAIL XYZRHW index buffer lock"))
        return FALSE;
    const BOOL mapped = Check(pointer != NULL, "FAIL XYZRHW index buffer pointer", 0);
    if (mapped)
        vbuffer_probe::Copy(pointer, values, bytes);
    return HR(IBUFFER(Unlock, *out), "FAIL XYZRHW index buffer unlock") && mapped;
}

static void IndexedCase(Device *dev, Surface *target) {
    static const WORD list[12] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7}, up[6] = {4, 5, 6, 6, 5, 7};
    Vertex user[8], dynamic[16], system[8];
    VertexBuffer *dynamic_vb = NULL, *system_vb = NULL;
    IndexBuffer *ib = NULL;
    Log("STAGE XYZRHW arrays indexed");
    /* User memory: decoy 0-3, quad 0 at 4-7. Dynamic: decoys 0-7, quad 1 at
     * 8-11, quad 2 at 12-15. System memory: decoy 0-3, quad 3 at 4-7. */
    Corners(user, DecoyPlace, Decoy, 0);
    Corners(user + 4, Places[0], Places[0].color, 0);
    Corners(dynamic, DecoyPlace, Decoy, 0);
    Corners(dynamic + 4, DecoyPlace, Decoy, 0);
    Corners(dynamic + 8, Places[1], Places[1].color, 0);
    Corners(dynamic + 12, Places[2], Places[2].color, 0);
    Corners(system, DecoyPlace, Decoy, 0);
    Corners(system + 4, Places[3], Places[3].color, 0);
    const BOOL ok =
        HR(VBUFFER_CREATE(dev, sizeof(dynamic), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, Fvf,
                          D3DPOOL_DEFAULT, &dynamic_vb),
           "FAIL XYZRHW dynamic vertex buffer creation") &&
        Write(dynamic_vb, 0, 0, dynamic, sizeof(dynamic), D3DLOCK_DISCARD,
              "FAIL XYZRHW dynamic vertex buffer lock") &&
        HR(VBUFFER_CREATE(dev, sizeof(system), 0, Fvf, D3DPOOL_SYSTEMMEM, &system_vb),
           "FAIL XYZRHW system-memory vertex buffer creation") &&
        Write(system_vb, 0, 0, system, sizeof(system), 0,
              "FAIL XYZRHW system-memory vertex buffer lock") &&
        Indices(dev, list, sizeof(list), &ib) && vbuffer_probe::Fixed(dev, Fvf) &&
        Frame(dev, target, "XYZRHW indexed: UP decoys, base vertices 4/8/4, start indices 6/6/0",
              Places, 4, [&]() -> BOOL {
                  return HR(DEVICE(DrawIndexedPrimitiveUP, dev, D3DPT_TRIANGLELIST, 0, 8, 2, up,
                                   D3DFMT_INDEX16, user, sizeof(Vertex)),
                            "FAIL XYZRHW indexed UP draw") &&
                         HR(VBUFFER_STREAM(dev, dynamic_vb, sizeof(Vertex)),
                            "FAIL XYZRHW dynamic stream") &&
                         vbuffer_probe::DrawIndexed(dev, ib, 4, 4, 4, 6, 2) &&
                         vbuffer_probe::DrawIndexed(dev, ib, 8, 4, 4, 6, 2) &&
                         HR(VBUFFER_STREAM(dev, system_vb, sizeof(Vertex)),
                            "FAIL XYZRHW system-memory stream") &&
                         vbuffer_probe::DrawIndexed(dev, ib, 4, 0, 4, 0, 2);
              });
    if (ok)
        Log("PASS XYZRHW arrays indexed: DrawIndexedPrimitiveUP, dynamic and SYSTEMMEM buffers "
            "with base vertices 4 and 8 and start indices 0 and 6 exact, decoys unseen");
    vbuffer_probe::Unbind(dev);
    if (ib)
        IBUFFER(Release, ib);
    if (system_vb)
        VBUFFER(Release, system_vb);
    if (dynamic_vb)
        VBUFFER(Release, dynamic_vb);
}

#if DG_D3D_VERSION == 9
static void MixedCase(Device *dev, Surface *target) {
    static const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0},
        {1, 0, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        D3DDECL_END()};
    struct Position {
        float x, y, z, rhw;
    } positions[8];
    DWORD colors[8];
    Vertex corners[8];
    IDirect3DVertexDeclaration9 *declaration = NULL;
    VertexBuffer *position_vb = NULL, *color_vb = NULL;
    Log("STAGE XYZRHW arrays mixed streams");
    Corners(corners, DecoyPlace, Decoy, 0);
    Corners(corners + 4, Places[1], Places[1].color, 0);
    for (UINT i = 0; i < 8; ++i) {
        positions[i] = {corners[i].x, corners[i].y, corners[i].z, corners[i].rhw};
        colors[i] = corners[i].diffuse;
    }
    const BOOL ok =
        HR(VBUFFER_CREATE(dev, sizeof(positions), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                          &position_vb),
           "FAIL XYZRHW static position buffer creation") &&
        Write(position_vb, 0, 0, positions, sizeof(positions), 0,
              "FAIL XYZRHW static position buffer lock") &&
        HR(VBUFFER_CREATE(dev, sizeof(colors), 0, 0, D3DPOOL_SYSTEMMEM, &color_vb),
           "FAIL XYZRHW system-memory colour buffer creation") &&
        Write(color_vb, 0, 0, colors, sizeof(colors), 0,
              "FAIL XYZRHW system-memory colour buffer lock") &&
        vbuffer_probe::Fixed(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) &&
        HR(DEVICE(CreateVertexDeclaration, dev, elements, &declaration),
           "FAIL XYZRHW vertex declaration") &&
        HR(DEVICE(SetVertexDeclaration, dev, declaration), "FAIL XYZRHW declaration") &&
        HR(DEVICE(SetStreamSource, dev, 0, position_vb, 0, sizeof(Position)),
           "FAIL XYZRHW position stream") &&
        HR(DEVICE(SetStreamSource, dev, 1, color_vb, 0, sizeof(DWORD)),
           "FAIL XYZRHW colour stream") &&
        Frame(dev, target, "XYZRHW mixed streams: static POSITIONT, SYSTEMMEM colour", &Places[1],
              1, [&]() -> BOOL { return vbuffer_probe::Strip(dev, 4); });
    if (ok)
        Log("PASS XYZRHW arrays mixed streams: static POSITIONT and SYSTEMMEM D3DCOLOR streams "
            "from start vertex 4 exact, decoy unseen");
    DEVICE(SetStreamSource, dev, 1, NULL, 0, 0);
    vbuffer_probe::Unbind(dev);
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    if (declaration)
        IDirect3DVertexDeclaration9_Release(declaration);
    if (color_vb)
        VBUFFER(Release, color_vb);
    if (position_vb)
        VBUFFER(Release, position_vb);
}
#endif

/* A 4x4 texture whose left two columns are left and right two are right. */
static BOOL Columns(Device *dev, StageTexture **texture, DWORD left, DWORD right) {
    D3DLOCKED_RECT locked;
    if (!HR(STAGE_CREATE_TEXTURE(dev, texture), "FAIL XYZRHW texture creation") ||
        !HR(STAGE_TEXTURE(LockRect, *texture, 0, &locked, NULL, 0), "FAIL XYZRHW texture lock"))
        return FALSE;
    for (UINT y = 0; y < 4; ++y)
        for (UINT x = 0; x < 4; ++x)
            *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4) = x < 2 ? left : right;
    return HR(STAGE_TEXTURE(UnlockRect, *texture, 0), "FAIL XYZRHW texture unlock");
}
static BOOL Address(Device *dev, DWORD stage, DWORD mode) {
#if DG_D3D_VERSION == 8
    return HR(DEVICE(SetTextureStageState, dev, stage, D3DTSS_ADDRESSU, mode) |
                  DEVICE(SetTextureStageState, dev, stage, D3DTSS_ADDRESSV, mode),
              "FAIL XYZRHW texture addressing");
#else
    return HR(DEVICE(SetSamplerState, dev, stage, D3DSAMP_ADDRESSU, mode) |
                  DEVICE(SetSamplerState, dev, stage, D3DSAMP_ADDRESSV, mode),
              "FAIL XYZRHW texture addressing");
#endif
}

/* The quad's screen rectangle and rhw at its left and right edges. */
static constexpr float Left = 40, Right = 280, Top = 40, Bottom = 200, RhwLeft = 1, RhwRight = 3;
/* At screen fraction s, u = s RhwRight / (s RhwRight + (1 - s) RhwLeft):
 * 1/2 at s = RhwLeft / (RhwLeft + RhwRight) = 1/4, pixel 100. */
static constexpr int Boundary = 100;
static_assert(Left + (Right - Left) * RhwLeft / (RhwLeft + RhwRight) == Boundary);
static constexpr DWORD Texture0[2] = {0xff402010, 0xff104020}, Texture1[2] = {0xff205080, 0xff803008};
/* Stage 1 adds texture 1, whose set runs right to left, to texture 0. */
static constexpr DWORD LeftColor = 0xffc05018, RightColor = 0xff3090a0;
static_assert((LeftColor & 0xffffff) == (Texture0[0] & 0xffffff) + (Texture1[1] & 0xffffff) &&
              (RightColor & 0xffffff) == (Texture0[1] & 0xffffff) + (Texture1[0] & 0xffffff));

/* Slice i of 160: rhw and u * rhw are linear in screen space, so each slice
 * interpolates the same perspective-correct coordinates as the whole quad. */
static void Strip(Textured *out) {
    for (UINT i = 0; i <= 160; ++i) {
        const float s = (float)i / 160, x = Left + s * (Right - Left);
        const float rhw = RhwLeft + s * (RhwRight - RhwLeft), u = s * RhwRight / rhw;
        out[2 * i] = {x, Top, .5f, rhw, 0xffffffff, u, .5f, 1 - u, .5f};
        out[2 * i + 1] = {x, Bottom, .5f, rhw, 0xffffffff, u, .5f, 1 - u, .5f};
    }
}

static void PerspectiveCase(Device *dev, Surface *target) {
    static Textured strip[322];
    static const Area halves[2] = {{(int)Left, (int)Top, Boundary, (int)Bottom, LeftColor},
                                   {Boundary, (int)Top, (int)Right, (int)Bottom, RightColor}};
    StageTexture *textures[2] = {};
    StageCaps caps = {};
    Log("STAGE XYZRHW arrays perspective two units");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL XYZRHW device caps"))
        return;
    if (caps.MaxSimultaneousTextures < 2 || !(caps.TextureOpCaps & D3DTEXOPCAPS_ADD)) {
        Number("SKIP XYZRHW perspective: MaxSimultaneousTextures", caps.MaxSimultaneousTextures);
        return;
    }
    Strip(strip);
    BOOL ok = Columns(dev, &textures[0], Texture0[0], Texture0[1]) &&
              Columns(dev, &textures[1], Texture1[0], Texture1[1]) &&
              vbuffer_probe::Fixed(dev, TexturedFvf);
    for (DWORD stage = 0; stage < 2 && ok; ++stage)
        ok = HR(STAGE_SET_TEXTURE(dev, stage, textures[stage]), "FAIL XYZRHW texture bind") &&
             HR(STAGE_POINT_FILTER(dev, stage), "FAIL XYZRHW point filter") &&
             Address(dev, stage, D3DTADDRESS_CLAMP) &&
             HR(DEVICE(SetTextureStageState, dev, stage, D3DTSS_TEXCOORDINDEX, stage),
                "FAIL XYZRHW coordinate set") &&
             HR(DEVICE(SetTextureStageState, dev, stage, D3DTSS_COLORARG1, D3DTA_TEXTURE),
                "FAIL XYZRHW colour argument") &&
             HR(DEVICE(SetTextureStageState, dev, stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
                "FAIL XYZRHW alpha operation") &&
             HR(DEVICE(SetTextureStageState, dev, stage, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE),
                "FAIL XYZRHW alpha argument");
    ok = ok &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL XYZRHW stage 0") &&
         HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_ADD),
            "FAIL XYZRHW stage 1") &&
         HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLORARG2, D3DTA_CURRENT),
            "FAIL XYZRHW stage 1 current") &&
         HR(DEVICE(SetTextureStageState, dev, 2, D3DTSS_COLOROP, D3DTOP_DISABLE),
            "FAIL XYZRHW stage 2") &&
         HR(DEVICE(SetTextureStageState, dev, 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE),
            "FAIL XYZRHW stage 2 alpha") &&
         Frame(dev, target, "XYZRHW perspective: 322-vertex strip, rhw 1 to 3, two units", halves,
               2, [&]() -> BOOL {
                   return HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 320, strip,
                                    sizeof(Textured)),
                             "FAIL XYZRHW perspective draw");
               });
    if (ok)
        Log("PASS XYZRHW arrays perspective two units: 322-vertex strip with rhw 1 to 3, "
            "perspective-correct texel boundary of both coordinate sets exact");
    for (DWORD stage = 0; stage < 2; ++stage) {
        STAGE_SET_TEXTURE(dev, stage, NULL);
        DEVICE(SetTextureStageState, dev, stage, D3DTSS_TEXCOORDINDEX, stage);
        Address(dev, stage, D3DTADDRESS_WRAP);
    }
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    DEVICE(SetTextureStageState, dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    for (StageTexture *texture : textures)
        if (texture)
            STAGE_TEXTURE(Release, texture);
}

/* Every case runs and logs its own result; any failure fails the probe. */
static void Run(Device *dev, Surface *target) {
    Log("STAGE XYZRHW arrays");
    BatchCase(dev, target);
    IndexedCase(dev, target);
#if DG_D3D_VERSION == 9
    MixedCase(dev, target);
#endif
    PerspectiveCase(dev, target);
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

} // namespace xyzrhw_probe
