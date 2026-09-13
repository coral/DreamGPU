# Guest native recipes

Cargo's `dreamgpu-build` helper verifies the compiler and source pins, creates
private patched donor trees, invokes these CMake recipes, audits the actual
binaries, and publishes separate Windows 98 and Windows 2000/XP packages.
No Python program runs as a build dependency. Python remains available for
source tests, fixture control and artifact inspection.

One CMake build directory targets `DREAMGPU_GUEST_OS=win98` or `nt5`. Both build
the same OpenGL, OpenGLide and WineD3D application libraries. Display drivers
and reusable control tools have separate output directories. The install
components are `win98`, `nt5` and `tools`; an ordinary install includes them all.

The Rust preparation contract supplies these absolute paths:

- `DREAMGPU_VMDISP_SOURCE`: isolated, patched `vmdisp9x` donor tree.
- `DREAMGPU_WATCOM_ROOT`: verified Open Watcom archive extraction.
- `DREAMGPU_WINE_SOURCE`: isolated, patched Wine9x tree, `config.mk`,
  `dg-imports.def`, and all recorded license notices.
- `DREAMGPU_GLIDE_SOURCE`: isolated, patched OpenGLide tree with the Wine GLU
  mipmap source, generated configuration/routing/allocation adapters,
  `dg-imports.def`, and LGPL/SGI notices.
- `DREAMGPU_RUNNER_ID`: 64 lowercase hexadecimal characters identifying the
  runner's real source and build recipe.

`DREAMGPU_MINGW_PREFIX` defaults to `i686-w64-mingw32-`. Cross compilation uses
GCC/G++ 16.1.1, C++23 without exceptions or RTTI, and explicit legacy Windows
imports. The NT kernel and Win98 policy cores use no SIMD or floating point.
OpenGLide alone uses its existing SSE1 arithmetic policy, without SSE2 or
fast-math. The Win98 register, VMM, NE and LE adapters retain the actual Watcom
compiler and linker ABI.

CMake emits `compile_commands.json` for maintained MinGW translation units.
The std-only build-host `record-compiler.rs` captures Wine's actual upstream
Make compiler commands in `wine-compile-commands/` before executing the compiler.
These records support analysis of maintained headers injected into the donor;
unmodified upstream source and actual Watcom dialects are separate scopes.

The checked-in `guest/opengl/scalar.inc` and `frontend.def` are compiler inputs.
Builds never regenerate them in the checkout. Source regeneration and its
consistency check are developer tasks, independent of building a package.
