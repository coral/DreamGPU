# Guest OpenGL frontend

This directory contains the runtime `dgpugl.dll` implementation. Build support is
in `support/guest/cmake/opengl.cmake`; the public probe source is in `tools/opengl/`.
The builder produces the frontend, the `dgpuicd.dll` Windows ICD and diagnostic
programs. The ICD implements all 336 GL1.1 dispatch slots, including context/thread
ownership, display lists, evaluators, selection, bounded texture/pixel transfers
and typed queries. It reports `1.1 DreamGPU`. `icd-coverage.json` records the four
known native-provider edge cases; dispatch coverage is not a formal conformance
certification.

The system installer registers the ICD with the DreamGPU display driver.
Applications continue loading the operating system's `opengl32.dll`; the
installer owns the DreamGPU frontend and ICD in the system directory.

Build on a host with the i686 MinGW compiler:

```
DREAMGPU_BUILD=guest cargo build --release
```

The build records input hashes, PE imports and outputs. The frontend and installed
NT driver must use the same `dg-escape.h` version. `support/guest/opengl/generate.py` is an optional development generator for the
checked-in implemented scalar exports; ordinary builds do not invoke it, and all pointer arguments are copied before entering the
immutable command channel. Drawing batches are bounded by the driver contract.

## Private frontend diagnostics and historical evidence

The following private-library probe is a developer diagnostic. Normal programs
use the installed system ICD; they do not need this deployment step.
Place the DLL beside the diagnostic EXE in a disposable guest and run the EXE.
The diagnostic dynamically loads the DLL and uses public WGL/OpenGL entrypoints;
it never calls the private transport itself. It checks two contexts/windows,
thread ownership rejection and transfer, and 18 presents with 300 quads per
frame, crossing the command limit inside Begin/End. Expected pixels: the left
pane at64,64 has red and blue vertical halves; the right pane at416,64 has yellow
and cyan halves. F1 redraws, F2 destroys the second HWND before deleting its
context. F3 uploads a padded 256-square RGBA texture and a white 16-square
subimage, overwrites both caller buffers immediately, checks typed public state
queries and draws the retained texture. F4 redraws without uploading. Escape
closes both. The log is `C:\DGWGL.LOG`; host GPU captures must
verify the displayed pixels and driver counters must verify cleanup.

The v4 frontend with the v16b NT driver passed these stages on both macOS and
Linux. `scripts/diagnostics/wgl-check.py --textured` checks every pane pixel, including
orientation and the partial update. Its only sampling allowance is the adjacent
texel at an exact integer nearest-filter boundary; arbitrary color differences
or shifts away from that boundary fail. Both hosts completed 14 textured redraws
and clean client teardown; the final seven redraws added no desktop returns or
coherence operations. These are correctness checks, not game FPS results.

The evolving front-buffer slice adds real FRONT/BACK selection, explicit bounded
RGB/RGBA pixel reads with caller packing, and common legacy vector/color entrypoints.
F6 checks a red front buffer published by Flush while the back stays green; F7
checks that SwapBuffers exchanges them. The host must capture each stage.
F5 is a bounded five-second textured-quad throughput check, with uploads and
state queries outside its timed loop. Its result is not game FPS.

Client arrays, typed queries, sharing and all GL1.1 dispatch entries are now
implemented. The measured native-provider edge cases and unsupported
`DrvCopyContext` remain explicit. Native transport/renderer tests alone do not
establish game compatibility.

Retail Half-Life 1.0 (build 742) now reaches and renders its opening tram scene
through this frontend on Windows 2000 on macOS and Linux. Those historical runs
used the game's custom mini-driver option. The maintained normal launcher now
selects the retail engine's `default` OpenGL driver, which loads system
`opengl32.dll`; it does not pass a custom `-gldrv` argument. The
original CUE/BIN disc supplies the game's track metadata. The first real-game
failure was its request for 32 depth bits: pixel-format selection now returns
the closest available RGBA8/D24S8 format and describes its actual sizes.
`SetPixelFormat` selects by index, even when passed the original request.
These follow the documented [ChoosePixelFormat](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-choosepixelformat)
and [SetPixelFormat](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setpixelformat)
contracts. The public probe exercises that request too.

Scalar packet construction writes each header word once and copies scalar bit
patterns in word-sized units. Generated argument copies use fixed-size compiler
builtins and restrict these packing-only i686 wrappers to integer registers.
This avoids a byte-loop `memcpy` for every float and preserves x87 exception
flags while copying subnormals and signaling NaNs. Actual arithmetic elsewhere
in the frontend keeps its normal floating-point code generation.
The wire record layout is unchanged. The frontend now honors the negotiated
record and byte limits, up to 1,024 records and the existing 64 KiB NT packet.
An older host or driver advertising 256 records still bounds every submission
to 256. Queries, flushes, swaps and resource transitions retain their ordering.

The packing translation unit uses `-O2` with integer-only i686 code generation
and inlines record construction into constant-specialized scalar entrypoints.
The common vertex path writes arguments directly from the caller's stack into
the packet, with one TLS lookup and no generic dispatch or temporary array.
Begin/End reuse their existing context lookup. Pure vector aliases submit the
original vector bits directly; byte-color conversion remains arithmetic.
Actual-source tests poison reused packet storage and check headers, scalar
bits and DATA padding. `tests/test_packing.py` executes the generated wrappers
as freestanding i686 code on Linux with GCC and Clang, checking exact argument
bits and x87 status without a Windows guest. Public pixel checks and matched
real-game performance measurements cover each frozen frontend separately.

Known configurations, FPS, limitations and trace evidence are recorded in
`benchmarks/retro-gpu/run.json` (`benchmarks/retro-gpu/run.json`, historical Juke evidence).
Use those historical runs instead of repeatedly restoring old builds. Profile
the accelerated path, combine justified fixes, pass the focused correctness
gates, and then benchmark the combined candidate. Game throughput measurements
are separate from host presentation timing and input-to-photon latency.

Pack/unpack state, including swap-byte and bit-order flags, belongs to the frontend context and is answered locally by all typed state-query entrypoints. Supported packed texture uploads and multibyte readbacks honor byte swapping; byte components are unaffected. Stencil bitmap readback honors bit ordering and preserves surrounding destination bits. These queries do not flush the guest stream or cross the driver boundary.

Texture names are tracked in a bounded shared hash table in the frontend.
`glGenTextures` reserves names locally; `glIsTexture` becomes true only after a
valid first bind. Manual names, shared contexts, deletion/recreation and target
mismatches use that same namespace. This removes the synchronous host query
previously issued for every generated name, without reserving a separate magic
ID range or relaxing native resource bounds.

Bulk readback is negotiated through `DG_CAP_GL_BULK_READBACK` and the driver's
`MaxResultBytes` reply. Current drivers expose up to 64KiB; existing 512-byte
snapshots remain compatible. Scalar queries keep their small stack buffer. Large
query replies and packing scratch are allocated lazily per context and freed with
that context. NT uses contiguous nonpaged DMA memory; Win98 uses sixteen fixed
contiguous pages. Neither puts a 64KiB buffer on a kernel stack.

`glReadPixels` uses bounded two-dimensional tiles, including clipping and caller
pack state. `glGetTexImage` carries the requested pixel count in the level
argument's upper sixteen bits; zero retains the original 128-pixel result. Native
validation checks the count, destination RAM extent and accepted result capacity
before submission. Large native results use heap ownership through completion;
ordinary draw batches retain their existing small storage.

Every 64KiB chunk is a synchronous round trip, and `glGetTexImage` reads the
whole level (after `GetTexLevelParameteriv` WIDTH, HEIGHT and, for depth-capable
targets, DEPTH_SIZE). An 800x600 RGBA8 level is 29 chunks of 16384 pixels
(level word `0x40000000`) and a 4864-pixel tail (`0x13000000`). Wine patch
0056 therefore reads a locked rectangle of a render target with one
`glReadPixels` of that rectangle from a read framebuffer instead of
downloading the whole surface (Unreal Tournament 2003 locks one backbuffer
pixel per frame).

Color readback supports RGB/BGR, RGBA/BGRA, individual channels, alpha,
luminance and luminance-alpha. The output types include signed/unsigned 8-, 16-
and 32-bit scalars, floats, RGB332/233, RGB565, RGBA4444/5551/8888/1010102,
and their reversed packed layouts. Framebuffer luminance combines the color
channels; texture luminance follows texture-query semantics. Invalid packed
format/type combinations fail before writing the caller's memory.

`DG_CAP_GL_DEPTH_STENCIL_READBACK`, negotiated through the owner-checked
`DG_ESCAPE_CAPABILITIES` operation, adds native depth-float, stencil-integer,
and full-precision RGBA-float replies. Older drivers and hosts cannot opt in
accidentally. Wide color and 1D texture reads preserve native precision before
conversion; normal 2D byte/packed16 reads retain their existing compact path.
Stencil supports scalar and bitmap destinations. Color-index readback is invalid
for the RGBA drawables provided here. These additions do not advertise unsupported
texture targets, compressed texture queries, or modern integer/half-float APIs.

The Links 2003 regression uses RGB565 readback to save and restore menu backgrounds.
Both framebuffer and texture readback must support that format: silently leaving
the destination untouched makes Wine publish black backing surfaces as valid.
`tests/guest/opengl/test_legacy.py` covers all 65,536 RGB565 values and packed
readback guards; the packaged OpenGL probe also exercises actual guest-to-native
format roundtrips, depth, and stencil.

The actual-source packing oracle reads 1024×1024 RGBA texture data in 64 result
requests instead of 8,192, and 640×480 framebuffer data in 20 instead of 2,400.
These are transport request counts, not a game-FPS claim. Native GPU tests verify
64KiB exact pixels, legacy queries, short tails, undersized destinations and guard
bytes. The public `-arrays` probe also checks 16,384 exact texture pixels through
the installed user-mode frontend and guest driver.

### Secondary color and the single-pass Glide path

The frontend probes the native canonical `glSecondaryColor3f` signature through
its existing driver metadata query. Only a host returning the full three-word
contract enables `GL_EXT_secondary_color` and its required
`GL_EXT_separate_specular_color` dependency. All17 secondary-color entrypoints
are exported, including integer normalization, float bit-preserving scalar and
vector calls, and guest-local array descriptors. `glLightModeliv` joins the
existing scalar/float-vector lighting setters. The reported base GL version is 1.1; these extensions are advertised only when
the host provides their required contract.

Packed draws preserve the existing64-byte vertex format. A secondary array adds
bit16 to the attribute mask and uses80-byte vertices: RGB floats at byte64 and
zero padding at76. Both driver packet bounds and native attribute/data validation
remain in force; no guest pointer enters the host stream. Native drawing restores
current secondary state after array calls. Queries return all four RGBA values,
with the specification's zero secondary alpha even on hosts returning one.
COLOR_SUM restores explicitly for fog/enable attribute groups to avoid a host
compatibility-driver difference. The existing Glide translator discovers the
extension and removes its second additive geometry pass automatically.

Source contracts follow the Khronos
[secondary-color specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_secondary_color.txt)
and [separate-specular specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_separate_specular_color.txt).
Focused actual-source sanitizer tests cover negotiation, unsupported-host errors,
array bounds/topology/normalization and four typed query guards. The optimized
i686 test checks special float bit patterns and live x87 state with GCC and Clang.
The diskless native `qemu_secondary_color_modulates_then_adds_preserving_alpha_and_context_state`
test covers post-texture addition, alpha, separate lighting, all three index
widths,64/80-byte draws, context/attribute restoration and DMA canaries on both
hosts. The public `dgwgl -arrays` probe adds192 exact pixels through the actual
Windows exports; see the current run manifest for runtime acceptance.

### Extension string order

`GL_EXTENSIONS` names only negotiated contracts, each name under its own
contract's gate, ordered by age rather than by contract. Games of 1997-2001
copy the string into fixed buffers and search only that copy (Quake III 1.11:
`Q_strncpyz` into 1024 bytes), so the names they probe come first:
`GL_ARB_multitexture`, `GL_EXT_compiled_vertex_array`, the texture
environment add/combine/dot3 names, anisotropy, edge clamp, point parameters,
`GL_ARB_texture_compression`/S3TC, secondary colour and fog coordinates, then
cube maps and the other fixed-function names of that era, then 3D textures,
texture control, and last the program, buffer object, framebuffer object,
BGRA, query and rectangle extensions that later titles find anywhere in the
string, then the names of the second capability word (depth textures). With
every contract negotiated the string is 1174 characters and the legacy names
end by character 461; `tests/guest/opengl/extension-names.h` is the suites'
model and `test_compiled_vertex_array.py` checks that prefix.

### ARB_multitexture

`DG_CAP_GL_MULTITEXTURE` enables `GL_ARB_multitexture` (eight units),
`GL_ARB_texture_env_combine`, `GL_EXT_texture_env_combine` and
`GL_ARB_texture_env_dot3`. The reported version stays `1.1 DreamGPU`, so callers
do not assume promoted core features. Without the capability the extension
string, submitted records and `wglGetProcAddress` results are unchanged; the new
exports fail locally with `GL_INVALID_OPERATION`.

`ActiveTexture` and every core or ARB `MultiTexCoord` variant are ordinary
scalar records; all coordinate variants submit one `MultiTexCoord4f` record
(target, then s,t,r,q float bits). Unit enums are checked before submission.
`ClientActiveTexture` is guest-only state that routes texture-coordinate
pointers, enables, queries and interleaved arrays; the client attribute stack
saves every unit. Draws keep the seven-descriptor `DrawArrays` record unless a
unit 1-7 array is enabled; those use `DrawArraysEXT` with sixteen descriptors.
Each `ActiveTexture` forgets the cached texture enable and coordinate-generation
bits, which belong to a unit. `tests/guest/opengl/test_multitexture.py` covers
the frontend contract and the i686 float wrappers.

### Fixed-function state

`DG_CAP_GL_FIXED_STATE` adds blend color, min/max/subtract and separate
equations, separate blend factors, wrapping and separate stencil, border/mirror/
edge clamping, anisotropic filtering, LOD bias, point parameters and sprites, and
the `ADD` environment, each advertised under its EXT/ARB/ATI/SGIS name.
`GL_EXTENSIONS` is assembled once per client open into one static buffer, in the
order multitexture, secondary color, fixed state; without the new capability it
equals the earlier strings. Without negotiation the new exports fail locally
with `GL_INVALID_OPERATION`, are hidden from `wglGetProcAddress`, and the new
vector parameters remain `GL_INVALID_ENUM`.

Core, EXT, ARB and ATI names submit one core scalar record, except two cases.
`glStencilFuncSeparateATI` checks both functions, then sends FRONT and BACK
`StencilFuncSeparate` records. `glPointParameterfv` sends one DATA record with
the pname word and one or three floats. None of them is legal inside Begin/End.
`BlendFuncSeparate`, and any `BlendFunc` whose factors the cache does not model
(including constant colors), make the cached blend factors unknown.
`tests/guest/opengl/test_fixed_state.py` covers the frontend contract.

Any window DC of the process takes a context, as with a native ICD, including
a minimized window or a hidden one without a client area (WineD3D recreates
its contexts in `IDirect3DDevice8/9::Reset`, also while a fullscreen game is
still minimized, and falls back to a hidden 10x10 decorated backup window
when the game window's DC fails). `wglCreateContext` no longer requires a
client area. While the current window has none, `wglMakeCurrent` keeps the
context's drawable, whichever window it was made for, and drops its window
binding; a context without a drawable gets a 1x1 placeholder. The first
window with a client area, at `wglMakeCurrent` or `wglSwapBuffers`, replaces a
placeholder or another window's drawable (contents undefined, as after a
resize) and binds again; returning to the window the drawable was made for
keeps it. Nothing is presented without a client area. A context first made
current without a binding takes the presentation capabilities of the last
binding (they belong to the display driver); before any binding, front-buffer
selection reports `GL_INVALID_OPERATION` until one succeeds. Previously each
of these failed with `ERROR_INVALID_HANDLE`, which Wine's reset and backup
fallback could not survive (Wine patch 0059 handles any remaining failure).
`tests/guest/opengl/test_frontend.py` covers fresh, shared and backup-window
contexts and placeholder failure.

Point sprites are forwarded unchanged. Mesa 26 (radeonsi and llvmpipe) does
not limit `COORD_REPLACE` to points: while `GL_POINT_SPRITE` is enabled, its
fixed-function vertex processing drops the replaced coordinate sets for every
primitive, so textured fixed-function triangles and lines sample one texel
(ARB vertex programs are unaffected). WineD3D therefore enables the state only
while it draws points (patch 0058); an OpenGL application that leaves it
enabled across textured fixed-function triangles sees Mesa's result.

Current build and evidence navigation: [guest runtime](../README.md),
[guest tools](../../tools/README.md), and [execution evidence](../../docs/progress.md).
Historical benchmark paths above belong to the original Juke evidence tree.

### Texture control, compression, fog coordinates and cube maps

`DG_CAP_GL_TEXTURE_CONTROL` adds `GL_ATI_texture_env_combine3`,
`GL_ATI_texture_mirror_once` and `GL_SGIS_generate_mipmap`; the host charges
generated levels with the image that produces them. Name-sized scalars
(`TexParameter`, `TexEnv`, `Hint`, `Fog`) are validated locally against the same
tables as their vector forms, so an unknown name is `GL_INVALID_ENUM` instead of
a record that fails the batch and the context.

`DG_CAP_GL_COMPRESSED_TEXTURES` adds `GL_ARB_texture_compression` and
`GL_EXT_texture_compression_s3tc`: exact-size `CompressedTexImage2D` (or none
for zero-defined storage), block-row `CompressedTexSubImage2D` bands, chunked
`GetCompressedTexImage`, and S3TC/generic compressed internal formats for
`TexImage2D`. Every split 2D upload starts its tiles on 4-pixel boundaries of
its rectangle and gathers padded rows, so uncompressed sources into compressed
storage stay block aligned.

`DG_CAP_GL_FOG_COORD` adds `GL_EXT_fog_coord`: `FogCoord*` inside Begin/End and
lists, `FOG_COORDINATE_SOURCE`, and a fog coordinate client array carried in
DrawArraysEXT slot 14. Without it Wine could not fog D3D's pretransformed
vertices after texturing.

`DG_CAP_GL_TEXTURE_CUBE_MAP` adds `GL_ARB_texture_cube_map` and
`GL_EXT_texture_cube_map`: a third binding per unit, square face uploads, copies,
reads and compressed faces, `PROXY_TEXTURE_CUBE_MAP`, `TEXTURE_WRAP_R`,
`NORMAL_MAP`/`REFLECTION_MAP` texture generation, `TEXTURE_BINDING_CUBE_MAP` and
`MAX_CUBE_MAP_TEXTURE_SIZE`. The host tracks dimensions, charges and undefined
levels per face.

### ARB_fragment_program

`DG_CAP_GL_FRAGMENT_PROGRAM` adds `GL_ARB_fragment_program`, last in
`GL_EXTENSIONS`, with nineteen ARB-named exports: Gen/Delete/Bind/IsProgram,
ProgramString, the four Env and Local parameter variants each and their f/d
queries, GetProgramiv and GetProgramString. Unless vertex programs are
negotiated too, only `FRAGMENT_PROGRAM_ARB` exists: the vertex program target
(which WineD3D's ARB backend still drives for applications that create vertex
shaders) is a local `GL_INVALID_ENUM`, as are
an unknown format, the address-register GetProgramiv names and a GetProgramString
pname other than `PROGRAM_STRING_ARB`; negative counts and lengths are
`GL_INVALID_VALUE`. Bind, ProgramString and the parameters are compiled into
display lists (errors deferred); Gen, Delete, Is and every query run at once.
Names, objects and parse results belong to the host, so GenPrograms (in batches of
`DG_GL_PROGRAM_NAME_BATCH`) and IsProgram are host queries. Float parameters are
copied as bits; double variants convert to the float record and the double
queries widen the float result. A string longer than the protocol bound or the
negotiated packet travels without its bytes and fails on the host with
`INVALID_OPERATION` at error position `len`. `PROGRAM_ERROR_STRING_ARB` is a host
query into per-context storage that stays valid for the context. Without the
capability the exports fail locally with `GL_INVALID_OPERATION`, are hidden from
`wglGetProcAddress`, and the new Get/IsEnabled names stay local
`GL_INVALID_ENUM`; Enable and `MatrixMode(MATRIXi_ARB)` pass through as before.
`tests/guest/opengl/test_fragment_program.py` covers the contract and the i686
parameter wrappers.

### ARB_vertex_program

`DG_CAP_GL_VERTEX_PROGRAM` adds `GL_ARB_vertex_program`, after the fragment
extension, and shares its program exports (exported once with either
capability). `VERTEX_PROGRAM_ARB` becomes a program target, and
`VERTEX_PROGRAM_POINT_SIZE_ARB`, `VERTEX_PROGRAM_TWO_SIDE_ARB`, the
address-register GetProgramiv names and MAX_VERTEX_ATTRIBS_ARB (16) become
valid. Forty-three more ARB-named exports cover the 36 `VertexAttrib*ARB`
variants, `VertexAttribPointerARB`, Enable/DisableVertexAttribArrayARB,
`GetVertexAttrib{d,f,i}vARB` and `GetVertexAttribPointervARB`. Every immediate
variant becomes one bit-exact `VertexAttrib4fARB` record (integer `N` forms per
GL 1.x Table 2.6, doubles narrowed), legal inside Begin/End and compiled into
display lists; attribute 0 then provokes a vertex. Indices from 16 are
`GL_INVALID_VALUE`. The generic array state lives in the frontend with the
other client arrays (saved by `CLIENT_VERTEX_ARRAY_BIT`) and is answered
locally, except the current value of attributes 1-15, a host query (attribute
0 is `GL_INVALID_OPERATION`, as the extension specifies). An array draw with
an enabled generic array uses the attribute draw record (function
`VertexAttribPointerARB`, 16 conventional and 16 generic descriptors), and
ArrayElement inside Begin/End submits the generic values as attribute records.
Attribute 0 is the position: its array draws without a vertex array. Without
the capability the new exports fail locally with `GL_INVALID_OPERATION`, are
hidden from `wglGetProcAddress`, and the vertex program modes stay local
`GL_INVALID_ENUM`. `tests/guest/opengl/test_vertex_program.py` covers the
contract and the i686 wrappers.

`DG_CAP_GL_TEXTURE_3D` adds `GL_EXT_texture3D`, in the extension string,
and six exports under core and EXT names: `TexImage3D`, `TexSubImage3D` (by
hand, `texture.cpp`) and `CopyTexSubImage3D` (generated scalars). `TEXTURE_3D`
becomes a binding, enable and parameter target, `PROXY_TEXTURE_3D` an
immediate proxy, `TEXTURE_WRAP_R` a parameter of every target, and the
`UNPACK/PACK_IMAGE_HEIGHT` and `SKIP_IMAGES` pixel storage client state
(saved with the rest of `CLIENT_PIXEL_STORE_BIT`). Images are gathered from the
caller's layout into tight rows then images in the 2D wire forms (packed types
expand to RGBA8, swapped components are swapped), one TexImage3D record when
the volume fits, otherwise a payload-free image followed by TexSubImage3D slabs
of whole images or 2D tiles of one image. A split subimage reads the level's
three sizes first; while compiling a list it is `GL_OUT_OF_MEMORY`. Everything
the host would refuse (target, level, sizes above `DG_GL_MAX_3D_TEXTURE_SIZE >>
level`, compressed or unknown internal formats, `GL_BITMAP`, a border) is a
local error. `GetTexImage` of a level reads its depth too and places images
through the pack image state. Without the capability the exports fail locally
with `GL_INVALID_OPERATION`, are hidden from `wglGetProcAddress`, and every new
enum stays the local `GL_INVALID_ENUM` it was. `tests/guest/opengl/test_texture_3d.py`
covers the contract.

### ARB_vertex_buffer_object

`DG_CAP_GL_VERTEX_BUFFER_OBJECT` adds `GL_ARB_vertex_buffer_object`, in
the extension string, and eleven ARB-named exports (`buffer.cpp`; no core
names, which WineD3D then maps to the ARB ones). Buffer objects live in a
per-share-group table beside the texture names (`buffers.h`), each with a
16-byte-aligned copy of its contents. Gen, Bind, Is, the parameter and
pointer queries, `GetBufferSubData` and `MapBuffer` never reach the host;
`BufferData` sends its contents in one record when they fit, otherwise
zero-defined storage and BufferSubData slabs, `UnmapBuffer` re-sends the whole
copy unless the map was READ_ONLY, and `DeleteBuffers` sends the names of
existing objects in bounded batches and resets this context's bindings. Every
pointer call latches the ARRAY_BUFFER binding (name and object identity) with
its offset; `CLIENT_VERTEX_ARRAY_BIT` saves both bindings. A draw with any
buffer-sourced array or an element buffer uses the buffer draw record
(function `DrawRangeElements`); client arrays in it send only the vertex range
their indices reach (scanned locally), buffer-only indexed draws are sent
without a scan and bounded by the host. Inside a display list, and for what
the record cannot express, the arrays resolve into the buffer copies and draw
as ordinary client arrays, as does ArrayElement. Missing, deleted or mapped
buffers and element ranges beyond their buffer are local
`GL_INVALID_OPERATION` where the frontend checks. An array whose buffer does
not hold every vertex read (empty, or offset, stride or index beyond its end)
is never read, as by the host's buffer draw: wherever the frontend bounds the
vertices (array draws, scanned indices, display lists, ArrayElement) it leaves
that array out, so its current value applies, and without a position or
generic attribute 0 left nothing is drawn, without an error (Doom 3's ARB2
path keeps a texture coordinate array enabled on a small, stale buffer).
Without the capability the exports fail locally with `GL_INVALID_OPERATION`,
are hidden from `wglGetProcAddress`, and the binding queries stay host queries.
`tests/guest/opengl/test_vertex_buffer_object.py` covers the contract.

Draws cost work per enabled array, not per descriptor slot: the array state
keeps its enabled flags as a slot mask (`EnabledArrays`), and the draw, its
buffer lookups, references, descriptors, client sections and layout visit only
its set bits. The frontend's buffer draw layout (`BufferLayout`) is the shared
`DgRawBufferLayoutOf` admission restricted to those slots; the test compares
the two over random records. Index bounds are scanned at most once per draw.
WineD3D's release and reacquisition around each D3D call write the context
owner with plain stores under the frontend lock (no locked exchange), and its
program parameters and generic attributes take the constant scalar paths.

### Indexed client-array draws and EXT_compiled_vertex_array

With the same capability, `DrawElements` of client arrays alone (`arrays.cpp`,
`RangedDraw`) is also a buffer draw, with no buffer slots: the indices are
scanned for their lowest and highest values, every enabled array's vertices of
that range travel once at their wire type (BGRA reordered, signed-normalized
colours and normals as floats, as in the expansion), then the indices inline.
It is chosen when it is no larger than the per-index expansion and fits one
packet; sparse ranges, ranges beyond `DG_GL_MAX_VERTICES`, more than
`DG_GL_MAX_INDICES` indices, display list compilation and every error keep the
expansion. `GL_EXT_compiled_vertex_array` is second in the extension string,
after `GL_ARB_multitexture` (see below); `glLockArraysEXT`/`glUnlockArraysEXT` (EXT names only) and
`ARRAY_ELEMENT_LOCK_FIRST/COUNT_EXT` are client state saved by
`CLIENT_VERTEX_ARRAY_BIT`, never recorded or compiled, and a locked draw
submits the unlocked draw's records. Quake III-engine renderers choose
DrawElements over strips of ArrayElement once they see the extension.
ArrayElement still submits one record per enabled attribute and the vertex,
unchanged, but builds them without per-component type dispatch or library
copies, and `JglScalarVector` dispatches the per-vertex functions to their
constant forms. `tests/guest/opengl/test_compiled_vertex_array.py` covers the
contract and prints the records, bytes and frontend time of a Quake III-like
batch along each path.

### EXT_framebuffer_object

`DG_CAP_GL_FRAMEBUFFER_OBJECT` adds `GL_EXT_framebuffer_object`,
`GL_EXT_framebuffer_blit` and `GL_EXT_packed_depth_stencil`, in the
extension string, and eighteen EXT-named exports (`framebuffer.cpp`; records use
the core function numbers). Framebuffer names live in a table of the context,
renderbuffer names in one of the share group; Gen and Is never reach the host,
an object exists from its first bind, which sends the bind record, and
`Delete*` sends the names of objects only, in bounded DATA batches, returning
this context's bindings to the window (or no renderbuffer). The draw, read and
renderbuffer bindings are answered locally; `MAX_COLOR_ATTACHMENTS` and
`MAX_RENDERBUFFER_SIZE` are host queries, as are `CheckFramebufferStatus` (zero
on failure) and both parameter queries. Every target, attachment, texture
target (cube faces and `TEXTURE_3D` once negotiated, checked even to detach),
level, zoffset, storage format and size, mipmap target, blit mask and filter
the host would refuse as malformed is a local error; what depends on state
(the window bound, missing objects, a texture's target) is the host's GL
error. Everything executes immediately, with immediate errors, even while
compiling a display list, except `BlitFramebuffer`, which compiles (a zero
mask sends nothing). With an object bound `DrawBuffer`/`ReadBuffer` take
`COLOR_ATTACHMENT0-3` and `NONE` without front-buffer presentation and refuse
the window's buffers (`GL_INVALID_OPERATION`), and the tracked draw buffer
stays the window's: drawing into an object, its lists and its attribute pops
never mark the window's front dirty, while a colour blit into a front window
buffer does. With an object bound for reading, `ReadPixels` tiles clip below
`DG_GL_MAX_DIMENSION` instead of the drawable, and `DEPTH_STENCIL` reads as
`UNSIGNED_INT_24_8` (with `DG_CAP_GL_DEPTH_STENCIL_READBACK`) compose each
tile's DEPTH and STENCIL replies under the pack state. `TEXTURE_2D` images
(TexImage2D/TexSubImage2D, CopyTexImage2D) take the depth internal formats
without a border and depth pixels (`DEPTH_COMPONENT` with one BYTE..FLOAT
component, `DEPTH_STENCIL` as four-byte `UNSIGNED_INT_24_8` units, swapped as
one), as do `DrawPixels` and the `CopyPixels` type `DEPTH_STENCIL`; depth levels
cannot be read back. Without the capability the exports fail locally with
`GL_INVALID_OPERATION`, are hidden from `wglGetProcAddress`, and every new enum
stays the local `GL_INVALID_ENUM` it was. `tests/guest/opengl/test_framebuffer_object.py`
covers the contract.

### ARB_vertex_array_bgra

`DG_CAP_GL_VERTEX_ARRAY_BGRA` adds `GL_ARB_vertex_array_bgra`, in the
extension string, and no export: `GL_BGRA` becomes a size of `ColorPointer`,
`SecondaryColorPointerEXT` and `VertexAttribPointerARB`. Only UNSIGNED_BYTE is
accepted (any other valid type is `GL_INVALID_OPERATION`, as is a generic BGRA
array that is not normalized); every other array keeps BGRA an invalid size
(`GL_INVALID_VALUE`), as does every array without the capability. The size
queries (`COLOR_ARRAY_SIZE`, `SECONDARY_COLOR_ARRAY_SIZE`,
`VERTEX_ATTRIB_ARRAY_SIZE`) answer `GL_BGRA`; `CLIENT_VERTEX_ARRAY_BIT` saves
it and `InterleavedArrays` defines an ordinary colour array. Client memory is
reordered while it is packed: a BGRA colour or generic attribute travels as an
RGBA section, a BGRA secondary colour as the RGB its slot admits, so client
draws, display lists and buffer arrays resolved into their copies send exactly
the records RGBA data would. ArrayElement reads B, G, R, A. A draw that reads a
BGRA array from a buffer object uses the BGRA buffer draw record (function
`DrawRangeElementsEXT`), the buffer draw with `DG_GL_ARRAY_BGRA` on those
buffer slots; a draw without one keeps `DrawRangeElements`.
`tests/guest/opengl/test_vertex_array_bgra.py` covers the contract.

### ARB_occlusion_query and NV_fence

`DG_CAP_GL_OCCLUSION_QUERY` adds `GL_ARB_occlusion_query GL_NV_fence`, in
the extension string, and the fifteen entry points of both under their
extension names (`occlusion.cpp`). Query and fence objects belong to one
context and live on the host, which owns their names, the active query and
every state-dependent error: Gen, Is and every state query are QUERY records,
deletion a DATA record executed immediately, and `BeginQueryARB`,
`EndQueryARB` and `SetFenceNV` CALL records compiled into display lists (their
local errors are deferred there). A target, condition or pname the host would
reject as malformed is a local `GL_INVALID_ENUM`. One `GetQueryObject` round
trip answers availability and the result together and carries the results of
earlier ended queries that completed meanwhile; the frontend keeps those and
fences it saw signalled (64 of each per context) and answers repeated polls
locally until the name is begun, set or deleted or a display list executes.
`GetQueryObjectivARB` clamps counts beyond `INT_MAX`; a name that is not a
fence makes `TestFenceNV` answer TRUE. Without the capability the exports fail
locally with `GL_INVALID_OPERATION` and `wglGetProcAddress` hides them.
`tests/guest/opengl/test_occlusion_query.py` covers the contract.

### ARB_texture_rectangle and NV_texture_rectangle

`DG_CAP_GL_TEXTURE_RECTANGLE` adds `GL_ARB_texture_rectangle
GL_NV_texture_rectangle`, in the extension string, and no entry points:
`TEXTURE_RECTANGLE` becomes a target of `BindTexture`, `TexParameter`,
`TexImage2D`/`TexSubImage2D`, `CopyTexImage2D`/`CopyTexSubImage2D`,
`GetTexImage`, `GetTexLevelParameter` and `FramebufferTexture2DEXT`, and
`PROXY_TEXTURE_RECTANGLE` of `TexImage2D` and `GetTexLevelParameter`, all in the
existing records. Everything the host would reject as malformed is a local GL
error: a nonzero level or a border (`GL_INVALID_VALUE`, also for readback and
level queries), a size beyond 2048, a depth internal format (`GL_INVALID_VALUE`),
a compressed internal format, the compressed records and `GetCompressedTexImage`
(`GL_INVALID_ENUM`), `GENERATE_MIPMAP` (no rectangle parameter) and
`GenerateMipmapEXT` (`GL_INVALID_ENUM`). Parameter values (wraps, filters,
`BASE_LEVEL`) and a nonzero attachment level are native GL's errors.
`TEXTURE_BINDING_RECTANGLE` and `MAX_RECTANGLE_TEXTURE_SIZE` are host answers.
Without the capability every rectangle enum keeps its `GL_INVALID_ENUM`, now
raised locally, and `Enable(TEXTURE_RECTANGLE)` still passes through.
`tests/guest/opengl/test_texture_rectangle.py` covers the contract.

### ARB_depth_texture, ARB_shadow and ARB_fragment_program_shadow

The first feature of the second capability word: `DG_CAP2_GL_DEPTH_TEXTURE`
arrives in the `DG_ESCAPE_CAPABILITIES` reply's `DeviceError` (older kernels
leave it zero) and adds `GL_ARB_depth_texture GL_ARB_shadow`, and
`GL_ARB_fragment_program_shadow` with `ARB_fragment_program`, as a GeForce FX
listed them; no entry points. Borderless depth images (`DEPTH_COMPONENT`,
`DEPTH_COMPONENT16/24/32`, `DEPTH_STENCIL`, `DEPTH24_STENCIL8`) of `TEXTURE_1D`,
`TEXTURE_2D` and `TEXTURE_RECTANGLE` travel in the existing image, subimage,
proxy and copy records, with `DEPTH_COMPONENT` pixels of every scalar type or
packed `DEPTH_STENCIL`/`UNSIGNED_INT_24_8` words (swapped as whole units).
A border is a local `GL_INVALID_VALUE`, depth pixels for a colour image (or
the reverse) `GL_INVALID_OPERATION`, and a depth format or depth pixels for a
cube map or 3D texture `GL_INVALID_OPERATION`, as the extension requires.
`TEXTURE_COMPARE_MODE`, `TEXTURE_COMPARE_FUNC` and `DEPTH_TEXTURE_MODE` are
parameters of every target in all four `TexParameter` forms; native GL takes
more values than the extensions do, so the frontend reports anything but
`NONE`/`COMPARE_R_TO_TEXTURE`, `LEQUAL`/`GEQUAL` (no `EXT_shadow_funcs`) and
`LUMINANCE`/`INTENSITY`/`ALPHA` as `GL_INVALID_ENUM` (float values truncate,
by integer arithmetic). `GetTexImage` reads a depth level as `DEPTH_COMPONENT`
in any scalar type (GL's conversion of depth in [0, 1]) or a packed level as
`DEPTH_STENCIL`: it first asks the level's `TEXTURE_DEPTH_SIZE` (and
`TEXTURE_STENCIL_SIZE`) because a host read of the wrong kind fails the
batch, and reports a mismatch, including a colour read of a depth level, as
`GL_INVALID_OPERATION`. Without the capability every new name keeps its
`GL_INVALID_ENUM`, now raised locally, and depth images stay
`EXT_packed_depth_stencil`'s `TEXTURE_2D` ones. `tests/guest/opengl/test_depth_texture.py`
covers the contract, `test_icd_textures.py` the ICD's 1D depth copies.

### RGB565 internal format, without an extension name

The second feature of the second capability word: with `DG_CAP2_GL_RGB565`
the internal format `RGB565` (0x8D62, `DG_GL_RGB565`) is a colour format of
`TexImage1D/2D/3D` and their proxies, `CopyTexImage1D/2D` (the ICD's 1D copy
too) and `RenderbufferStorage`, on every target that takes one, through the
existing records. Nothing is added to the extension string, whose bytes stay
those of the other bits: the format's extension, `GL_ARB_ES2_compatibility`,
also claims shader precision queries, `FIXED` arrays and float depth-range
entry points that a GeForce FX never had and DreamGPU does not provide.
WineD3D asks for the format directly (patch 0057: a 1x1 texture, then
`glGetError`) so that Direct3D's R5G6B5 keeps six green bits; `RGB5` stays
what native GL makes of it (five green bits on Mesa). The host stores the
format natively or, without native `RGB565`, as `RGB8`; readback of either
gives back every 5/6/5 value from the high bits of the RGBA8 wire bytes.
Without the capability `RGB565` is an unknown internal format: the local
`GL_INVALID_VALUE` of `TexImage` and `CopyTexImage` (also for `TEXTURE_2D`,
whose copy record the host would otherwise refuse) and `GL_INVALID_ENUM` of
`RenderbufferStorage`. `tests/guest/opengl/test_rgb565.py` covers the
contract, `test_icd_textures.py` the ICD's 1D copies.

### No ARB_vertex_blend

Neither provider offers `GL_ARB_vertex_blend` (or `GL_ARB_matrix_palette`):
Mesa has neither, and a GeForce FX listed neither (its OpenGL had
`EXT_vertex_weighting`; its Direct3D driver blended in its own vertex
programs). WineD3D's fixed-function vertex blending, which used only that
extension, is Wine patch 0052's CPU blend instead: positions and normals are
blended into world space and drawn as ordinary client arrays (FLOAT 4
positions, FLOAT 3 normals) under the view matrix, with `MatrixMode`,
`PushMatrix`, `LoadMatrixf` and `PopMatrix` records around the draw, so the
frontend sees nothing new and keeps no matrix state. Emulating the extension
here would have meant shadowing every modelview operation bit-exactly
(display lists, whose matrix commands apply to the matrix mode current when
they execute, included), generating object-linear texture coordinates and
blending raster positions on the CPU, and binding `vertex.weight` and
`state.matrix.modelview[n]` in host vertex programs; and applications would
see an extension the FX never had. `tests/native_qemu/vertex_blend.rs` draws
such records. Patch 0053's vertex tweening (`D3DVBF_TWEENING`) sends the same
client arrays, or the same immediate-mode `Normal3fv`/`Vertex4fv`, in object
space under Wine's own modelview, without the matrix records.

## System OpenGL provider

Cargo packages `application/dgpuicd.dll` as the normal system ICD, alongside
`dgpugl.dll`, which the Wine/OpenGLide providers use. Both share the same frontend
and host protocol. The single production NT and Win98 display drivers expose ICD
discovery; there is no separate non-ICD shipping driver or diagnostic DLL path.
Installer transaction readiness is tracked separately from API coverage.

The checked-in `icd-slots.inc` follows the pinned ReactOS 336-slot OpenGL 1.1
layout, with provenance in the file. [icd-coverage.json](icd-coverage.json) records
the current contract and retained native-provider limitations. Actual-source
sanitizer tests and native pixel tests cover numeric aliases, client arrays,
pixel transfer/streaming, textures, evaluators, selection/feedback, and display
lists. Lists preserve logical resource ownership and deferred compile-time error
semantics. Native Mesa extreme integer-transfer, fractional negative zoom, and
legacy texture-border failures remain explicit limitations; passing transport
does not erase those findings.

The NT `drivers/nt5/dgpudisp.dll` supports bounded `OPENGL_GETINFO` replies:
520 bytes for the pinned ReactOS layout or 532 bytes for the actual Windows2000
loader, UTF-16 `DGPUICD` at offset 8, interface version 2 and driver version 1.
It provides RGBA8/D24S8 pixel-format DDIs and ownership-checked swaps; GDI window
tracking is created under the `WNDOBJ_SETUP` engine lock. NT registration uses
`OpenGLDrivers\DGPUICD` with `Dll=dgpuicd.dll`, `Version=2`, `DriverVersion=1`,
and `Flags=1`.

Win98 `drivers/win98/dgpumini.drv` uses the checked donor `Control` patch and a
270-byte Win16 ANSI descriptor, verified with the actual Watcom compiler. Its
registration is the `DGPUICD` named value in the Win98 `OpenGLDrivers` key. This
is a different ABI from the NT wide-name response. The VxD remains shared with
the existing transport and rendering implementation.

Independent Windows2000 and Win98 fixtures passed ordinary Microsoft system
OpenGL loading, 8,192 exact red/green pixels and two swaps. The fixed diagnostic
bootstrap separately proved restoration of prior registry state; those frozen
receipts retain their original artifact paths. Current normal-loader checks use
`DGSYSGL.EXE` without private API DLLs beside the executable or in its working
directory. Full installer completion additionally requires exact file/registry
ownership, driver activation, reboot continuation, and the other system-provider
checks; a DLL copy alone is not an installed-state receipt.
