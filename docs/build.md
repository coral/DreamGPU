# Building DreamGPU

`build.rs` calls the Rust `dreamgpu-build` crate. The sequence is source checks,
native QEMU compilation, optional guest preparation, CMake compilation, binary
audits, then per-OS packaging. A failed step fails Cargo. No Python script in
this repository is on that build path.

| Command | Result |
| --- | --- |
| `cargo build --release` | Rust SDK and native QEMU for the current host |
| `DREAMGPU_BUILD=all cargo build --release` | Native components plus Win98 and Windows 2000/XP guest packages |
| `DREAMGPU_BUILD=guest cargo build --release` | Guest packages and SDK; useful inside the guest toolchain container |
| `DREAMGPU_BUILD=sdk cargo clippy --workspace --all-targets --all-features -- -D warnings` | Rust checks without rebuilding external components |

These are build component selectors. Runtime graphics paths and performance
behavior do not depend on them.

Use the native runtime and guest installer from the same build. Current guest
frontends send packed 16-bit texture pixels directly and require the matching
native support; the updated native runtime also accepts older byte-pixel packets.


Native QEMU defaults to x86_64, i386, PPC and m68k system binaries. An explicit
`DREAMGPU_QEMU_TARGETS` comma-separated list narrows the native target set.
`DREAMGPU_NATIVE_DEBUG=1` retains QEMU debugging information;
`DREAMGPU_FORCE_CONFIGURE=1` invalidates configuration. `DREAMGPU_OUTPUT` selects
an output parent, otherwise the checkout's `target/` is used. Juke receives the
native output directory through Cargo dependency metadata.

QEMU's Meson target invokes `rustc` directly for the dependency-free
`dreamgpu-host` static library. This avoids recursive Cargo locking. Source
changes remain Meson dependencies. Upstream QEMU itself uses Meson/Python; this
is distinct from DreamGPU-owned build orchestration. Wine keeps its upstream
Make/NASM build, and Win98's segmented NE/LE ABI adapters keep Watcom.

## Guest toolchain and packaging

Use the [pinned Linux compiler environment](../support/guest/toolchain/README.md).
The Rust orchestrator verifies GCC/G++ version and target, donor commits and
nested pins, the Watcom download hash, and exact input/patch/output hashes.
Patches apply to private source copies, never to a checked-out donor.

On macOS, `DREAMGPU_GUEST_HOST=<ssh-alias>` delegates guest compilation to a
configured x86-64 Linux host. Native macOS artifacts still build locally. The
SSH cache is dedicated build space, not a consumer checkout. A marked build
cache or empty directory is required before syncing files. Only source inputs
and finished packages are copied; VM fixtures are not involved.

The packages under `target/guest/packages/{win98,windows2000-xp}` contain:

- `drivers/`: the matching display driver and install metadata.
- `application/`: OpenGL, Glide and WineD3D translation libraries.
- `switchers/`: upstream app-local Direct3D/DirectDraw switchers for that OS.
- `tools/`: reusable installers, controls and probes, separate from drivers.
- `licenses/`: applicable component notices.
- Manifests recording exact source, patch, compiler and payload identities.

CMake emits compilation databases in the intermediate build directory. Wine's
real compiler invocations are recorded by a small Rust launcher during the same
build; analysis does not require recompiling Wine merely to discover its flags.

A source build cannot establish guest runtime compatibility. Preserve previous
acceptance evidence and use the new package identity for subsequent validation.

## Formatting and analysis

`.clang-format` defines formatting for maintained C/C++ sources and headers.
`.clang-tidy` enables the analyzer and selected correctness checks. Analyze real
translation units with their compilation databases and target headers. Do not
reformat untouched upstream repositories or reinterpret Watcom segmented code
as an ordinary flat Clang target. The actual Watcom compiler/linker plus NE/LE
binary audits cover those ABI boundaries.

Clippy runs with warnings denied. Narrow native API argument-count expectations
explain their ABI constraints in source; they are not workspace-wide allowances.

Personal builder defaults can live in ignored `.cargo/config.toml`:

```toml
[env]
DREAMGPU_GUEST_HOST = { value = "your-builder", force = false }
```

Runtime media scripts need optional Python automation dependencies such as
`pycdlib`; install those in a virtual environment. They are not needed by Cargo.
