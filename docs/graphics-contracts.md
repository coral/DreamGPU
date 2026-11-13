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
- Indexed client-array draws travel in bulk with
  `DG_CAP_GL_VERTEX_BUFFER_OBJECT`: DrawElements of client arrays alone is the
  buffer draw with a zero buffer mask (each array's vertices of the indexed
  range once at its wire type, then the indices inline) whenever that is no
  larger than the per-index expansion and fits one packet; sparse ranges,
  ranges beyond DG_GL_MAX_VERTICES, more than DG_GL_MAX_INDICES indices,
  display list compilation and every error keep the expansion's records and
  GL errors. The host admits it as any buffer draw, bounding every inline index
  by the client vertices, so the frontend sends only indices it scanned. The
  frontend then offers EXT_compiled_vertex_array (LockArraysEXT,
  UnlockArraysEXT, ARRAY_ELEMENT_LOCK_FIRST/COUNT_EXT under EXT names only,
  saved by CLIENT_VERTEX_ARRAY_BIT, executed immediately, never recorded); a
  locked draw submits exactly the unlocked draw's records. GL_EXTENSIONS lists
  names by age, not contract, each under its own gate: the names games of
  1997-2001 probe (multitexture, compiled vertex arrays, texture environments,
  anisotropy, edge clamp, point parameters, compression, secondary colour, fog
  coordinates, cube maps) end by character 461, inside Quake III 1.11's
  1023-character copy, and the newer program, buffer, framebuffer, BGRA, query
  and rectangle names follow (1108 characters with every contract). Quake
  III-engine games then draw with DrawElements instead of strips of
  ArrayElement (r_ext_compiled_vertex_array 0 keeps their old path); without the
  capability the extension stays hidden and every record is unchanged.
  ArrayElement keeps its records, built without per-component dispatch.
  Native pixel tests show the buffer draw and the expansion drawing identical
  images (a two-pass multitexture batch, mixed formats, lighting, generic
  attributes); the Quake III timedemo has not yet been measured on a guest.
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
- BGRA vertex arrays (`DG_CAP_GL_VERTEX_ARRAY_BGRA`) add ARB_vertex_array_bgra:
  size BGRA for ColorPointer, SecondaryColorPointerEXT and
  VertexAttribPointerARB, UNSIGNED_BYTE only (other types are
  GL_INVALID_OPERATION, as is a generic BGRA array that is not normalized;
  BGRA stays GL_INVALID_VALUE for every other array). The size queries answer
  BGRA, CLIENT_VERTEX_ARRAY_BIT saves it and InterleavedArrays clears it. The
  frontend reorders client memory into the RGBA sections of the existing array
  records while packing (a secondary colour as its RGB), so every older
  record keeps its admission on older kernels and hosts, and display lists and
  ArrayElement read B, G, R, A locally. Buffer arrays cannot be reordered
  without reading them back, so a draw with a BGRA buffer slot uses a fifth
  array record, the BGRA buffer draw (DATA function DrawRangeElementsEXT): the
  buffer draw's words, payload and rules plus `DG_GL_ARRAY_BGRA` on colour,
  secondary colour and generic buffer slots, which the host passes to native
  GL as size BGRA and bounds as four-byte elements. The NT and Win9x kernels
  check it like the buffer draw and keep refusing the flag there; client
  sections never carry it. Native BGRA arrays are required of the host GL
  (core in GL 3.2; Mesa's compatibility profile has them; macOS CGL's
  EXT_vertex_array_bgra is unverified), and a native refusal is a GL error.
  From the Wine source, WineD3D then stops drawing fixed-function draws whose
  D3DCOLOR diffuse or specular streams are not all in buffer objects in
  immediate mode (D3D8/9 UP draws, dynamic and SYSTEMMEM buffers, D3D5-7
  DrawPrimitive from user memory and system-memory vertex buffers), no longer
  converts D3DCOLOR in buffer objects, and vertex shaders read D3DCOLOR through
  BGRA generic arrays instead of swizzling in the shader. Wine patch 0048
  draws pretransformed (XYZRHW) streams outside buffer objects from arrays too,
  positions converted as immediate mode converted them (see the patch README;
  its installed probes have not yet run on a guest). Wine patch 0047 binds a
  stride-zero D3DCOLOR attribute 0 correctly (see the patch README). No D3D
  cap changes. The installed D3D7 and D3D8/9 colour-array probes (diffuse
  plus specular from user memory, dynamic, system-memory and static buffers,
  and a vs_1_1 D3DCOLOR input) have not yet run on a guest.
- Occlusion queries and fences (`DG_CAP_GL_OCCLUSION_QUERY`) add
  ARB_occlusion_query (SAMPLES_PASSED, 32 counter bits) and NV_fence
  (ALL_COMPLETED_NV) under their extension names. Query and fence objects
  belong to one context, as both extensions require, even between sharing
  contexts; the host owns names (GenQueries/GenFencesNV are QUERY records),
  objects (a query from its first BeginQuery, a fence from its first
  SetFenceNV, of any nonzero name), the active query and every
  state-dependent error: BeginQuery while one is active or of zero, EndQuery
  without one, GenQueries and DeleteQueries while one is active (the extension's
  rule, which leaves names and the active query untouched), results of the
  active query or of a non-object, and the fence records of a non-fence are
  GL errors, never record faults. BeginQuery, EndQuery and SetFenceNV compile
  into display lists, as the extensions specify (only Gen, Delete, Is and the
  queries execute immediately); deletion is a DATA record that executes
  immediately even while compiling. Each object owns a native query, or the
  native sync object of its last SetFenceNV, in the context's native context,
  so pending results survive presents, context switches and switching back;
  host-internal blits and clears count no samples. Results stay asynchronous:
  the host never waits except for QUERY_RESULT and FinishFenceNV, and a
  pending availability or fence status flushes native GL so it completes in
  finite time. One GetQueryObjectuiv QUERY answers both pnames (state, result)
  and carries up to 63 (name, result) reports of earlier ended queries that
  completed, oldest first, each reported once; the frontend keeps them and
  signalled fences per context until the name is begun, set or deleted or a
  display list executes, so Wine's availability-then-result poll costs one
  round trip and polling several ended queries usually one. Objects and name
  tables are charged against the resource budget (4096 names per kind and
  context). From the Wine source, WineD3D then creates D3DQUERYTYPE_OCCLUSION
  queries (D3DERR_NOTAVAILABLE before; D3D9 has no caps bit for them) and
  backs D3DQUERYTYPE_EVENT with NV fences: before, Wine created event queries
  but an issued one's GetData failed with D3DERR_INVALIDCALL ("Event query
  created despite lack of GL support"). Timestamp queries stay unavailable
  (no ARB_timer_query), D3D8 has no queries, and this Wine still answers a
  pending event S_OK with FALSE where Windows answers S_FALSE. No D3D cap
  changes and no Wine patch. The installed D3D9 query probe (2000 visible,
  3200 partially occluded, 0 hidden and 65200 reused samples exactly, and an
  event polled to signalled) has not yet run on a guest.
- Rectangle textures (`DG_CAP_GL_TEXTURE_RECTANGLE`) add ARB_texture_rectangle
  and NV_texture_rectangle, the GeForce FX's non-power-of-two textures, and no
  entry points: `TEXTURE_RECTANGLE` is a binding, enable and parameter target
  with its own binding per unit (TEXTURE_BIT, precedence cube > 3D > rectangle
  > 2D > 1D), and the existing 2D image, subimage, copy, readback, level query
  and `FramebufferTexture2D` records name it (`TexImage2D` and level queries also
  its proxy). A rectangle has level zero only, no border, an uncompressed
  colour internal format and at most 2048 texels per side; these are record
  rules the frontend reports locally (`GL_INVALID_VALUE`, `GL_INVALID_ENUM` for
  compressed formats and records, `GENERATE_MIPMAP` and `GenerateMipmapEXT`),
  while parameter values (REPEAT, MIRRORED_REPEAT and mirror-once wraps, mipmap
  filters: `GL_INVALID_ENUM`; a nonzero BASE_LEVEL: `GL_INVALID_OPERATION`) and
  a nonzero attachment level are native or host GL errors. Coordinates are
  unnormalized, also through ARB_fragment_program's RECT target. Storage is
  zero-defined without a payload and charged like a 2D level.
  ARB_texture_non_power_of_two is deliberately not claimed: a real FX driver
  only implied it through GL 2.0 and WineD3D's FX quirk removes it, while
  rectangles are what Wine uses on that card, so DreamGPU takes the same Wine
  path. Native pixel tests (Linux host) sample a 37x23 rectangle exactly with
  NEAREST and LINEAR, its last row and column and CLAMP_TO_EDGE included, and
  cover subimages, copies, readback, proxies, the errors, rendering into a
  rectangle through a framebuffer object and sampling it back, fragment
  program TEX/TXP with RECT, precedence, bindings, lists and deletion. From the
  Wine source, WineD3D then reports D3DPTEXTURECAPS_POW2 with
  NONPOW2CONDITIONAL, as the GeForce FX 5800 it names does (TextureCaps D3D9
  0x7ed87, D3D8 0x7ed9f; before 0x7ec87/0x7ec9f), and Wine patch 0049 keeps
  formats a rectangle cannot hold in padded 2D textures (see the patch README):
  NPOT render targets, backbuffers, offscreen plain surfaces, DirectDraw
  surfaces and NPOT textures become exact-size rectangles (800x600 stays
  800x600 instead of a 1024x1024 texture), depth/stencil surfaces and DXTn
  textures stay padded, an NPOT DXT texture still fails to create
  (D3DERR_NOTAVAILABLE) and a mipmapped NPOT texture with
  D3DERR_INVALIDCALL. The installed D3D8/9 NPOT probe (the caps, a 640x480
  texture sampled 1:1 with clamping, a 640x480 render target sampled back,
  and the conditional restrictions) has not yet run on a guest.
- Depth textures (`DG_CAP2_GL_DEPTH_TEXTURE`, the first bit of the second
  capability word `DG_GL_REG_CAPS2`, which older hosts read as zero and older
  kernels report as zero) add ARB_depth_texture, ARB_shadow and, with fragment
  programs, ARB_fragment_program_shadow, as a GeForce FX listed them, and no
  entry points or records: borderless depth images (`DEPTH_COMPONENT(16/24/32)`,
  `DEPTH_STENCIL`, `DEPTH24_STENCIL8`) of 1D, 2D and rectangle textures and
  their proxies through the existing image, subimage and copy records,
  `TEXTURE_COMPARE_MODE`/`FUNC` and `DEPTH_TEXTURE_MODE` on every target (values
  beyond the extensions, such as the other six functions, are local
  `GL_INVALID_ENUM`; there is no EXT_shadow_funcs), the depth and stencil level
  sizes, and `GetTexImage` level-word bit `DG_GL_TEXTURE_READ_DEPTH` for float
  depth (with `READ_FLOAT`: packed `UNSIGNED_INT_24_8` words). Cube and 3D
  depth images are `GL_INVALID_OPERATION`. Comparison, `DEPTH_TEXTURE_MODE` and
  filtering are native; fixed-function 1D lookups on Mesa take the reference
  from t, not r (SHADOW1D programs use r). Native pixel tests (Linux host,
  private Mesa on Radeon) compare every function, mode and reference exactly
  with NEAREST, average comparisons with LINEAR (a quarter texel: 64 and 191),
  divide projective references, copy the window's depth bit-exactly into 2D, 1D
  and rectangle textures, render into depth (and packed depth/stencil, also
  rectangle) attachments of framebuffer objects and sample them, read depth as
  floats and packed words, run SHADOW1D/2D/RECT programs (TEX, TXP), compile the
  parameters into lists and keep the batch through the spec's errors; the
  device without a GL engine reads both capability words without GL bits.
  WineD3D's CheckDeviceFormat(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE) then
  accepts D16, D24X8 and D24S8 (all `D3DERR_NOTAVAILABLE` before); without Wine
  patch 0050 it would also accept D16_LOCKABLE, D32 and INTZ and depth cube
  textures, which a GeForce FX never had. Such textures compare (NVIDIA hardware
  shadow maps, `DEPTH_TEXTURE_MODE` INTENSITY); Wine 0051 names SHADOW targets
  in its fixed-function replacement and pixel shader programs, whose lookups
  were otherwise undefined. With the depth formats rectangle-capable, NPOT
  D16/D24X8/D24S8 depth-stencil surfaces become exact-size rectangles like
  colour targets. The installed D3D8/9 probe (`tools/d3d/depth-texture-probe.inc`:
  the format answers, a D24S8 texture rendered as a depth-stencil surface and
  sampled with projected comparisons through fixed function, ps_1_1 and, on
  D3D9, ps_2_0 `texldp`, and a LINEAR boundary) has not yet run on a guest;
  macOS CGL, Win98/XP guests and games remain unverified.
- Exact R5G6B5 storage (`DG_CAP2_GL_RGB565`, the second bit of the second
  capability word) admits the internal format `RGB565` (0x8D62) in the
  existing 1D, 2D, rectangle, cube face and 3D image, proxy, CopyTexImage and
  RenderbufferStorage records, with no extension name: ARB_ES2_compatibility
  (2010) would also claim shader precision queries, `FIXED` arrays and float
  depth-range entry points no GeForce FX had. WineD3D maps Direct3D's R5G6B5 to
  `GL_RGB565` only under that extension; otherwise to `GL_RGB5`, which Mesa
  stores in 5/5/5 bits, so a 6-bit green 13 read back as 12 in every R5G6B5
  texture, render target and 16-bit back buffer (a GeForce FX kept all six
  bits). Wine 0057 asks the frontend instead (a 1x1 `RGB565` texture, then
  `glGetError`) and keeps `GL_RGB5` on older frontends and hosts. The host
  stores `RGB565` natively where the native GL has it (GL 4.1 or
  ARB_ES2_compatibility: Mesa on Linux) and otherwise in `RGB8` (macOS legacy
  CGL, unverified); both hold every 5/6/5 value exactly, and level and
  renderbuffer queries answer the native storage. `RGB5` itself is unchanged.
  X1R5G5B5 (`GL_RGB5`), A1R5G5B5 (`GL_RGB5_A1`) and A4R4G4B4 (`GL_RGBA4`)
  were already exact on Mesa (every 16-bit word round-trips, clears exact).
  Colour-keyed R5G6B5 textures are still converted to B5G5R5A1 by Wine and
  keep five green bits. Native pixel tests (Linux, Mesa on Radeon) round-trip
  all 65536 words as RGBA8 and float, render column clears (exact and 8-bit
  colours), a texture sampled 1:1 and DrawPixels into an `RGB565` texture
  attachment and renderbuffer, copy from it and from the RGBA8 window with
  CopyTexImage/CopyTexSubImage and define an image through a display list.
  The installed D3D8/9 probes check a Lock-filled R5G6B5 texture sampled 1:1
  into a lockable R5G6B5 target plus clears of all 64 greens (rtt-probe) and
  lock all 64 greens (partial-lock probe), skipping both without the
  capability; they have not yet run on a guest.
- Bulk reads (`DG_CAP2_GL_BULK_READ`, the third bit of the second capability
  word) make a whole-surface readback one synchronous QUERY instead of one per
  64 KiB. WineD3D downloads a whole surface for every full lock: Warcraft III
  locks its 1024x768 back buffer read-only after each present, 3
  `GetTexLevelParameteriv` and 48 `GetTexImage` round trips per frame (about
  40 FPS, vCPU-bound, on a Win2000 guest). A QUERY's result may now go through
  a page list (`DG_GL_RESULT_PAGES` in the capacity word): the kernel's
  driver-owned 4 MiB buffer, never a caller's pages, described by a
  driver-owned list of its frames; the device checks the list and every page
  as writable RAM at submission and again before writing, and writes nothing
  outside the listed pages. A level's shape (width, height, depth, depth and
  stencil sizes, from the same native calls) is one query, and
  `GetTexImage`/`GetCompressedTexImage` counts are in units of 64 pixels or
  words, so the frontend reads that back buffer with 2 round trips (51
  before); `ReadPixels` of a 1024x768 window takes 1 (48 before), packed
  depth-stencil 2. Results are byte-identical to the chunked reads, which
  older hosts, kernels and frontends keep using unchanged. NT and Win9x
  kernels offer 4 MiB results only with the capability and their buffer;
  Win9x copies the caller's output in pinned 64 KiB windows. The frontend
  converts RGBA/BGRA 8-bit reads row by row. Native tests (Linux, Mesa on
  Radeon) read 1024x768 RGBA8, a BGRA-uploaded 1023x767 rectangle, RGB565,
  depth and packed depth-stencil levels, float levels across 4 MiB chunks,
  unit counts around page, 64 KiB and 4 MiB boundaries, a DXT1 image and
  whole-window colour, depth, stencil and float reads through both
  transports, scattered over a reversed page list with canary pages, and
  reject malformed lists (unaligned, zero, past RAM, ROM, over-capacity)
  without writing. The installed D3D8/9 probe's whole-surface stage
  (`tools/d3d/partial-lock-probe.inc`: 1024x768 X8R8G8B8 and R5G6B5 targets,
  a colour per row and per 64th column, locked whole twice) has not yet run
  on a guest; Warcraft III FPS remains to be measured.
- Direct3D fixed-function vertex blending (skinning: `D3DRS_VERTEXBLEND` 1-3
  weights over `D3DTS_WORLD`..`WORLD3`) needs no capability bit and no frontend
  or host change. WineD3D's fixed-function pipeline blended only through
  GL_ARB_vertex_blend, which Mesa lacks and a GeForce FX never listed, so it
  claimed one matrix and drew skinned meshes in their bind pose. The
  app-visible GL stays faithful (no ARB_vertex_blend; emulating it in the
  frontend would need every modelview operation, display lists included,
  shadowed bit-exactly in the guest, object-linear texture generation and
  raster positions on the CPU and `vertex.weight` in host programs), and Wine
  patch 0052 blends instead: each vertex's position and normal into world
  space (sum of w_i v WORLDi, normals through each WORLDi inverse transpose,
  the last weight 1 minus the others, as ARB_vertex_blend's WEIGHT_SUM_UNITY)
  as client arrays of the draw, or per vertex in immediate mode, drawn under
  the view matrix alone. Wine specifies lights, clip planes and texture
  generation planes under the view matrix and lighting, NORMALIZE, texture
  generation and fog read eye space, which view * sum(w_i WORLDi v) is
  unchanged. Draws without blending are byte-identical; 0 weights (indexed
  blending), unsupported weight formats and unreadable streams keep today's
  unblended draw (tweening is 0053, below). D3D8 and D3D9 then report the GeForce FX 5800's
  MaxVertexBlendMatrices 4 and MaxVertexBlendMatrixIndex 0 (before 1 and 0;
  D3D7 wMaxVertexBlendMatrices likewise); 0052 left VertexProcessingCaps at
  D3D9 0x13b and D3D8 0x13f (the FX's D3D9 0x17b adds TWEENING, which 0053
  claims). Source tests run the prepared helpers against
  an eye-space ARB_vertex_blend reference; the native pixel test draws the
  frontend's records of such draws with lighting, fog and a clip plane under
  the view. The installed D3D8/9 probe (`tools/d3d/vertex-blend-probe.inc`:
  two-, three- and four-matrix quads from user memory and indexed buffers, lit
  normals with and without NORMALIZENORMALS; it skips below two matrices) has
  not yet run on a guest; D3D7, macOS CGL, Win98/XP guests and games remain
  unverified.
- Direct3D fixed-function vertex tweening (`D3DRS_VERTEXBLEND`
  `D3DVBF_TWEENING`, `D3DRS_TWEENFACTOR`; D3D8 and D3D9 only) likewise needs
  no capability bit and no frontend or host change. WineD3D never tweened: it
  printed a FIXME, gave the declaration's second position and normal (D3D9
  usage index 1, D3D8 `D3DVSDE_POSITION2`/`NORMAL2`) no fixed-function slot
  and drew the first position. Wine patch 0053 tweens on the CPU through 0052's
  helpers, in Direct3D's documented order `POSITION1 * (1 - TWEENFACTOR) +
  POSITION2 * TWEENFACTOR` (normals likewise) and in object space: the second
  elements are read from their streams' system memory copies (one stream or a
  second key-frame stream), the results replace Wine's position and normal as
  client arrays or immediate-mode `Normal3fv`/`Vertex4fv`, and the modelview
  stays Wine's, so the GL records are 0052's without its matrix calls (the
  native vertex blend test draws that shape; no new native test). Factor 0
  draws exactly the first key frame and 1 the second. Vertex shaders,
  pretransformed positions, formats other than FLOAT3/FLOAT4, per-instance
  data and unreadable second streams keep the untweened draw; draws without
  tweening are byte-identical. D3D9 VertexProcessingCaps become the GeForce FX
  5800's 0x17b (before 0x13b) and D3D8 0x17f (before 0x13f; D3D8 copies Wine's
  bits unmasked); D3D7, which has no tweening, masks it out. Source tests run
  the prepared helpers against a double-precision reference of the formula.
  The installed D3D8/9 probe (`tools/d3d/tween-probe.inc`: positions from user
  memory, a vertex buffer and two indexed streams at factors 0, 1/2 and 1, a
  stretched quad, tweening disabled, lit normals with and without
  NORMALIZENORMALS; it skips without the cap) has not yet run on a guest;
  macOS CGL, Win98/XP guests and games remain unverified.
- d3d8.dll's `ValidatePixelShader(code, caps, return_error, errors)` and
  `ValidateVertexShader(code, declaration, caps, return_error, errors)` follow
  the runtime's contract (Wine patch 0054, upstream Wine's implementation):
  the version token is checked (vs.1.0-1.1, ps.1.0-1.4), and against the caps
  when given, and `errors` receives a process-heap string, empty unless
  `return_error` is set and the call failed. Wine's stubs failed with any caps
  or declaration and never wrote the string. D3DX8, linked statically into
  games, calls both through `GetProcAddress` on the loaded `d3d8.dll` after
  assembling each shader and fails the assembly with `D3DXERR_INVALIDDATA`
  when they fail. In Unreal Tournament 2003's `D3DDrv.dll` and 3DMark2001 SE's
  `e2_d3d8_driver_mfc.dll` it passes NULL caps and declaration and a
  NULL-initialized string, which it frees with `HeapFree(GetProcessHeap())`;
  the Wine stubs returned `S_OK` for those calls. The failure was the d3d8
  switcher (patch 0055): with `unkToSystemDll` (0003) its user list held only
  `Direct3DCreate8`, so both validators dispatched to its system list, which
  without `msd3d8.dll` (Win2000/XP images) holds stubs returning `E_FAIL`
  without a string. UT2003's DM-Antalus terrain (Win2000) failed
  `DXAssembleShader` with `88760B59` and rendered once `wined8.dll` was its
  application-local `d3d8.dll`. 0055 routes every export Wine implements to
  Wine (d3d8: `DebugSetMute`, `D3D8GetSWInfo`, both validators), fixes
  `D3D8GetSWInfo`'s stub arity (0, not 4 arguments) and makes the pointer and
  query stubs of d3d8, d3d9 and ddraw return safe values. The installed D3D8
  probes (`tools/d3d/shader-validation-probe.inc`, run by `DGD3D8` on
  `wined8.dll` and by `DGSYS8` through the system `d3d8.dll` switcher, where
  before 0055 they fail without `msd3d8.dll`) make
  D3DX8's calls and the runtime's other cases; they and the games have not yet
  run on a guest.
- A Direct3D lock of part of a render target whose current copy is its GL
  texture transfers only the locked rectangle (Wine patch 0056; no frontend or
  host change). WineD3D downloaded the whole surface for any lock that found
  its system memory stale: `glGetTexImage` of every texel, which the frontend
  splits into 64 KiB QUERY round trips (level word `count << 16 | level`:
  `0x40000000` is 16384 RGBA8 pixels of level 0, `0x13000000` a 4864-pixel
  tail; 29 + 1 for 800x600, each preceded by `GetTexLevelParameteriv` WIDTH,
  HEIGHT and, on a rectangle, DEPTH_SIZE). Unreal Tournament 2003's
  `ReduceMouseLag` locks one backbuffer pixel read-only (`D3DLOCK_READONLY`,
  `0,0-1,1`) after every present: 75 FPS on a Win2000 guest at 800x600,
  669 with the option off. 0056 reads the rectangle with one `glReadPixels`
  (one QUERY) from a read framebuffer holding the texture, into the system
  memory copy at the rectangle's offset, so the pointer and pitch are those
  of a whole-surface lock. System memory is not marked current and the
  texture stays authoritative: a read-only unlock transfers nothing, a
  writable one uploads only the rectangle (`glTexSubImage2D`), and a later
  whole lock downloads as before. It applies to backbuffers and lockable
  render targets rendered into their texture through framebuffer objects, in
  the 16- and 32-bit RGB(A) target formats; full and `DISCARD` locks,
  `NO_DIRTY_UPDATE` writes, a current system memory copy, depth/stencil and
  other formats, multisampling, the front buffer, `ORM_BACKBUFFER` and
  onscreen drawables, DIB/user-memory/PBO/client storage, mipmap levels and
  cube faces keep the full transfer, as does an unusable context. Source tests
  run the prepared map/unmap state machine against a pixel-tracking GL. The
  installed D3D8/9 probe (`tools/d3d/partial-lock-probe.inc`: read-only 1x1,
  16x16 and last-pixel and writable 8x8 locks of the backbuffer and of
  lockable X8R8G8B8 and R5G6B5 400x300 render targets, exact against cleared
  pixels and the whole-lock pitch) reads only pixels inside each locked
  rectangle; how much a lock transfers is measured with the transport
  counters instead. With `wined3d.dll` from 0056 app-local in UT2003 on a
  Win2000 guest, DM-Antalus with `ReduceMouseLag=True` ran 806 presents in
  3 s (about 269 FPS, before 67-75) with one `ReadPixels` QUERY per frame
  instead of 3 `GetTexLevelParameteriv` and 30 `GetTexImage` QUERYs, and 14
  batches per frame instead of 46. With exact R5G6B5 storage
  (below, Wine 0057) R5G6B5 targets keep all six green bits and the probe's
  R5G6B5 colours use all 64 greens; without it Wine stores `GL_RGB5` (five
  green bits on Mesa) and the probe uses only greens that survive that. The probe has not yet passed on a
  guest; Win98/XP game runs and macOS CGL remain unverified.
- Point sprites apply to points only (Wine patch 0058; no frontend or host
  change). WineD3D kept `GL_POINT_SPRITE` enabled whenever
  `D3DRS_POINTSPRITEENABLE` was set, with `COORD_REPLACE` on every unit, and
  Mesa 26 (radeonsi and llvmpipe) then drops those coordinate sets from
  fixed-function vertex processing for every primitive, not only points, so
  fixed-function triangles and lines sample one texel (ARB vertex programs
  are unaffected). The Halo: Combat Evolved PC trial (D3D9) enables sprites at
  device creation and never disables them; its vertex-shader scene, bloom and
  interface render into an 800x600 texture that one pretransformed
  fixed-function fan copies to the back buffer, so every frame was a single
  colour (the scene's corner texel), also with `-useff` and before Wine 0049.
  0058 enables the GL state only for point lists and for triangles in point
  fill mode, re-evaluated on primitive changes to or from points and on fill
  mode changes; sprites off send nothing new. The source test drives the
  prepared handlers through a model of Wine's invalidation and apply loop
  (200000 random operations against the rule). The installed D3D8/9 probe
  (`tools/d3d/point-sprite-probe.inc`) draws Halo's 28-byte pretransformed
  indexed fan, a 16-pixel point and an untransformed strip with sprites on
  and off and compares every pixel; it checks the sprite itself only when
  dgpugl.dll reports `GL_ARB_point_sprite`. It has not yet run on a guest, and
  Halo itself remains to be rerun. Native OpenGL guests that leave sprites
  enabled while drawing textured fixed-function primitives still meet Mesa's
  behaviour.
- Device reset without a client area (frontend and Wine patch 0059). The
  Warcraft III demo (D3D8, Win2000) died with a NULL dereference in
  `wined3d_device_reset` -> `delete_opengl_contexts` after it lost focus:
  dgpugl.dll created no context on a window without a client area, so the
  reset that followed focus returning to the still-minimized fullscreen
  window failed after destroying every context, and its retry found none.
  WineD3D's hidden backup window (empty client area) failed the same way
  (Halo logs, last error 0x6). The frontend now accepts any window DC of the
  process: a context keeps its drawable without a client area, a new one gets
  a 1x1 placeholder that the first window with a client area replaces, and
  nothing is presented meanwhile. 0059 fails a reset cleanly with
  `D3DERR_DEVICELOST` when its context cannot be made current, keeps a device
  whose primary context could not be created again lost without any context
  until a later reset creates it, and lets teardown, uninit, unloads and the
  game's scene loop run without a context. Source tests execute the frontend
  and the prepared functions; the installed D3D8/9 probe
  (`tools/d3d/reset-probe.inc`: visible, minimized twice, restored without a
  reset, hidden/shown and final resets, four exact frames) has not yet run on
  a guest, and Warcraft III remains to be rerun.
- Fullscreen device window after focus changes (Wine patch 0060). Splinter
  Cell (D3D8, 1024x768 on a window created at 640x480) stayed minimized after
  returning to it, waiting for a restore before its Reset: WineD3D minimizes
  the fullscreen window on focus loss but did not restore it on activation,
  and it sees only sent messages (the taskbar's `SC_RESTORE` is posted). Once
  restored, the game sized its window back to 640x480 from WM_SIZE, nested in
  WineD3D's own `SetWindowPos`; DreamGPU presents into the client area, so the
  game showed in a corner of the mode. 0060 restores the iconic window on
  activation (`SW_SHOWNOACTIVATE`) and, while a D3D8/9 swapchain is
  fullscreen and the application active, subclasses the device window and
  clamps its `WM_WINDOWPOSCHANGING` to 0,0 and the mode size when a request
  would leave part of the mode uncovered (bounded per present; resets update
  the size before their mode change; DirectDraw and NOWINDOWCHANGES devices
  unchanged). A clamp in WineD3D's `WH_CALLWNDPROC` hook, as first written,
  had no effect on Windows 2000 (the installed probe's window stayed
  320x240): Windows gives hooks a copy. The procedure is removed when keeping
  stops, or left as a pass-through when another module subclassed later.
  On Windows XP and 98 the probe then showed focus loss ignored altogether:
  WineD3D's activation hack passed `sizeof(struct wined3d_device)` (about 50
  KiB) as `VirtualQuery`'s buffer length, which Windows validates and which
  ran past a shallow stack's base, so the query failed; 0060 passes the
  buffer's size (Windows 2000 had passed). The installed D3D8/9 probe
  (`tools/d3d/reset-probe.inc`, `Fullscreen`, run last: a 640x480 device on
  a 320x240 window that shrinks itself, application resizes, Reset, focus
  loss and activation) passes on Windows 2000 and remains to be rerun on XP
  and 98. Source tests execute the prepared functions against a user32
  model that discards hook-time edits; Splinter Cell remains to be rerun.
- Direct3D cursor (`IDirect3DDevice8/9::SetCursorProperties`,
  `SetCursorPosition`, `ShowCursor`). WineD3D makes a 32x32 image a Windows
  cursor (the display drivers' native cursor plane) and draws any other size,
  or a 32x32 one that `SetCursorPos` could not reach, into the back buffer at
  Present with an alpha-tested blit. The installed D3D8/9 probe
  (`tools/d3d/cursor-probe.inc`, run last on its own fullscreen 640x480
  device) reads presented frames back with `GetFrontBuffer`/
  `GetFrontBufferData`: a 16x16 cursor shown, moved and hidden, a 32x32
  Windows cursor shown and moved (`GetCursorInfo`, nothing in the frame), and
  a 32x32 cursor drawn once `ClipCursor` keeps the Windows cursor away, and
  a 16x16 cursor across the bottom-right and (hotspot 8,8) top-left corners,
  with nothing else in the frame. It has not run on a guest. WineD3D did not
  draw a cursor any part of which was beyond the back buffer
  (`wined3d_surface_blt` rejects the rectangle); Wine patch 0061 clips the
  destination and source rectangles first, as native Direct3D and later Wine
  do (source test: every pixel compared, 13 mutations rejected). Battlefield 1942 and Painkiller draw their own menu
  cursor sprites at DirectInput-delta positions clamped to the screen, so
  their vanishing cursors are an input-delta problem, not this path.
- Table fog with a vertex shader that writes oFog (`D3DRS_FOGTABLEMODE`
  LINEAR/EXP/EXP2, vs_1_x/vs_2_x). NVIDIA drivers before 61.77 and the ATI
  drivers of 2003-2004 ran the shader's oFog through the table equation with
  the application's start, end and density; the reference rasterizer and
  later drivers use the depth. Far Cry's NVIDIA path (DreamGPU reports a
  GeForce FX) writes oFog with LINEAR table fog from 0 to 1 and drew its
  terrain, water and boat in the fog colour while WineD3D used the vertex
  output z (Wine bug 25761; Prince of Persia: The Sands of Time, bug 17423).
  Wine patch 0062 keeps the oFog of such shaders as the fog coordinate,
  unclamped; vertex fog, vs_3_0 and shaders without oFog are unchanged.
  Without oFog native table fog uses W under a perspective projection matrix,
  which WineD3D does not (known divergence). Source test: the prepared
  epilogue interpreted with the prepared fog source (7 mutations rejected).
  The installed D3D8/9 probe (`tools/d3d/table-fog-probe.inc`, after the
  vertex shader stage) requires oFog 0 clear, 1 fogged, 0.25 three quarters
  clear and 2 of 4 half fogged under table fog, and oFog 1 clear and 0 fogged
  under vertex fog; it has not run on a guest.
- NPOT textures sampled by fixed-function stages with vertex shader
  coordinates. Without ARB_texture_non_power_of_two WineD3D creates NPOT
  textures (an 800x600 render-target texture, for one) as rectangles, which
  take texel coordinates, and scales the application's normalized coordinates
  with the texture matrix; transform_texture() skips the texture matrix under
  a vertex shader. Thief: Deadly Shadows draws its 800x600 scene texture to
  the back buffer with a vertex shader and fixed-function stage 0, so every
  pixel showed texel (0, 0): a uniform near-black frame with only the HUD.
  Wine patch 0064 scales such a stage's coordinate in the fixed-function
  fragment program by the texture's pow2_matrix (a program local per stage)
  for plain, projected and bump-mapped lookups; fixed-function vertices and
  pixel shaders (np2fixup) are unchanged. Source test: the prepared programs
  and loader interpreted (10 mutations rejected). The installed D3D8/9 NPOT
  probe (`tools/d3d/npot-probe.inc`) draws the 640x480 texture through vs_1_1
  into stage 0 and requires every texel; it has not run on a guest.
- Buffer object creation without GL error round trips. A glGetError waits
  for the host to execute everything queued before it, and WineD3D asked four
  times per buffer object it created. Thief: Deadly Shadows creates two small
  buffers every frame after its scene, which made the guest wait for the
  host's frame once per frame. Wine patch 0065 drops the queries; a zero
  buffer name still takes the non-VBO fallback and glBufferData errors stay
  pending as on a native driver. Source test: the prepared `buffer.c` has no
  glGetError calls.
- WineD3D context acquisition cost. Every D3D operation acquires and releases
  WineD3D's context, and with the native context owner released after each
  one (for cross-thread handoff), every acquisition activated the context
  again and asked `wglGetPixelFormat` for its window DC; DreamGPU's frontend
  answers that with `WindowFromDC`, an NT system call (Far Cry spends about
  17% of guest time in the NT kernel). Wine patch 0063 keeps the format of
  WineD3D's own GetDC window DC once it was verified or set, as Wine already
  did for private DCs: WGL sets a window's format once and the held DC stays
  with its window. It is verified again after a window change, a failed
  binding or a reset; a class DC, another format already on the window or
  `WGL_WINE_pixel_format_passthrough` (which restores the application's format
  after each operation) keep the per-acquisition check. Source test: the
  prepared acquire/release path against a window/DC model makes no query in
  100 operations (9 mutations rejected); the guest profile remains to be
  measured.
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
defect; do not "fix" them with blanket invalidation. Evidence:
`target/golf-coherence-linux/verification.json`. On the m28 package (framebuffer
objects, BGRA vertex arrays; disc 1 inserted through QMP) a Quick Start round
completes a shot: three clicks on the golfer play the animated swing, the
result panel reports the ball in play, and after Continue the stroke counter
shows E (1) with the full toolbar drawn.

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
correct colours, and the computer player completes its first shot. With m27
(framebuffer objects, its depth Blt on the GPU) it plays hole 1 to the green
and continues on hole 2.

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
  On the m28 package (framebuffer objects, BGRA vertex arrays) its demo runs
  with correct colours and lighting. A logging `wined3d.dll` built from m28's
  Wine sources (app-local, `WINEDEBUG=trace+d3d_perf`, about a minute of play)
  logs no "manual fog coordinates" emulation (21979 before) and no
  drawStridedSlow ("slow vertex array code") at all: every draw uses arrays.
  It does log 258 overlapping blits within one surface taking the CPU path
  (Wine 0045 under ORM_FBO).
- **Diablo II 1.12** on m28 (default video mode, Win2000): menu, hero select
  and the Rogue Encampment render with HUD, lighting and NPCs.
- **SimCity 3000** (DirectDraw, Win2000): reaches its menu and renders a
  starter town's city view, UI and ticker after Wine patch 0032. The same holds
  on the m28 package, whose DirectDraw fills and blits go through framebuffer
  objects.
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
  quitting from the console restores the 1024×768 desktop. With
  `GameRenderDevice=D3DDrv.D3DRenderDevice` on the m28 package (framebuffer
  objects, BGRA vertex arrays) its Direct3D renderer draws CityIntro on Win98
  with lit buildings, billboards, coronas, smoke and sky. A stale application-local `glide2x.dll` in the
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
  again after upgrading the fixture from m16 to the m23 package. With the m27
  package (framebuffer objects) Fill Rate (single and multi), High Polygon Count
  (1 and 8 lights), Environment Bump Mapping and Point Sprites (157.3
  MSprites/s) run; the test sequence then faults loading DOT3 Bump Mapping in
  3DMark's `P_Material::~P_Material`, on a garbage pointer from its own
  `P_VertexBuffer` code (Dr. Watson), as the scene tests do.
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
