/* SPDX-License-Identifier: GPL-2.0-or-later
 * Vertex shaders through the public D3D8/9 API. Included by probe.cpp after
 * shader-probe.inc, whose ps_1_1 shader, texture fill and constants it reuses.
 * Shaders are hand-assembled token streams (no D3DX, no compiler). vs_1_1
 * transforms a unit-square quad by the rows c0-c3 and passes the D3DCOLOR
 * input v1 to oD0 and the coordinates v2 to oT0. One constant set covers the
 * backbuffer, another only its left half, so ignored, stale or swapped
 * constants fail. D3D8 binds inputs through a shader declaration, D3D9 through
 * a vertex declaration and dcl instructions. On D3D9 a vs_2_0 shader copies
 * v0 through r0 and multiplies the colour by an unclamped c4, exact for two
 * constants. One case feeds ps_1_1. Below VertexShaderVersion 1.1 the probe
 * records the caps and skips.
 */
#if DG_D3D_VERSION == 8
typedef DWORD VertexShader;
#define VSHADER_NONE 0
#define VSHADER_CONSTANT(device, start, values, count)                                             \
    DEVICE(SetVertexShaderConstant, device, start, values, count)
#define VSHADER_SET(device, shader) DEVICE(SetVertexShader, device, shader)
#define VSHADER_DELETE(device, shader) DEVICE(DeleteVertexShader, device, shader)
#else
typedef IDirect3DVertexShader9 *VertexShader;
#define VSHADER_NONE NULL
#define VSHADER_CONSTANT(device, start, values, count)                                             \
    DEVICE(SetVertexShaderConstantF, device, start, values, count)
#define VSHADER_SET(device, shader) DEVICE(SetVertexShader, device, shader)
#define VSHADER_DELETE(device, shader) IDirect3DVertexShader9_Release(shader)
#endif

namespace vshader_probe {

/* Register types (D3DSPR_*) shared by both SDKs below type 8. */
static constexpr DWORD RegisterTemp = 0, RegisterInput = 1, RegisterConst = 2,
                       RegisterRasterOut = 4, RegisterAttributeOut = 5, RegisterTexCoordOut = 6;
static constexpr DWORD Param(DWORD type, DWORD number) {
    return 0x80000000u | (type & 7) << 28 | number;
}
/* Destination: write mask nibble (x=1 y=2 z=4 w=8) in bits 16-19. */
static constexpr DWORD Dst(DWORD type, DWORD number, DWORD mask) {
    return Param(type, number) | mask << 16;
}
/* Source: identity swizzle .xyzw = 0xe4 in bits 16-23. */
static constexpr DWORD Src(DWORD type, DWORD number) {
    return Param(type, number) | 0xe4u << 16;
}
/* Opcode in bits 0-15; from 2.0 the number of following tokens in 24-27. */
static constexpr DWORD Instruction(DWORD opcode, DWORD length) {
    return opcode | length << 24;
}
static_assert(Param(RegisterInput, 0) == SHADER_HEADER_PARAM(D3DSPR_INPUT));
static_assert(Param(RegisterConst, 0) == SHADER_HEADER_PARAM(D3DSPR_CONST));
static_assert(Param(RegisterRasterOut, 0) == SHADER_HEADER_PARAM(D3DSPR_RASTOUT));
static_assert(Param(RegisterAttributeOut, 0) == SHADER_HEADER_PARAM(D3DSPR_ATTROUT));
static_assert(Param(RegisterTexCoordOut, 0) == SHADER_HEADER_PARAM(D3DSPR_TEXCRDOUT));
static_assert(Param(RegisterTemp, 0) == SHADER_HEADER_PARAM(D3DSPR_TEMP));

/* vs_1_1: dp4 oPos.x..w, v0, c0..c3; mov oD0, v1; mov oT0, v2. D3D9 first
 * declares v0-v2 (dcl: opcode 31, then a usage token and the register). */
static constexpr DWORD vs11[] = {
    0xfffe0101, /* vs_1_1: 0xfffe vertex, major 1, minor 1 */
#if DG_D3D_VERSION == 9
    0x0000001f, /* dcl, no length below 2.0 */
    0x80000000, /*   D3DDECLUSAGE_POSITION (0), index 0 */
    0x900f0000, /*   v0.xyzw: bit 31, type 1 << 28, mask 0xf << 16 */
    0x0000001f, /* dcl */
    0x8000000a, /*   D3DDECLUSAGE_COLOR (10), index 0 */
    0x900f0001, /*   v1 */
    0x0000001f, /* dcl */
    0x80000005, /*   D3DDECLUSAGE_TEXCOORD (5), index 0 */
    0x900f0002, /*   v2 */
#endif
    0x00000009, /* dp4: opcode 9 */
    0xc0010000, /*   oPos.x: bit 31, type 4 << 28, mask x */
    0x90e40000, /*   v0: type 1, swizzle 0xe4 */
    0xa0e40000, /*   c0: type 2 */
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
    0x00000001, /* mov: opcode 1 */
    0xd00f0000, /*   oD0.xyzw: type 5 << 28 */
    0x90e40001, /*   v1 */
    0x00000001, /* mov */
    0xe00f0000, /*   oT0.xyzw: type 6 << 28 */
    0x90e40002, /*   v2 */
    0x0000ffff, /* end */
};
static constexpr DWORD vs11_body = DG_D3D_VERSION == 9 ? 10 : 1;
static_assert(vs11[0] == D3DVS_VERSION(1, 1) && vs11[sizeof(vs11) / 4 - 1] == D3DVS_END());
static_assert(vs11[vs11_body] == Instruction(D3DSIO_DP4, 0) &&
              vs11[vs11_body + 1] == Dst(RegisterRasterOut, 0, 1) &&
              vs11[vs11_body + 2] == Src(RegisterInput, 0) &&
              vs11[vs11_body + 3] == Src(RegisterConst, 0));
static_assert(vs11[vs11_body + 12] == Instruction(D3DSIO_DP4, 0) &&
              vs11[vs11_body + 13] == Dst(RegisterRasterOut, 0, 8) &&
              vs11[vs11_body + 15] == Src(RegisterConst, 3));
static_assert(vs11[vs11_body + 16] == Instruction(D3DSIO_MOV, 0) &&
              vs11[vs11_body + 17] == Dst(RegisterAttributeOut, 0, 0xf) &&
              vs11[vs11_body + 18] == Src(RegisterInput, 1));
static_assert(vs11[vs11_body + 19] == Instruction(D3DSIO_MOV, 0) &&
              vs11[vs11_body + 20] == Dst(RegisterTexCoordOut, 0, 0xf) &&
              vs11[vs11_body + 21] == Src(RegisterInput, 2));

#if DG_D3D_VERSION == 8
/* Stream 0: v0 position FLOAT3, v1 D3DCOLOR, v2 FLOAT2. */
static const DWORD declaration[] = {D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3),
                                    D3DVSD_REG(1, D3DVSDT_D3DCOLOR), D3DVSD_REG(2, D3DVSDT_FLOAT2),
                                    D3DVSD_END()};
#else
static const D3DVERTEXELEMENT9 elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
    {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END()};
static_assert(D3DDECLUSAGE_POSITION == 0 && D3DDECLUSAGE_COLOR == 10 && D3DDECLUSAGE_TEXCOORD == 5);
/* vs_2_0: dcl_position v0; dcl_color v1; mov r0, v0; dp4 oPos.x..w, r0,
 * c0..c3; mul oD0, v1, c4. */
static constexpr DWORD vs20[] = {
    0xfffe0200, /* vs_2_0 */
    0x0200001f, /* dcl: opcode 31, 2 tokens follow */
    0x80000000, /*   POSITION 0 */
    0x900f0000, /*   v0 */
    0x0200001f, /* dcl */
    0x8000000a, /*   COLOR 0 */
    0x900f0001, /*   v1 */
    0x02000001, /* mov: opcode 1, 2 tokens follow */
    0x800f0000, /*   r0.xyzw: type 0 */
    0x90e40000, /*   v0 */
    0x03000009, /* dp4: opcode 9, 3 tokens follow */
    0xc0010000, /*   oPos.x */
    0x80e40000, /*   r0 */
    0xa0e40000, /*   c0 */
    0x03000009, /* dp4 */
    0xc0020000, /*   oPos.y */
    0x80e40000, /*   r0 */
    0xa0e40001, /*   c1 */
    0x03000009, /* dp4 */
    0xc0040000, /*   oPos.z */
    0x80e40000, /*   r0 */
    0xa0e40002, /*   c2 */
    0x03000009, /* dp4 */
    0xc0080000, /*   oPos.w */
    0x80e40000, /*   r0 */
    0xa0e40003, /*   c3 */
    0x03000005, /* mul: opcode 5, 3 tokens follow */
    0xd00f0000, /*   oD0 */
    0x90e40001, /*   v1 */
    0xa0e40004, /*   c4 */
    0x0000ffff, /* end */
};
static_assert(vs20[0] == D3DVS_VERSION(2, 0) && vs20[30] == D3DVS_END());
static_assert(vs20[1] == Instruction(D3DSIO_DCL, 2) && vs20[3] == Dst(RegisterInput, 0, 0xf));
static_assert(vs20[7] == Instruction(D3DSIO_MOV, 2) && vs20[8] == Dst(RegisterTemp, 0, 0xf) &&
              vs20[9] == Src(RegisterInput, 0));
static_assert(vs20[10] == Instruction(D3DSIO_DP4, 3) && vs20[11] == Dst(RegisterRasterOut, 0, 1) &&
              vs20[12] == Src(RegisterTemp, 0) && vs20[13] == Src(RegisterConst, 0));
static_assert(vs20[26] == Instruction(D3DSIO_MUL, 3) &&
              vs20[27] == Dst(RegisterAttributeOut, 0, 0xf) && vs20[28] == Src(RegisterInput, 1) &&
              vs20[29] == Src(RegisterConst, 4));
#endif

struct Vertex {
    float x, y, z;
    DWORD color;
    float u, v;
};
static_assert(sizeof(Vertex) == 24);

/* Rows c0-c3: the unit square onto the whole backbuffer, or onto its left
 * half. c2 keeps z = 0.5 and c3 w = 1. */
static const float whole[16] = {2, 0, 0, -1, 0, 2, 0, -1, 0, 0, 1, 0, 0, 0, 0, 1};
static const float half[16] = {1, 0, 0, -1, 0, 2, 0, -1, 0, 0, 1, 0, 0, 0, 0, 1};
static const UINT columns[4] = {20, 140, 180, 300};

struct Check {
    const char *name;
    const float *rows;
    float c4[4];
    DWORD left, right;
};

/* Clear black, draw the quad, and compare two columns of each half on three
 * rows. */
static BOOL Draw(Device *dev, Surface *target, DWORD color, const Check &check) {
    const Vertex quad[4] = {{0, 0, .5f, color, 0, 0},
                            {1, 0, .5f, color, 1, 0},
                            {0, 1, .5f, color, 0, 1},
                            {1, 1, .5f, color, 1, 1}};
    D3DLOCKED_RECT locked;
    BOOL passed = TRUE;
    if (!HR(VSHADER_CONSTANT(dev, 0, check.rows, 4), "FAIL vertex shader rows") ||
        !HR(VSHADER_CONSTANT(dev, 4, check.c4, 1), "FAIL vertex shader c4") ||
        !HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1, 0),
            "FAIL vertex shader clear") ||
        !HR(DEVICE(BeginScene, dev), "FAIL vertex shader begin scene") ||
        !HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
            "FAIL vertex shader draw") ||
        !HR(DEVICE(EndScene, dev), "FAIL vertex shader end scene") ||
        !HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY),
            "FAIL vertex shader readback"))
        return FALSE;
    for (UINT y = 20; y < 240 && passed; y += 100)
        for (UINT x : columns) {
            const DWORD expected = x < 160 ? check.left : check.right;
            DWORD actual = *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4) & 0x00ffffff;
            if (actual != expected) {
                Log(check.name);
                Number("FAIL vertex shader pixel actual", actual);
                Number("vertex shader pixel expected", expected);
                Number("vertex shader pixel x", x);
                Number("vertex shader pixel y", y);
                passed = FALSE;
                failed = TRUE;
                break;
            }
        }
    SURFACE(UnlockRect, target);
    return passed;
}

/* Create and bind one shader with its input layout; FALSE has logged. */
static BOOL Bind(Device *dev, const DWORD *tokens, VertexShader *shader, void **layout) {
#if DG_D3D_VERSION == 8
    *layout = NULL;
    return HR(DEVICE(CreateVertexShader, dev, declaration, tokens, shader, 0),
              "FAIL vertex shader creation") &&
           HR(VSHADER_SET(dev, *shader), "FAIL vertex shader bind");
#else
    IDirect3DVertexDeclaration9 *decl = NULL;
    BOOL ok = HR(DEVICE(CreateVertexDeclaration, dev, elements, &decl),
                 "FAIL vertex declaration creation") &&
              HR(DEVICE(SetVertexDeclaration, dev, decl), "FAIL vertex declaration bind") &&
              HR(DEVICE(CreateVertexShader, dev, tokens, shader), "FAIL vertex shader creation") &&
              HR(VSHADER_SET(dev, *shader), "FAIL vertex shader bind");
    *layout = decl;
    return ok;
#endif
}
/* Back to fixed function with a pretransformed FVF (on D3D8 SetVertexShader
 * with an FVF, on D3D9 SetFVF, which replaces the declaration). */
static void Unbind(Device *dev, VertexShader shader, void *layout) {
#if DG_D3D_VERSION == 9
    VSHADER_SET(dev, VSHADER_NONE);
#endif
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
#if DG_D3D_VERSION == 9
    if (layout)
        IDirect3DVertexDeclaration9_Release((IDirect3DVertexDeclaration9 *)layout);
#else
    (void)layout;
#endif
    if (shader != VSHADER_NONE)
        VSHADER_DELETE(dev, shader);
}

/* Fixed-function fragments: the interpolated diffuse colour alone. */
static BOOL Diffuse(Device *dev) {
    return HR(STAGE_SET_TEXTURE(dev, 0, NULL), "FAIL vertex shader texture unbind") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
              "FAIL vertex shader color op") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
              "FAIL vertex shader color arg") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
              "FAIL vertex shader alpha op") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE),
              "FAIL vertex shader alpha arg") &&
           HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE),
              "FAIL vertex shader stage 1") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, FALSE),
              "FAIL vertex shader blend") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_FOGENABLE, FALSE), "FAIL vertex shader fog");
}

static void Program(Device *dev, Surface *target, const DWORD *tokens, DWORD color,
                    const Check (&checks)[2], const char *pass) {
    VertexShader shader = VSHADER_NONE;
    void *layout = NULL;
    BOOL ok = Diffuse(dev) && Bind(dev, tokens, &shader, &layout);
    for (const Check &check : checks)
        ok = ok && Draw(dev, target, color, check);
    if (ok)
        Log(pass);
    Unbind(dev, shader, layout);
}

/* vs_1_1 feeding ps_1_1 (shader-probe.inc): texel times the pixel c0. */
static void Pixel(Device *dev, Surface *target) {
    static const Check check = {"vs_1_1 left half into ps_1_1", half, {0, 0, 0, 0}, 0x008040, 0};
    static const float c0[4] = {0, 1, 1, 1};
    StageTexture *texture = NULL;
    PixelShader pixel = SHADER_NONE;
    VertexShader shader = VSHADER_NONE;
    void *layout = NULL;
    BOOL ok = Diffuse(dev) &&
              shader_probe::Fill(dev, &texture, D3DFMT_A8R8G8B8, 4, shader_probe::texel,
                                 shader_probe::texel) &&
              HR(STAGE_SET_TEXTURE(dev, 0, texture), "FAIL vertex+pixel texture bind") &&
              HR(STAGE_POINT_FILTER(dev, 0), "FAIL vertex+pixel filter") &&
              HR(SHADER_CREATE(dev, shader_probe::ps11, &pixel), "FAIL vertex+pixel ps creation") &&
              HR(SHADER_SET(dev, pixel), "FAIL vertex+pixel ps bind") &&
              HR(SHADER_CONSTANT(dev, c0), "FAIL vertex+pixel ps constant") &&
              Bind(dev, vs11, &shader, &layout) && Draw(dev, target, 0xffffffff, check);
    if (ok)
        Log("PASS vertex shader vs_1_1 into pixel shader ps_1_1: oT0 sampled, left half exact");
    Unbind(dev, shader, layout);
    SHADER_SET(dev, SHADER_NONE);
    STAGE_SET_TEXTURE(dev, 0, NULL);
    if (pixel != SHADER_NONE)
        SHADER_DELETE(dev, pixel);
    if (texture)
        STAGE_TEXTURE(Release, texture);
}

static void Run(Device *dev, Surface *target) {
    StageCaps caps = {};
    static const Check checks11[2] = {
        {"vs_1_1 rows over the backbuffer", whole, {0, 0, 0, 0}, 0x336699, 0x336699},
        {"vs_1_1 rows over the left half", half, {0, 0, 0, 0}, 0x336699, 0}};
    DWORD version;
    Log("STAGE vertex shaders");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL vertex shader device caps"))
        return;
    Number("VertexShaderVersion", caps.VertexShaderVersion);
    Number("MaxVertexShaderConst", caps.MaxVertexShaderConst);
    Number("VertexProcessingCaps", caps.VertexProcessingCaps);
#if DG_D3D_VERSION == 9
    Number("VS20Caps.Caps", caps.VS20Caps.Caps);
    Number("VS20Caps.NumTemps", (DWORD)caps.VS20Caps.NumTemps);
    Number("VS20Caps.DynamicFlowControlDepth", (DWORD)caps.VS20Caps.DynamicFlowControlDepth);
    Number("VS20Caps.StaticFlowControlDepth", (DWORD)caps.VS20Caps.StaticFlowControlDepth);
    Number("MaxVShaderInstructionsExecuted", caps.MaxVShaderInstructionsExecuted);
    Number("DeclTypes", caps.DeclTypes);
#endif
    /* Compare major/minor only: D3D8 reports D3DVS_VERSION(0,0) without shaders. */
    version = caps.VertexShaderVersion & 0xffff;
    if (version < (D3DVS_VERSION(1, 1) & 0xffff)) {
        Log("SKIP vertex shaders: VertexShaderVersion below 1.1");
        return;
    }
    Program(dev, target, vs11, 0xff336699, checks11,
            "PASS vertex shader vs_1_1: dp4 rows and D3DCOLOR to oD0 exact for two constant sets");
#if DG_D3D_VERSION == 9
    /* 0x40, 0x80, 0x20 times (2, 0.5, 2) and (0, 1, 4): every product exact. */
    static const Check checks20[2] = {
        {"vs_2_0 c4=(2,0.5,2,1)", whole, {2, .5f, 2, 1}, 0x804040, 0x804040},
        {"vs_2_0 c4=(0,1,4,1)", whole, {0, 1, 4, 1}, 0x008080, 0x008080}};
    if (version < (D3DVS_VERSION(2, 0) & 0xffff))
        Log("SKIP vertex shader vs_2_0: VertexShaderVersion below 2.0");
    else
        Program(dev, target, vs20, 0xff408020, checks20,
                "PASS vertex shader vs_2_0: r0, dp4 rows and unclamped c4 to oD0 exact for two "
                "constants");
#endif
    if (failed)
        return;
    if ((caps.PixelShaderVersion & 0xffff) < (D3DPS_VERSION(1, 1) & 0xffff))
        Log("SKIP vertex shader into pixel shader: PixelShaderVersion below 1.1");
    else
        Pixel(dev, target);
}

} // namespace vshader_probe
