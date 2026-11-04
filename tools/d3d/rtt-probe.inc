/* SPDX-License-Identifier: GPL-2.0-or-later
 * Render-target textures, depth-stencil surfaces and surface copies through
 * the public D3D8/9 API. Included by probe.cpp after vbuffer-probe.inc. With
 * framebuffer objects WineD3D renders offscreen targets directly, not through
 * the 320x240 backbuffer, so every target here is larger than the window and
 * every case checks contents beyond it, up to texel (1023, 1023). Results
 * reach the lockable backbuffer by point-sampling a target texture 1:1 or by
 * a copy, and the whole backbuffer is compared with a model of what each
 * surface holds: clears, quads, Lock-filled gradients and copies of other
 * surfaces. Clears and copies are compared to the pixel; the pixel on either
 * side of a drawn edge is not. Cases:
 * - colour: 1024x1024 X8R8G8B8 and A8R8G8B8 render-target textures cleared and
 *   drawn in eight regions (two opposite corners, beside (1000, 1000), right
 *   of, below, inside and across the corner of the window, a quad over a
 *   clear), sampled in ten views. Views of the two empty corners fail a
 *   flipped target; views beyond the window a clipped one.
 * - depth: the texture with a 1024x1024 D24S8 (else D16) depth-stencil
 *   surface. Far quads drawn after near ones show only outside them, beside
 *   (1000, 1000) and inside the window. With D24S8 a stencil REPLACE pass then
 *   an EQUAL pass clip a larger quad to the first one's rectangle.
 * - return: a quad drawn on the backbuffer before rendering to a texture
 *   survives SetRenderTarget back to it; after Present an ordinary frame
 *   renders exactly.
 * - copy: D3D9 StretchRect (1:1 sub-rectangles, 2x and 8:1/16:1 POINT) or
 *   D3D8 CopyRects (three rectangles per call) from a 1024x1024 target texture
 *   into a 256x256 one and into the backbuffer, then from the backbuffer into
 *   the 256x256 texture.
 * - offscreen: a Lock-filled gradient in a SYSTEMMEM surface (D3D9
 *   CreateOffscreenPlainSurface and UpdateSurface, D3D8 CreateImageSurface and
 *   CopyRects) whole, as a sub-rectangle and across the window corner, and a
 *   second gradient (D3D9 DEFAULT pool and StretchRect) into a target texture
 *   and the backbuffer.
 * Returning to the backbuffer restores its own depth-stencil surface (this
 * device has none). Target textures of both formats, 1024-texel textures and
 * D24S8 or D16 depth are required; unclaimed plain surfaces are recorded and
 * skipped.
 */
#if DG_D3D_VERSION == 8
typedef D3DVIEWPORT8 TargetViewport;
#define RTT_TEXTURE(device, width, height, format, out)                                            \
    DEVICE(CreateTexture, device, width, height, 1, D3DUSAGE_RENDERTARGET, format,                 \
           D3DPOOL_DEFAULT, out)
#define RTT_DEPTH(device, format, out)                                                             \
    DEVICE(CreateDepthStencilSurface, device, 1024, 1024, format, D3DMULTISAMPLE_NONE, out)
#define RTT_PLAIN(device, pool, out)                                                               \
    DEVICE(CreateImageSurface, device, 64, 64, D3DFMT_X8R8G8B8, out)
#define RTT_ADDRESS(device, mode)                                                                  \
    (DEVICE(SetTextureStageState, device, 0, D3DTSS_ADDRESSU, mode) |                              \
     DEVICE(SetTextureStageState, device, 0, D3DTSS_ADDRESSV, mode))
#define RTT_COPY "CopyRects"
#define RTT_UPLOAD "CopyRects"
#else
typedef D3DVIEWPORT9 TargetViewport;
#define RTT_TEXTURE(device, width, height, format, out)                                            \
    DEVICE(CreateTexture, device, width, height, 1, D3DUSAGE_RENDERTARGET, format,                 \
           D3DPOOL_DEFAULT, out, NULL)
#define RTT_DEPTH(device, format, out)                                                             \
    DEVICE(CreateDepthStencilSurface, device, 1024, 1024, format, D3DMULTISAMPLE_NONE, 0, FALSE,   \
           out, NULL)
#define RTT_PLAIN(device, pool, out)                                                               \
    DEVICE(CreateOffscreenPlainSurface, device, 64, 64, D3DFMT_X8R8G8B8, pool, out, NULL)
#define RTT_ADDRESS(device, mode)                                                                  \
    (DEVICE(SetSamplerState, device, 0, D3DSAMP_ADDRESSU, mode) |                                  \
     DEVICE(SetSamplerState, device, 0, D3DSAMP_ADDRESSV, mode))
#define RTT_COPY "StretchRect"
#define RTT_UPLOAD "UpdateSurface"
#endif

namespace rtt_probe {

static constexpr int ScreenWidth = 320, ScreenHeight = 240, Big = 1024, Small = 256, Plain = 64;
static constexpr DWORD Background = 0xff102030, TargetBase = 0xff406080, SmallBase = 0xff503010;
static constexpr DWORD Unchecked = 0xffffffff;
static constexpr float Near = .25f, Middle = .5f, Far = .75f;

struct Image;
/* One layer of a surface over [left, right) x [top, bottom): a solid colour,
 * a gradient written by Lock (colour is then its XOR mask), or source's
 * [sx, sx + sw) x [sy, sy + sh) scaled onto the layer. Drawn layers are
 * rasterised (quads and sampled views); the others are clears and copies. */
struct Layer {
    int left, top, right, bottom;
    DWORD color;
    BOOL drawn, gradient;
    const Image *source;
    int sx, sy, sw, sh;
};
/* A surface: base everywhere, then the layers in painting order. */
struct Image {
    DWORD base;
    const Layer *layers;
    UINT count;
};
template <UINT N> static constexpr Image Make(DWORD base, const Layer (&layers)[N]) {
    return {base, layers, N};
}
/* A Clear rectangle. */
static constexpr Layer Fill(int left, int top, int right, int bottom, DWORD color) {
    return {left, top, right, bottom, color, FALSE, FALSE, nullptr, 0, 0, 0, 0};
}
/* A diffuse pretransformed quad. */
static constexpr Layer Quad(int left, int top, int right, int bottom, DWORD color) {
    return {left, top, right, bottom, color, TRUE, FALSE, nullptr, 0, 0, 0, 0};
}
/* Ramp(x, y) ^ mask, written through LockRect. */
static constexpr Layer Gradient(int left, int top, int right, int bottom, DWORD mask) {
    return {left, top, right, bottom, mask, FALSE, TRUE, nullptr, 0, 0, 0, 0};
}
/* A copy of source [sx, sx + sw) x [sy, sy + sh) onto the layer. */
static constexpr Layer Copy(int left, int top, int right, int bottom, const Image &source, int sx,
                            int sy, int sw, int sh) {
    return {left, top, right, bottom, 0, FALSE, FALSE, &source, sx, sy, sw, sh};
}
/* A w x h textured quad point-sampling source 1:1 from (sx, sy). */
static constexpr Layer View(int left, int top, int w, int h, const Image &source, int sx, int sy) {
    return {left, top, left + w, top + h, 0, TRUE, FALSE, &source, sx, sy, w, h};
}
/* Every texel of a 64x64 gradient differs. */
static constexpr DWORD Ramp(int x, int y) {
    return 0xff000000u | (DWORD)(x * 4) << 16 | (DWORD)(y * 4) << 8 | (DWORD)((x + y) * 2 & 0xff);
}

/* Colour 1024x1024 target: the top-left and bottom-right corners and a region
 * right of the window cleared; quads beside (1000, 1000), below and inside the
 * window, across its corner (320, 240), and over the bottom-right clear. */
static constexpr Layer painted_layers[] = {
    Fill(0, 0, 24, 24, 0xff10e060),       Fill(960, 960, 1024, 1024, 0xffe0c020),
    Fill(600, 40, 680, 120, 0xff20c0e0),  Quad(896, 896, 952, 952, 0xffc02080),
    Quad(40, 600, 120, 680, 0xff80e040),  Quad(100, 100, 180, 180, 0xff6040f0),
    Quad(280, 200, 400, 280, 0xfff08040), Quad(976, 976, 1008, 1008, 0xff4020c0),
};
static constexpr Image painted = Make(TargetBase, painted_layers);
/* Ten 64x64 views in a grid; the last two show the empty top-right and
 * bottom-left corners. */
static constexpr Layer painted_views[] = {
    View(8, 8, 64, 64, painted, 920, 920),    View(88, 8, 64, 64, painted, 960, 960),
    View(168, 8, 64, 64, painted, 570, 20),   View(248, 8, 64, 64, painted, 20, 580),
    View(8, 88, 64, 64, painted, 80, 80),     View(88, 88, 64, 64, painted, 290, 210),
    View(168, 88, 64, 64, painted, 360, 240), View(248, 88, 64, 64, painted, 0, 0),
    View(8, 168, 64, 64, painted, 960, 0),    View(88, 168, 64, 64, painted, 0, 960),
};
static constexpr Image painted_frame = Make(Background, painted_views);

/* Depth: drawn Q1 far, Q2 near, Q3 far, Q4 near, Q5 far. A far quad drawn
 * after a near one shows only outside it, so it is listed below it. The last
 * layer is the stencil-masked quad, inside the stencil-writing one. */
static constexpr Layer layered_layers[] = {
    Quad(800, 800, 960, 960, 0xff2080f0),   /* Q1 */
    Quad(920, 920, 1016, 1016, 0xff20f080), /* Q3 */
    Quad(880, 880, 1000, 1000, 0xfff08020), /* Q2 */
    Quad(80, 80, 160, 160, 0xff8040c0),     /* Q5 */
    Quad(40, 40, 120, 120, 0xffa0f0f0),     /* Q4 */
    Quad(600, 860, 680, 940, 0xff40ffa0),   /* StencilMasked within StencilWriter */
};
static constexpr Image layered = {TargetBase, layered_layers, 5};
static constexpr Image layered_stencil = Make(TargetBase, layered_layers);
static constexpr Layer StencilWriter = Quad(600, 860, 680, 940, 0xffff4040);
static constexpr Layer StencilMasked = Quad(580, 840, 700, 960, 0xff40ffa0);

/* Return: a texture with two corner layers; the frame keeps the quad drawn
 * before the switch, then a quad and two views drawn after it. */
static constexpr Layer corner_layers[] = {Fill(992, 0, 1024, 32, 0xff20e0e0),
                                          Quad(0, 992, 32, 1024, 0xffe02040)};
static constexpr Image corners = Make(TargetBase, corner_layers);
static constexpr Layer returned_layers[] = {
    Quad(16, 16, 96, 96, 0xffd0d020),
    Quad(112, 16, 192, 96, 0xff2060d0),
    View(208, 16, 64, 64, corners, 960, 0),
    View(208, 96, 64, 64, corners, 0, 960),
};
static constexpr Image returned = Make(Background, returned_layers);
static constexpr Layer normal_layers[] = {Quad(40, 120, 140, 220, 0xff80c0f0),
                                          Quad(180, 120, 300, 220, 0xfff0c080)};
static constexpr Image normal = Make(Background, normal_layers);

/* Copy source: clears on multiples of 8, so POINT minification by 8 or 16
 * reads one uniform block whichever texel of the footprint it takes. */
static constexpr Layer original_layers[] = {
    Fill(960, 960, 1024, 1024, 0xffe0a040), Fill(984, 968, 1016, 992, 0xff4080ff),
    Fill(896, 8, 952, 40, 0xff60f060),      Fill(8, 896, 40, 1016, 0xfff06090),
    Fill(480, 480, 544, 544, 0xffa0a0f0),
};
static constexpr Image original = Make(TargetBase, original_layers);
#if DG_D3D_VERSION == 8
/* CopyRects cannot scale: three 1:1 rectangles per call. */
static constexpr Layer copied_layers[] = {
    Copy(8, 8, 72, 72, original, 944, 944, 64, 64),
    Copy(88, 8, 168, 56, original, 888, 0, 80, 48),
    Copy(8, 112, 64, 240, original, 0, 888, 56, 128),
};
static constexpr Layer direct_layers[] = {
    Copy(16, 16, 88, 88, original, 952, 952, 72, 72),
    Copy(104, 16, 168, 160, original, 0, 880, 64, 144),
    Copy(176, 168, 320, 232, original, 880, 0, 144, 64),
};
#else
/* 1:1 sub-rectangle, 2x magnification, the whole surface at 8:1 and 16:1. */
static constexpr Layer copied_layers[] = {
    Copy(8, 8, 72, 72, original, 944, 944, 64, 64),
    Copy(88, 8, 168, 104, original, 920, 0, 40, 48),
    Copy(8, 112, 136, 240, original, 0, 0, 1024, 1024),
};
static constexpr Layer direct_layers[] = {
    Copy(16, 16, 88, 88, original, 952, 952, 72, 72),
    Copy(104, 16, 200, 112, original, 0, 976, 48, 48),
    Copy(216, 16, 280, 80, original, 0, 0, 1024, 1024),
};
#endif
static constexpr Image copied = Make(SmallBase, copied_layers);
static constexpr Image direct = Make(Background, direct_layers);
/* Then two backbuffer rectangles 1:1 into free parts of the small texture. */
static constexpr Layer recopied_layers[] = {
    copied_layers[0],
    copied_layers[1],
    copied_layers[2],
    Copy(176, 8, 248, 80, direct, 16, 16, 72, 72),
    Copy(160, 120, 256, 216, direct, 104, 16, 96, 96),
};
static constexpr Image recopied = Make(SmallBase, recopied_layers);
static constexpr Layer copied_views[] = {View(32, 0, 256, 240, copied, 0, 0)};
static constexpr Image copied_frame = Make(Background, copied_views);
static constexpr Layer recopied_views[] = {View(32, 0, 256, 240, recopied, 0, 0)};
static constexpr Image recopied_frame = Make(Background, recopied_views);

/* Offscreen: two 64x64 gradients, the second inverted. The first goes into
 * the target whole beside (1000, 1000), as its centre 32x32 and across the
 * window corner; the second whole right of the window. */
static constexpr Layer ramp_layers[] = {Gradient(0, 0, 64, 64, 0)};
static constexpr Image ramp = Make(0, ramp_layers);
static constexpr Layer inverse_layers[] = {Gradient(0, 0, 64, 64, 0x00ffffff)};
static constexpr Image inverse = Make(0, inverse_layers);
static constexpr Layer uploaded_layers[] = {
    Copy(900, 900, 964, 964, ramp, 0, 0, 64, 64),
    Copy(960, 976, 992, 1008, ramp, 16, 16, 32, 32),
    Copy(296, 216, 360, 280, ramp, 0, 0, 64, 64),
    Copy(600, 40, 664, 104, inverse, 0, 0, 64, 64),
};
static constexpr Image uploaded = Make(TargetBase, uploaded_layers);
/* Four views of the target, then the gradients copied to the backbuffer
 * directly (D3D9: only the DEFAULT surface, through StretchRect). */
static constexpr Layer uploaded_views[] = {
    View(8, 8, 64, 64, uploaded, 900, 900),         View(88, 8, 64, 64, uploaded, 944, 960),
    View(168, 8, 64, 64, uploaded, 600, 40),        View(248, 8, 64, 64, uploaded, 290, 210),
#if DG_D3D_VERSION == 8
    Copy(8, 120, 72, 184, ramp, 0, 0, 64, 64),
#endif
    Copy(88, 120, 152, 184, inverse, 0, 0, 64, 64),
};
static constexpr Image uploaded_frame = Make(Background, uploaded_views);
static constexpr UINT UploadedViews = 4;

static Surface *backbuffer;
static Surface *original_depth;

static DWORD Texel(const Image &image, int x, int y);

/* The texels a destination coordinate d of [start, start + size) reads from
 * [from, from + extent): its pixel centre maps to from + (d - start + 0.5) *
 * extent / size. On a texel boundary either neighbour may be taken. */
static void Source(int d, int start, int size, int from, int extent, int *low, int *high) {
    const int p = (2 * (d - start) + 1) * extent, q = 2 * size;
    *high = from + p / q;
    *low = p % q ? *high : *high - 1;
}
/* A copied or sampled pixel: Unchecked unless every candidate texel agrees. */
static DWORD Sampled(const Layer &layer, int x, int y) {
    int x0, x1, y0, y1;
    Source(x, layer.left, layer.right - layer.left, layer.sx, layer.sw, &x0, &x1);
    Source(y, layer.top, layer.bottom - layer.top, layer.sy, layer.sh, &y0, &y1);
    const Image &source = *layer.source;
    const DWORD value = Texel(source, x0, y0);
    if (value == Unchecked)
        return Unchecked;
    if ((x1 != x0 && Texel(source, x1, y0) != value) ||
        (y1 != y0 && Texel(source, x0, y1) != value) ||
        (x1 != x0 && y1 != y0 && Texel(source, x1, y1) != value))
        return Unchecked;
    return value;
}
/* Within one pixel of a drawn layer's edge, inside or outside. */
static BOOL Edge(const Layer &layer, int x, int y) {
    if (x < layer.left - 1 || x > layer.right || y < layer.top - 1 || y > layer.bottom)
        return FALSE;
    return x <= layer.left || x >= layer.right - 1 || y <= layer.top || y >= layer.bottom - 1;
}
/* The RGB expected at (x, y) of image, or Unchecked. */
static DWORD Texel(const Image &image, int x, int y) {
    for (UINT i = image.count; i-- > 0;) {
        const Layer &layer = image.layers[i];
        if (layer.drawn && Edge(layer, x, y))
            return Unchecked;
        if (x < layer.left || x >= layer.right || y < layer.top || y >= layer.bottom)
            continue;
        if (layer.source)
            return Sampled(layer, x, y);
        if (layer.gradient)
            return (Ramp(x - layer.left, y - layer.top) ^ layer.color) & 0xffffff;
        return layer.color & 0xffffff;
    }
    return image.base & 0xffffff;
}

/* Compare the whole backbuffer with image; on a mismatch log the first wrong
 * pixel, the counts and every layer's centre. */
static BOOL Verify(const char *name, const Image &image) {
    D3DLOCKED_RECT locked;
    UINT wrong = 0, checked = 0;
    int first_x = 0, first_y = 0;
    DWORD first_actual = 0, first_expected = 0;
    if (!HR(SURFACE(LockRect, backbuffer, &locked, NULL, D3DLOCK_READONLY),
            "FAIL render target readback")) {
        Log(name);
        return FALSE;
    }
    if (!locked.pBits || locked.Pitch < ScreenWidth * 4) {
        SURFACE(UnlockRect, backbuffer);
        Log(name);
        return Check(FALSE, "FAIL render target readback layout", (DWORD)locked.Pitch);
    }
    const auto pixel = [&](int x, int y) -> DWORD {
        return *(const DWORD *)((const BYTE *)locked.pBits + y * locked.Pitch + x * 4) & 0xffffff;
    };
    for (int y = 0; y < ScreenHeight; ++y)
        for (int x = 0; x < ScreenWidth; ++x) {
            const DWORD expected = Texel(image, x, y);
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
        Log("FAIL render target pixels");
        Log(name);
        Number("render target pixel actual", first_actual);
        Number("render target pixel expected", first_expected);
        Number("render target pixel x", (DWORD)first_x);
        Number("render target pixel y", (DWORD)first_y);
        Number("render target wrong pixels", wrong);
        Number("render target checked pixels", checked);
        for (UINT i = 0; i < image.count; ++i) {
            const int x = (image.layers[i].left + image.layers[i].right) / 2;
            const int y = (image.layers[i].top + image.layers[i].bottom) / 2;
            Number("render target layer centre x", (DWORD)x);
            Number("render target layer centre y", (DWORD)y);
            Number("render target layer expected", Texel(image, x, y));
            Number("render target layer actual", pixel(x, y));
        }
        failed = TRUE;
    }
    SURFACE(UnlockRect, backbuffer);
    return !wrong;
}

static BOOL Claimed(D3D *d3d, DWORD usage, D3DRESOURCETYPE type, D3DFORMAT format,
                    const char *name) {
    const HRESULT claim = D3D_CALL(CheckDeviceFormat, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                   D3DFMT_X8R8G8B8, usage, type, format);
    Number(name, (DWORD)claim);
    return SUCCEEDED(claim);
}
/* D24S8 if claimed as depth-stencil and matching X8R8G8B8 targets, else D16,
 * else D3DFMT_UNKNOWN. */
static D3DFORMAT DepthFormat(D3D *d3d) {
    static const D3DFORMAT formats[2] = {D3DFMT_D24S8, D3DFMT_D16};
    for (D3DFORMAT format : formats) {
        const BOOL d16 = format == D3DFMT_D16;
        const BOOL claimed = Claimed(d3d, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, format,
                                     d16 ? "D16 depth-stencil CheckDeviceFormat"
                                         : "D24S8 depth-stencil CheckDeviceFormat");
        const HRESULT match = D3D_CALL(CheckDepthStencilMatch, d3d, D3DADAPTER_DEFAULT,
                                       D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8, format);
        Number(d16 ? "D16 CheckDepthStencilMatch with X8R8G8B8"
                   : "D24S8 CheckDepthStencilMatch with X8R8G8B8",
               (DWORD)match);
        if (claimed && SUCCEEDED(match))
            return format;
    }
    return D3DFMT_UNKNOWN;
}

/* Bind the colour target and depth-stencil surface (NULL: none) with a viewport
 * over the whole target. */
static BOOL Target(Device *dev, Surface *target, Surface *depth, int width, int height) {
    TargetViewport viewport = {0, 0, (DWORD)width, (DWORD)height, 0, 1};
#if DG_D3D_VERSION == 8
    if (!HR(DEVICE(SetRenderTarget, dev, target, depth), "FAIL SetRenderTarget"))
        return FALSE;
#else
    if (!HR(DEVICE(SetRenderTarget, dev, 0, target), "FAIL SetRenderTarget") ||
        !HR(DEVICE(SetDepthStencilSurface, dev, depth), "FAIL SetDepthStencilSurface"))
        return FALSE;
#endif
    return HR(DEVICE(SetViewport, dev, &viewport), "FAIL render target viewport");
}
/* The backbuffer and its own depth-stencil surface (none for this device). */
static BOOL Back(Device *dev) {
    return Target(dev, backbuffer, original_depth, ScreenWidth, ScreenHeight);
}
static BOOL Present(Device *dev) {
    return HR(DEVICE(Present, dev, NULL, NULL, NULL, NULL), "FAIL render target present");
}
static BOOL Clear(Device *dev, DWORD color) {
    return HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, color, 1, 0),
              "FAIL render target clear");
}

/* A DEFAULT-pool render-target texture of one level, and that level. */
static BOOL Create(Device *dev, int width, int height, D3DFORMAT format, StageTexture **texture,
                   Surface **level) {
    D3DSURFACE_DESC desc = {};
    if (!HR(RTT_TEXTURE(dev, width, height, format, texture),
            "FAIL render target texture creation") ||
        !HR(STAGE_TEXTURE(GetSurfaceLevel, *texture, 0, level), "FAIL render target level") ||
        !HR(SURFACE(GetDesc, *level, &desc), "FAIL render target description"))
        return FALSE;
    return Check(desc.Width == (UINT)width && desc.Height == (UINT)height &&
                     desc.Format == format && (desc.Usage & D3DUSAGE_RENDERTARGET) &&
                     desc.Pool == D3DPOOL_DEFAULT,
                 "FAIL render target texture description", desc.Width << 16 | desc.Height);
}
static void Release(StageTexture *texture, Surface *level) {
    if (level)
        SURFACE(Release, level);
    if (texture)
        STAGE_TEXTURE(Release, texture);
}

/* Fixed function, no blending, fog or depth; stage 0 selects arg 1 with point
 * filtering and clamping; stage 1 off. */
static BOOL State(Device *dev) {
#if DG_D3D_VERSION == 9
    if (!HR(DEVICE(SetVertexShader, dev, NULL), "FAIL render target vertex shader unbind"))
        return FALSE;
#endif
    return HR(SHADER_SET(dev, SHADER_NONE), "FAIL render target pixel shader unbind") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_LIGHTING, FALSE), "FAIL render target lighting") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ZENABLE, D3DZB_FALSE),
              "FAIL render target depth") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ZWRITEENABLE, TRUE),
              "FAIL render target depth write") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL),
              "FAIL render target depth function") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILENABLE, FALSE),
              "FAIL render target stencil") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_CULLMODE, D3DCULL_NONE),
              "FAIL render target culling") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_FILLMODE, D3DFILL_SOLID),
              "FAIL render target fill mode") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, FALSE),
              "FAIL render target blend") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ALPHATESTENABLE, FALSE),
              "FAIL render target alpha test") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_FOGENABLE, FALSE), "FAIL render target fog") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_SPECULARENABLE, FALSE),
              "FAIL render target specular") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_DITHERENABLE, FALSE),
              "FAIL render target dither") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
              "FAIL render target color op") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
              "FAIL render target alpha op") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXCOORDINDEX, 0),
              "FAIL render target coordinates") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE),
              "FAIL render target texture transform") &&
           HR(STAGE_POINT_FILTER(dev, 0), "FAIL render target filter") &&
           HR(RTT_ADDRESS(dev, D3DTADDRESS_CLAMP), "FAIL render target clamp") &&
           HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE),
              "FAIL render target stage 1") &&
           HR(DEVICE(SetTextureStageState, dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE),
              "FAIL render target stage 1 alpha");
}

struct Colored {
    float x, y, z, rhw;
    DWORD color;
};
struct Textured {
    float x, y, z, rhw;
    float u, v;
};
static_assert(sizeof(Colored) == 20 && sizeof(Textured) == 24);

/* Corners half a pixel before pixel centres, so a quad covers exactly
 * [left, right) x [top, bottom) and a 1:1 view samples texel centres. */
static BOOL Solid(Device *dev, const Layer &layer, float z) {
    const float x0 = layer.left - .5f, x1 = layer.right - .5f;
    const float y0 = layer.top - .5f, y1 = layer.bottom - .5f;
    const Colored quad[4] = {{x0, y0, z, 1, layer.color},
                             {x1, y0, z, 1, layer.color},
                             {x0, y1, z, 1, layer.color},
                             {x1, y1, z, 1, layer.color}};
    return HR(STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE), "FAIL render target quad FVF") &&
           HR(STAGE_SET_TEXTURE(dev, 0, NULL), "FAIL render target texture unbind") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
              "FAIL render target diffuse arg") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE),
              "FAIL render target diffuse alpha arg") &&
           HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
              "FAIL render target quad");
}
static BOOL Sample(Device *dev, StageTexture *texture, int width, int height, const Layer &layer) {
    const float x0 = layer.left - .5f, x1 = layer.right - .5f;
    const float y0 = layer.top - .5f, y1 = layer.bottom - .5f;
    const float u0 = (float)layer.sx / width, u1 = (float)(layer.sx + layer.sw) / width;
    const float v0 = (float)layer.sy / height, v1 = (float)(layer.sy + layer.sh) / height;
    const Textured quad[4] = {{x0, y0, Middle, 1, u0, v0},
                              {x1, y0, Middle, 1, u1, v0},
                              {x0, y1, Middle, 1, u0, v1},
                              {x1, y1, Middle, 1, u1, v1}};
    return HR(STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1), "FAIL render target view FVF") &&
           HR(STAGE_SET_TEXTURE(dev, 0, texture), "FAIL render target texture bind") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
              "FAIL render target texture arg") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
              "FAIL render target texture alpha arg") &&
           HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])),
              "FAIL render target view");
}
/* Paint layers [first, last) of image into the bound target in one scene:
 * clears as Clear rectangles, quads at depth Middle, views sampling texture
 * (width x height). Copies and gradients are made by their own calls. */
static BOOL Layers(Device *dev, const Image &image, UINT first, UINT last, StageTexture *texture,
                   int width, int height) {
    BOOL ok = TRUE;
    if (!HR(DEVICE(BeginScene, dev), "FAIL render target begin scene"))
        return FALSE;
    for (UINT i = first; i < last && ok; ++i) {
        const Layer &layer = image.layers[i];
        const D3DRECT rect = {layer.left, layer.top, layer.right, layer.bottom};
        if (layer.source)
            ok = !layer.drawn || Sample(dev, texture, width, height, layer);
        else if (layer.drawn)
            ok = Solid(dev, layer, Middle);
        else if (!layer.gradient)
            ok = HR(DEVICE(Clear, dev, 1, &rect, D3DCLEAR_TARGET, layer.color, 1, 0),
                    "FAIL render target rectangle clear");
    }
    return HR(DEVICE(EndScene, dev), "FAIL render target end scene") && ok;
}
static BOOL Paint(Device *dev, const Image &image, StageTexture *texture, int width, int height) {
    return Clear(dev, image.base) && Layers(dev, image, 0, image.count, texture, width, height);
}

/* Make copy layers [layers, layers + count) from one surface into another: a
 * D3D8 CopyRects of every rectangle; per layer a D3D9 UpdateSurface (update)
 * or POINT StretchRect. */
static BOOL Blit(Device *dev, Surface *from, Surface *to, const Layer *layers, UINT count,
                 BOOL update, const char *stage) {
#if DG_D3D_VERSION == 8
    RECT rects[4];
    POINT points[4];
    (void)update;
    if (!Check(count <= 4, stage, count))
        return FALSE;
    for (UINT i = 0; i < count; ++i) {
        const Layer &layer = layers[i];
        if (!Check(layer.sw == layer.right - layer.left && layer.sh == layer.bottom - layer.top,
                   "FAIL CopyRects cannot scale", i))
            return FALSE;
        rects[i] = {layer.sx, layer.sy, layer.sx + layer.sw, layer.sy + layer.sh};
        points[i] = {layer.left, layer.top};
    }
    return HR(DEVICE(CopyRects, dev, from, rects, count, to, points), stage);
#else
    for (UINT i = 0; i < count; ++i) {
        const Layer &layer = layers[i];
        const RECT source = {layer.sx, layer.sy, layer.sx + layer.sw, layer.sy + layer.sh};
        const RECT dest = {layer.left, layer.top, layer.right, layer.bottom};
        const POINT point = {layer.left, layer.top};
        if (!HR(update ? DEVICE(UpdateSurface, dev, from, &source, to, &point)
                       : DEVICE(StretchRect, dev, from, &source, to, &dest, D3DTEXF_POINT),
                stage))
            return FALSE;
    }
    return TRUE;
#endif
}

static void ColorCase(D3D *d3d, Device *dev, D3DFORMAT format, const char *stage, const char *frame,
                      const char *pass) {
    StageTexture *texture = NULL;
    Surface *level = NULL;
    Log(stage);
    const BOOL ok = Check(Claimed(d3d, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, format,
                                  "render-target texture CheckDeviceFormat"),
                          "FAIL render-target texture format not claimed", (DWORD)format) &&
                    Create(dev, Big, Big, format, &texture, &level) &&
                    Target(dev, level, NULL, Big, Big) && Paint(dev, painted, NULL, 0, 0) &&
                    Back(dev) && Paint(dev, painted_frame, texture, Big, Big) &&
                    Verify(frame, painted_frame);
    if (ok)
        Log(pass);
    STAGE_SET_TEXTURE(dev, 0, NULL);
    Back(dev);
    Release(texture, level);
}

/* Stencil 1 where the writer draws, then the masked quad only there. */
static BOOL Stencil(Device *dev) {
    return HR(DEVICE(SetRenderState, dev, D3DRS_STENCILENABLE, TRUE), "FAIL stencil enable") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILFUNC, D3DCMP_ALWAYS),
              "FAIL stencil function") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILREF, 1), "FAIL stencil reference") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILMASK, 0xff), "FAIL stencil mask") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILWRITEMASK, 0xff),
              "FAIL stencil write mask") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP),
              "FAIL stencil fail op") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP),
              "FAIL stencil depth-fail op") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE),
              "FAIL stencil replace") &&
           Solid(dev, StencilWriter, Middle) &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILFUNC, D3DCMP_EQUAL), "FAIL stencil equal") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILPASS, D3DSTENCILOP_KEEP),
              "FAIL stencil keep") &&
           Solid(dev, StencilMasked, Middle);
}

static void DepthCase(D3D *d3d, Device *dev) {
    StageTexture *texture = NULL;
    Surface *level = NULL, *depth = NULL;
    const Layer *q = layered_layers;
    Log("STAGE render target depth-stencil 1024x1024");
    const D3DFORMAT format = DepthFormat(d3d);
    if (!Check(format != D3DFMT_UNKNOWN, "FAIL render target depth: neither D24S8 nor D16 claimed",
               0))
        return;
    const BOOL stencil = format == D3DFMT_D24S8;
    if (!stencil)
        Log("SKIP render target stencil: D24S8 not claimed, depth from D16 only");
    const Image &image = stencil ? layered_stencil : layered;
    const Layer views[] = {
        View(8, 8, 64, 64, image, 860, 860),   View(88, 8, 64, 64, image, 960, 960),
        View(168, 8, 64, 64, image, 780, 780), View(8, 88, 64, 64, image, 60, 60),
        View(88, 88, 64, 64, image, 570, 830), View(168, 88, 64, 64, image, 640, 900),
    };
    const Image frame = Make(Background, views);
    const DWORD clear = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | (stencil ? D3DCLEAR_STENCIL : 0);
    BOOL ok = Create(dev, Big, Big, D3DFMT_X8R8G8B8, &texture, &level) &&
              HR(RTT_DEPTH(dev, format, &depth), "FAIL render target depth-stencil creation") &&
              Target(dev, level, depth, Big, Big) &&
              HR(DEVICE(SetRenderState, dev, D3DRS_ZENABLE, D3DZB_TRUE),
                 "FAIL render target depth enable") &&
              HR(DEVICE(Clear, dev, 0, NULL, clear, TargetBase, 1, 0),
                 "FAIL render target depth-stencil clear") &&
              HR(DEVICE(BeginScene, dev), "FAIL render target depth begin scene");
    if (ok) {
        const BOOL drawn = Solid(dev, q[0], Far) && Solid(dev, q[2], Near) &&
                           Solid(dev, q[1], Far) && Solid(dev, q[4], Near) &&
                           Solid(dev, q[3], Far) && (!stencil || Stencil(dev));
        ok = HR(DEVICE(EndScene, dev), "FAIL render target depth end scene") && drawn;
    }
    DEVICE(SetRenderState, dev, D3DRS_ZENABLE, D3DZB_FALSE);
    DEVICE(SetRenderState, dev, D3DRS_STENCILENABLE, FALSE);
    ok = ok && Back(dev) && Paint(dev, frame, texture, Big, Big) &&
         Verify(stencil ? "depth: D24S8, far quads drawn after near ones, stencil EQUAL"
                        : "depth: D16, far quads drawn after near ones",
                frame);
    if (ok)
        Log(stencil ? "PASS render target depth-stencil: 1024x1024 D24S8 with a 1024x1024 "
                      "target, near quads beat later far ones beyond and inside the window, "
                      "stencil REPLACE/EQUAL exact"
                    : "PASS render target depth: 1024x1024 D16 with a 1024x1024 target, near "
                      "quads beat later far ones beyond and inside the window");
    STAGE_SET_TEXTURE(dev, 0, NULL);
    Back(dev);
    if (depth)
        SURFACE(Release, depth);
    Release(texture, level);
}

static void ReturnCase(Device *dev) {
    StageTexture *texture = NULL;
    Surface *level = NULL;
    Log("STAGE render target return to the backbuffer");
    const BOOL ok =
        Create(dev, Big, Big, D3DFMT_X8R8G8B8, &texture, &level) && Clear(dev, Background) &&
        Layers(dev, returned, 0, 1, NULL, 0, 0) && Target(dev, level, NULL, Big, Big) &&
        Paint(dev, corners, NULL, 0, 0) && Back(dev) &&
        Layers(dev, returned, 1, returned.count, texture, Big, Big) &&
        Verify("return: quad from before SetRenderTarget kept, quad and views after", returned) &&
        Present(dev) && Paint(dev, normal, NULL, 0, 0) &&
        Verify("return: ordinary frame after Present", normal) && Present(dev);
    if (ok)
        Log("PASS render target return: backbuffer kept across SetRenderTarget, texture "
            "corners sampled, ordinary frame after Present exact");
    STAGE_SET_TEXTURE(dev, 0, NULL);
    Back(dev);
    Release(texture, level);
}

static void CopyCase(Device *dev) {
    StageTexture *big = NULL, *small = NULL;
    Surface *from = NULL, *to = NULL;
    Log("STAGE render target copies through " RTT_COPY);
    const BOOL ok =
        Create(dev, Big, Big, D3DFMT_X8R8G8B8, &big, &from) &&
        Create(dev, Small, Small, D3DFMT_X8R8G8B8, &small, &to) &&
        Target(dev, from, NULL, Big, Big) && Paint(dev, original, NULL, 0, 0) &&
        Target(dev, to, NULL, Small, Small) && Clear(dev, SmallBase) && Back(dev) &&
        Blit(dev, from, to, copied.layers, copied.count, FALSE,
             "FAIL " RTT_COPY " between render-target textures") &&
        Paint(dev, copied_frame, small, Small, Small) &&
        Verify("copy: 1024x1024 to 256x256 render-target texture, sampled", copied_frame) &&
        Clear(dev, Background) &&
        Blit(dev, from, backbuffer, direct.layers, direct.count, FALSE,
             "FAIL " RTT_COPY " render-target texture to backbuffer") &&
        Verify("copy: 1024x1024 render-target texture to the backbuffer", direct) &&
        Blit(dev, backbuffer, to, recopied.layers + copied.count, recopied.count - copied.count,
             FALSE, "FAIL " RTT_COPY " backbuffer to render-target texture") &&
        Paint(dev, recopied_frame, small, Small, Small) &&
        Verify("copy: backbuffer to 256x256 render-target texture, sampled", recopied_frame);
    if (ok)
        Log("PASS render target " RTT_COPY ": far sub-rectangles"
#if DG_D3D_VERSION == 9
            ", 2x and whole-surface 8:1/16:1 POINT"
#endif
            " between target textures, to and from the backbuffer exact");
    STAGE_SET_TEXTURE(dev, 0, NULL);
    Back(dev);
    Release(small, to);
    Release(big, from);
}

/* Lock a 64x64 X8R8G8B8 surface and write Ramp(x, y) ^ mask. */
static BOOL Write(Surface *surface, DWORD mask) {
    D3DLOCKED_RECT locked;
    if (!HR(SURFACE(LockRect, surface, &locked, NULL, 0), "FAIL offscreen surface lock"))
        return FALSE;
    const BOOL mapped = Check(locked.pBits && locked.Pitch >= Plain * 4,
                              "FAIL offscreen surface layout", (DWORD)locked.Pitch);
    if (mapped)
        for (int y = 0; y < Plain; ++y)
            for (int x = 0; x < Plain; ++x)
                *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4) = Ramp(x, y) ^ mask;
    return HR(SURFACE(UnlockRect, surface), "FAIL offscreen surface unlock") && mapped;
}

static void OffscreenCase(D3D *d3d, Device *dev) {
    StageTexture *texture = NULL;
    Surface *level = NULL, *first = NULL, *second = NULL;
    Log("STAGE render target offscreen surfaces through " RTT_UPLOAD);
    if (!Claimed(d3d, 0, D3DRTYPE_SURFACE, D3DFMT_X8R8G8B8,
                 "X8R8G8B8 plain surface CheckDeviceFormat")) {
        Log("SKIP offscreen surfaces: X8R8G8B8 plain surfaces not claimed");
        return;
    }
    const Layer *direct_copies = uploaded_frame.layers + UploadedViews;
    BOOL ok =
        Create(dev, Big, Big, D3DFMT_X8R8G8B8, &texture, &level) &&
        HR(RTT_PLAIN(dev, D3DPOOL_SYSTEMMEM, &first),
           "FAIL system-memory offscreen surface creation") &&
        HR(RTT_PLAIN(dev, D3DPOOL_DEFAULT, &second), "FAIL second offscreen surface creation") &&
        Write(first, 0) && Write(second, 0x00ffffff) && Target(dev, level, NULL, Big, Big) &&
        Clear(dev, TargetBase) && Back(dev) &&
        Blit(dev, first, level, uploaded.layers, 3, TRUE,
             "FAIL " RTT_UPLOAD " system-memory surface to render-target texture") &&
        Blit(dev, second, level, uploaded.layers + 3, 1, FALSE,
             "FAIL " RTT_COPY " offscreen surface to render-target texture") &&
        Paint(dev, uploaded_frame, texture, Big, Big);
#if DG_D3D_VERSION == 8
    ok = ok && Blit(dev, first, backbuffer, direct_copies, 1, FALSE,
                    "FAIL CopyRects image surface to backbuffer");
    ++direct_copies;
#endif
    ok = ok &&
         Blit(dev, second, backbuffer, direct_copies, 1, FALSE,
              "FAIL " RTT_COPY " offscreen surface to backbuffer") &&
         Verify("offscreen: Lock-filled gradients into a render-target texture, sampled, and "
                "into the backbuffer",
                uploaded_frame);
    if (ok)
        Log("PASS render target offscreen surfaces: Lock-filled gradients through " RTT_UPLOAD
#if DG_D3D_VERSION == 9
            " (SYSTEMMEM) and StretchRect (DEFAULT)"
#endif
            " beyond and across the window and into the backbuffer exact");
    STAGE_SET_TEXTURE(dev, 0, NULL);
    Back(dev);
    if (second)
        SURFACE(Release, second);
    if (first)
        SURFACE(Release, first);
    Release(texture, level);
}

/* Every case runs and logs its own result; any failure fails the probe. */
static void Run(D3D *d3d, Device *dev, Surface *target) {
    StageCaps caps = {};
    Log("STAGE render targets");
    if (!HR(DEVICE(GetDeviceCaps, dev, &caps), "FAIL render target device caps"))
        return;
    Number("MaxTextureWidth", caps.MaxTextureWidth);
    Number("MaxTextureHeight", caps.MaxTextureHeight);
    Number("StencilCaps", caps.StencilCaps);
    Number("ZCmpCaps", caps.ZCmpCaps);
#if DG_D3D_VERSION == 9
    Number("NumSimultaneousRTs", caps.NumSimultaneousRTs);
    Number("DevCaps2", caps.DevCaps2);
#endif
    if (!Check(caps.MaxTextureWidth >= (DWORD)Big && caps.MaxTextureHeight >= (DWORD)Big,
               "FAIL render target textures need 1024 texels", caps.MaxTextureWidth))
        return;
    backbuffer = target;
    original_depth = NULL;
    const HRESULT depth = DEVICE(GetDepthStencilSurface, dev, &original_depth);
    Number("backbuffer depth-stencil surface (D3DERR_NOTFOUND: none)", (DWORD)depth);
    if (FAILED(depth))
        original_depth = NULL;
    if (State(dev)) {
        ColorCase(d3d, dev, D3DFMT_X8R8G8B8, "STAGE render target texture X8R8G8B8 1024x1024",
                  "colour: X8R8G8B8 1024x1024 render-target texture, ten sampled views",
                  "PASS render target texture X8R8G8B8: 1024x1024 cleared and drawn beyond the "
                  "window, ten sampled views exact");
        ColorCase(d3d, dev, D3DFMT_A8R8G8B8, "STAGE render target texture A8R8G8B8 1024x1024",
                  "colour: A8R8G8B8 1024x1024 render-target texture, ten sampled views",
                  "PASS render target texture A8R8G8B8: 1024x1024 cleared and drawn beyond the "
                  "window, ten sampled views exact");
        DepthCase(d3d, dev);
        ReturnCase(dev);
        CopyCase(dev);
        OffscreenCase(d3d, dev);
    }
    STAGE_SET_TEXTURE(dev, 0, NULL);
    (void)RTT_ADDRESS(dev, D3DTADDRESS_WRAP);
    DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    STAGE_FVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    if (original_depth)
        SURFACE(Release, original_depth);
    original_depth = NULL;
}

} // namespace rtt_probe
