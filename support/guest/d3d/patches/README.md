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
