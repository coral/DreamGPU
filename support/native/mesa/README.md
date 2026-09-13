# Private Linux OpenGL provider

Cargo pins the complete Mesa release in `source.json`, checks every patch and
before/after source identity, and builds upstream Meson into the native output
tree. No system Mesa installation is modified.

The patches modify Mesa's own implementation: integer pixel-transfer parameters
avoid a lossy float conversion, and GPU Draw/CopyPixels retain fractional raster
origins. Original file copyright and license headers remain in the verified
archive. The patches are DreamGPU modifications to those named upstream files;
they do not replace their licenses.

Source: https://archive.mesa3d.org/mesa-26.1.8.tar.xz, release 26.1.8; exact SHA256
and each modified path are in `source.json`. Mesa's full license documentation
and source archive accompany the runtime provenance. Build dependencies and
libraries copied from the build host are recorded separately by exact identity.

The changed provider passed the preserved extreme INDEX_OFFSET and fractional
negative PixelZoom GPU oracles through DreamGPU, plus ordinary image IPC and
release-slot checks.

The 1D border patch retains actual native-format image texels, including both
border texels at every mip level. A private GPU image holds the interiors and
borders; fixed-function NIR performs the GL 1.1 wrap and filter rules on the GPU.
Queries and copies access the stored image. Subimage writes update an existing
GPU image in place so another sharing context's bound view sees the new texels.
Unchanged draws reuse the image; borderless textures use the original path.

Border storage has a separate process-wide 64 MiB allocation budget, alongside
the host's existing 256 MiB logical texture allowance. It charges full native-format
CPU image bytes, every GPU atlas's width × height × format bytes, and its ownership
record. Driver-private padding and command storage are outside this texel budget.
Credit is reserved before allocation and released after actual CPU buffer or final
GPU resource destruction. Before invoking the GPU destructor, the exact registry
entry is detached as a still-charged retirement token; releasing that token after
destruction cannot accidentally retire a new allocation at a reused address.
A pointer-keyed registry avoids copied or uninitialized
ownership fields in Gallium resources. Old atlases retained by another context's
sampler views remain charged; failed replacement preserves the old atlas. There
is no draw-time registry scan. Ordinary borderless 8-bit uploads retain their
previous host allocation charge; newly admitted high-precision formats reserve
up to eight bytes per texel.

This is the DreamGPU fixed-function GL 1.1 provider contract, not general Mesa
conformance. Native programmable shaders using a bordered image are rejected
with `GL_INVALID_OPERATION` before drawing; a defensive binding guard also
prevents an incompatible `sampler1D` view of the private image. DreamGPU does not
expose shader creation, source, or use commands through either frontend or its
host command validator. No Mesa version or hardware capabilities are invented.

`tests/native_provider/border.c` checks real Radeon-rendered border colors,
mip filters, base/max level changes, shared mutation/deleted bindings, and the
programmable rejection. The adjacent Python tests compile the actual patched
allocation/publication, map-boundary and final resource release code with
ASan/UBSan, including concurrent budget exhaustion and shared-view teardown. The existing
diskless `qemu_texture_control_1d_border_conformance` test exercises native
dimensions, full/split uploads, neutral zero initialization, signed border updates,
readback, and both border copies through DreamGPU commands.

The filter rules are from the [OpenGL 1.1 specification, sections 3.8–3.8.3](https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf).
Each checked recipe gets a separate source/build directory named by its complete
pin-file hash; older accepted provider source trees and runtime snapshots remain
available when a patch set changes.
