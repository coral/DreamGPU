# Guest runtime

This directory contains code loaded as a display driver or graphics API inside
Windows. The QEMU device is `dreamgpu` (experimental PCI `1234:1113`). Runtime
support and game coverage are recorded in [execution evidence](../docs/progress.md);
a source build does not establish compatibility with every Windows 98/2000/XP
installation or game.

| Directory | Runtime responsibility |
| --- | --- |
| `nt/` | NT5 miniport/display pair, kernel transport and GDI integration |
| `win9x/` | Win98 NE display driver, LE VxD and flat C++23 validation/ownership logic |
| `opengl/` | WGL/OpenGL frontend, state, command encoding and platform transport adapters |
| `include/` | Guest protocol contracts and freestanding shared ownership helpers |
| `glide/` | DreamGPU adaptations applied to pinned OpenGLide sources |
| `d3d/` | DreamGPU adaptations and bounded diagnostics applied to pinned WineD3D sources |

Guest executables for installation, diagnosis and testing are in
[`tools/`](../tools/README.md). Compiler pins, dependency locks and build support
are in [`support/guest/`](../support/README.md). Source tests live under
`tests/guest/`; host orchestration remains in `scripts/`.

## Build

Run from the repository root inside the pinned
[Linux guest-builder environment](../support/guest/toolchain/README.md):

```sh
DREAMGPU_BUILD=guest cargo build --release
```

Cargo produces matching runtime components and tools in
`target/guest/packages/win98` and `target/guest/packages/windows2000-xp`.
Each OS package records exact source and payload identities. CMake owns the
individual compile targets; [build documentation](../docs/build.md) describes
local and remote toolchain setup.

The [C++23 build contract](../docs/guest-cpp23.md) describes the freestanding
compiler and ABI constraints. Maintained OpenGL and NT code use C++23. Win98
retains the 16-bit and register-call boundaries needed by Open Watcom; supported
flat logic is compiled with the modern cross compiler. Pinned upstream Wine and
OpenGLide retain their source and licenses.

## Driver and protocol boundaries

Install the NT miniport and display DLL from one package. A kernel-only callback
connects them; user-mode requests cannot obtain that callback. Native 2D, mode
changes and GL traffic share a sleeping kernel mutex. The miniport stamps the
actual process token and device generation into a bounded immutable command copy
before DMA submission. GL client storage is allocated on first use and released
through explicit close or process-exit cleanup.

The Win98 driver has separate segmented display and VxD boundaries. Its checked
client memory, process ownership and GDI clipping contracts are described in
[the Win98 OpenGL notes](win9x/OPENGL.md). Debug/identity packages are diagnostic
artifacts; the normal runtime omits their logging paths.

Run the combined `dreamgpu.exe` inside the guest to install the display driver,
OpenGL ICD, Glide2 and DirectDraw/Direct3D providers system-wide. Microsoft
`opengl32.dll` remains the OpenGL loader; it discovers DreamGPU's system ICD.
Games do not need neighboring provider DLLs. See the
[installer and acceptance status](../tools/setup/README.md), [OpenGL](opengl/README.md),
[Glide](glide/README.md) and [Direct3D](d3d/README.md) for their contracts.
Use the matched native runtime and guest package. Driver/service changes require
a cold boot and new snapshots; historical snapshots retain their installed
instances and identifiers. Original OS/game media and private VM disks stay
outside source control.

## Source checks and provenance

```sh
python3 tests/guest/opengl/test_frontend.py
python3 tests/guest/nt/test_gl.py
python3 tests/guest/win9x/test_owner.py
```

These source checks supplement real GPU pixel, lifetime and guest runtime gates.
They do not replace them. The NT code derives from ReactOS; Win98 derives from
VMDisp9x. Shared GL vocabulary and translator dependencies retain their original
terms. See [licenses and provenance](../docs/licensing.md); compiler and source
hashes belong with every distributed package.
