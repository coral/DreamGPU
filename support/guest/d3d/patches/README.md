# WineD3D adaptations

The manifests apply to Wine9x commit
`8ab16c6c0930efc1f9138eddda7b3114d7f31e62`. Each patch and changed source file has
an exact SHA-256 identity before and after application. `prepare.py` applies the
base transaction first and the diagnostic transaction only when requested.
Compiler-prefix configuration and generated frontend import definitions remain
ordinary build inputs outside these source transformations.

The base patch preserves these existing behaviors:

- WineD3D imports the application-local `dgpugl.dll` and its WGL pixel-format
  aliases. Native DirectDraw switchers retain their Windows GDI dependencies.
- Capability detection uses a hidden 32×32 popup client area. The original
  decorated 10×10 outer window had an empty client rectangle on Windows 2000.
- The DreamGPU GL vendor receives a bounded 64 MiB translator texture-accounting
  budget. Wine otherwise guesses a RIVA128 with 4 MiB, insufficient for its
  desktop and another primary-sized surface. This is accounting, not a physical
  VRAM claim.
- D24S8 has framebuffer storage semantics without claiming depth-texture support.
  Rectangle textures and renderbuffers require the corresponding actual GL
  capabilities; unavailable types use Wine's existing padded 2D fallback.
- Failed DirectDraw surface creation records the original application descriptor
  and return location through the bounded failure-only diagnostic helper.
- Owned front/back swapchain buffers qualify for the GPU blitter even when the
  front buffer's usage flag is zero. Valid GPU source storage survives read-only
  maps; genuine partial CPU uploads propagate preparation failure. Existing
  desktop map/GetDC coherence remains in Wine.

The optional diagnostic patch instruments surface-location transitions and
selected readback/map/present boundaries. Its manifest preserves the original
numeric source-ID ordering and line numbers from the source after the base
patch, before instrumentation. The helper bounds aggregation and output; normal
builds do not include these copy counters.

Upstream Makefile bytes use CRLF. The base patch intentionally preserves them
and appends the same LF build-binding lines as the prior recipe. C sources use
LF. No fuzzy matching or silent line-ending normalization occurs here.

`0009-legacy-device-enumeration.patch` backports Wine 11.8's legacy enumeration
helper and interface wrappers. Direct3D 1/2 enumerate RAMP, RGB, then HAL;
Direct3D 3 enumerates RGB, then HAL. Microsoft Golf 1999 selects the second
Direct3D 1 device without checking the count, so exposing only HAL led to a null
GUID, failed device creation and a later null-material dereference. The backport
also retains upstream descriptor sizes, software capability masks, writable
names and callback cancellation. It does not enumerate the REF rasterizer or
change device creation/rendering.

Upstream source: [Wine 11.8 ddraw.c](https://github.com/wine-mirror/wine/blob/wine-11.8/dlls/ddraw/ddraw.c).
Relevant upstream changes are
[`f4626afe` (RAMP enumeration)](https://github.com/wine-mirror/wine/commit/f4626afec7c18e9e504bcee00484fdcba3ac9397),
[`440797bf` (RGB on version 1)](https://github.com/wine-mirror/wine/commit/440797bf08b831ca7cfb68b04dfc4009bf177b3e),
[`648cb489` (versioned descriptor sizes)](https://github.com/wine-mirror/wine/commit/648cb48954ccbbd34db97d76754724d9354917cf), and
[`2f561b97` (RGB device name)](https://github.com/wine-mirror/wine/commit/2f561b971bc3d708e062fcfe30d67fef08720b65).
The source retains Wine's LGPL licensing. `test_legacy_enumeration.py` executes
the actual prepared helper and all three wrappers against deterministic API
boundaries; the packaged D3D probe checks the guest interfaces themselves.

`0010-system-memory-render-targets.patch` gives owned DirectDraw system-memory
3D targets and depth buffers internal default-pool, CPU-accessible GPU storage.
Their public system-memory caps remain unchanged; ordinary CPU surfaces and
caller-owned `DDSD_LPSURFACE` storage keep their existing behavior. This is a
local adaptation for the pinned Wine architecture, not a modern Wine backport.
Microsoft Golf 1999 creates its RGB device on a system-memory RGB565 target;
the old translator returned success without binding that target and later
crashed while clearing color and depth. Invalid render-target views now fail
binding, and a color clear without a target reports an invalid call.
`test_system_memory_targets.py` executes the prepared storage policy, binding
and clear functions. The packaged D3D probe additionally checks actual RGB565
readback and preservation of CPU-written pixels outside a partial GPU clear.

`0011-thread-contexts.patch` brings the legacy `WIN32_NATIVE` context branch
in line with the production MinGW branch: reuse an already-selected pixel format
and share the device's texture/list namespace before publishing a new context,
with failure cleanup. It also rejects unmatched context releases. The production
build does not enable that legacy branch; Golf's actual cross-thread ownership
and drawable failures are addressed by 0012 below. `test_thread_pixel_format.py`
executes both prepared pixel-format helpers and sharing-failure cleanup.

`0012-native-context-migration.patch` retains one native context per swapchain,
including its color/depth drawable, and hands it between rendering threads
under Wine's existing mutex. The live threaded probe demonstrated that sharing
textures alone still left the main thread reading its old private framebuffer.
Outermost release now restores the caller's context or unbinds and clears Wine
TLS; WGL publishes pending commands before releasing ownership. Nested releases
keep the context active. Acquisition activates the retained context before
copying an old render target, preserves pending rebinds, and updates the owner
only after activation succeeds. Exact prior Wine context identity is retained
for restoration across devices; unrelated application GL bindings are preserved.
This supersedes 0011's explicit release flush and per-thread native contexts.
`test_context_migration.py` executes the prepared acquire/release/restore and
swapchain-selection functions with deterministic WGL boundaries, covering
handoff, multiple swapchains/devices, stale TLS, target-copy ordering and failures.

The bundled MinGW Makefile defines `USE_WIN32_OPENGL`, not `WIN32_NATIVE`.
Migration gates accept the actual build flag; they do not enable the unrelated
legacy `WIN32_NATIVE` pixel-format and context-creation branches. The migration
harness uses `USE_WIN32_OPENGL` alone and checks the Makefile binding. The
pixel-format harness covers both the production helper and the legacy helper.

`0013-paletted-textures.patch` exposes indexed 8-bit 2D textures on the
fixed-function backend and expands their attached palettes to BGRA on upload.
Microsoft Golf 1999 otherwise falls back to an RGB565 descriptor for its indexed
texture loader, rejects SetPalette, and never obtains any texture handles. Each
Wine texture owns its palette reference; replacing or editing that palette
invalidates GPU copies while retaining the original CPU indices. Conversion
respects palette alpha and index color keys. Cube, volume, render-target, A8P8,
and the separate fragment-program palette path are not newly advertised.
`test_paletted_textures.py` exercises the prepared conversion, attachment and
update paths; the packaged legacy probe verifies enumeration and rendered pixels
through Texture1 Load/GetHandle with real system-memory textures.
The new palette attachment API is listed in both `wined3d.spec` and the
checked `wined3d.def`: the shared NT5/Win98 Makefile links the latter directly
and does not regenerate it from the specification.
The drawable blitter compares converted P8 against transparent alpha zero;
only the unconverted fragment-program path compares palette indices in alpha.
`test_palette_blit.py` executes the prepared blitter with zero, nonzero and 255
keys, including conversion during texture loading.

`0014-context-readback-recovery.patch` lets a retained native GL context recover
from a temporary binding failure instead of remaining permanently invalid.
A successful real bind restores validity; destroyed contexts are rejected,
including stale TLS cases. Golf's minimized-window crash reached a framebuffer
read with an invalid cached context and then dereferenced null Wine TLS in the
fog wrapper. Framebuffer, texture and PBO downloads now propagate binding
failure without validating an unread CPU copy. CPU-only copies still work
without a GL context. `test_context_migration.py` executes the actual binding
helper and recovery paths; `test_readback_recovery.py` executes the prepared
readback/location pipeline through failures and a later successful retry. The
frontend separately retains existing drawable storage while the window has a
zero-sized client area.

`0015-map-dc-transfer-failures.patch` checks map storage and download results
before publishing CPU pointers or invalidating valid copies. GetDC keeps its
output unpublished on allocation, download or DIB failure; ReleaseDC keeps DC
ownership and CPU data when writeback fails, allowing a retry. CPU-only location
copies do not require a current GL context. `test_map_dc_failures.py` executes
the prepared public boundaries with allocation, context, transfer and retry
failures under ASan/UBSan.

`0016-legacy-indexed-immediate.patch` replaces silent BeginIndexed/Index success
with an owned copy of input vertices and checked index expansion into the
existing DrawPrimitive path. Begin/End nesting, invalid indices and vertex
strides, allocation overflow/failure and submitted-draw failures are handled
without dangling caller memory. Destruction releases both buffers.
`test_indexed_immediate.py` executes the prepared functions with caller-memory
mutation, interleaved devices and failures; `DGCAP6.EXE` adds public pixel checks.

`0017-depth-cpu-transfers.patch` supplies CPU depth transfers for the GL 1.1
backbuffer renderer. D16/D16_LOCKABLE, D32_UNORM, D24/S8, X8D24, D15/S1 and
D24/X4/S4 use the masks exposed by DirectDraw. Separate normalized depth and
integer stencil staging avoids requiring depth textures or packed GL extensions.
Readback commits only after both native reads succeed; uploads retain CPU
authority on failure, finish before validating GPU storage, and preserve pixel
transfer, matrix, raster and packing state. Transfers honor padded CPU pitch and
the onscreen/offscreen row orientation. Missing contexts, oversized surfaces,
unsupported layouts, PBO mappings and allocation/native failures return errors.
The upstream FBO path remains separate; this does not claim FBO CPU depth access
or floating depth formats.

Draw and clear preparation now reload CPU depth, stop after failed preservation,
and retain the untouched aspect of a depth-only or stencil-only clear. Stencil
rendering also participates in location invalidation. Switching an onscreen depth
buffer saves its backbuffer contents to CPU storage before changing ownership.
`test_depth_transfer.py` executes the prepared conversion, transfer and clear
preparation functions under ASan/UBSan, including guarded rows, both orientations,
all seven layouts, failed reads/writes and retries. The separate capability probe
uses public DirectDraw D16 Lock/Unlock, CPU edits and a partial GPU clear to verify
2,048 actual depth values. That installed-API probe still requires guest execution;
source tests alone do not establish GPU or game acceptance.

`0018-buffer-map-failures.patch` validates buffer lock ranges without overflow,
normalizes whole-buffer locks, checks acquired contexts, and rejects failed
native/CPU maps before publishing discard, dirty-range or synchronization state.
Failed attempts roll back map counts and preserve the output pointer for retry.
`test_buffer_map.py` runs boundary, read-only, discard, no-overwrite, nested-map
and retry cases against the prepared implementation under ASan/UBSan. Buffer
unmap failure behavior is not covered by this patch.

`0019-depth-state-guards.patch` handles overflow of attribute, client-attribute,
projection and modelview stacks before transferring pixels. Cleanup pops only
successfully saved frames. Depth transfer tests inject every push failure and
verify that prior frames, authoritative locations and CPU bytes are preserved.

`0021-map-bounds-and-fallbacks.patch` validates surface rectangles and the 2D
slice before allocation, transfers, pointer arithmetic or dirty-state publication.
This closes the unchecked D3D9 LockRect path; the prepared source tests reject
negative casts, reversed/empty rectangles, out-of-range edges and invalid slices,
while accepting the final valid pixel.

Native buffer maps record the access granted by the outermost mapping. Nested
locks cannot upgrade a read-only mapping to writable storage or read a write-only
mapping; compatible nested access and ordinary CPU double buffers remain valid.
A native pointer with unsupported alignment is unmapped and rejected without
entering the old unchecked allocation/download/delete-buffer fallback. The GPU
object and CPU storage remain owned and no dirty range is published. This is a
safe rejection, not a new aligned-copy implementation. `test_buffer_map.py`
executes both native mapping APIs, both nested access failures, compatible locks,
and dynamic/static unaligned failures under ASan/UBSan. Other buffer unload paths
and native unmap failures remain outside this fix.

`0022-depth-transfer-state-mask.patch` fixes the installed DirectDraw D16 Lock
failure exposed by the Win98 capability suite. Wine's generated header defines
`GL_ALL_ATTRIB_BITS` as `0xffffffff`, which the shipped OpenGL 1.1 frontend
rejects. The transfer now saves the eight state groups it actually changes,
using their defined GL 1.1 bits. The actual-source sanitizer harness imports the
pinned Wine GL constants and rejects unknown attribute bits, so it reproduces
this cross-layer failure instead of accepting every push through a no-op mock.
The public Clear → Lock → CPU edit → partial Clear → Lock probe remains the
installed acceptance check.

`0023-depth-transfer-entrypoints.patch` checks every core GL function needed by
a depth transfer before calling the private provider. The Wine loader resolves
`dgpugl.dll` exports; functions reachable only through the system ICD table do
not satisfy that contract. Missing state or read/write operations now fail
without issuing GL work or changing authoritative storage. Downloads remain
available when only an upload operation is absent. The prepared-source test
checks pointer failures, complete preflight coverage, and agreement with the
private provider's export definition; its implementations share the ICD helpers.
