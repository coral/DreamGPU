# Guest OpenGL frontend

This directory contains the runtime `dgpugl.dll` implementation. Build support is
in `support/guest/cmake/opengl.cmake`; the public probe source is in `tools/opengl/`.
The builder produces the frontend and `dgwgl.exe`. It is the evolving WGL
frontend for the versioned NT driver channel. It currently exposes the implemented
scalar OpenGL inventory, context ownership, thread transfer, window binding,
bounded texture uploads and typed state queries. It is not a complete OpenGL implementation or registered Windows
ICD yet, and reports a development version (0.0), without advertising a complete GL version. Do not replace a guest's
system OpenGL DLL with this intermediate package.

Build on a host with the i686 MinGW compiler:

```
DREAMGPU_BUILD=guest cargo build --release
```

The build records input hashes, PE imports and outputs. The frontend and installed
NT driver must use the same `dg-escape.h` version. `support/guest/opengl/generate.py` is an optional development generator for the
checked-in implemented scalar exports; ordinary builds do not invoke it, and all pointer arguments are copied before entering the
immutable command channel. Drawing batches are bounded by the driver contract.

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

Pending frontend work includes client-array entrypoints, remaining query coverage,
shared-window drawable semantics, sharing and complete GL/ICD coverage. Native
transport/renderer tests alone do not establish game compatibility.

Retail Half-Life 1.0 (build 742) now reaches and renders its opening tram scene
through this frontend on Windows 2000 on macOS and Linux. Select the custom mini-driver in
the game's own video menu; do not replace the system OpenGL library. The
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

Pack/unpack state, including swap-byte and bit-order flags, belongs to the frontend context and is answered locally by all typed state-query entrypoints. Supported packed texture uploads and packed32 readbacks honor byte swapping; byte components are unaffected. Bit-order state does not imply bitmap pixel-type support. These queries do not flush the guest stream or cross the driver boundary.

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
existing scalar/float-vector lighting setters. The reported base GL version
remains the implemented development subset.

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

Current build and evidence navigation: [guest runtime](../README.md),
[guest tools](../../tools/README.md), and [execution evidence](../../docs/progress.md).
Historical benchmark paths above belong to the original Juke evidence tree.
