/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pixel shaders and bump environment mapping through the public D3D8/9 API.
 * Included by probe.cpp after cube-probe.inc. Shaders are hand-assembled token
 * streams (no D3DX, no compiler). ps_1_1 multiplies a solid texture by c0; on
 * D3D9 ps_2_0 does the same through dcl/texld/oC0. Each runs with two
 * constants whose factors (0, 1, and on 2.0 an unclamped 2) keep every 8-bit
 * product exact, so ignored, stale or 1.x-clamped constants fail. The
 * fixed-function case perturbs stage 1's coordinate through a V8U8 map with
 * D3DTOP_BUMPENVMAP from red onto a green band, then zeroes BUMPENVMAT00 to
 * return to red. Unclaimed versions, ops and formats are recorded and skipped.
 */
#if DG_D3D_VERSION == 8
typedef DWORD PixelShader;
#define SHADER_NONE 0
#define SHADER_CREATE(device, tokens, out) DEVICE(CreatePixelShader, device, tokens, out)
#define SHADER_SET(device, shader) DEVICE(SetPixelShader, device, shader)
#define SHADER_CONSTANT(device, values) DEVICE(SetPixelShaderConstant, device, 0, values, 1)
#define SHADER_DELETE(device, shader) DEVICE(DeletePixelShader, device, shader)
#define SHADER_CREATE_TEXTURE(device, format, out)                                                 \
    DEVICE(CreateTexture, device, 8, 8, 1, 0, format, D3DPOOL_MANAGED, out)
#define SHADER_ADDRESS_U(device, stage, mode)                                                      \
    DEVICE(SetTextureStageState, device, stage, D3DTSS_ADDRESSU, mode)
#define BUMP_QUERY_USAGE 0
/* d3d8types.h pre-shifts D3DSPR_* into bits 28-30 and has no type above 7. */
#define SHADER_HEADER_PARAM(type) (0x80000000u | (DWORD)(type))
#else
typedef IDirect3DPixelShader9 *PixelShader;
#define SHADER_NONE NULL
#define SHADER_CREATE(device, tokens, out) DEVICE(CreatePixelShader, device, tokens, out)
#define SHADER_SET(device, shader) DEVICE(SetPixelShader, device, shader)
#define SHADER_CONSTANT(device, values) DEVICE(SetPixelShaderConstantF, device, 0, values, 1)
#define SHADER_DELETE(device, shader) IDirect3DPixelShader9_Release(shader)
#define SHADER_CREATE_TEXTURE(device, format, out)                                                 \
    DEVICE(CreateTexture, device, 8, 8, 1, 0, format, D3DPOOL_MANAGED, out, NULL)
#define SHADER_ADDRESS_U(device, stage, mode)                                                      \
    DEVICE(SetSamplerState, device, stage, D3DSAMP_ADDRESSU, mode)
#define BUMP_QUERY_USAGE D3DUSAGE_QUERY_LEGACYBUMPMAP
/* d3d9types.h keeps D3DSPR_* unshifted and splits types above 7. */
#define SHADER_HEADER_PARAM(type)                                                                  \
    (0x80000000u | (((DWORD)(type) << D3DSP_REGTYPE_SHIFT) & (DWORD)D3DSP_REGTYPE_MASK) |          \
     (((DWORD)(type) << D3DSP_REGTYPE_SHIFT2) & (DWORD)D3DSP_REGTYPE_MASK2))
#endif

namespace shader_probe {

/* Register types (D3DSPR_*), local because the two SDK headers encode them
 * differently. Sampler and colour-output types exist only from ps_2_0. */
static constexpr DWORD RegisterTemp = 0, RegisterConst = 2, RegisterTexture = 3,
                       RegisterColorOut = 8, RegisterSampler = 10;

/* Parameter token: bit 31 always set, register number in bits 0-10, type bits
 * 0-2 in 28-30 and type bits 3-4 in 11-12 (zero for every type below 8). */
static constexpr DWORD Param(DWORD type, DWORD number) {
    return 0x80000000u | (type & 7) << 28 | (type >> 3) << 11 | number;
}
/* Destination: write mask nibble (x=1 y=2 z=4 w=8) in bits 16-19. */
static constexpr DWORD Dst(DWORD type, DWORD number, DWORD mask) {
    return Param(type, number) | mask << 16;
}
/* Source: identity swizzle .xyzw = 0xe4 (2 bits per component) in 16-23. */
static constexpr DWORD Src(DWORD type, DWORD number) {
    return Param(type, number) | 0xe4u << 16;
}
/* Instruction: opcode in bits 0-15; from 2.0 the number of following tokens
 * in 24-27. Below 2.0 those bits are reserved zero. */
static constexpr DWORD Instruction(DWORD opcode, DWORD length) {
    return opcode | length << 24;
}
static_assert(Param(RegisterTemp, 0) == SHADER_HEADER_PARAM(D3DSPR_TEMP));
static_assert(Param(RegisterConst, 0) == SHADER_HEADER_PARAM(D3DSPR_CONST));
static_assert(Param(RegisterTexture, 0) == SHADER_HEADER_PARAM(D3DSPR_TEXTURE));
static_assert(Dst(RegisterTemp, 0, 0xf) == (0x80000000u | D3DSP_WRITEMASK_ALL));
static_assert(Src(RegisterTemp, 0) == (0x80000000u | D3DSP_NOSWIZZLE));

/* ps_1_1: tex t0; mul r0, t0, c0. The result is r0. */
static constexpr DWORD ps11[] = {
    0xffff0101, /* ps_1_1: 0xffff pixel, major 1, minor 1 */
    0x00000042, /* tex: opcode 66, no length below 2.0 */
    0xb00f0000, /*   t0.xyzw: bit 31, type 3 << 28, mask 0xf << 16 */
    0x00000005, /* mul: opcode 5 */
    0x800f0000, /*   r0.xyzw: bit 31, type 0, mask 0xf << 16 */
    0xb0e40000, /*   t0: bit 31, type 3 << 28, swizzle 0xe4 << 16 */
    0xa0e40000, /*   c0: bit 31, type 2 << 28, swizzle 0xe4 << 16 */
    0x0000ffff, /* end */
};
static_assert(ps11[0] == D3DPS_VERSION(1, 1) && ps11[7] == D3DPS_END());
static_assert(ps11[1] == Instruction(D3DSIO_TEX, 0) && ps11[2] == Dst(RegisterTexture, 0, 0xf));
static_assert(ps11[3] == Instruction(D3DSIO_MUL, 0) && ps11[4] == Dst(RegisterTemp, 0, 0xf) &&
              ps11[5] == Src(RegisterTexture, 0) && ps11[6] == Src(RegisterConst, 0));

#if DG_D3D_VERSION == 9
static_assert(Param(RegisterColorOut, 0) == SHADER_HEADER_PARAM(D3DSPR_COLOROUT));
static_assert(Param(RegisterSampler, 0) == SHADER_HEADER_PARAM(D3DSPR_SAMPLER));
static_assert(Instruction(0, 1) == 1u << D3DSI_INSTLENGTH_SHIFT);
/* ps_2_0: dcl t0.xy; dcl_2d s0; texld r0, t0, s0; mul r0, r0, c0; mov oC0, r0. */
static constexpr DWORD ps20[] = {
    0xffff0200, /* ps_2_0 */
    0x0200001f, /* dcl: opcode 31, 2 tokens follow */
    0x80000000, /*   bit 31; usage, usage index and texture type 0 for t# */
    0xb0030000, /*   t0.xy: bit 31, type 3 << 28, mask 0x3 << 16 */
    0x0200001f, /* dcl: opcode 31, 2 tokens follow */
    0x90000000, /*   bit 31, D3DSTT_2D = 2 << 27 */
    0xa00f0800, /*   s0: bit 31, type 10 = (2 << 28) | (1 << 11), mask 0xf << 16 */
    0x03000042, /* texld: opcode 66, 3 tokens follow */
    0x800f0000, /*   r0.xyzw */
    0xb0e40000, /*   t0: type 3, swizzle 0xe4 */
    0xa0e40800, /*   s0: type 10 split, swizzle 0xe4 */
    0x03000005, /* mul: opcode 5, 3 tokens follow */
    0x800f0000, /*   r0.xyzw */
    0x80e40000, /*   r0: type 0, swizzle 0xe4 */
    0xa0e40000, /*   c0: type 2, swizzle 0xe4 */
    0x02000001, /* mov: opcode 1, 2 tokens follow */
    0x800f0800, /*   oC0: bit 31, type 8 = (0 << 28) | (1 << 11), mask 0xf << 16 */
    0x80e40000, /*   r0 */
    0x0000ffff, /* end */
};
static_assert(ps20[0] == D3DPS_VERSION(2, 0) && ps20[18] == D3DPS_END());
static_assert(ps20[1] == Instruction(D3DSIO_DCL, 2) && ps20[2] == 0x80000000u &&
              ps20[3] == Dst(RegisterTexture, 0, 0x3));
static_assert(ps20[4] == Instruction(D3DSIO_DCL, 2) &&
              ps20[5] == (0x80000000u | (DWORD)D3DSTT_2D) &&
              ps20[6] == Dst(RegisterSampler, 0, 0xf));
static_assert(ps20[7] == Instruction(D3DSIO_TEX, 3) && ps20[8] == Dst(RegisterTemp, 0, 0xf) &&
              ps20[9] == Src(RegisterTexture, 0) && ps20[10] == Src(RegisterSampler, 0));
static_assert(ps20[11] == Instruction(D3DSIO_MUL, 3) && ps20[12] == Dst(RegisterTemp, 0, 0xf) &&
              ps20[13] == Src(RegisterTemp, 0) && ps20[14] == Src(RegisterConst, 0));
static_assert(ps20[15] == Instruction(D3DSIO_MOV, 2) && ps20[16] == Dst(RegisterColorOut, 0, 0xf) &&
              ps20[17] == Src(RegisterTemp, 0));
#endif

struct Vertex {
    float x, y, z, rhw;
    float u0, v0, u1, v1;
};
/* Stage 0 spans its solid texture. Stage 1 samples u = 0.25 everywhere: the
 * boundary of texels 1 and 2 of 8, both red. */
static const Vertex quad[4] = {{0, 0, .5f, 1, 0, 0, .25f, .5f},
                               {320, 0, .5f, 1, 1, 0, .25f, .5f},
                               {0, 240, .5f, 1, 0, 1, .25f, .5f},
                               {320, 240, .5f, 1, 1, 1, .25f, .5f}};

struct Check {
    const char *name;
    float c0[4];
    DWORD expected;
};

/* A = ff, R = ff, G = 80, B = 40: x * 0 = 0 and x * 1 = x exactly; on 2.0,
 * 0x40 * 2 = 0x80 exactly and 0x80 * 2 saturates to 0xff. */
static const DWORD texel = 0xffff8040;

static DWORD Bits(float value) {
    return __builtin_bit_cast(DWORD, value);
}

/* 8x8 MANAGED texture: columns 5-6 hold band, the others base, each texel the
 * low size bytes of the value in little-endian order. */
static BOOL Fill(Device *dev, StageTexture **texture, D3DFORMAT format, UINT size, DWORD base,
                 DWORD band) {
    D3DLOCKED_RECT locked;
    if (!HR(SHADER_CREATE_TEXTURE(dev, format, texture), "FAIL shader texture creation") ||
        !HR(STAGE_TEXTURE(LockRect, *texture, 0, &locked, NULL, 0), "FAIL shader texture lock"))
        return FALSE;
    for (UINT y = 0; y < 8; ++y)
        for (UINT x = 0; x < 8; ++x) {
            DWORD value = x == 5 || x == 6 ? band : base;
            for (UINT i = 0; i < size; ++i)
                ((BYTE *)locked.pBits)[y * locked.Pitch + x * size + i] = (BYTE)(value >> (8 * i));
        }
    return HR(STAGE_TEXTURE(UnlockRect, *texture, 0), "FAIL shader texture unlock");
}

static BOOL Draw(Device *dev, Surface *target, const char *name, DWORD expected) {
    D3DLOCKED_RECT locked;
    BOOL passed = TRUE;
    if (!HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1, 0), "FAIL shader clear") ||
        !HR(DEVICE(BeginScene, dev), "FAIL shader begin scene") ||
        !HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
            "FAIL shader draw") ||
        !HR(DEVICE(EndScene, dev), "FAIL shader end scene") ||
        !HR(SURFACE(LockRect, target, &locked, NULL, D3DLOCK_READONLY), "FAIL shader readback"))
        return FALSE;
    for (UINT y = 20; y < 240 && passed; y += 100)
        for (UINT x = 20; x < 320 && passed; x += 140) {
            DWORD actual = *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4) & 0x00ffffff;
            if (actual != expected) {
                Log(name);
                Number("FAIL shader pixel actual", actual);
                Number("shader pixel expected", expected);
                Number("shader pixel x", x);
                Number("shader pixel y", y);
                passed = FALSE;
                failed = TRUE;
            }
        }
    SURFACE(UnlockRect, target);
    return passed;
}

static void Program(Device *dev, Surface *target, const DWORD *tokens, const Check (&checks)[2],
                    const char *pass) {
    StageTexture *texture = NULL;
    PixelShader shader = SHADER_NONE;
    BOOL ok = Fill(dev, &texture, D3DFMT_A8R8G8B8, 4, texel, texel) &&
              HR(SHADER_CREATE(dev, tokens, &shader), "FAIL pixel shader creation") &&
              HR(STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX2), "FAIL shader FVF") &&
              HR(STAGE_SET_TEXTURE(dev, 0, texture), "FAIL shader texture bind") &&
              HR(STAGE_POINT_FILTER(dev, 0), "FAIL shader filter") &&
              HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXCOORDINDEX, 0),
                 "FAIL shader coordinates") &&
              HR(DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, FALSE), "FAIL shader blend") &&
              HR(SHADER_SET(dev, shader), "FAIL pixel shader bind");
    for (const Check &check : checks)
        ok = ok && HR(SHADER_CONSTANT(dev, check.c0), "FAIL pixel shader constant") &&
             Draw(dev, target, check.name, check.expected);
    if (ok)
        Log(pass);
    SHADER_SET(dev, SHADER_NONE);
    STAGE_SET_TEXTURE(dev, 0, NULL);
    if (shader != SHADER_NONE)
        SHADER_DELETE(dev, shader);
    if (texture)
        STAGE_TEXTURE(Release, texture);
}

static void Bump(Device *dev, Surface *target, const StageCaps &caps) {
    /* du = 64 is +64/127 or +(2 * 64 + 1)/255, about 0.504: u = 0.754 lands on
     * the green texels 5-6, which take any offset in [0.375, 0.625). Unsigned
     * 64/255 (0.501), biased 192/255 (1.003, clamped), negated (-0.498,
     * clamped) or ignored offsets all stay red. dv = 0 and the texture is
     * constant in v. */
    static const struct {
        const char *name;
        float m00;
        DWORD expected;
    } checks[2] = {{"bump BUMPENVMAT00=1 onto green band", 1, 0x00ff00},
                   {"bump BUMPENVMAT00=0 stays red", 0, 0xff0000}};
    D3D *object = NULL;
    StageTexture *textures[2] = {};
    HRESULT claim;
    BOOL ok;
    if (!(caps.TextureOpCaps & D3DTEXOPCAPS_BUMPENVMAP)) {
        Log("SKIP bump environment map: BUMPENVMAP not claimed");
        return;
    }
    if (caps.MaxSimultaneousTextures < 2 || caps.MaxTextureBlendStages < 2) {
        Log("SKIP bump environment map: device exposes a single texture stage");
        return;
    }
    if (!HR(DEVICE(GetDirect3D, dev, &object), "FAIL bump Direct3D object"))
        return;
    claim = D3D_CALL(CheckDeviceFormat, object, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                     BUMP_QUERY_USAGE, D3DRTYPE_TEXTURE, D3DFMT_V8U8);
    D3D_CALL(Release, object);
    Number("V8U8 texture CheckDeviceFormat", (DWORD)claim);
    if (FAILED(claim)) {
        Log("SKIP bump environment map: V8U8 textures not claimed");
        return;
    }
    ok = Fill(dev, &textures[0], D3DFMT_V8U8, 2, 0x0040, 0x0040) &&
         Fill(dev, &textures[1], D3DFMT_A8R8G8B8, 4, 0xffff0000, 0xff00ff00) &&
         HR(STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX2), "FAIL bump FVF") &&
         HR(STAGE_SET_TEXTURE(dev, 0, textures[0]), "FAIL bump map bind") &&
         HR(STAGE_SET_TEXTURE(dev, 1, textures[1]), "FAIL bump environment bind") &&
         HR(STAGE_POINT_FILTER(dev, 0), "FAIL bump map filter") &&
         HR(STAGE_POINT_FILTER(dev, 1), "FAIL bump environment filter") &&
         HR(SHADER_ADDRESS_U(dev, 1, D3DTADDRESS_CLAMP), "FAIL bump environment clamp") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXCOORDINDEX, 0),
            "FAIL bump map coordinates") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_BUMPENVMAP),
            "FAIL bump stage 0 op") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
            "FAIL bump stage 0 arg1") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG2, D3DTA_CURRENT),
            "FAIL bump stage 0 arg2") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
            "FAIL bump stage 0 alpha op") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
            "FAIL bump stage 0 alpha arg") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_BUMPENVMAT01, Bits(0)),
            "FAIL bump matrix 01") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_BUMPENVMAT10, Bits(0)),
            "FAIL bump matrix 10") &&
         HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_BUMPENVMAT11, Bits(1)),
            "FAIL bump matrix 11") &&
         HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_TEXCOORDINDEX, 1),
            "FAIL bump environment coordinates") &&
         HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL bump stage 1 op") &&
         HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLORARG1, D3DTA_TEXTURE),
            "FAIL bump stage 1 arg") &&
         HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
            "FAIL bump stage 1 alpha op") &&
         HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
            "FAIL bump stage 1 alpha arg") &&
         HR(DEVICE(SetTextureStageState, dev, 2, D3DTSS_COLOROP, D3DTOP_DISABLE),
            "FAIL bump stage 2 disable") &&
         HR(DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, FALSE), "FAIL bump blend");
    for (const auto &check : checks)
        ok = ok &&
             HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_BUMPENVMAT00, Bits(check.m00)),
                "FAIL bump matrix 00") &&
             Draw(dev, target, check.name, check.expected);
    if (ok)
        Log("PASS bump environment map: V8U8 offset through BUMPENVMAT, identity green, "
            "zeroed red");
    /* Stage-state defaults. */
    STAGE_SET_TEXTURE(dev, 0, NULL);
    STAGE_SET_TEXTURE(dev, 1, NULL);
    SHADER_ADDRESS_U(dev, 1, D3DTADDRESS_WRAP);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_BUMPENVMAT00, 0);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_BUMPENVMAT01, 0);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_BUMPENVMAT10, 0);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_BUMPENVMAT11, 0);
    DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    DEVICE(SetTextureStageState, dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    for (StageTexture *texture : textures)
        if (texture)
            STAGE_TEXTURE(Release, texture);
}

static void Run(Device *dev, Surface *target) {
    StageCaps caps = {};
    static const Check checks11[2] = {{"ps_1_1 c0=(0,1,1,1)", {0, 1, 1, 1}, 0x008040},
                                      {"ps_1_1 c0=(1,0,1,1)", {1, 0, 1, 1}, 0xff0040}};
    DWORD version;
    Log("STAGE pixel shaders and bump environment mapping");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL shader device caps"))
        return;
    Number("PixelShaderVersion", caps.PixelShaderVersion);
#if DG_D3D_VERSION == 8
    Number("MaxPixelShaderValue float bits", Bits(caps.MaxPixelShaderValue));
#else
    Number("PixelShader1xMaxValue float bits", Bits(caps.PixelShader1xMaxValue));
    Number("PS20Caps.Caps", caps.PS20Caps.Caps);
    Number("PS20Caps.NumTemps", (DWORD)caps.PS20Caps.NumTemps);
    Number("PS20Caps.NumInstructionSlots", (DWORD)caps.PS20Caps.NumInstructionSlots);
#endif
    Number("TextureOpCaps", caps.TextureOpCaps);
    /* Compare major/minor only: D3D8 reports D3DPS_VERSION(0,0) without shaders. */
    version = caps.PixelShaderVersion & 0xffff;
    if (version < (D3DPS_VERSION(1, 1) & 0xffff)) {
        Log("SKIP pixel shaders: PixelShaderVersion below 1.1");
    } else {
        Program(dev, target, ps11, checks11,
                "PASS pixel shader ps_1_1: tex t0 times c0 exact for two constants");
#if DG_D3D_VERSION == 9
        static const Check checks20[2] = {{"ps_2_0 c0=(1,1,0,1)", {1, 1, 0, 1}, 0xff8000},
                                          {"ps_2_0 c0=(0,2,2,1)", {0, 2, 2, 1}, 0x00ff80}};
        if (version < (D3DPS_VERSION(2, 0) & 0xffff))
            Log("SKIP pixel shader ps_2_0: PixelShaderVersion below 2.0");
        else
            Program(dev, target, ps20, checks20,
                    "PASS pixel shader ps_2_0: texld times unclamped c0 to oC0 exact for two "
                    "constants");
#endif
    }
    Bump(dev, target, caps);
}

} // namespace shader_probe
