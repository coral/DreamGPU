# Graphics contracts and milestone verification

The current target is reliable behavior for shipped OpenGL 1.1/WGL, DirectDraw,
legacy Direct3D through the D3D8/9 providers, and Glide2x and Glide3x on Windows 98/2000/XP,
with macOS and Linux hosts. Earlier game and installer passes establish those
workloads only. Entrypoint dispatch coverage does not establish API conformance.

`tests/graphics/contracts.json` is the executable inventory. Each row identifies
an operation, format, resource, usage and pool, its implementation status, and
its tests. Broad rows remain to be expanded; `catalog_complete` is deliberately
false. Optional formats require actual per-usage capability checks. Unsupported,
unverified and missing mandatory behavior have different meanings.

## Current implementation milestone

This batch addresses related resource and API failures before another installer
and disposable-VM acceptance cycle:

- Surface Lock/GetDC propagates failed downloads and allocation failures without
  publishing stale memory. Failed ReleaseDC writeback retains the DC and CPU
  authority so the caller can retry.
- Buffer mapping checks bounds, invalid current contexts and failed native maps
  before publishing dirty ranges or discard state.
- Normalized backbuffer depth/stencil has CPU download and writeback paths,
  pitch/orientation conversion, staged downloads and aspect preservation during
  partial clears. Failed GL state pushes unwind only successfully saved frames.
  Transfers use explicit GL1.1 state groups and preflight every required native
  entrypoint. The private Wine provider exports the same client/pixel/raster
  implementations as the ICD, including CPU depth upload operations.
  FBO/PBO CPU depth transfers and floating-point depth are still unsupported.
- Legacy BeginIndexed/Index/End copies caller vertices and expands indices into
  the existing draw path, with bounds, allocation, nesting and error handling.
- GL texture work preserves all seven GL1.1 scalar types, component/index formats,
  bitmap indices, sized internal formats, empty definitions and proxy feasibility.
  Split subimages are checked before writing; native errors preserve the context.
  Supplied 1D/2D image borders participate in filtering through the Mesa provider
  and a private CGL sampler, behind a separate negotiated capability.
- Win98 installation can migrate the exact legacy `qemumini.drv`/`qemumini.vxd`
  binding, preserving verified originals and private backups for rollback.
  Installed INF short/long aliases and bounded nonterminated registry strings
  are handled without treating legacy files as DreamGPU-owned files.
- Capability probes capture installed GL/WGL and D3D8/9 claims independently of
  rendering validation. DGCAP6 exercises indexed draws and depth Lock/edit/clear
  contents, beyond the existing triangle smoke tests. The separate `bordergl`
  probe runs through system OpenGL/WGL and checks supplied-border pixels, caller
  memory ownership, negative-offset updates and preserved texture contents.
- Juke screenshot events identify their CPU or prepared-image source and
  publication freshness. A screenshot request alone does not synchronize new
  guest rendering; CPU publication generations are included when available.

- Fixed-function multitexture (`DG_CAP_GL_MULTITEXTURE`) exposes eight units
  through ARB_multitexture with combine, crossbar and dot3 environments.
  `ActiveTexture` moves binding ownership on the host; every unit's bindings,
  enables, environments, coordinate generation and texture matrices follow the
  active unit, and TEXTURE_BIT push/pop restores all units. Client arrays for
  units 1–7 travel in `DrawArraysEXT`, a sixteen-slot compact layout under a
  distinct function number so older kernels' seven-slot admission is unchanged.
  Wine's fast vertex path packs up to two units; more fall back to its strided
  path. The C mirror of the Rust context state is size-checked at QEMU startup.
- Fixed-function state (`DG_CAP_GL_FIXED_STATE`) adds constant blend colour,
  blend equations (subtract/reverse/min/max, separate RGB/alpha), separate blend
  factors, two-sided stencil with wrapping operations, point parameters and
  sprites, anisotropic filtering and LOD bias. With both capabilities Wine
  reports 8 stages and 8 simultaneous textures, TextureOpCaps `0x0280bbff`,
  StencilCaps `0x1ff` (TWOSIDED, INCR/DECR), MaxAnisotropy 16, BLENDOP,
  SEPARATEALPHABLEND and BLENDFACTOR. Frontend uploads accept every GL1.2
  packed pixel type, which Wine uses for its per-unit dummy textures.
- Wine patch 0027 keeps texture-less fixed-function stages active (see the
  patch README).
- Texture control (`DG_CAP_GL_TEXTURE_CONTROL`) adds ATI_texture_env_combine3
  (MODULATE_ADD/SIGNED_ADD/SUBTRACT), ATI_texture_mirror_once and
  SGIS_generate_mipmap. Values ride existing records; native levels generated
  below a defined image are charged with that image. Wine then claims
  ADDSMOOTH, BLENDTEXTUREALPHAPM, MODULATEALPHA_ADDCOLOR,
  MODULATECOLOR_ADDALPHA and MULTIPLYADD, MIRRORONCE addressing and
  automatic mipmap generation. The macOS border sampler evaluates the
  combine3 modes itself.
- Compressed textures (`DG_CAP_GL_COMPRESSED_TEXTURES`) add ARB_texture_compression
  and EXT_texture_compression_s3tc for 2D images: `CompressedTexImage2D` with an
  exact payload or none (zero-defined storage), block-row `CompressedTexSubImage2D`
  bands, `GetCompressedTexImage` through the shared texture read cache, the
  host's fixed four-format list, and S3TC/generic compressed internal formats for
  `TexImage2D`. Levels are charged at their compressed size. Every split 2D
  upload now starts its tiles on 4-pixel boundaries of the rectangle (gathering
  padded rows instead of sending single rows), and the host's zero fill uses
  4-row bands, so uncompressed sources into compressed storage stay block
  aligned. Rules that depend on the level (edge-only partial blocks, bounds,
  format) are GL errors, never record faults.
- Fog coordinates (`DG_CAP_GL_FOG_COORD`) add EXT_fog_coord: `FogCoordf` inside
  Begin/End and lists, `FOG_COORDINATE_SOURCE`, and a per-vertex FLOAT/DOUBLE
  coordinate in DrawArraysEXT slot 14 (slot 15 stays reserved), restoring the
  current coordinate after array draws. Without it Wine emulated D3D's
  specular-alpha fog of pretransformed vertices by blending vertex colours toward
  the fog colour before texturing, so fogged textured geometry darkened instead
  of fading to the fog colour. Wine's fast vertex path now packs those fog
  coordinates; other fog-coordinate states still use its strided path.
- Cube maps (`DG_CAP_GL_TEXTURE_CUBE_MAP`) add ARB_texture_cube_map: a third
  texture target with its own default object, per-unit binding and enable, six
  square face chains addressed by face target in every image, copy, compressed
  and readback record, the proxy target, and NORMAL_MAP/REFLECTION_MAP texgen.
  Completeness needs all six faces at one size; residency charges every face.
  Wine then claims D3DPTEXTURECAPS_CUBEMAP (and MIPCUBEMAP).
- Fragment programs (`DG_CAP_GL_FRAGMENT_PROGRAM`) add ARB_fragment_program
  (`FRAGMENT_PROGRAM_ARB` only). Program names live in a bounded table shared
  with the texture namespace (wglShareLists): GenPrograms reserves names,
  objects appear at first bind, the default object is private to each share
  group, and a deleted program stays usable by other contexts until they
  rebind. The host checks every ProgramString before native GL (transferred
  length up to `DG_GL_MAX_PROGRAM_BYTES`, bytes 1-127, `texture[n]` and
  `texcoord[n]` within the reported units), parses it into a private object
  first, and only then loads the bound program, so a rejected string sets
  INVALID_OPERATION and PROGRAM_ERROR_POSITION/STRING and leaves the program
  untouched (Mesa 26 clears a program's input usage on a failed parse).
  Env/local parameters, the enable (ENABLE_BIT), MATRIXi_ARB, CURRENT_MATRIX
  and every GetProgram* query follow; binds, parameters and strings compile
  into display lists, Gen/Delete/Is execute immediately. MAX_TEXTURE_COORDS and
  MAX_TEXTURE_IMAGE_UNITS clamp to the logical units (8); other limits are
  native. Objects and strings are charged to the 256 MiB resource budget.
  Vertex program modes are GL_INVALID_ENUM unless vertex programs are
  negotiated as well (next item). An enabled program samples bound textures whatever their
  fixed-function enables, so undefined-level admission covers every unit. On
  macOS the border sampler steps aside while a program is enabled (CGL then
  ignores supplied borders); on Linux the private Mesa provider rejects drawing
  while a program samples a bordered image (GL_INVALID_OPERATION). From the
  Wine source, Wine then selects its ARB shader backend, arbfp fragment pipeline
  and arbfp blitter; without vertex programs VertexShaderVersion stays 0. Wine patches 0029–0031 adapt
  it (see the patch README). Installed on Win2000, XP and Win98 SE (Linux
  host), D3D9 reports PixelShaderVersion 2.0 (D3D8 1.4), PixelShader1xMaxValue
  8, PS20 NumTemps 32, TextureOpCaps `0x03feffff` (adding BUMPENVMAP,
  BUMPENVMAPLUMINANCE, MODULATEINVALPHA_ADDCOLOR, MODULATEINVCOLOR_ADDALPHA)
  and PrimitiveMiscCaps `0x000a0ff2` (TSSARGTEMP). The installed probe renders a
  ps_1_1 and a ps_2_0 shader exactly for two constants each, and a fixed-function
  `D3DTOP_BUMPENVMAP` stage from a V8U8 map exactly for an identity and a zero
  matrix. The thirteen stage cases, DXT, fog and cube probes, now running through
  the arbfp pipeline, still pass.
- Vertex programs (`DG_CAP_GL_VERTEX_PROGRAM`) add ARB_vertex_program
  (`VERTEX_PROGRAM_ARB`) to the same program namespace: each target has its own
  binding and default object, a name keeps the target it was first bound to,
  and the extension string and entry points exist only when negotiated. The
  host applies the fragment checks with `vertex.attrib[n]` below
  MAX_VERTEX_ATTRIBS_ARB (16) instead of the texture limits, and loads every
  vertex string into a fresh native object that replaces the program's object
  only on success, copying its local parameters (Mesa 26.1 frees a serialized
  program twice once one vertex object is translated a third time). Env and
  local parameters, VERTEX_PROGRAM_POINT_SIZE/TWO_SIDE (ENABLE_BIT), program
  matrices, GetProgram*, GetVertexAttrib{d,f,i}v and GetVertexAttribPointerv
  follow. `VertexAttrib*ARB` are bit-exact scalar records, legal inside
  Begin/End and in display lists; attribute 0 provokes a vertex there. Generic
  client arrays use a third array draw, the DATA record numbered
  `VertexAttribPointerARB`: the DrawArraysEXT scalars plus a generic mask and 32
  descriptors (16 conventional, then generic 0-15 with
  `DG_GL_ATTRIB_NORMALIZED` for integer types). A distinct function keeps the
  DrawArrays (7 slots) and DrawArraysEXT (16 slots) shapes older kernels and
  hosts admit unchanged. Attribute 0 is the position, as in GL 2.0 and Mesa's
  non-aliasing mode: its array supersedes the vertex array, and generic
  attributes 1-15 never alias conventional ones; CURRENT_BIT and
  CLIENT_VERTEX_ARRAY_BIT save them. Native array-draw errors are GL errors.
  Wine's ARB backend then compiles D3D vertex shaders: 256 native env
  parameters give VertexShaderVersion 2.0 with 256 constants (D3D8 1.1). Wine
  patch 0033 adapts it (see the patch README). VS 2.0 also changes the card
  Wine reports for the unknown vendor from a GeForce 256 to its D3D9 fallback,
  a GeForce FX 5800 (`10de:0302`). That card's quirk enabled rectangle
  textures, so DirectDraw/D3D6/D3D7 presented black until patch 0034. The
  installed probe renders a vs_1_1 shader for two constant sets, a vs_2_0
  shader on D3D9 and vs_1_1 into ps_1_1 exactly on Win2000, XP and Win98 SE
  (Linux host). On macOS, CGL's aliasing and the border sampler under a vertex
  program are unverified.
- 3D textures (`DG_CAP_GL_TEXTURE_3D`) add EXT_texture3D with the
  TexSubImage3DEXT and CopyTexSubImage3DEXT forms: a fourth texture target
  with its own default object, per-unit binding and attrib-stack slots, the
  proxy target, TEXTURE_WRAP_R on every target, TEXTURE_DEPTH and the
  UNPACK/PACK_IMAGE_HEIGHT and SKIP_IMAGES pixel storage. TexImage3D and
  TexSubImage3D are nine- and ten-word DATA records carrying tight rows then
  images (or nothing for zero-defined storage); the frontend resolves every
  unpack value, splits large volumes into bounded slabs of whole images (or 2D
  tiles of one image) and checks a split subimage against the level first.
  CopyTexSubImage3D is a nine-word CALL. MAX_3D_TEXTURE_SIZE is 256 (or a
  smaller native limit); images use the uncompressed formats and every 2D
  wire type, are borderless (a border is GL_INVALID_OPERATION, its proxy is
  rejected), and charge every image of a level. Enable precedence follows GL:
  cube map, 3D, 2D, 1D. GetTexImage reads a level through all of its images.
  Without the bit the extension, entry points, enums and pixel storage stay
  hidden. Wine then claims D3DPTEXTURECAPS_VOLUMEMAP, MIPVOLUMEMAP and
  VOLUMEMAP_POW2 (TextureCaps `+0x0004a000`), VolumeTextureFilterCaps
  `0x03030300` on D3D9 (`0x0303033f` on D3D8, which keeps the legacy bits),
  VolumeTextureAddressCaps `0x3f` and MaxVolumeExtent 256; DXT volumes stay
  unclaimed. Wine patch 0035 states the unpack alignment of volume uploads (see
  the patch README). The installed probe samples the four slices of a MANAGED
  A8R8G8B8 volume exactly; it has not yet run on a guest.
- Vertex buffer objects (`DG_CAP_GL_VERTEX_BUFFER_OBJECT`) add
  ARB_vertex_buffer_object for ARRAY_BUFFER and ELEMENT_ARRAY_BUFFER, ARB
  names only. Buffer names and objects share the texture namespace
  (wglShareLists). Records name their buffer and the host holds no binding:
  BufferData (name, size, usage; the exact contents or none for zero-defined
  storage), BufferSubData (name, offset) and DeleteBuffers execute
  immediately, never compiled into lists; contents beyond one packet travel
  as bounded BufferSubData slabs. Bindings, per-array latches (conventional
  arrays, units 1-7, fog coordinates, generic attributes, InterleavedArrays)
  and mappings are frontend client state, saved by CLIENT_VERTEX_ARRAY_BIT.
  The guest cannot address host memory, so the frontend keeps a copy of every
  buffer: MapBuffer returns it (16-byte aligned), UnmapBuffer re-sends all of
  it unless the map was READ_ONLY and returns TRUE, and GetBufferSubData, READ
  maps, ArrayElement and display list compilation read it without waiting for
  the host. Mapping twice, unmapping an unmapped buffer, BufferSubData or
  GetBufferSubData on a mapped buffer and drawing from one are
  GL_INVALID_OPERATION; BufferData on a mapped buffer replaces the store and
  unmaps it (Mesa and GL 2.1). Draws reading buffers use a fourth array
  record, the buffer draw (DATA function DrawRangeElements, ten words): the
  attribute draw's 32 descriptors, a (buffer, offset, stride) reference per
  buffer slot, client sections holding only the vertex range the indices
  reach, and inline indices or an element buffer and offset. A distinct
  function keeps every older record's admission; the NT and Win9x kernels
  check its layout. The host keeps its own copy beside each native buffer and
  bounds every read against current sizes: element-buffer indices by the
  buffer's tracked maximum values when those fit (static index buffers), else
  by scanning them in that copy; a missing buffer, a read beyond one or an index outside the client
  vertices is GL_INVALID_OPERATION, never a batch fault, as is a buffer draw
  while compiling. Lists dereference buffers at compile time into client
  draws, as the extension requires, and so do draws the record cannot express
  (unaligned offsets, strides beyond 2048, oversized client sections).
  Storage counts against the resource budget at its size, at most
  DG_GL_MAX_BUFFER_BYTES (128 MiB) per buffer and DG_GL_MAX_BUFFERS (16384)
  names per share group; big-endian hosts do not advertise it. From the Wine
  source, WineD3D then creates buffer objects for non-dynamic D3D vertex and
  index buffers outside SYSTEMMEM; dynamic buffers stay in system memory
  (Wine wants ARB_map_buffer_range for them, which DreamGPU does not
  advertise). Wine patches 0037-0040 adapt it (see the patch README): buffer
  objects keep their system memory copy and upload dirty ranges, this tree's
  broken fixed-function D3DCOLOR/XYZRHW conversion is repaired, immediate mode
  reads memory instead of buffer offsets, and the zero attribute buffer covers
  every vertex and dies with its contexts. No D3D cap changes. The installed
  vertex buffer probe has not yet run on a guest.
- Framebuffer objects (`DG_CAP_GL_FRAMEBUFFER_OBJECT`) add
  EXT_framebuffer_object, EXT_framebuffer_blit and EXT_packed_depth_stencil.
  Framebuffer names are virtual and private to each context; renderbuffers
  share the texture namespace. The guest's framebuffer zero stays the host's
  drawable framebuffer (back buffer COLOR_ATTACHMENT0, front COLOR_ATTACHMENT1,
  packed depth/stencil): the host owns the draw and read bindings, rebinds the
  guest's objects natively whenever the platform rebinds the drawable (make
  current, SwapBuffers, export) and binds the drawable again for zero, so
  presentation, front/back selection and window readback are unchanged with an
  object bound. Draw and read buffers are per framebuffer: the window keeps
  its own, an object starts with COLOR_ATTACHMENT0, and PopAttrib restores a
  saved buffer only into the framebuffer it came from. Names reach the host
  only through Bind (the frontend answers Gen/Is), every command but
  BlitFramebuffer executes immediately, and everything that depends on state
  (attachments of the window, wrong or missing objects, levels beyond a
  target's limit, storage beyond the native limit, a window buffer for an
  object) is a GL error. Completeness is native GL 3's, so mixed sizes and
  formats can be complete. Attachments hold their textures and renderbuffers:
  an image deleted while attached to an unbound framebuffer lives until
  detached, and deletion detaches from this context's bound framebuffers
  only. Rendering into attachments is fenced when the context stops drawing
  into them (rebinding, detaching, deleting, switching contexts) and versioned
  before texture reads, so other contexts and the texture read cache see it.
  Renderbuffers are zero-initialized and charged against the resource budget;
  reads (ReadPixels, CopyPixels, CopyTex*, blit sources) of an image the host
  never initialized are INVALID_FRAMEBUFFER_OPERATION. ReadPixels from an
  object is clipped by the host to its attachments, so reads beyond the
  window work. GenerateMipmapEXT defines and charges the generated chain. For
  depth attachments, TEXTURE_2D images also take the depth and packed
  depth/stencil internal formats (DEPTH_COMPONENT/BYTE..FLOAT and
  DEPTH_STENCIL/UNSIGNED_INT_24_8 pixels, CopyTexImage from the read
  framebuffer's depth, zero-defined storage); ARB_depth_texture is not
  claimed and depth GetTexImage is unsupported. Native tests cover
  render-to-texture with depth and stencil, cube faces, 3D slices, 1D,
  renderbuffers, status, scaled, flipped, depth and out-of-bounds blits, the
  window after binding zero including presentation, deletion, sharing,
  GenerateMipmap, lists, depth textures and the point sprite origin (Linux
  host). The scalar PointParameterf record passes POINT_SPRITE_COORD_ORIGIN
  (GL 2.0) to native GL, and objects and the window share one orientation.
  From the Wine source, WineD3D then selects ORM_FBO instead of backbuffer
  offscreen rendering. With this tree's always_offscreen every D3D8/9
  swapchain renders into an FBO texture and presents with
  `glBlitFramebuffer`; offscreen targets, depth surfaces and DirectDraw
  surfaces get their own textures, colour and depth blits use the FBO blitter,
  and adapter initialization decides render-target and post-pixel-shader
  blending support per format with FBO checks (several hundred synchronous
  queries). Wine patches 0042-0046 adapt it (see the patch README): NPOT
  resources stay padded 2D textures instead of falling to renderbuffers,
  CPU depth transfers (0017) gain an FBO mode, a depth buffer larger than
  its target is attached directly, overlapping self-blits go to the CPU
  blitter, and point sprites stay upright below GL 2.0. Expected caps changes:
  D3DCAPS8/9 and the reported card are unchanged; CheckDeviceFormat render
  targets follow attachable formats instead of the window's pixel format
  (R5G6B5, A2B10G10R10 and, on D3D9, A2R10G10B10 and A16B16G16R16 on a 32-bit
  desktop; the 32-bit formats on a 16-bit one), D16_LOCKABLE and D32 join the
  depth formats, every render-target/depth pair matches, and DirectDraw
  enumerates a 32-bit Z buffer. The installed render-target probe (render
  targets of 1024x1024 beside a 320x240 window, depth and stencil of that
  size, StretchRect/CopyRects, offscreen surfaces) has not yet run on a guest.
- Name-sized scalars (`TexParameter`, `TexEnv`, `Hint`) are pre-validated by the
  frontend against the same tables as their vector forms. An unknown,
  vector-only or unnegotiated name is a local `GL_INVALID_ENUM`; previously the
  host rejected the record as malformed, which failed the whole batch and left
  the context permanently failed.

Patch provenance stays checked in `support/guest/d3d/patches`; vendor trees are
not edited. New texture-border behavior is negotiated between frontend and host.
Use matching native and guest artifacts when testing the milestone.

`DG_CAP_GL_TEXTURE_IMAGES` gates the extended existing image-record semantics;
`DG_CAP_GL_TEXTURE_BORDERS` additionally promises supplied border texels in
filtering. Linux's patched Mesa provider implements 1D/2D storage and sampling.
On macOS, a private fragment sampler compensates for CGL retaining supplied
border bytes while ignoring them during native sampling. It uses an RGBA16 atlas
with complete mip images, projected coordinates, wrap modes, texture environments,
COMBINE and fog; native vertex processing and depth/stencil/blend tests remain
active. Temporary texture-unit and program state is restored after immediate,
array and evaluator draws. The atlas is invalidated by shared texture versions,
retains referenced storage through name deletion and is charged against the
resource budget. The sampler itself is not a public shader API, and it steps
aside while a guest ARB fragment program is enabled.
Exact native tests check 1D edges and 2D corners, signed border updates, copies,
readback and subsequent draws. Differential tests compare all six minification
filters and base-format/environment/fog combinations with ordinary textures.
Linux provider software runtime tests separately exercise sampling and resource
lifecycles; acceptance on Linux hardware and all installed guest APIs remains
part of the broader verification matrix.

## Required local verification

Run these once after the milestone sources settle. Use fresh output directories;
reports include exact source and log hashes and reject source changes during a
run. Native graphics verification is an explicit required job: ordinary
`cargo test` skips these GPU tests.

```sh
python3 scripts/quality/graphics.py inventory
python3 scripts/quality/graphics.py source --output target/graphics-m1-source
python3 scripts/quality/graphics.py native --output target/graphics-m1-native
```

The native job rebuilds QEMU from this checkout, runs host unit tests and every
ignored diskless native GPU test with one test thread. A successful command that
runs zero tests or leaves native tests ignored fails verification. Do not use
`DREAMGPU_BUILD=sdk` to claim a rebuilt native renderer.

Run `source-i686` inside the pinned Linux guest toolchain container, using the
same mounted checkout. It verifies the real 32-bit frontend ABI and packing:

```sh
python3 scripts/quality/graphics.py source-i686 --output target/graphics-m1-i686
```

Build the combined installer once using [the guest build instructions](build.md).
Keep the audited installer manifest and hashes with the verification artifacts.
Only repeat packaging and VM tests for a failed case that requires a fix or a
later milestone; source tests do not require guest installation.

## Installed API acceptance

Glide 3: `PROBE <id> sysglide3` runs the installed `C:\DGSYSG3.EXE` from `C:\` and writes
`C:\DGSYSG3.LOG`. It is not an installer activation proof; record it with a Glide 3 game
(Diablo II `-3dfx`) on each combination.

Use disposable clones. Do not install the milestone into the user's VM.
Upgrade and recovery on Win2000 (m23 package): an m16 installation upgrades
in place to a terminal receipt (`intent` upgrade, exit 0). A failed upgrade
whose activation probe failed (exit 26, an m19 package) recovers with that
installer's `/rollback` (exit 11, then 12 "restored" after one restart), after
which the m23 installer upgrades cleanly and `sysgl`, `sysd3d9` and `sysglide3`
pass. A newer installer cannot continue another installer's unfinished
operation (exit 29), and owned system files changed by hand also give exit 29.

The fixed serial runner accepts `capd3d6`, `capd3d8`, `capd3d9` and `capgl`;
[capability.md](../tools/d3d/capability.md) documents staging and collected JSON.
Capability JSON is bounded and retained verbatim. A capture records claims;
it never becomes a rendering pass merely because the process exited normally.

For each of the six OS/host combinations, retain the installed provider identities,
capability capture, exact public API results and relevant screenshots. Required
chains include render/read/edit/upload/render, partial updates with untouched
texels, palette changes without recreation, target A/B/A preservation, context
sharing/deletion, GPU/GDI ordering, allocation/transfer failure and retry, and
fullscreen/minimize/reset/exit. Integer data needs exact checks; floating-point
and rasterization comparisons need stated tolerances.

Golf acceptance requires a visible animated golfer and a completed shot.
Terrain/HUD rendering, a triangle, or a successful installer alone is insufficient.
The Win2000 regression also starts at **Quick Start**, when the launcher hands
off to the course process: the initial course must retain its requested display
mode without an Alt+Tab workaround. Open and close menus, click and drag the shot
control, and switch away and back; UI and golfer pixels must survive without
hovering to repaint them. A correctly sized launcher menu does not cover this
handoff. Equal-sized fullscreen mode requests still need the native ownership
call, and windowed swapchains must not restore a fullscreen display mode.

Linux result (2026-11-03, installer built from patches 0001–0026, QEMU
`e8e4a38a`): Quick Start keeps 800×600 scanout and renders golfer, terrain and HUD,
and opening menus/options/preferences no longer erases them. With the preferences
modal open, guest Win+D then Alt+Tab leaves black terrain apart from GDI-repainted
rectangles and blank YES/NO boxes until the modal closes. Closing it restores the
course, but the toolbar's right half stays blank. A bounded call trace shows Golf
presents with a full-frame `BltFast` from an 800×600 system-memory canvas and
issues no DirectDraw call at all after reactivation until the modal closes. The
same sequence is **pixel-identical** with Microsoft's DirectDraw on DreamGPU and
on a stock QEMU Cirrus adapter with Microsoft's driver and DirectDraw. Those
artifacts are therefore the game's own Windows 2000 behavior, not a DreamGPU
defect; do not "fix" them with blanket invalidation. A completed shot is still
unverified. Evidence: `target/golf-coherence-linux/verification.json`.

Linux multitexture result (2026-11-03, patches 0001–0027, frontend with
GL1.2 packed uploads, multitexture/fixed-state host): a fresh Win2000 clone
installs to a terminal receipt, and `sysgl`, `bordergl`, `sysglide`, `sysddraw`,
`sysd3d6`–`sysd3d9` and all four capability captures pass, including the D3D8/9
multi-stage probe. The Half-Life `jrgperf` timedemo ran at 193.6 and 205.0 FPS
(177.4 before multitexture). UT99 through D3D renders CityIntro with lightmaps
in a window. macOS CGL, Win98 and XP remain unverified for these capabilities.
With texture control the installed D3D9 caps add CANAUTOGENMIPMAP (Caps2
`0x60020000`), TextureOpCaps `0x0386ffff` and MIRRORONCE addressing
(`0x3f`); the stage probe renders all thirteen claimed cases exactly.

With compressed textures, fresh Win2000 and Windows XP clones (the user's XP
image, never modified) install to terminal receipts and pass every probe; the
D3D8/9 probes render DXT1/DXT3/DXT5 blocks exactly on both. Half-Life runs at
192 FPS on Win2000.

With fog coordinates, a textured pretransformed quad fogs from red to blue
exactly on Win2000 and XP through D3D8/9; the same probe reads red fading to
black (the emulation) on the previous build.

With cube maps, D3D8 and D3D9 report CUBEMAP, MIPCUBEMAP and CUBEMAP_POW2, and
a MANAGED A8R8G8B8 cube texture samples each of its six faces exactly on
Win2000, XP and Win98 SE.

Windows 98 SE (Linux host, fresh clone of the user's Win98 image, legacy
`qemumini` migration): installs to a terminal receipt; OpenGL, border, Glide,
DirectDraw and D3D6–9 probes pass, including all thirteen D3D8/9 stage cases,
DXT1/3/5 and textured vertex fog. Two Win9x `CreateThread` calls passed a NULL
thread-id destination, which Windows 95/98/Me reject: the D3D6 probe's worker
and the interactive installer's worker (the interactive installer exited with
code 32 on Win98). GL window presentation on Win98 now accepts 16-bpp (RGB565)
desktops as well as 32-bpp ones: the frontend bind, the 16-bit BitBlt DDI and
the VxD primary metadata take matching depths, every desktop record carries the
format word, and a depth change while GPU-owned faults like a size change.
Before this every 16-bpp exclusive fullscreen DirectDraw/Direct3D primary
failed (`capd3d6` failed at that step); it now passes at 640×480×16.

Win98 USER passes a colour cursor whose bitmap matches the desktop depth at
that depth. It converts every other colour cursor to packed 4-bit indices of
the standard 16-colour palette (Windows order), with a centred hotspot, as
observed on Win98 SE through a traced driver. That includes Wine's 32-bit
D3D8/9 hardware cursors on a 16-bpp desktop, as well as 8-, 24- and 32-bit
bitmaps. The driver used to keep only monochrome and 32-on-32 cursors native,
so on a 16-bpp desktop every colour cursor fell back to GDI's software cursor.
GL takeover then refused every bind while one was shown.

1-, 4-, 16- and 32-bit cursors now stay native on 16- and 32-bpp desktops. The
host composite `(desktop & and) ^ xor` commutes exactly with the
bit-replicating 565 expansion it already applies to a 16-bpp desktop. A colour
is therefore truncated to 565 and expanded the same way, which gives exactly a
565 framebuffer's pixels. 8-bit indices would need the system palette and
still fall back.

Links 2003 (DirectDraw7 through Wine, Win2000): two defects stopped it. Every
depth-to-depth Blt into the game's spare Z-buffer returned INVALIDCALL, because
mapping a discarded offscreen depth surface asked for a native depth transfer
only the device-owned drawable has (patch 0028 defines a zeroed CPU copy
instead). With multitexture, Wine's fast pretransformed path set the unit-0
coordinate pointer on whichever client unit Wine's own loaders left selected,
so every menu tile sampled one texel; it now selects unit 0. Menus and Quick
Start render correctly. With the m24 package (vertex buffer objects) a Kauri
Cliffs round on Win2000 renders the course, sea, sky, golfer and HUD with
correct colours, and the computer player completes its first shot.

Game checks on DreamGPU fixtures (2026-11-03, Linux host):
- **Quake III Arena demo** (OpenGL, Win2000): installs, runs at 640×480, and its
  menu and demo playback render correctly (lightmaps, additive effects, fog,
  HUD).
- **Expendable demo** (D3D6): runs at 640×480×16 on Win2000 and Win98 SE once
  the retro profile hides modern CPUID 0x80000001 EDX bits (Juke 797432a). The
  game's 3DNow! test (`test edx, 0xffffffff`) otherwise selects 3DNow! code that
  faults on FEMMS. It does not use its EMBM path: with `BumpMapping [1]`, an
  app-local logging `wined3d.dll` shows gameplay compiling no BUMPENVMAP
  fragment program. The same trace showed its specular-alpha vertex fog
  falling back to Wine's per-vertex software emulation on nearly every draw.
- **SimCity 3000** (DirectDraw, Win2000): reaches its menu and renders a
  starter town's city view, UI and ticker after Wine patch 0032.
- **Turok 3Dfx demo** (Glide, Win98 SE): faults before its video DLL loads,
  identically without any Glide provider and on an emulated Pentium III, so the
  fault is not DreamGPU's. Its cause is unknown.
- **Diablo II 1.12** (Win2000): installs and plays in DirectDraw and Direct3D
  modes (menus, hero select, town). `-3dfx` runs full screen at 800×600 through
  `glide3x.dll`. Its menu exposed two chroma-key bugs in the Glide core: an RGBA
  key converted the wrong way round (the logo was drawn on black, Glide patch
  0005), and keying skipped for blended batches (the multiplied button labels
  were black blocks, 0007). Both render correctly from the m23 package.
- **Unreal Tournament GOTY** (Glide, Win98 SE): renders CityIntro through the
  system glide2x provider. With the m20 package its first-time configuration
  picks "3dfx Glide for Windows", the intro runs full screen at 640×480, and
  quitting from the console restores the 1024×768 desktop. A stale application-local `glide2x.dll` in the
  image's `UT99\System` (an old OpenGLide 0.09rc9 build) hung it in
  `grSstOpen`; it is not part of DreamGPU.
- **3DMark2001 SE** (Win2000, build 330 Pro): its bundled SystemInfo reports
  no CPU family, clocks or capabilities, then warns that no MMX was found
  (build 300 refuses to run). Windows itself reports MMX and SSE
  (`IsProcessorFeaturePresent`), and a P6 or Pentium 4 CPUID signature, an
  SMBIOS Pentium III entry and a 32-bit SMBIOS entry point all change nothing.
  The Fill Rate test renders correctly through D3D8. Every scene test (Game 1,
  Vertex Shader) crashes before issuing a shader or failing D3D call, inside
  3DMark's own `P_VertexShader` cleanup, presumably on the code path chosen by
  its failed CPU detection. The same crash happens with D3D Software T&L and
  again after upgrading the fixture from m16 to the m23 package.
- **GTA Vice City** (Win2000): installs, then exits silently before loading
  D3D8. The executable is SecuROM-protected (`.cms_t`/`.cms_d` sections), and
  SecuROM's physical-disc checks fail on an emulated CD drive.

`capd3d6` additionally checks fullscreen primary/GDI coherence at 640×480×16.
It compares exact RGB565 pixels through primary Lock, GetDC, source blits,
batched GDI writes followed by ReleaseDC, and partial/keyed primary blits over
changing GDI backgrounds. This tests destination preservation at the installed
API boundary; the native compositor's clipped-copy tests alone cannot establish
that Wine supplies the correct untouched pixels when publishing a whole window.
Glide, execute buffers, D3D7/8/9 fixed-function behavior, resource pressure and
lifecycle cases still need the expanded installed suite. Live GPU snapshot
restoration remains unsupported.

```sh
python3 scripts/quality/graphics.py acceptance
```

This currently fails closed and lists known gaps, including missing installed
API evidence. Evidence ingestion for full conformance is not yet implemented.
Neither local sanitizer success nor capability capture can clear this gate.
