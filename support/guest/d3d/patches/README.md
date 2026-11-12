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


`0024-primary-gdi-coherence.patch` reads accelerated primaries from the actual
GDI desktop instead of treating a live swapchain as proof that its private GL
image is current. Partial primary publication seeds the native front from that
desktop; keyed primary writes also refresh pixels inside the destination before
applying the key. Full replacements retain their fast path. FFP blits load the
source before the destination (which can share a physical backbuffer), preserve
the destination for partial/keyed writes, and propagate transfer failures before
invalidating CPU storage. Back-to-front Present applies only to complete positive
rectangles. ReleaseDC flushes this thread's GDI batch before accessing DIB bits,
retaining the DC and its authoritative image if flushing or copying fails.

`test_frontbuffer_coherence.py` executes the prepared primary and FFP helpers
under ASan/UBSan with independent desktop, logical primary and native front
storage. Exact pixels cover partial publication, keyed holes, whole copies and
failed transfers. `test_map_dc_failures.py` includes delayed GDI writes and flush
failure/retry. These source checks do not establish Golf acceptance; the public
installed fullscreen probe and game lifecycle remain required.

`0025-fullscreen-mode-handoff.patch` keeps windowed swapchains out of fullscreen
activation handling: losing focus must not restore the desktop mode or minimize
and resize a window that never owned fullscreen state. Explicit display-mode
requests always reach native `CDS_FULLSCREEN`, including a game's request for
the launcher's existing resolution. Equal dimensions alone do not establish
native process ownership. Wine's [native user32 regression](https://list.winehq.org/hyperkitty/list/wine-gitlab@list.winehq.org/thread/MWHNDHCGDELZ2O6UF4SOXBMYI2MHOT6N/)
checks two processes requesting the same mode: exiting the earlier caller leaves
the later caller's mode active. Redundant restoration still avoids a native call
and updates the adapter's reported format. Native failures retain the prior format
and refresh-rate fallback remains available.

`test_fullscreen_activation.py` exercises the prepared activation and display-mode
functions, including windowed no-ops, fullscreen focus changes, same-mode native
ownership requests and failure paths. Launcher-to-game acceptance still requires
a disposable Windows guest; these source tests do not establish cross-process
message ordering or native process-exit restoration by themselves.


`0026-primary-surface-loss.patch` keeps DirectDraw primary loss visible after an
exclusive application relinquishes scanout or receives a display-mode change.
Window callbacks cannot take the Wine mutex, so an atomic primary-loss epoch
records this transition without walking live surfaces. IsLost, Lock, GetDC,
Blt/BltFast, Flip and pending primary publication reject the lost primary until
Restore. Restore requires exclusive availability and matching display dimensions
and format, restores implicit primary flip buffers together, and retains loss on
failure or a concurrent mode change. RestoreAllSurfaces propagates restore errors.
Windowed focus changes alone do not mark storage lost. System-memory surfaces
and unrelated video textures keep their contents and existing Wine loss state.

This models lost scanout ownership, not blanket loss of native GL allocations:
those remain retained across ordinary display-mode changes. The API contract is
[IDirectDrawSurface7::Restore](https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdrawsurface7-restore).
`test_primary_loss.py` executes prepared callbacks, IsLost and Restore under
ASan/UBSan; public entrypoint ordering is checked alongside the existing pixel
coherence regressions. Actual game focus recovery remains an installed test.

`0027-ffp-textureless-stages.patch` enables a texture target on an active
fixed-function stage that reads no texture (TFACTOR, DIFFUSE, CURRENT only).
GL applies a unit's texture environment only while that unit has an enabled
target, so Wine silently skipped such stages' combiners once more than one unit
was available. The unit keeps Wine's dummy texture bound when the application
sets none, matching D3D's sampling of an unbound stage. Unmapped stages, stages
beyond the GL unit limit, disabled stages and pixel-shader draws are unchanged.
`test_ffp_stages.py` executes the prepared `tex_colorop` under ASan/UBSan and
fails without the patch. The installed D3D8/9 stage probe draws every claimed
two-stage op and a three-stage `ADD` then `TFACTOR MODULATE` chain; that chain
also needs the frontend's GL1.2 packed uploads, since Wine creates the dummy
textures as RGBA `UNSIGNED_INT_8_8_8_8`.

`0028-discarded-depth-sysmem.patch` defines the CPU copy of a discarded depth
surface instead of requesting a native transfer. Offscreen depth surfaces have no
GL storage without depth textures, and only the device-owned drawable has a
transfer path, so mapping an application's spare Z-buffer failed and every
depth-to-depth DirectDraw Blt returned `D3DERR_INVALIDCALL`. Links 2003 copies
its course depth into such a buffer at startup and exited on that error.
Discarded contents are undefined in D3D; the copy is zeroed for determinism.
Real contents, buffer bindings, GL-side locations and non-owned drawables keep
their previous paths. `test_discarded_depth.py` executes the prepared function
under ASan/UBSan and fails without the patch. The diagnostic stack is rebased
onto it with its call-site line numbers renumbered.

`0029-arbfp-fixed-function.patch` adapts WineD3D to `GL_ARB_fragment_program`,
which selects its ARB shader backend, arbfp fragment pipeline and arbfp
blitter instead of the fixed-function pipeline and blitter:

- Indexed (P8) surfaces, the front buffer included, keep expanding to BGRA on
  the CPU with an identity fixup, and 2D P8 textures stay advertised (0013
  gated them on the absence of ARB_fragment_program). Wine would otherwise give
  P8 a palette-shader fixup that the pipeline rejects for textures, so
  Microsoft Golf 1999 again lost its texture handles, and blits would read
  expanded alpha as an index. Shader colour keying (which arbfp claims) still
  converts P8 keys to alpha zero on the CPU, and the P8 shader key matches
  exactly those texels.
- Vertex shaders never compile, bind or enable a vertex program without
  `GL_ARB_vertex_program` (VertexShaderVersion stays 0, so the vertex constant
  arrays are empty); the backend otherwise wrote past them and enabled a
  vertex program mode DreamGPU rejects unless `DG_CAP_GL_VERTEX_PROGRAM` is
  negotiated (see 0033).
- The fixed-function replacement program declares its colour key after the
  fog `OPTION`: ARBfp accepts options only before any statement, so colour key
  plus fog (common in Direct3D 7) failed to compile.
- NP2 coordinate fixups are program locals; as environment parameters they
  overwrote the application's `c0`.. constants whenever a shader sampled a
  padded NP2 texture.
- Projected bump mapping emits `MAD` instead of a three-operand `MUL`.
- PS 2.0 `NumTemps` is clamped to the D3D9 range (native ARB reports more).
- CPU depth uploads disable a fragment program left enabled by the last draw;
  `ENABLE_BIT` (0022) restores it.

`test_arbfp_adaptations.py` checks each change in the prepared source. The
diagnostic stack is rebased onto it with its call-site line numbers renumbered.

`0030-arb-constants-without-current-context.patch` marks ARB shader constants
dirty even when no context is current. The ARB backend's dirty arrays belong to
the device, but Wine marked them only when the changing thread had a current
context. Since 0012 the outermost context release clears Wine's current context,
so a `SetPixelShaderConstantF`/`SetVertexShaderConstantF` between draws found
none. The previous constants then stayed loaded when the same context returned,
because only a context switch forces a full reload. The installed D3D8/9 shader
probe drew its second constant with the first's values until this patch.
`test_arb_constant_dirty.py` fails without it.

`0031-arbfp-sign-fixup-temp.patch` declares `TEMP TA` in the arbfp
fixed-function generator. `gen_color_correction()` swizzles sign-fixed sources
through `TA`, which only the shader-model generators declared. A V8U8 (or other
emulated signed-format) texture in a fixed-function stage therefore produced a
program that failed to parse, so `D3DTOP_BUMPENVMAP` drew nothing.
`test_arbfp_adaptations.py` checks the declaration.

`0032-ddraw-sysmem-3ddevice-p8.patch` excludes palette-indexed and FOURCC
formats from 0010's GPU backing for owned system-memory 3D surfaces. They cannot
be render targets, so they stay ordinary CPU surfaces, as DirectDraw's HEL
allows. SimCity 3000 creates its news ticker as a 640x48 P8
`OFFSCREENPLAIN | SYSTEMMEMORY | 3DDEVICE` surface. Creation failed, and the game
showed an error that stayed invisible behind its black exclusive-mode screen,
then exited on the next key. A first version asked wined3d whether the format
could be a render target. Without FBOs that check requires a matching WGL pixel
format, so it also refused RGB565 system-memory targets (the installed D3D6
probe's legacy RGB device failed). `test_system_memory_targets.py` covers
palettized, FOURCC, RGB, texture and depth cases.

`0033-arbvp-vertex-shaders.patch` adapts WineD3D to `GL_ARB_vertex_program`.
With 256 native vertex env parameters the ARB backend compiles D3D8/9 vertex
shaders (VertexShaderVersion 2.0, D3D8 1.1), and paths 0029 left dormant run:

- `glVertexAttrib2sv` maps to its ARB name. ARB_vertex_program exports only
  `glVertexAttrib2svARB`, so the core-name pointer stayed NULL and a stride-zero
  or immediate SHORT2 attribute called it.
- VS 2.0 `NumTemps` is clamped to the D3D9 range (native ARB reports more).
- `VERTEX_PROGRAM_POINT_SIZE_ARB` is enabled only for shaders that write `oPts`.
  Wine never touched it, so every other shader had GL's fixed point size
  instead of `D3DRS_POINTSIZE`, and one writing `oPts` was ignored.
- CPU depth uploads disable a vertex program left enabled by the last draw, as
  0029 does for fragment programs: the program would otherwise transform the
  raster position. `ENABLE_BIT` (0022) restores it.
- Without `ARB_vertex_buffer_object` the zero vertex attribute
  (`VBOX_WITH_WINE_FIX_ZEROVERTATTR`, bound whenever a declaration leaves
  attribute 0 unused or gives it stride zero, since attribute 0 provokes the
  vertices) is a process-wide client array repeating the value, at least 65536
  entries so every 16-bit index is covered. Wine called the NULL
  `glGenBuffersARB` to create a VBO instead.

`test_arbvp_adaptations.py` checks each change in the prepared source, executes
the client array helper under ASan/UBSan, and checks that the generator still
emits the line templates of the vs_1_1/vs_2_0 programs
`tests/native_qemu/vertex_program.rs` loads on Mesa. The diagnostic stack is
rebased onto it with its call-site line numbers renumbered.

`0034-geforce-fx-npot-quirk.patch` keeps the GeForce FX quirk from enabling
`GL_ARB_texture_rectangle` on an implementation that never reported it.
WineD3D does not recognize DreamGPU's GL vendor, so it reports the NVIDIA
fallback card for the device's D3D level. With fragment programs alone that
was a GeForce 256 (`10de:0100`). With 0033's VS 2.0 it is a GeForce FX 5800
(`10de:0302`), which matches the "Geforce 5 NP2 disable" quirk. The quirk
cleared NPOT and set rectangles supported unconditionally, on the assumption
that the driver had reported them and Wine had only hidden them behind NPOT.
DreamGPU reports neither extension, so Wine sent every NPOT surface (front
buffer, backbuffer) through `GL_TEXTURE_RECTANGLE_ARB`. The frontend rejected
that target, and DirectDraw, D3D6 and D3D7 presented black. Detection now
records whether full NPOT support hid a reported rectangle extension. The
quirk does nothing without NPOT and restores rectangles only when they were
hidden, so real FX drivers behave as before. No other quirk depends on the
NVIDIA device. The reported identity is otherwise the table's: "NVIDIA GeForce
FX 5800", `nv4_disp.dll` 6.14.11.7516 on NT (4.14.11.7516 on Win9x), with
DreamGPU's fixed 64 MiB of video memory (0001). `test_geforce_fx_quirk.py`
runs the prepared card guess, the NPOT bookkeeping and the quirk under
ASan/UBSan. The diagnostic stack is rebased onto it.

`0035-volume-row-alignment.patch` states the unpack alignment of volume
uploads. DreamGPU's `EXT_texture3D` (`DG_CAP_GL_TEXTURE_3D`) makes WineD3D
claim `D3DPTEXTURECAPS_VOLUMEMAP`, and volume levels then travel through
`glTexSubImage3D`. A volume's memory pads every row to the device's surface
alignment (`wined3d_volume_get_pitch`, four for D3D8/9), while rows converted
for upload (V8U8, L6V5U5, ...) are tight. A context is created with unpack
alignment one, but every `VBOX_WITH_WINE_FIX_SURFUPDATA` surface upload
leaves it at the surface alignment. So a volume whose row bytes are not a
multiple of four (an L8 or A8L8 mip level, an odd-width 16-bit level, a
converted level) was read with whichever alignment came last: skewed texels,
or reads past the end of the converted copy. Uploads now use the surface
alignment for volume memory and one for converted rows, then restore the
previous value; the frontend answers `GL_UNPACK_ALIGNMENT` locally, so this
costs no host query. Downloads need no change: the pack alignment stays the
surface alignment (the depth transfers push and pop it). `test_volume_alignment.py`
runs the prepared upload under ASan/UBSan. The diagnostic stack does not touch
`volume.c`.

`0036-volume-map-binding.patch` gives volumes a SYSMEM map binding. This wine9x
tree comments out `resource_init`'s default `map_binding`; surfaces set their
own, volumes never did. `wined3d_volume_map` invalidates every location except
the binding, so with a zero binding it also discarded the system-memory copy
that LockBox had just exposed. The next texture load then found no location
("Implement texture loading from 0") and the volume sampled black. The
installed volume probe failed this way on Win2000 until this patch.
`test_volume_map_binding.py` checks the prepared source.

Patches 0037-0040 adapt WineD3D to DreamGPU's `ARB_vertex_buffer_object`
(`DG_CAP_GL_VERTEX_BUFFER_OBJECT`). With the extension, non-dynamic D3D vertex
and index buffers outside `D3DPOOL_SYSTEMMEM` get GL buffer objects; dynamic
buffers stay in system memory, since this Wine creates buffer objects for
them only with `ARB_map_buffer_range` or `APPLE_flush_buffer_range`, which
DreamGPU does not advertise. The guest frontend keeps a copy of every buffer
(the guest cannot address host memory), so a GL map returns that copy and
unmapping re-sends all of it.

`0037-vbo-double-buffered-uploads.patch` keeps the system memory copy of
every buffer object (`WINED3D_BUFFER_DOUBLEBUFFER`). Locks then never map GL:
this Wine mapped static buffers `GL_READ_WRITE` for the whole Lock-Unlock
interval, so every Unlock re-sent the whole buffer, a draw from a locked
buffer read a mapped buffer, and the pointer had to stay 16-byte aligned.
`buffer_direct_upload` now sends each dirty range with `glBufferSubData`
instead of mapping write-only, which also re-sent everything. Creation no
longer marks the copy it just uploaded with `glBufferData` dirty, which sent
each buffer twice; a needed conversion reloads it regardless.

`0038-vbo-conversion-offsets.patch` repairs fixed-function vertex conversion,
which every buffer object holding XYZRHW data needs, and D3DCOLOR data without
BGRA vertex arrays (a host without `DG_CAP_GL_VERTEX_ARRAY_BGRA`). This tree's
`VBOX_WITH_WINE_FIX_BUFOFFSET` conversion never set the stream element's
offset or the buffer's stream offset, marked only the first byte of each
converted attribute and left the D3DCOLOR swizzle commented out, so colours
reached GL with red and blue swapped and pretransformed positions unconverted;
it also re-read the whole buffer on every declaration check and eventually
dropped the buffer object. The element offset, the stream offset and every
attribute byte are now recorded, the swizzle runs, and the upload covers the
range widened to whole vertices. Without the BUFOFFSET macros the structure
layouts would differ between units, so the repair stays in that code.

`0039-vbo-mixed-stream-fallbacks.patch` keeps immediate mode on memory.
`context_update_stream_info` preloads every stream before taking any buffer
object: a preload that stops converting unloads its buffer object, and
elements taken earlier in the same loop kept the deleted name. Immediate mode
(`drawStridedSlow`, `drawStridedSlowVs`) reads every element and index from
memory, but draws mixing buffer objects with system memory streams reach it
with buffer object offsets, which it dereferenced as pointers near zero.
`draw_primitive` now replaces buffer objects with their system memory copies
for any immediate-mode draw (not only emulation) and reads indices from the
index buffer's copy. FLOAT16 declarations with vertex shaders always take
immediate mode, since DreamGPU has no `ARB_half_float_vertex` and buffer
objects would be read as floats.

`0040-vbo-zero-attribute.patch` fixes the zero attribute buffer that VBox's
`VBOX_WITH_WINE_FIX_ZEROVERTATTR` binds when a vertex shader leaves attribute 0
without a stream (patch 0033 gave the no-buffer-object case a client array).
It was sized to the draw's index count, so an array draw from a nonzero start
or indices beyond that count read past it; it now covers `start + count`
vertices of an array draw and at least 65536 for an indexed draw (every 16-bit
index; larger 32-bit indices stay uncovered, and the host refuses such a draw
instead of reading past the buffer). It was never deleted, so after a device
Reset the stale name bound, or overwrote, whichever buffer reused it; it is now
deleted with the device's contexts and at uninit.

DreamGPU's packed XYZRHW/D3DCOLOR path (`guest/d3d/wine-vertex-array.h`)
returned to Wine's per-vertex immediate mode whenever the extension existed;
it now packs any draw whose elements are all in system memory (0039 guarantees
that in immediate mode) and binds `ARRAY_BUFFER` zero for its client arrays.
`test_vbo_adaptations.py` checks 0037, 0039 and 0040 in the prepared source and
runs 0038's converted-attribute bookkeeping under ASan/UBSan;
`test_vertex_arrays.py` covers the packed path. The diagnostic stack is
rebased onto 0040 with its call-site line numbers renumbered.

`0041-fog-coordinate-arrays.patch` draws D3D vertex fog supplied in the
specular alpha ("manual fog coordinates": FOGTABLEMODE NONE with FOGVERTEXMODE
NONE or pretransformed vertices, linear from 255 to 0) from arrays. Wine sent
every such fixed-function array draw to `drawStridedSlow`, one immediate-mode
call per attribute and vertex, because `glFogCoordPointerEXT` takes no
unsigned bytes. The array draw now gets a client float fog coordinate array
holding exactly what `drawStridedSlow` passes to `glFogCoordfEXT`:
`(float)(specular >> 24)` of each vertex it reads, under its own condition
(fog enabled, table fog off, untransformed vertex fog off or an XYZRHW position,
a D3DCOLOR specular). The array covers the GL array indices the draw reads:
`start..start + count - 1`, or the referenced index range (indices from memory
or an index buffer's system memory copy) plus GL's base vertex. Vertex
`load_base_vertex_index + j` backs array index `j`, which is
`drawStridedSlow`'s index plus `base_vertex_index`. Buffer objects are read
through their system memory copy (0037), which conversion never writes (its
D3DCOLOR swizzle keeps alpha in place anyway); system memory streams are read
in place. Afterwards the last vertex's coordinate is current again, as
`drawStridedSlow` leaves it for a later draw without coordinates. A fogged draw
without specular-alpha coordinates is drawn from arrays without the array,
since `drawStridedSlow` sets none either.

The emulation remains where the array draw could differ: vertices or indices
beyond their buffers, a stride-zero element (D3D repeats it, named GL arrays
read it packed), a loaded base vertex that differs from `drawStridedSlow`'s,
generic attributes, instancing, or no position. It also remains when the fog
coordinates themselves are emulated: `gl_compat.c`'s fallback (a GL without
`EXT_fog_coord`, e.g. a host without `DG_CAP_GL_FOG_COORD`) exists only in
immediate mode, so it now clears the driver's `glFogCoordPointerEXT`, which
Wine loads regardless of the extension string. DreamGPU's packed XYZRHW path
requires that entry point for its fog array as well; it used to draw such fog
through the frontend's rejected array, without fog.

Immediate-mode draws no longer set the emulation flag (they already read every
element from memory), so the `d3d_perf` "Using software emulation because
manual fog coordinates are provided" warning no longer reports them. Without
BGRA vertex arrays Wine still draws any fixed-function D3DCOLOR stream outside
buffer objects in immediate mode: UP draws, dynamic and system-memory buffers
(with them, 0047, those reach this array path too). Textured XYZRHW draws
of up to 64 vertices can take the packed path, which already fed fog
coordinates as an array. `test_fog_coord_arrays.py` runs the prepared
helpers under ASan/UBSan against a reference read the way
`drawStridedSlow` reads memory and checks the draw and fallback structure; it
fails without the patch. The installed D3D7 probe and the D3D8/9 vertex buffer
probe draw specular-alpha fog from buffers and user memory with exact pixels;
the emulation produced the same pixels, so they check results, not the path.
The diagnostic stack is rebased onto 0041 with its call-site line numbers
renumbered.

Patches 0042-0045 adapt WineD3D to DreamGPU's framebuffer objects
(`DG_CAP_GL_FRAMEBUFFER_OBJECT`: `EXT_framebuffer_object`,
`EXT_framebuffer_blit`, `EXT_packed_depth_stencil`). With the extension Wine
selects `ORM_FBO` instead of backbuffer offscreen rendering. Because this tree
defaults to `always_offscreen`, every swapchain with a backbuffer then renders
into an FBO and presents with `glBlitFramebuffer` (`swapchain_blit`), and the
window's pixel format carries no depth buffer; offscreen surfaces render into
their own textures, `fbo_blit` handles GPU colour and depth blits, and
`check_fbo_compat` decides render-target and post-pixel-shader-blending support
per format at adapter initialization. DreamGPU reports neither NPOT nor
rectangle textures nor `ARB_depth_texture`, so depth surfaces are depth
textures that cannot be sampled or read with `GetTexImage`.

`0042-fbo-padded-2d-targets.patch` keeps NPOT resources in padded 2D textures.
`resource_init` skips the 2D type of an NPOT resource "to try texture
rectangle"; without rectangles the next candidate was a renderbuffer, and since
this tree disables the texture usage check, every NPOT resource took it:
backbuffers, depth buffers, DirectDraw offscreen plain surfaces and textures. A
renderbuffer draw binding (`WINED3D_LOCATION_RB_RESOLVED`) is loaded only by
resolving a multisampled renderbuffer, so CPU uploads and partial blits into
such a surface were lost, and `swapchain_blit` presents from the texture. The
2D type is now skipped only when rectangles exist; a renderbuffer remains the
fallback for a format whose 2D texture the FBO cannot attach. This is the
padded texture 0001 already selected without framebuffer objects. NPOT targets
are therefore limited by `GL_MAX_TEXTURE_SIZE` after padding (2048), not by the
renderbuffer limit. `test_fbo_padded_targets.py` runs the prepared
`resource_init` under ASan/UBSan.

`0043-fbo-depth-transfers.patch` gives 0017's CPU depth transfers an FBO mode.
An offscreen depth surface now has GL storage of its own, its draw binding, but
0017 read and wrote only the device-owned drawable, and `GetTexImage` cannot
read depth here: locking a depth surface failed, and CPU depth (a Lock/Unlock,
0028's defined discarded copy) never reached the GPU. `surface_transfer_depth`
now attaches the surface's own texture or renderbuffer alone to an FBO (no
draw or read buffer) and reads `DEPTH_COMPONENT`/`STENCIL_INDEX` or draws
`DEPTH_COMPONENT` pixels in offscreen, unflipped orientation, with the same
state saving, entry-point preflight (0022, 0023), program disabling (0029,
0033) and failure handling. Stencil formats are drawn packed as
`DEPTH_STENCIL`/`UNSIGNED_INT_24_8`: the GL contract has no `STENCIL_INDEX`
DrawPixels on an FBO. Storage is allocated before an upload; ownership of the
drawable is not involved. `surface_load_ds_location` (draws, clears, depth
blits) uploads a CPU-only surface to the requested binding, or to the depth
texture before upstream's texture-to-drawable copy. Upstream completed a partly
current binding by copying the window's depth over it, which only makes sense
when the drawable holds that depth; otherwise (always here) the binding is now
complete as it is, for example a depth `Blt`/`StretchRect` destination whose
size Wine leaves at zero. The drawable/texture copies and the discarded
shortcut are unchanged. `test_fbo_depth_transfers.py` runs the prepared
functions in all seven depth layouts through texture and renderbuffer
attachments; `test_depth_transfer.py` keeps covering the drawable mode, which
never binds an FBO.

`0044-fbo-mixed-size-attachments.patch` attaches a shared depth surface
directly. `EXT_framebuffer_object` requires attachments of one size, so Wine
attached a depth surface larger than the render target through a separate
renderbuffer of the target's size: the surface's contents never reached it,
and `surface_load_ds_location` then refused to load the surface, which skips
draws and clears. D3D shares one depth buffer between targets of different
sizes (a reflection, shadow or glow target with the auto depth buffer).
DreamGPU's completeness follows GL 3 rules, so adapter initialization now asks
the implementation (a 16x16 colour and 32x32 depth renderbuffer, one
`CheckFramebufferStatus`); `ARB_framebuffer_object` implies the answer, and an
implementation that reports `INCOMPLETE_DIMENSIONS` keeps the substitution.
`test_fbo_mixed_size_attachments.py` runs the prepared probe and
`surface_set_compatible_renderbuffer`.

`0045-fbo-blit-destinations.patch` covers blits that drew into the window
drawable before. A surface that is its own source is one GL image under FBOs:
GL leaves a blit, copy or textured draw between overlapping regions of it
undefined (DirectDraw scrolling within a surface). `wined3d_surface_blt` sends
such a blit to the CPU blitter, which copies overlapping rectangles in a safe
order; non-overlapping blits within a surface keep `glBlitFramebuffer`. The
arbfp blitter drew into any destination, and one the FBO cannot attach dropped
the blit while the destination was marked current; it now declines such a
destination unless it is a render target, as `fbo_blit_supported` and the
fixed-function colour fill already do. `test_fbo_blit_destinations.py` runs the
prepared helper and `arbfp_blit_supported` and checks the guard's place. The
diagnostic stack is rebased onto 0045 with its call-site line numbers
renumbered.

Under `ORM_FBO` the drawable-specific adaptations stay in place but are
dormant: 0002 (backbuffer blit bounds), 0017's drawable transfers and 0020's
ownership tracking apply only to `ORM_BACKBUFFER`. Alpha blending into an
offscreen target requires the format's post-pixel-shader-blending flag, which
`check_fbo_compat` now measures by drawing a blended quad into each format and
reading it back.

`0046-point-sprite-origin.patch` keeps point sprite textures upright. Wine
flips offscreen rendering vertically and compensates point sprites with
`GL_POINT_SPRITE_COORD_ORIGIN` (`LOWER_LEFT` offscreen, `UPPER_LEFT`
onscreen), but sets it only on GL 2.0. With framebuffer objects every D3D
target renders offscreen, so on DreamGPU's GL 1.1 every point sprite texture
was upside down (without them only offscreen targets were). DreamGPU's scalar
`glPointParameterfARB` (`ARB_point_parameters`) takes the origin, while
`glPointParameteri` is not exported. Adapter initialization therefore asks once
below GL 2.0 with `ARB_point_sprite`: it sets `LOWER_LEFT`, checks for a GL
error and restores the default. When the driver accepts it, the state handler
that only warned (`psorigin_w`) sets the same origin as upstream's GL 2.0
handler through the scalar float form, from its own context. A driver that
rejects the parameter keeps the warning. `test_fbo_point_sprite_origin.py`
runs the prepared probe and handler under ASan/UBSan. The diagnostic stack is
rebased onto 0046.

`0047-bgra-zero-attribute.patch` adapts WineD3D to DreamGPU's BGRA vertex
arrays (`DG_CAP_GL_VERTEX_ARRAY_BGRA`, `ARB_vertex_array_bgra`). With the
extension a D3DCOLOR's `gl_vtx_format` is `GL_BGRA`, which upstream Wine
already passes as the size of colour, secondary colour and generic attribute
arrays (normalized), so most of the change needs no patch. Fixed-function
draws no longer take immediate mode for D3DCOLOR diffuse or specular streams
outside buffer objects (`slow_mask`): D3D8/9 UP draws and dynamic or `SYSTEMMEM`
buffers, and D3D5-7 `DrawPrimitive` from user memory and system-memory vertex
buffers, now draw from client arrays, and so reach 0041's fog array.
Pretransformed (XYZRHW) streams outside buffer objects still take immediate
mode, since the fixed-function vertex pipeline has no `xyzrhw`; 0048 draws
them from arrays. D3DCOLOR in buffer
objects is no longer converted (only XYZRHW positions are, through 0038's
repaired bookkeeping) and draws from the buffer with `GL_BGRA`. Vertex shaders
read D3DCOLOR through `GL_BGRA` generic arrays instead of an in-shader
swizzle (`swizzle_map` stays zero); their stride 0 and immediate-mode
D3DCOLOR attributes already swizzle to RGBA before `glVertexAttrib4Nubv`.

The patch fixes the one path that took `gl_vtx_format` for a component count.
VBox's zero attribute (`VBOX_WITH_WINE_FIX_ZEROVERTATTR`) binds a stride 0
attribute 0 of a vertex shader draw from its own array or buffer, and
`zv_bind_by_element` passed `GL_BGRA` (0x80e1) as the count: `zv_create`
refused it, the client array was never built, and attribute 0 pointed at the
previous zero attribute buffer or at nothing. A `GL_BGRA` element now binds
its colour swizzled to RGBA as four normalized unsigned bytes, as the other
stride 0 attributes do; every other format is unchanged.
`test_vertex_array_bgra.py` runs the prepared zero attribute functions under
ASan/UBSan against a recording GL, with and without buffer objects, and fails
without the patch; it also checks the format, swizzle, immediate-mode,
conversion and array paths above in the prepared source. The diagnostic stack
is rebased onto 0047 (only its `device.c` hashes change).

`0048-xyzrhw-arrays.patch` draws pretransformed (XYZRHW) fixed-function
vertices outside buffer objects from arrays. Such a draw still selects immediate
mode (`use_immediate_mode_draw`: this pipeline has no `xyzrhw`, so arrays would
read the positions unconverted), and `drawStridedSlow` sent every vertex as one
call per attribute: position, colours, each coordinate set, fog coordinate.
That covers D3D5-7 `DrawPrimitive`, `DrawIndexedPrimitive` and
`DrawPrimitiveStrided` from user memory and system-memory vertex buffers, D3D8/9
UP draws, `DYNAMIC` and `SYSTEMMEM` buffers, and declarations mixing those with
buffer objects (draw_primitive reads the buffer objects' system memory copies,
0039): the 2D, UI, sprite and much of the terrain rendering of DX5-7 games.
`drawStridedSlow` now tries, after 0006's packed path (textured draws of up to 64
vertices), one array draw of exactly the values it would send. GL array index j is vertex vertex_base + j of every
stream, which is the vertex `drawStridedSlow` reads: `glDrawArrays(0, count)`
from vertex `start`, or `glDrawElements` with the draw's own indices (the index
buffer's system memory) plus `base_vertex_index`; `load_base_vertex_index` plays
no part, as in `drawStridedSlow`. The positions of the referenced vertex range
are converted into a client array by `position_float4`'s expression (x, y, z
times w = 1 / rhw and w, or x, y, z, 1 for rhw 0 and 1); with `-std=c99`'s
standard excess precision both round w and each product to float the same way
on the x87. The fog coordinates are 0041's (`dg_fog_coord_range` finds the
index range, `dg_fog_coord_fill` the values) under `drawStridedSlow`'s
condition. Every other attribute is read in place through the array whose
values are those of the immediate-mode call it replaces: `glNormal3fv` (FLOAT3,
FLOAT4), `glColor3fv`/`glColor4fv`/`glColor4ubv` (FLOAT3, FLOAT4, UBYTE4N),
`diffuse_d3dcolor` as a `GL_BGRA` colour array, `glSecondaryColor3fvEXT` and
`specular_d3dcolor` as a `GL_BGRA` secondary colour array (which drops alpha,
as GL does), `glMultiTexCoord{1,2,3,4}fv` and `{2,4}sv`. Texture stages are
selected as `drawStridedSlow` selects them (mapped, textured or under a pixel
shader, application coordinates), textured stages without a coordinate set get
(0, 0, 0, 1), absent normals, colours and secondary colours get its defaults,
and point size and blend weights stay ignored. Array draws leave the current
attributes of their arrays undefined, so the last vertex's attributes are sent
again by the calls `drawStridedSlow` made for it (a later draw without fog
coordinates reads the current one). The draw binds `ARRAY_BUFFER` (and for
indices `ELEMENT_ARRAY_BUFFER`, which immediate mode leaves at zero) to zero,
disables every array again and clears the converted arrays' pointers, and
returns the client unit to 0. Up to 64 vertices convert on the stack, larger
ranges on the heap; nothing GL is called before every check has passed.

These XYZRHW draws keep `drawStridedSlow`: a stride-zero element (D3D repeats
it, an array reads it packed); a vertex or index outside its stream or index
buffer, or a negative vertex; a D3DCOLOR that Wine swizzles (no usable
`ARB_vertex_array_bgra`; 0006 still packs small textured ones); per-vertex
material tracking; SHORT4N/USHORT4N colours, FLOAT16 coordinates (NV half) or a
non-FLOAT4 position; fog coordinates emulated by `gl_compat.c` while fog uses
coordinates; coordinates on a unit beyond the client units (gl_compat.c's
one-unit multitexture); stippled line strips (the frontend cannot split them
across packets); named or numbered arrays still loaded; generic attributes
(`drawStridedSlowVs`). Draws whose streams are all buffer objects keep their
converted buffers, and 0041's emulated fog stays `drawStridedSlow` too. Buffer
object conversion (`fixup_transformed_pos`) keeps w = 0 for rhw 0 where
immediate mode, and so this path, sends w = 1; that difference predates 0048.

`test_xyzrhw_arrays.py` compiles the prepared `drawStridedSlow`, 0041's helpers,
the new path and `directx.c`'s attribute functions and table under ASan/UBSan
against a recording GL that assembles vertices as GL does (immediate-mode
current values, array reads by size, type, stride, BGRA and normalization,
current values undefined after an array draw). Each case draws once in immediate
mode and once through 0048 and compares every vertex attribute and the current
attributes left behind bit for bit: 45 draws (sizes around the local storage up
to 5998 vertices, 16- and 32-bit indices with positive, zero and negative base
vertices, rhw 0, 1, -0 and others, four units with swapped, missing, generated,
remapped and untextured stages, a pixel shader, every accepted format, every fog
state, two streams, no buffer objects, secondary colour or real multitexture)
and 30 fallbacks, which must not call GL. It fails without the patch. It also
prices the recorded calls with the frontend's record format (32-byte headers,
one CALL record per immediate-mode attribute, one DATA record per array draw as
`gl-arrays.h` lays it out, 1024 records and 64 KiB per packet):

| Draw | Immediate: calls / records / bytes / packets | Arrays |
| --- | --- | --- |
| 1000 TL quads (diffuse, specular fog, one unit), a draw each | 23000 / 23000 / 1116000 / 23 | 25000 / 6000 / 504000 / 8 |
| 1000 diffuse-only quads, a draw each | 12000 / 12000 / 588000 / 12 | 12000 / 4000 / 312000 / 5 |
| 300-vertex TL list | 1503 / 1503 / 74128 / 2 | 25 / 6 / 10864 / 1 |
| 3000-vertex TL list | 15003 / 15003 / 740120 / 15 | 25 / 7 / 105488 / 3 |
| 64x32 TL grid, 11718 indices | 58593 / 58593 / 2890564 / 58 | 26 / 12 / 411220 / 8 |

The array path's extra calls for a small draw (pointers, enables, the array
buffer binding) stay in the frontend; it sends a quarter to a third of the
records and about half the bytes. The installed D3D7 probe and the D3D8/9 probe
(`xyzrhw-probe.inc`) draw a 312-vertex user-memory list of cells with specular
plus specular-alpha fog, indexed draws with base vertices and decoys from user
memory and dynamic and system-memory buffers (D3D7: `DrawIndexedPrimitiveVB`
start vertices and `DrawPrimitiveStrided`; D3D9: a static POSITIONT stream with
a system-memory colour stream), and a 322-vertex two-unit textured strip whose
rhw grows from 1 to 3, so the perspective-correct texel boundary shows a wrong
1/rhw, with exact pixels. Immediate mode gives the same pixels, so they check
results, not the path. The diagnostic stack is rebased onto 0048 with its
`drawprim.c` call-site line renumbered.

`0049-rectangle-texture-types.patch` puts NPOT resources into rectangle
textures exactly where a rectangle can hold them. With DreamGPU's rectangle
textures (`DG_CAP_GL_TEXTURE_RECTANGLE`: `GL_ARB_texture_rectangle` and
`GL_NV_texture_rectangle`, no `ARB_texture_non_power_of_two`, GL 1.1) WineD3D
reports what a GeForce FX reaches through the "Geforce 5 NP2 disable" quirk:
the FX driver's GL 2.0 NPOT is removed and its rectangles restored, while here
the quirk has nothing to remove (0034) and the rectangles stay. TextureCaps
gain `NONPOW2CONDITIONAL` beside `POW2`, `CUBEMAP_POW2` and `VOLUMEMAP_POW2`:
0x7ec9f becomes 0x7ed9f (D3D9, after its mask: 0x7ec87 becomes 0x7ed87). Three
places then disagreed with the rectangle rules.
`init_format_texture_info` gave compressed formats rectangle flags, although
`ARB_texture_rectangle` has no compressed internal formats (`INVALID_ENUM`).
`resource_init` (after 0042) skipped the 2D type of every NPOT resource to try
a rectangle, and since this tree disables the texture usage check, a DXT
texture took the rectangle anyway and a depth buffer, which a rectangle cannot
hold without `ARB_depth_texture`, fell through to the renderbuffer 0042 had
removed. The 2D type is now skipped only when the rectangle type passes the
texture, render-target, depth-stencil and FBO-attachability checks for the
format and usage; otherwise the padded 2D texture stays. And
`surface_private_setup` and `wined3d_surface_update_desc` unpadded every
surface whenever rectangles existed, including those of a padded 2D texture,
whose `pow2_matrix` `texture_init` computes for the padded size; a surface is
now unpadded only in a rectangle texture (or with full NPOT support). NPOT
colour textures, render targets, offscreen plain surfaces and backbuffers
become exact-size rectangles (800x600 stays 800x600, no 1024x1024 texture);
blits, presents (`glBlitFramebuffer`), readback (`GetTexImage`,
`CopyTexSubImage2D` into the rectangle), the fixed-function enable and the
arbfp blitter and replacement shaders already handle `GL_TEXTURE_RECTANGLE_ARB`
with unnormalized coordinates, and D3D9 pixel shaders sample them as `RECT`
through the np2 fixup constants. Depth-stencil surfaces stay padded 2D textures
and attach beside an exact-size colour rectangle through 0044's mixed sizes.
NPOT DXT textures keep their padded 2D type and therefore today's answer
(`D3DERR_NOTAVAILABLE`, compressed padded surfaces are unsupported), P8 keeps
its CPU-expanded 2D texture (it has no rectangle flags), and a mipmapped NPOT
texture remains `D3DERR_INVALIDCALL` (`texture_init`), as for conditional NPOT
hardware. `CheckDeviceFormat` has no dimensions and is unchanged.
`test_rectangle_texture_types.py` runs the prepared caps computation (DreamGPU,
and a real FX driver through 0034's bookkeeping and the quirk, give the same
TextureCaps), the format flag assignment, `resource_init` and
`surface_private_setup` under ASan/UBSan; `test_fbo_padded_targets.py` covers
the unchanged rectangle-free selection. The diagnostic stack is rebased onto
0049 with its `surface.c` call-site line numbers renumbered.

`0050-depth-texture-formats.patch` makes Direct3D depth formats textures
exactly where a GeForce FX 5800 had them. With `DG_CAP2_GL_DEPTH_TEXTURE`
DreamGPU reports `GL_ARB_depth_texture` and `GL_ARB_shadow`, and
`init_format_texture_info` no longer clears the depth formats' TEXTURE flag:
`CheckDeviceFormat(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE)` answered D16,
D24X8, D24S8, D16_LOCKABLE, D32 and the INTZ FourCC (and depth cube textures,
whose flags `ARB_texture_cube_map` copies) available, where every one was
`D3DERR_NOTAVAILABLE` before. A GeForce FX samples D16, D24X8 and D24S8 only,
as NVIDIA's hardware shadow maps (`WINED3DFMT_FLAG_SHADOW`: the texture
compares the coordinate's r with the stored depth), and has no cube or volume
depth textures (nor has `ARB_depth_texture`; DreamGPU's frontend reports
them as `GL_INVALID_OPERATION`). The depth rule now always clears the cube and
volume TEXTURE flags of depth/stencil formats and, with `ARB_depth_texture`,
the remaining ones of every depth format but those three; without it nothing
changes. Rectangles can then hold the three formats, so NPOT depth-stencil
surfaces of them become exact-size rectangles like colour targets (0049's
rule; D32 and D16_LOCKABLE keep padded 2D textures), which the host attaches,
copies, clears and reads as rectangle depth images. Depth-stencil surfaces
themselves (`D3DRTYPE_SURFACE`) are unchanged. `test_depth_textures.py` runs
the prepared assignment on the actual depth entries of `format_texture_info`
and the actual `wined3d_check_device_format` for every depth format before and
after the capability under ASan/UBSan; `test_rectangle_texture_types.py`
runs `resource_init` for the rectangle choice.

`0051-shadow-sampling-programs.patch` makes those textures compare in ARB
fragment programs. Sampler state turns `TEXTURE_COMPARE_MODE` on for a shadow
format, but `ARB_fragment_program_shadow` leaves a lookup through an ordinary
target of a comparing depth texture undefined, and upstream's GLSL backend
alone wrote shadow samplers. WineD3D now knows
`GL_ARB_fragment_program_shadow`; the fixed-function replacement keys each
stage by a shadow bit (in `texture_stage_op`'s padding, also cleared for a
disabled stage, so the memcmp key stays exact) and the pixel shader backend by
the existing `ps_compile_args.shadow`; both emit
`OPTION ARB_fragment_program_shadow;` before any statement and the
`SHADOW1D`, `SHADOW2D` or `SHADOWRECT` target (TEX, TXP and TXB alike), so
D3D projected lookups compare r/q. Cube and volume targets never compare (an
FX has no such depth textures). `cs.c` already invalidates the pixel shader
state when a stage's shadow flag changes. `test_depth_textures.py` checks the
prepared key, option placement and target selection and runs the selection
helper under ASan/UBSan; the native QEMU test runs SHADOW1D/2D/RECT programs
with TEX and TXP. The diagnostic stack is rebased onto the new
`wined3d_private.h`.

`0052-ffp-vertex-blend.patch` gives the fixed-function pipeline vertex
blending (`D3DRS_VERTEXBLEND` 1, 2 or 3 weights over `WORLD`..`WORLD3`)
without `GL_ARB_vertex_blend`. WineD3D blended only through that extension,
which Mesa lacks and a GeForce FX never listed; without it `vp_ffp_get_caps`
claimed `gl_info->limits.blends` (one matrix), `state_vertexblend_w` printed a
FIXME and blended draws came out in their bind pose. The FX's Direct3D driver
blended four matrices itself and had no indexed blending, so the caps now
claim `MAX_VERTEX_BLENDS` (4) and index 0 without the extension (D3D8/D3D9
MaxVertexBlendMatrices 4, MaxVertexBlendMatrixIndex 0; D3D7
wMaxVertexBlendMatrices 4), and `ffp_vertex_blend_matrices` keeps the
WORLD1-3 states. A fixed-function draw with 1-3 weights in its vertices
(`dg_vertex_blend_init`: float FLOAT1-4 or D3DCOLOR weights, float positions
with three or four components, float normals) blends each position into world
space as `sum_i w_i (v WORLDi)` and each normal through every WORLDi inverse
transpose (`dg_vertex_blend_normal_matrix`, identity for a singular matrix as
Mesa's inverse), the last weight being 1 minus the others, exactly
ARB_vertex_blend's weighted sum with `WEIGHT_SUM_UNITY` but in world rather
than eye space, and draws under the view matrix alone (`glPushMatrix`,
`glLoadMatrixf(view)`, `glPopMatrix`). Wine specifies lights, clip planes and
texture generation planes under the view matrix, and lighting, `NORMALIZE`,
texture generation, fog and point sizes read eye space, which
`view * sum(w_i WORLDi v)` leaves unchanged; normals factor the same way for
affine world matrices. Array draws read every stream's system memory copy
(0037) for GL array indices first..last (0041's index scan, plus the load base
vertex), fill client arrays of positions (FLOAT 4) and normals (FLOAT 3) in
place of Wine's own two, and put Wine's pointers back exactly as
`load_vertex_data` set them, so the next draw needs no reload; every other
array stays a buffer object or system memory array, which the frontend's
buffer draw mixes. `drawStridedSlow` sends the blended normal and position of
each vertex instead. Draws without blending take no new GL call; tweening, 0
weights (indexed blending), vertex shaders, pretransformed vertices, other
formats and vertices outside their streams keep the unblended draw. The
weight count follows `D3DRS_VERTEXBLEND`, as Direct3D defines it, not the
vertex format (missing weight components are 0). `test_vertex_blend.py` runs
the prepared caps, state handler and blending helpers under ASan/UBSan against
an eye-space ARB_vertex_blend reference (2-4 matrices, every weight format,
non-uniform scale and shear, base vertices, buffer-object and system-memory
streams, every fallback before any GL call); `test_xyzrhw_arrays.py` stubs the
new hooks of `drawStridedSlow`; the native QEMU test `vertex_blend.rs` draws
the resulting records with lighting, fog and a clip plane under the view; the
installed D3D8/9 probe (`vertex-blend-probe.inc`) checks the caps and exact
two-, three- and four-matrix and lit pixels. The diagnostic stack is rebased
onto 0052 with its `drawprim.c` call-site line renumbered.

`0053-ffp-vertex-tween.patch` gives the fixed-function pipeline vertex
tweening (`D3DRS_VERTEXBLEND` `D3DVBF_TWEENING`, `D3DRS_TWEENFACTOR`), which
WineD3D never implemented: `state_vertexblend_w` and `state_vertexblend`
printed a FIXME, the declaration's second position and normal (usage index 1:
D3D9 `POSITION1`/`NORMAL1`, D3D8 `D3DVSDE_POSITION2`/`NORMAL2`, which d3d8
already converts to index 1) had no fixed-function stream slot
(`fixed_get_input` printed a FIXME for each whenever the stream info was
rebuilt), and the first
position was drawn. Direct3D documents `POSITION = POSITION1 * (1 -
TWEENFACTOR) + POSITION2 * TWEENFACTOR`, the same for normals, before lighting
and clipping, mutually exclusive with weighted blending; a GeForce FX claimed
`D3DVTXPCAPS_TWEENING` (D3D9 VertexProcessingCaps 0x17b). Tweening takes
0052's path with `matrices` 0: `dg_vertex_tween_init` finds the first
position and normal in Wine's stream info and the second ones in the vertex
declaration (the first of each usage index 1 element, not the tessellation
pseudo-stream), and `dg_vertex_tween_sources` reads them from their streams'
system memory copies (0037) at stream offset plus element offset, every
vertex the draw reads checked against the stream (0048's span check). Each
vertex is tweened in object space in that formula's order (all four position
components, a missing w being 1; without a second position or normal the
first is copied, and a second normal without a first is ignored), so factor
0 draws exactly the first values and 1 the second, and replaces Wine's
position and normal as 0052's client arrays (array draws) or
`glNormal3fv`/`glVertex4fv` (`drawStridedSlow`, whose vertices
`dg_vertex_tween_immediate` checks before `glBegin`). The modelview stays
Wine's WORLD and VIEW: no matrix call is made, and Wine's array pointers are
restored as 0052 restores them. Vertex shaders, pretransformed positions, a
generic-attribute pipeline, positions or normals other than FLOAT3/FLOAT4,
per-instance data, second streams missing, too short or of stride zero, and
indices beyond them keep the untweened draw (FIXME once); draws without
tweening, blended draws among them, are unchanged, as tweening and blending
are selected by the same render state. Tweening does not depend on
`ARB_vertex_blend`, whose handler now disables GL blending for it without the
FIXME. `vp_ffp_get_caps` claims `WINED3DVTXPCAPS_TWEENING`, so D3D9 reports
the FX's 0x17b (before 0x13b) and D3D8 0x17f (before 0x13f; D3D8 copies
Wine's bits unmasked, including D3D7's VERTEXFOG and D3D9's TEXGEN_SPHEREMAP),
while D3D7's mask drops it. `fixed_get_input` declines usage index 1
positions and normals without the FIXME. `test_vertex_tween.py` runs the
prepared caps, state handler, `fixed_get_input` and helpers under ASan/UBSan
against a double-precision reference of the documented formula (one and two
streams, stream offsets, FLOAT3/FLOAT4, position-only and normal-only, factors
0, 1/2, 1, fractions and extrapolation, factors 0 and 1 and integer midpoints
bit for bit, array and immediate-mode draws, every fallback before any GL
call); `test_vertex_blend.py` and `test_xyzrhw_arrays.py` follow the shared
helpers; the installed D3D8/9 probe (`tween-probe.inc`) checks the cap and
exact tweened pixels. The diagnostic stack is rebased onto 0053 with its
`drawprim.c` call-site line renumbered.

`0054-d3d8-shader-validation.patch` replaces d3d8.dll's `ValidateVertexShader`
and `ValidatePixelShader` stubs with upstream Wine's implementation and the
runtime's signatures: `ValidatePixelShader(code, caps, return_error, errors)`
and `ValidateVertexShader(code, declaration, caps, return_error, errors)`. The
stubs named the declaration and caps "reserved", failed with `E_FAIL` whenever
one was given and never wrote the error string. Now the version token is
checked (vs.1.0-1.1, ps.1.0-1.4), and against `VertexShaderVersion` or
`PixelShaderVersion` when caps are given; when `errors` is not NULL it receives
a `HeapAlloc(GetProcessHeap())` string, empty unless `return_error` is set and
the call failed, which the caller frees with `HeapFree` (a NULL pixel shader
returns `E_FAIL` without one, as the runtime does). D3DX8's assembler, linked
statically into Unreal Tournament 2003's `D3DDrv.dll` and 3DMark2001 SE's
`e2_d3d8_driver_mfc.dll`, finds both in the loaded `d3d8.dll` after assembling
each shader, passes NULL caps and declaration, `return_error` and a
NULL-initialized string when its caller wants errors, turns a failure into
`D3DXERR_INVALIDDATA` and frees the string with `HeapFree(GetProcessHeap(), 0,
...)` (disassembly of both binaries); with those arguments the stubs already
returned `S_OK` for valid versions, so the patch makes the contract exact but
is not by itself shown to change those games. `test_shader_validation.py` runs
the prepared functions under ASan/UBSan against the real `D3DCAPS8` layout and
a recording process heap: upstream Wine's `test_validate_vs`/`test_validate_ps`,
every version token against caps none, below, equal and above, declarations,
every `return_error`/`errors` combination with each string freed once,
allocation failure, D3DX8's exact calls, and the export signatures against
`d3d8.spec` and the switcher's `FUNC4`/`FUNC5` entries. The installed D3D8
probes (`shader-validation-probe.inc` in `DGD3D8` through `wined8.dll` and
`DGSYS8` through the system `d3d8.dll` switcher) make the same calls through
`GetProcAddress`. The diagnostic stack does not touch `d3d8_main.c`.

`0055-switcher-provider-exports.patch` routes every export the Wine provider
implements through the switcher's user list. Patch 0003 set `unkToSystemDll`,
so `switcherLoadUserLib` fills the d3d8 and ddraw user lists only from
`*_list_safe.h` (d3d9's from `d3d9_list_safe.h` whenever `msd3d9.dll` is
loaded); every other export dispatches to the system list, whose slots stay
the switcher's own stubs when no system DLL loads (`msd3d8.dll`, then
`system32\d3d8.dll`, which is the switcher itself and which `tryLoad`
rejects). d3d8's safe list held only `Direct3DCreate8`, so on Win2000/XP images
without `msd3d8.dll` D3DX8's `ValidatePixelShader`/`ValidateVertexShader`
calls reached stubs returning `E_FAIL` without a string, and every shader
D3DX8 assembled in Unreal Tournament 2003 (DM-Antalus terrain) and 3DMark2001
SE failed with `D3DXERR_INVALIDDATA`; with `wined8.dll` as an application-local
`d3d8.dll` the terrain renders. Per DLL:
- d3d8: the safe list adds `DebugSetMute`, `D3D8GetSWInfo`,
  `ValidatePixelShader` and `ValidateVertexShader`, everything `wined8.dll`
  exports. `CheckFullscreen`, which Wine lacks, stays on the system list.
  `D3D8GetSWInfo` becomes `FUNC0`: Wine's is an argument-less stdcall, and the
  `FUNC4` stub would have popped 16 bytes. The `Direct3DCreate8` stub returns
  NULL instead of `DDERR_GENERIC` as an interface pointer.
- d3d9: the safe list already covers `wined9.dll`'s exports except
  `Direct3DShaderValidatorCreate9`, which stays with Microsoft's d3d9 when that
  is loaded (Wine's returns NULL; without `msd3d9.dll` the full list reaches
  Wine's). Stubs: `Direct3DCreate9` and `Direct3DShaderValidatorCreate9`
  return NULL, `D3DPERF_GetStatus` and `D3DPERF_QueryRepeatFrame` 0 ("not
  profiling"), instead of `DDERR_GENERIC`.
- ddraw: unchanged routing. `winedd.dll` also exports `AcquireDDThreadLock`,
  `ReleaseDDThreadLock` and, as aliases of those argument-less functions,
  `DDInternalLock`, `DDInternalUnlock`, `InternalLock` and `InternalUnlock`;
  native components call them on native ddraw objects (and the `FUNC2`/`FUNC1`
  `DDInternalLock`/`DDInternalUnlock` signatures do not match the aliases), so
  they stay on the system DLL. The `DllGetClassObject` stub returns
  `CLASS_E_CLASSNOTAVAILABLE` instead of `S_FALSE`, which claims success
  without an object.

Every stub's argument count now equals Wine's stdcall argument count where Wine
declares one. The `E_FAIL` `Validate*` stubs remain for a switcher that is
not ready or a blacklisted caller without a system d3d8; D3DX8 initializes the
string it frees, so they cannot hand it garbage. `test_switcher_dispatch.py`
builds each switcher's prepared loaders, lists and stubs and `switcher.c`'s
`switcher_init`/`tryLoad` natively under ASan/UBSan against a mock loader whose
modules export what the Wine and native `.def` files list, executes
`switcher.inc`'s trampoline (checked in the source), and requires every
export to reach Wine, the system DLL or a stub as the policy above says, with
and without a system DLL and for allowed and blacklisted callers; it also
checks the stub arities and return values and reproduces the failure with
d3d8's lists before 0055.

`0056-partial-render-target-maps.patch` maps only the locked rectangle of a
render target whose current copy is its GL texture. `wined3d_surface_map`
loaded the whole map binding (`surface_load_location`), so any lock that found
system memory stale downloaded every texel with `glGetTexImage`: in DreamGPU
`ceil(width * height / 16384)` synchronous 64 KiB QUERY records. Unreal
Tournament 2003's `ReduceMouseLag` locks backbuffer pixel `0,0-1,1`
read-only after every present; at 800x600 that was 30 round trips and 1.9 MB
per frame (75 FPS, 669 with the option off, Win2000 guest). A diagnostic
`wined3d.dll` attributed every download to that lock: `resource_map` from
`wined8.dll` (`d3d8_surface_LockRect`) with `WINED3D_MAP_READONLY`, a
B8G8R8X8 800x600 rectangle backbuffer with `TEXTURE_RGB` current.

`dg_surface_partial_map_mode` qualifies a map when the box is smaller than the
surface, the flags are neither `DISCARD` nor (for writes) `NO_DIRTY_UPDATE`,
Direct3D is initialized with `ORM_FBO` and blit framebuffers, and the surface
is a non-multisampled render target (not depth/stencil) in
B8G8R8A8/X8, R8G8B8A8/X8, B5G6R5, B5G5R5X1/A1 or B4G4R4A4/X4 with an identity
fixup, no conversion or color-key conversion, an FBO-attachable format, a
level-0 2D or rectangle texture, `SYSMEM` map binding with memory, no client
storage or DIB section, not converted or pinned, not the front buffer, drawn
into its texture (`draw_binding` `TEXTURE_RGB`), whose `TEXTURE_RGB` is
current and `SYSMEM` not, and whose pitch is a whole number of pixels. Those
are the cases where a framebuffer read equals the texture download Wine would
do (the frontend converts both from the same RGBA8 wire form; luminance and
other formats read differently from a framebuffer) and where offscreen
rendering keeps D3D row y in texture row y, as `surface_blt_fbo` assumes.

`dg_surface_partial_map_read` attaches the texture to the read framebuffer
(`context_apply_fbo_state_blit`, `GL_COLOR_ATTACHMENT0`, `STATE_FRAMEBUFFER`
invalidated), sets `PACK_ALIGNMENT` 1 and `PACK_ROW_LENGTH` pitch/bpp, reads
the box into `heap_memory` at `top * pitch + left * bpp` and restores 0 and
the device's surface alignment. A context that is missing, invalid or not
current declines before any GL call and Wine's own path runs (and reports the
failure as before). The map then publishes no location: neither `SYSMEM`
validation nor the `~SYSMEM` invalidation of a writable map, and records the
mode in the new `wined3d_surface.dg_partial_map`. `surface_unmap` clears it:
a read-only map does nothing, a writable one binds the texture and calls
`wined3d_surface_upload_data` for the box from `heap_memory` at the surface
pitch, then invalidates everything but `TEXTURE_RGB`. If the texture stopped
being current while mapped, or no context exists, it reports an ERR and keeps
the texture (the rectangle is lost, as a failed lazy upload would lose it).
The pointer, pitch, slice pitch, locked rectangle, lock counting and failure
behaviour are those of Wine's map. The diagnostic patch's line references
were renumbered for the inserted functions.

`test_partial_maps.py` compiles the prepared `wined3d_surface_map`,
`wined3d_surface_unmap`, `surface_unmap` and 0056's helpers under ASan/UBSan
against a GL whose textures hold real texels: `ReadPixels` reads the texture
attached to the read framebuffer with the pack state, uploads write rectangles,
the location loader downloads and uploads whole surfaces, and draws rewrite
texels and invalidate other locations. It checks read-only 1x1, 16x16 and
last-pixel boxes (one `ReadPixels`, no download, Wine's pointer and pitch,
nothing outside the box written, no location change), repeated maps without
draws and maps interleaved with draws, writable boxes (box read, box-only
upload at unmap, texture the only current copy, the next whole map downloads
the merged image), NOOVERWRITE, 16-bit targets with padded pitch, restored
pack and framebuffer state, Wine's path for full, `DISCARD`, `NO_DIRTY_UPDATE`
and current-`SYSMEM` maps, depth/stencil and every other ineligible surface,
and context failures at map and unmap. `test_map_dc_failures.py` stubs the
helper (its surfaces never qualify). The installed D3D8/9 probe
`tools/d3d/partial-lock-probe.inc` locks the backbuffer and lockable render
targets and reads only pixels inside each locked rectangle; it does not try
to observe how much a lock transferred (the transport counters do: UT2003's
DM-Antalus with `ReduceMouseLag=True` went from 67-75 to about 269 FPS on a
Win2000 guest, one 1-pixel `ReadPixels` per frame instead of 33 queries).
Its R5G6B5 colours use greens that survive Wine's `GL_RGB5` storage of that
format (5-bit green without `ARB_ES2_compatibility`).

`0057-rgb565-storage.patch` stores Direct3D's R5G6B5 with a six-bit green, as
a GeForce FX 5800 did. WineD3D's format table maps `WINED3DFMT_B5G6R5_UNORM`
to `GL_RGB5`, and only to `GL_RGB565` with `ARB_ES2_compatibility`, an OpenGL
ES 2.0 compatibility extension DreamGPU does not report (its other entry
points, the `FIXED` type and the shader precision queries have no GeForce FX
counterpart). Mesa keeps `GL_RGB5` in 5/5/5 bits, so every R5G6B5 texture,
render target and 16-bit back buffer lost green's low bit: a clear or upload
of six-bit green 13 read back as 12 on Win2000, XP and Win98 guests.

DreamGPU's frontend accepts `GL_RGB565` (0x8D62) as an internal format, with
no extension name, exactly when the host stores it with all 5/6/5 bits
(`DG_CAP2_GL_RGB565`); older frontends and hosts report a GL error for it.
`dg_rgb565_storage` asks that once, in `init_format_texture_info` with the
capability context current: it drains pending errors, saves
`GL_TEXTURE_BINDING_2D`, defines a 1x1 `GL_RGB565` image (`GL_RGB`,
`GL_UNSIGNED_SHORT_5_6_5`, no pixels) in a new 2D texture, takes "no error"
as the answer, restores the binding, deletes the texture and drains errors
again. With `ARB_ES2_compatibility` it is not called. Table assignment then
takes B5G6R5's existing `GL_RGB565` entry also when the probe succeeded;
`supported[ARB_ES2_COMPATIBILITY]` is unchanged, so nothing else in WineD3D
changes. The entry keeps `GL_RGB`/`GL_UNSIGNED_SHORT_5_6_5` and its
`GL_RGB8` render-target fallback, which `check_fbo_compat` replaces with
`GL_RGB565` once the format attaches to a framebuffer object; offscreen
render targets and the `ORM_FBO` back buffer therefore use `GL_RGB565` too.
On an older host or frontend, B5G6R5 stays `GL_RGB5` exactly as before.

X1R5G5B5 (`GL_RGB5`), A1R5G5B5 (`GL_RGB5_A1`) and A4R4G4B4 (`GL_RGBA4`) are
unchanged: on Mesa they keep 5/5/5, 5/5/5/1 and 4/4/4/4 bits, and all 65536
words of each round-trip through upload, readback and framebuffer clears.
Colour-keyed textures are still converted by `color_key_info`: a keyed
B5G6R5 texture becomes B5G5R5A1, so its green keeps five bits (the key
comparison itself is on the original 16-bit words).

`test_rgb565_storage.py` runs the prepared `dg_rgb565_storage` and
`init_format_texture_info` table assignment under ASan/UBSan against a fake
GL that accepts or rejects `GL_RGB565`, and with `ARB_ES2_compatibility`
(where the probe makes no GL call). It checks B5G6R5's internal, render-target,
format and type fields, that the 16-bit formats above keep their entries, and
that the probe restores the 2D binding, deletes its texture and leaves no GL
error; it fails without the patch.

`0058-point-sprite-primitives.patch` enables `GL_POINT_SPRITE` only while
points are rasterized. Direct3D's `D3DRS_POINTSPRITEENABLE` replaces the
texture coordinates of point primitives only, and so does `GL_POINT_SPRITE`
by its specification; Wine set the GL enable from the render state alone,
with `COORD_REPLACE` on for every unit since context creation. Mesa 26
(radeonsi and llvmpipe, Mesa 26.2.6 on the Linux host) does not keep to the
specification there: while the enable is set, its fixed-function vertex
processing drops every coordinate set with `COORD_REPLACE`, so fixed-function
triangles and lines sample one texel (ARB vertex programs are unaffected;
points and polygons in point mode get correct sprites). Halo: Combat Evolved
(the PC trial, D3D9) enables point sprites once at device creation and never
disables them. Its scene, bloom and interface draws use vertex shaders and
render into an 800x600 render-target texture; the one fixed-function draw that
copies that texture to the back buffer (a pretransformed, indexed four-vertex
triangle fan, POINTSPRITEENABLE still set) then filled the screen with one
texel's colour every frame, with or without `-useff` and before 0049 too.

The fixed-function vertex pipe (`vp_ffp_states`, the ARB backend's) now
handles `STATE_RENDER(WINED3D_RS_POINTSPRITEENABLE)` with
`state_pointsprite_draw`: it enables `GL_POINT_SPRITE` when the render state
is set and the draw's primitive type is `GL_POINTS`, or when fill mode is
`WINED3D_FILL_POINT` (GL rasterizes those polygons as points, which keep
their sprites as before), and disables it otherwise. `STATE_POINT_ENABLE`,
which upstream already invalidates whenever the primitive type changes to or
from points (directly and through state blocks) and which this pipe handled
with `state_nop`, now reapplies it through `state_pointsprite_primitive`
when sprites are on and the render state is not pending; `state_fillmode`
does the same. With sprites off nothing is sent for primitive or fill-mode
changes (context creation, the render-state handler and `SetupForBlit`
already leave the enable clear). Without `ARB_point_sprite` the FIXME and
nop handlers are unchanged, and the GLSL vertex pipe keeps upstream's
`state_pointsprite`. Native OpenGL guests that leave `GL_POINT_SPRITE`
enabled while drawing textured fixed-function primitives still meet Mesa's
behaviour; the host forwards the enable unchanged.

`test_point_sprite_primitives.py` runs the prepared `state_pointsprite_draw`,
`state_pointsprite_primitive` and `state_fillmode` under ASan/UBSan against a
recording GL and a model of WineD3D's invalidation and apply loop (render
state, primitive type, fill mode, `SetupForBlit`, context creation), checked
against the prepared `device.c`, `context.c`, `cs.c` and `stateblock.c`:
after every draw the enable equals the rule above over Halo's sequence,
point/triangle switches, point fill mode, blits, new contexts and 200000
random operations, and sprites off send nothing. It also checks the state
template rows. It fails without the patch, and each mutation of the rule or
of the two reapplications fails it. The installed D3D8/9 probe
(`tools/d3d/point-sprite-probe.inc`) draws, with sprites on and then off, a
quadrant-textured pretransformed quad in Halo's 28-byte layout as an indexed
fan, a 16-pixel point and an untransformed quad after the point, and compares
every pixel; the point shows upright quadrants when dgpugl.dll reports
`GL_ARB_point_sprite`, and otherwise its own texel (logged as a skip).

`0059-reset-without-context.patch` keeps WineD3D's device reset, uninit and
resource unloading working when no GL context exists. A state reset
(`wined3d_device_reset`) unloads every resource and destroys every context
(`delete_opengl_contexts`) before `create_primary_opengl_context` creates the
primary one again on the device window. Warcraft III (D3D8) resets when it
regains focus, and Windows may return focus while its fullscreen window is
still minimized. dgpugl.dll then refused a context on a window without a
client area (fixed in the frontend, see `guest/opengl/README.md`), so the
reset failed after its teardown, freeing the shader and blitter state and the
swapchain's context array but leaving the pointer. The game's retry acquired
a context, found none and could create none, and `delete_opengl_contexts`
dereferenced the NULL result (`wined3d.dll+0x92BE9` in the m41 package);
otherwise it would have freed that state and array a second time.

Before changing anything, a state reset now requires the device's context to
be made current (its GL objects are read back through it). When it cannot be,
the reset returns `WINED3DERR_DEVICELOST` and the device is unchanged; D3D8/9
report it to the application, which keeps its device in the not-reset state
and retries. When the primary context cannot be created again after the
teardown, the device records that it has no context (`contexts_lost`, one of
the device's padding bits), the reset returns `WINED3DERR_DEVICELOST`, and the
swapchain no longer names the freed array. While lost, `context_acquire`
returns NULL instead of creating a context without the shader, blitter and
dummy-texture state; the next reset skips the completed teardown and creates
the primary context again, and uninit skips the GL state already freed. Later
Wine treats `context_acquire` as fallible as well; this keeps the pinned
reset order (teardown, state initialization, creation).

Without a context, GL objects cannot be reached; they die with the contexts.
Reset teardown, uninit and resource unloading then forget their names without
GL calls: a valid CPU copy in the map location stays, a PBO map binding falls
back to system memory, content held only in GL becomes zero system memory
(application and DIB memory keep their bytes), and buffers, volumes and
textures follow the same rule. `EndScene`, clears, draws and presents already
skipped an invalid context and now also skip a missing one, so a game that
keeps its loop running between failed resets does not crash. No GL call is
made through a context Wine does not hold: no DreamGPU context is current on
the thread when none can be acquired, and the frontend ignores such calls.
The GLSL shader backend (`shader_glsl_destroy`) is not changed; DreamGPU never
selects it.

`test_reset_context_loss.py` runs the prepared `delete_opengl_contexts`,
`create_primary_opengl_context`, the two reset steps, `wined3d_device_end_scene`,
`wined3d_device_uninit_3d`, `swapchain_destroy_contexts` and the surface,
buffer, volume and texture unloads under ASan/UBSan against counted context,
GL and allocation boundaries: an unusable context leaves the device intact,
Warcraft III's sequence (failed re-creation, retry, scenes, recovery), uninit
of a lost device and a teardown without any context, with CPU contents kept
or defined. It checks the order of the steps in the prepared
`wined3d_device_reset`, and each of eleven mutations of the patch fails it.
`test_context_migration.py` runs the prepared `context_acquire` of a lost
device. The installed D3D8/9 probe (`tools/d3d/reset-probe.inc`), run before the other
probe stages create resources, resets its windowed device with the window
visible, hidden, minimized (twice, with a scene and Present between) and
visible again, and renders after restoring without a reset; four reference
frames must match exactly, and the later stages then run on the reset device.

`0060-fullscreen-window-activation.patch` keeps the device window of a
fullscreen D3D8/9 swapchain usable after focus changes and covering its mode.
Native fullscreen flips cover the screen whatever the device window's size;
DreamGPU presents into the window's client area (WineD3D's `GetClientRect`
destination, composited at the display driver's WNDOBJ client rectangle).
Splinter Cell (D3D8) creates its window at 640x480, renders fullscreen at
1024x768 and answers every WM_SIZE with its creation size, synchronously and
nested in WineD3D's own `SetWindowPos` (device creation, Reset, activation),
so it showed as a 640x480 image in a corner of the mode with the desktop
around it. On focus loss `wined3d_swapchain_activate` restores the desktop
mode and minimizes the window; on activation it only called `SetWindowPos`,
which leaves an iconic window iconic. Native D3D8 restores the window there,
and native D3D9 restores it through its `SC_RESTORE` handling, but this tree
is built with `USE_HOOKS`: WineD3D sees only sent messages through a
`WH_CALLWNDPROC` hook, and the taskbar's `SC_RESTORE` is posted. The game,
which resets only once its window is restored, waited in
`D3DERR_DEVICENOTRESET` forever.

Activation now restores an iconic device window first, with
`SW_SHOWNOACTIVATE` (the application is already being activated; the device
window may differ from the focus window), for devices with
`WINED3D_HANDLE_RESTORE` (D3D8 and D3D9, never DirectDraw) and without
`WINED3DCREATE_NOWINDOWCHANGES`. WineD3D minimized the window itself, so
both D3D versions restore it.

While such a device's swapchain is fullscreen and the application is active,
the device keeps its window covering the mode (`kept_window`,
`kept_width`/`kept_height`): `wined3d_device_setup_fullscreen_window` starts
keeping, `wined3d_device_restore_fullscreen_window` and
`wined3d_device_release_focus_window` stop it, focus loss suspends it before
the desktop mode returns and activation resumes it. While it is kept, the
window is subclassed, and that procedure clamps `WM_WINDOWPOSCHANGING` to 0,0
and the mode size when a request changes the size and would leave part of
the mode uncovered. The device's `WH_CALLWNDPROC` hook cannot do it: Windows
gives a hook a copy, and the first 0060, which edited the hook's `WINDOWPOS`,
left the probe's window at 320x240 on Windows 2000. The procedure does not
depend on `filter_messages`, which under `USE_HOOKS` only stops WineD3D's own
handling: the application still receives (and answers) the messages of
WineD3D's `SetWindowPos`, which is exactly when Splinter Cell shrinks the
window. For a sent message Windows calls the hook first, then the procedure
installed last: a module that subclassed after WineD3D sees the request
unclamped, WineD3D's procedure clamps it, the application's original
procedure sees the clamped request, and WineD3D clamps again after that
procedure returns, in case its own handler changed the request. The
procedure is installed and called with the `SetWindowLongPtrA`/`W` and
`CallWindowProcA`/`W` functions matching `IsWindowUnicode` (`SetWindowLongPtrA`
would make a Unicode window ANSI on NT; Windows 98 windows are ANSI). When
keeping stops, the original procedure is installed again if WineD3D's is
still the window's; otherwise unlinking would cut the later module's chain,
so WineD3D's stays as a pass-through without a device and is taken over again
if keeping restarts. `WM_NCDESTROY` ends either. A table of up to 16 windows
holds the original procedures, under a spin lock never held while a message
is sent. A clamped request changes nothing, so Windows sends no further WM_SIZE
and an application resizing from WM_SIZE does not loop. Minimizing, restoring
and maximizing (`SWP_STATECHANGED` or an iconic window), pure moves, larger
windows that still cover the mode, child windows, DirectDraw and
NOWINDOWCHANGES devices pass unchanged. At most eight requests are clamped
between two presents (and after each of WineD3D's own window changes); an
application that answers every `WM_WINDOWPOSCHANGED` with another request then
gets its size instead of recursing without end. `wined3d_device_reset` sets
`swapchain->desc.windowed` and the back buffer size only after it has moved
the window, so it sets the kept size itself before its mode change (the
application may resize from `WM_DISPLAYCHANGE`): the new mode's size for a
fullscreen-to-fullscreen reset, none for a reset to windowed (restored when
the mode change fails); a reset from windowed is kept by the setup.
`reapply_mode` is unchanged: it stays set after the first focus loss, so
later resets take the mode-change path, which keeps the new size as well.

WineD3D's activation handling first checks that the device is still mapped
(a Wine9x hack): `VirtualQuery(device, &meminfo, sizeof(struct
wined3d_device))`. The length is the buffer's, and Windows validates all of
it as writable, so the query covered about 50 KiB from a stack local and
failed whenever that ran past the base of a shallow stack. Focus loss and
activation were then ignored entirely (no desktop mode, no minimize, no
restore), while D3D8/9 still marked the device lost. The installed probe
showed it on Windows XP and 98 (Windows 2000 passed): the window was not
minimized on focus loss. 0060 passes `sizeof(meminfo)`. Because Windows 98
has no `SWP_STATECHANGED`, the clamp also leaves alone requests to the
parked minimized position (-32000), in case a window is not yet iconic
while it is being minimized.

A pass-through procedure left in a chain refers to wined3d.dll, which must
then stay loaded while the window exists (as WineD3D's own window procedure
does in builds without `USE_HOOKS`).

`test_fullscreen_window_keep.py` runs the prepared setup, restore, focus,
hook, activation and present functions and the window part of the prepared
`wined3d_device_reset` under ASan/UBSan against a user32 model that, as
Windows, gives the hook a copy of a `WINDOWPOS` (edits are discarded), then
calls the procedure installed last, tracks `SetWindowLongPtr`/`CallWindowProc`
character sets and sends `WM_SIZE` from DefWindowProc only for a changed
client size, in three platform variants (Windows 2000/XP with
`SWP_STATECHANGED` and windows minimized to -32000; Windows 98 without it,
minimized on screen; and without it, iconic only after
`WM_WINDOWPOSCHANGING`), and with `VirtualQuery` failing for a length
beyond its buffer, as on XP and 98: Splinter Cell's
WM_SIZE handler at device creation, application `SetWindowPos`/`MoveWindow`,
same-mode, smaller, larger, failed and windowed resets, focus loss with a
`WM_DISPLAYCHANGE` resize, activation, the application's own minimize, an
application answering every `WM_WINDOWPOSCHANGED`, one editing its own
`WM_WINDOWPOSCHANGING`, a Unicode window, a module subclassing after WineD3D
(pass-through, keeping again, destruction), NOWINDOWCHANGES and DirectDraw
devices. It checks the order of the steps in the prepared
`wined3d_device_reset` and the subclass procedure, and each of twenty-two
mutations of the patch fails it in at least one variant, among them the
first 0060's hook-time clamp and the old `VirtualQuery` length. `test_fullscreen_activation.py` models the new device fields and the
`VirtualQuery` length (a third defect control).
The installed D3D8/9 probe (`tools/d3d/reset-probe.inc`, `Fullscreen`, run
last on its own device and window) creates a fullscreen 640x480 device on a
320x240 window whose procedure shrinks it back from WM_SIZE, and requires the
window to cover the mode after creation, the application's own resizes and a
Reset, to be minimized and the device lost after focus loss (the
`WM_ACTIVATEAPP` Windows sends, sent directly so that foreground rules do not
matter), restored and covering the mode after activation, Reset to succeed
and render (exact frames with X8R8G8B8), and the shrink requests never to
nest. The diagnostic stack is rebased onto 0060 with its call-site line
numbers renumbered.

`0061-software-cursor-clip.patch` draws the part inside the back buffer of a
Direct3D cursor that crosses an edge. WineD3D draws a cursor that is not a
Windows cursor (any size but 32x32, or a 32x32 one that `SetCursorPos` could
not reach) into the back buffer at Present with an alpha-tested
`wined3d_surface_blt`. That blit rejects any destination rectangle outside
the surface (`WINEDDERR_INVALIDRECT`), so the whole cursor vanished as soon as
any part of it crossed the right or bottom edge, or the left or top edge
through a hotspot beyond the position (whose unsigned difference also wrapped).
Native Direct3D clips the cursor, as later Wine does with `wined3d_clip_blit`
(Wine 1.9.x). 0061 computes the destination with signed edges, maps it to the
client area in windowed mode as before, then clips it to the back buffer and
moves each clipped source edge by the same amount (the cursor is never
scaled); nothing is blitted when nothing is left.

`test_software_cursor_clip.py` runs the prepared cursor block of
`swapchain_gl_present` and its clip helper under ASan/UBSan, with the
prepared rectangle validation of `wined3d_surface_blt` and an alpha-tested
unscaled copy, for a non-square cursor inside, at the origin, touching and
crossing each edge and corner (with hotspots beyond the position and with a
windowed client offset), entirely outside, hidden and as a Windows cursor. It
compares every back buffer pixel with the cursor's visible texels, requires
no rejected or stretched blit, and each of thirteen mutations of the patch
(among them the unclipped blit) fails it. The installed D3D8/9 probe
(`tools/d3d/cursor-probe.inc`, run last on its own fullscreen 640x480
device) reads presented frames back and requires the 16x16 cursor's visible
part exactly, and nothing else, across the bottom-right corner and, with
hotspot 8,8, across the top-left corner. The diagnostic stack is rebased
onto 0061 with its call-site line numbers renumbered.

`0062-table-fog-shader-fog-coordinate.patch` makes table fog (`FOGTABLEMODE`
LINEAR, EXP or EXP2) use the fog coordinate a vs_1_x or vs_2_x shader writes
to oFog. The ARB vertex program epilogue wrote the vertex output z as
`result.fogcoord` whenever table fog was on, and the arbfp fog source is then
FOGSOURCE_FFP with the application's FOGSTART/FOGEND. That is what the
reference rasterizer and later drivers do, but NVIDIA drivers before 61.77 and
the ATI drivers of 2003-2004 ran the shader's oFog through the table equation
instead, and games of that time rely on it. Far Cry's NVIDIA path (DreamGPU
reports a GeForce FX 5800) draws terrain, water and vehicles with vertex
shaders that write oFog, FOGVERTEXMODE NONE, FOGTABLEMODE LINEAR, start 0 and
end 1, so with the vertex output z (about the eye distance) all of it was the
fog colour. The demo looks the same on Windows with later NVIDIA drivers and is
correct with an AMD vendor ID (Wine bug 25761, closed invalid); Prince of
Persia: The Sands of Time does the same (Wine bug 17423). A shader that writes
oFog with built-in varyings (`TMP_FOGCOORD`) now passes it to
`result.fogcoord` unclamped, as a fog coordinate rather than a factor: whether
the period drivers clamped it is not documented, and Wine's proof-of-concept
fix for bug 25761 left it unclamped. Vertex fog (`FOGTABLEMODE` NONE) still
clamps oFog as the fog factor. vs_3_0 shaders, declared varyings and shaders
without oFog keep the vertex output z.

Known divergence, not changed: for a shader without oFog, native table fog
uses W when `D3DTS_PROJECTION` is a perspective matrix, and with an
orthographic one the depth z/w (AMD) or the vertex output z (NVIDIA); upstream
Wine chooses between them with `ortho_fog`. WineD3D here always uses the
vertex output z, which matches NVIDIA only for an orthographic projection.

`test_table_fog_shader_coordinate.py` runs the prepared
`vshader_add_footer` (with `need_helper_const` and `arb_get_helper_value`),
`state_arbfp_fog` and `get_fog_start_end` under ASan/UBSan for vs_1_1, vs_2_0
and vs_3_0 with and without oFog, built-in and declared varyings, table fog
LINEAR/EXP/EXP2 and vertex fog, interprets each generated epilogue on a
clip-space position and an oFog value, and compares the ARB_fog fragment
factor and the final position: oFog through the table equation, unclamped
(oFog 2 with end 4 is half fogged), for vs_1_x/vs_2_x shaders that write oFog,
the vertex output z otherwise, and the clamped oFog factor with vertex fog.
Each of seven mutations (among them the vertex output z, a clamped
coordinate and a vertex shader fog source) fails it. The installed D3D8/9
probe (`tools/d3d/table-fog-probe.inc`, after the vertex shader stage) draws a
vs_1_1 quad that writes oFog under LINEAR table fog from 0 to 1 and requires
the diffuse colour for oFog 0, the fog colour for oFog 1 and three quarters
diffuse for oFog 0.25 (depth fog would give half or full fog), half fog for
oFog 2 with end 4, and with vertex fog the diffuse colour for oFog 1 and the
fog colour for oFog 0; the table cases also run after a ps_1_1. The
diagnostic stack does not touch `arb_program_shader.c` and needs no rebase.

`0063-window-dc-pixel-format.patch` removes the per-operation pixel-format
query. WineD3D re-activates its context on every first-level acquisition
(this build releases the native context owner after each operation so that
other threads can take it; that is unchanged), and `context_set_pixel_format`
asked `wglGetPixelFormat` for the context's DC each time unless the DC was a
private backup-window DC with `hdc_has_format`; `context_enter` did the same
when the context was still current. DreamGPU's frontend must find the DC's
window with `WindowFromDC`, a system call on NT, for every such query, and
Far Cry, guest-CPU-bound at about 200 FPS, spends about 17% of guest time in
the NT kernel. `hdc_has_format` now also covers the context's own window DC:
it is set when `context_set_pixel_format` verified (`current == format`) or
set the context's `pixel_format` on `context->hdc`, the DC WineD3D obtained
with GetDC for `context->win_handle` and holds for the context's lifetime.
While held, that DC stays with its window, and WGL sets a window's pixel
format once, so neither the application nor another device can change it.
Exceptions keep the check on every acquisition, as before: a class DC
(`CS_CLASSDC`, which any GetDC for a window of its class moves; the class
style is read once, at verification), and `WGL_WINE_pixel_format_passthrough`,
with which another device or the application may replace the format and with
which WineD3D itself replaces an existing format and restores it at release
(`restore_pf`); `restore_pf` is only ever recorded with that extension, so a
kept format never coexists with a pending restore. A window that already had
another format (WGL refuses the change, WineD3D renders with it) never reaches
the kept state either. The flag is cleared, so the format is verified again,
when `context_update_window` moves the context to the swapchain's new window
(Present's override window, a new device window), when `wglMakeCurrent` on the
DC fails and the backup window is used, and a new context (as after a reset)
starts with it only for a private DC; `context_create`'s earlier check runs
before `context->hdc` is assigned, so the first activation verifies once. In
steady state an acquisition makes no `wglGetPixelFormat` call; previously one.
The legacy `WIN32_NATIVE` helper is not built and never sets the flag for a
window DC.

`test_window_dc_pixel_format.py` runs the prepared `context_acquire`,
`context_enter`, `context_update_window`, `context_set_current`,
`context_set_gl_context`, `context_set_pixel_format`,
`context_restore_pixel_format`, `context_release` and context_create's DC
initialisation under ASan/UBSan against a model of windows, cache, own, class
and private DCs and WGL's set-once format, in which every successful
`wglMakeCurrent` must find the context's format on the DC's window. A context
on a new window queries twice at creation and then not at all in 100
operations, nor when still current at acquisition; a window change, a failed
binding (with the DC's window replaced) and a reset verify it once again; a
window with another format, passthrough with `restore_pf` (the application's
format back after every operation) or a change by another device, and a class
DC moved to another window keep checking; a private DC is never queried after
creation. Each of nine mutations (the old private-only skip, keep or
`context_enter` check, keeping another format, ignoring passthrough or class
DCs, keeping the flag across a window change or failed binding, and setting
it at creation) fails it. `test_thread_pixel_format.py` gains the fields and
`GetClassLongA` the prepared helper now names. No probe stage is added: the
change is not observable in rendered pixels, and the probe cannot count
system calls. The diagnostic stack is rebased onto 0063 with its
`context.c` call-site line number renumbered.
