# Glide provider scope

DreamGPU ships two Glide providers, `glide2x.dll` and `glide3x.dll`. Both link the
same pinned OpenGLide core (with the checked DreamGPU patch stack) and translate
through the DreamGPU OpenGL channel used by WineD3D. The installer must put the
providers and their shared runtime in the Windows system directory. Games using
these APIs use normal Windows DLL loading; application-local deployment is a
diagnostic fixture only.

| API | Source | Current package | Remaining work |
| --- | --- | --- | --- |
| Glide2x | Pinned `vendor/qemu-xtra/openglide`, with checked DreamGPU adaptations | `glide2x.dll` | Finish installer ownership/removal and final OS/host acceptance |
| Glide3x | DreamGPU front end `guest/glide/glide3.cpp` and `glide3-core.h` on the same core; the donor `g3wrap.cpp` is not linked | `glide3x.dll` (Windows 98 and Windows 2000/XP packages) | Guest runtime acceptance (`DGSYSG3.EXE`, games) and the partial rows below |
| Glide 2.11 (`glide.dll`) | Donor `g2xwrap/wr211dll.cpp` and `gl211wrp.def` | Not shipped as a supported provider | Audit the older ABI and forwarding exports, add legacy compiler/import checks and a real public-API probe before packaging |

## Full screen

Both providers own the display while a Glide context is open, as Glide does on
Voodoo hardware (`support/guest/glide/patches/0004-fullscreen-default.patch`):

- `grSstWinOpen` switches the desktop to the Glide resolution at the current
  colour depth (`ChangeDisplaySettings` with `CDS_FULLSCREEN`, so nothing is
  written to the registry) and makes the game window a borderless popup at 0,0.
  If the desktop already has that size, or no such mode exists at that depth,
  the mode is left alone and only the popup is used.
- `grSstWinClose`, `grGlideShutdown` and every failed open restore the mode and
  the window's original style and position. Unloading the provider closes an
  open context the same way. Windows 2000/XP also restore a temporary mode when
  a process ends without closing Glide; on Windows 98 an abnormal exit can leave
  the Glide mode until the next mode change.
- `InitFullScreen=0` in `OpenGLid.ini` (in the game's working directory) keeps a
  window and changes no mode.

Earlier builds wrote `OpenGLid.ini` with `InitFullScreen=0` as a default, not a
choice. The configuration now carries its own version (`Version=0.09rc9-dg2`):
a file with any other version is regenerated once with `InitFullScreen=1`,
through the core's existing version check. Hand edits to such an older file
are reset by that regeneration; set `InitFullScreen=0` again afterwards for a
window. A file that cannot be written or removed (a read-only directory)
leaves the defaults in effect; previously this crashed or recursed.

`DGSYSGR.EXE`, `DGGLIDE.EXE` and `DGSYSG3.EXE` record `DISPLAY before/open/closed
<w>x<h>` and fail if a close does not restore the desktop size. The GL mode
change itself is the path `tools/opengl/mode-probe.c` checks (WGL recreated
after the change). `tests/guest/glide/test_fullscreen.py` compiles the patched
configuration and window code: defaults, regeneration, the windowed override,
unwritable directories, the mode and popup, and restoration on close and on
failed opens.

## Colour formats

`grSstWinOpen`'s colour format applies to every colour value an application
passes (constant, fog, clear and chroma key colours); palettes and LFB writes
have their own formats. The chroma key is converted to `0xAARRGGBB` and compared
with the ARGB palette. The donor rotated `GR_COLORFORMAT_RGBA` the wrong way, so
an RGBA key never matched: Diablo II (`-3dfx`, RGBA, key `0x000000ff`, opaque
black) drew its menu text and logo on black rectangles. Patch
`0005-chromakey-rgba.patch` fixes the rotation; `tests/guest/glide/test_chromakey.py`
runs the patched conversion and palette keying for all four formats.

A chroma-keyed batch with blending off is drawn under an alpha test
(`GEQUAL 0.5`) on the keyed texture alpha. The donor drew those batches in
immediate mode, five GL calls per vertex; patch `0006-chromakey-arrays.patch`
draws them from the same arrays as every other batch, with a three-component
colour pointer so vertex alpha stays 1 (`tests/guest/glide/test_chromakey_arrays.py`).
Voodoo hardware discards keyed pixels in every blend mode; the donor keyed only
unblended batches. Diablo II draws its menu text with chroma key on and
multiplicative blending (`ZERO, SRC_COLOR`), so the black around every glyph
multiplied the stone buttons to black. Patch `0007-chromakey-blended.patch` also
keys blended batches whose blend factors never read the source alpha while alpha
writes are off: there the alpha test's replacement fragment alpha cannot reach
the framebuffer. It restores the application's alpha function after the keyed
draw (the donor left `GEQUAL 0.5` behind). Blends that read the source alpha
(`SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`, `SRC_ALPHA_SATURATE`) or batches with alpha
writes on still draw keyed texels as black with alpha 0.

## Glide 3.x provider

`glide3x.dll` is a separate CMake target. It compiles the OpenGLide core units
exactly as `glide2x.dll` does, but replaces the donor `g3wrap.cpp` with the
DreamGPU front end and links its own export table, `guest/glide/glide3x.def`.
`glide2x.dll` keeps its historical source list (still including the unexported
donor wrapper), `Glide2x.def` and link options, so its exports are unchanged. The
shared core changes (such as [full screen](#full-screen)) apply to both.

The donor wrapper was not adapted. Besides the empty `grFinish`/`grFlush`, its
layout offsets were unbounded and offset zero meant "disabled", `GR_PARAM_Q1`
overwrote TMU1 `tow`, clip coordinates ignored the viewport origin and texture
scale, `grGet` reported success for unknown names and zero-filled caller buffers,
strip/fan helpers kept static state with inconsistent winding, and Glide 3 fog
modes reached the Glide 2 core with Glide 2 meanings.

### ABI

The DLL exports the 93 functions of the 3dfx Glide 3.x `glide.h`/`glideutl.h`
plus four exports the Voodoo `glide3x.dll` also carries (`grCheckForRoom`,
`grGlideGetVersion`, `grSetNumPendingBuffers`, `grSstConfigPipeline`): 97 stdcall
names such as `_grDrawVertexArray@12` and `_grSstWinOpen@28`. Each `.def` entry
maps `_grName@N` to `dg3_grName@N` and the target links with
`--disable-stdcall-fixup`, so a prototype whose argument bytes differ from its
export cannot link. `test_glide3_exports.py` checks the `.def` against the `dg3_`
prototypes (compiled for i686 Windows with `static_assert`s over their real
parameter types) and against an independent table of the SDK argument counts;
the package build compares the linked export table with the `.def`.

`GrContext_t` is a 32-bit handle, `GrTexInfo` carries log2 LOD/aspect values, and
`GrLfbInfo_t`, `GrResolution` and `Gu3dfInfo` keep their 32-bit layouts. The
`grGlideGetState` and `grGlideGetVertexLayout` blobs are DreamGPU records (sizes
from `grGet`) with a magic and version; the setters ignore foreign or corrupt
blobs.

### Coverage

| Area | Status | Notes |
| --- | --- | --- |
| `grGlideInit/Shutdown`, `grSstSelect` | Implemented | Init resets the vertex layout, coordinate space, depth range and counters |
| `grSstWinOpen/Close`, `grSelectContext` | Implemented | One context per board, as on Voodoo: a stable non-zero handle, selectable only while open; close rejects foreign handles; reopen on the same HWND |
| Glide 3 default state | Implemented | Clamp, point sampling, no mipmaps and a disabled texture combine at open; window coordinates and a full-window viewport |
| `grVertexLayout`, `grReset(GR_VERTEX_PARAMETER)` | Implemented | XY, Z, W, Q, FOG_EXT, A, RGB, PARGB, ST0/1 and Q0/1. Offsets must fit a 1024-byte vertex; an out-of-range enable disables the parameter. Disabled parameters are never read. Colour type follows the last enabled colour parameter. XY is read at offset 0, as by the hardware setup code |
| `grDrawPoint/Line/Triangle`, `grAADrawTriangle` | Implemented | Converted to the core's Glide 2 vertices; edge antialiasing is not emulated |
| `grDrawVertexArray(Contiguous)` | Implemented | Points, lines, line strips, triangles, strips, fans, polygons and both `_CONTINUE` modes using the setup unit's last two vertices and strip parity. Strip winding alternates for culling. Stride is Glide's `stride & ~3`. A null array or vertex pointer stops the draw; wrapping address arithmetic is refused |
| `grCoordinateSpace`, `grViewport`, `grDepthRange` | Implemented | Window coordinates: Z, Q (else W, else 1) and per-TMU Qn. Clip coordinates: 1/W, viewport origin and size, depth range, 0..1 float colours, ST scaled by 1/W and the texture's aspect scale |
| `grGet` | Implemented | Exact byte length per name; null buffers, wrong lengths and unknown names return 0 without writing. Pixel counters, rasterized-triangle counts and swap history are not observable and are not reported |
| `grGetString`, `grQueryResolutions`, `grReset` | Implemented | A single-TMU Voodoo Banshee, Glide 3.01, no extensions advertised. Resolutions follow the 3dfx template rules within the frame-buffer budget |
| `grGlideGet/SetState`, `grGlideGet/SetVertexLayout` | Implemented | Core state plus the fog table, STW hint and Glide 3 layout, coordinate space, viewport, fog mode and texture scale. The core replays colour combine before texture combine and blending last, which recomputed the order-derived combine flags (texture enable, local/other use) from stale state and disabled texturing; the blob carries those flags and restores them exactly (`test_glide3_state.py`) |
| Texture memory and downloads | Implemented | `grTexMin/MaxAddress`, `grTexCalc/TextureMemRequired`, `grTexSource`, `grTexDownloadMipMap/Level/LevelPartial`, `grTexDownloadTable/Partial`, `grTexNCCTable`: log2 descriptors are validated and translated; application descriptors are never modified. One TMU; TMU1 calls are ignored |
| `grLfbLock/Unlock/ReadRegion/WriteRegion` | Implemented | Reads must lie inside the frame buffer; writes are clipped to it. The Glide 3 `pixelPipeline` flag reaches the core lock |
| `grFinish/grFlush` | Implemented | Flush submits batched Glide work (`glFlush`); finish also waits for the GPU (`glFinish`) |
| Fog | Implemented | Glide 3 fog modes are renumbered for the core; FOG_EXT feeds the fog iterator |
| Combine, blend, depth, chroma, cull, clip, dither | Implemented | Enumerations are validated before reaching the core's tables |
| `gu3dfGetInfo/Load`, fog and gamma utilities | Implemented | Bounded `.3df` parser with Glide 3 header values; gamma uses the core's display ramp |
| `grEnable/grDisable` | Partial | Modes are recorded; `GR_AA_ORDERED` and video smoothing have no effect |
| LFB pixel pipeline | Partial | Only the clip rectangle applies; `GR_LFB_PIXEL_PIPE` reports false |
| FOG_EXT with W-buffering | Partial | Depth keeps Q, so fog then uses Q instead of the fog coordinate |
| `GR_TEXTABLE_PALETTE_6666_EXT` | Partial | Not advertised; colours are accepted, palette alpha stays opaque in the core |
| Mipmap downloads | Partial | As for Glide 2, the core keeps the largest level and generates the chain |
| `GR_TEXTURECLAMP_MIRROR_EXT` | Stubbed | Not advertised; wraps |
| `grStippleMode/Pattern` | Stubbed | Recorded only |
| `grTexDetailControl`, `grTexMultibase(Address)`, `grLfbWriteColorFormat/Swizzle`, `grLfbConstantAlpha/Depth`, `grSstConfigPipeline`, `grCheckForRoom`, `grSetNumPendingBuffers` | Stubbed | Validated and accepted; the core has no equivalent behaviour |
| `grGetProcAddress` | Stubbed | Returns null: no Glide 3 extensions are provided |

### Installation and verification

`glide3x.dll` is in the installer's shared runtime next to `glide2x.dll`, so the
same journalled before-image capture, staging, activation, rollback, repair and
uninstall code owns it on Windows 98 and Windows 2000/XP. An upgrade from an
installation without it records its absent original, and removal deletes it.

Activation still requires the six accepted normal-loader proofs (OpenGL, Glide 2,
Direct3D 6–9). Glide 3 has a separate oracle in the package tools, `DGSYSG3.EXE`
from `tools/glide/probe3.cpp`. Run it as `C:\DGSYSG3.EXE` with working directory
`C:\` (runner route `PROBE <request-id> sysglide3`, 90 s budget); it writes
`C:\DGSYSG3.LOG`, reads the frame once per stage, and reports each wrong block
with its first wrong pixel, actual and expected values.
It rejects preloaded or neighbouring Glide providers, requires `glide3x.dll` and
`dgpugl.dll` from the system directory, resolves all 97 decorated exports, checks
the `grGet` and `grQueryResolutions` contracts, and checks exact pixels for a
layout with disabled and out-of-range parameters, a contiguous strip under both
cull signs, a continued fan, clip coordinates, a log2 texture, restored state and
layout blobs and 16-bit LFB writes. It then performs eight swaps, closes the
context and reinitializes on the same window. Adding it to activation is a
separate decision after guest acceptance.

Source gates: `tests/guest/glide/test_glide3_core.py` (layout, conversion,
assembly, `grGet`, resolutions, textures, fog and `.3df` under ASan/UBSan),
`test_glide3_state.py` (the actual core combine/state code: replay defect and
exact restore), `test_glide3_exports.py` and `test_glide3_system_loader.py`.

The shared core's `grGlideSetState` has the same replay-order defect for
`glide2x.dll`; it is left unchanged there.

### Diagnostics

`DREAMGPU_GLIDE3_TRACE=<file>` makes `glide3x.dll` log every Glide 3 call with
its arguments, vertices, texture/palette checksums and results, after a header
naming the loaded `glide3x.dll`, `dgpugl.dll` and executable. It is off unless
the variable is set. See `guest/glide/README.md` for how to enable it and the
format; `tests/guest/glide/test_glide3_trace.py` checks the formatter and that
all 97 entry points are logged.

### Remaining

- Runtime acceptance of `DGSYSG3.EXE` and Glide 3 games on the six guest/host
  combinations. Recorded so far on Linux: `sysglide3` passes on Windows 98, 2000
  and XP, and Diablo II `-3dfx` (menus) on Windows 2000.
- The partial and stubbed rows above, multiple TMUs, and Glide 3.10/Napalm
  additions (extension entry points, 32-bit formats, 2048-texel textures).

## Other notes

The donor G2X wrapper Makefile defaults to `-march=x86-64-v2`; that build recipe is
not suitable for DreamGPU's 32-bit legacy guests. Providers use the existing
audited Cargo/CMake toolchain and retain their original notices and source
provenance.

`tools/glide/probe.cpp` is shared by diagnostic `DGGLIDE.EXE` and ordinary-system
`DGSYSGR.EXE`. The system variant rejects neighboring providers, verifies loaded
system paths, then checks 2,304 pixels, eight swaps and context recreation. Its
results establish those tested behaviors only; they do not establish all Glide
versions or every game's compatibility.
