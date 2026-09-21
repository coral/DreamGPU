# Building DreamGPU

Graphics changes also require the [explicit graphics verification jobs](graphics-contracts.md).
Installer smoke tests and ordinary Cargo tests alone do not establish API coverage.

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
native support. Compact typed-array packets likewise need the matching host and
driver validation. The updated native runtime also accepts older byte-pixel and
fixed-array packets. Native compilation uses optimization level 3 with normal
floating-point semantics; no fast-math option is enabled.


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

### Local Docker build on macOS

Run these commands from the repository root with Docker Desktop running. Docker
builds the Windows guest components locally; the macOS QEMU runtime builds with
the host's Cargo toolchain. Use `linux/amd64` on both Intel and Apple Silicon Macs:
the pinned compiler and Open Watcom tools are x86-64 Linux binaries.

Build the toolchain image once (repeat when its Containerfile changes):

```sh
docker build --platform linux/amd64 \
  -t dreamgpu-guest-toolchain:16.1.1 \
  -f support/guest/toolchain/Containerfile support/guest/toolchain
```

Build both guest packages and the combined installer:

```sh
docker run --rm --platform linux/amd64 \
  --mount type=bind,source="$PWD",target=/src \
  --mount type=tmpfs,target=/src/.cargo \
  --mount type=volume,source=dreamgpu-guest-cargo,target=/root/.cargo \
  -e CARGO_TARGET_DIR=/src/target/docker-cargo \
  dreamgpu-guest-toolchain:16.1.1 \
  cargo run --locked --release -p dreamgpu-build -- \
    guest --root /src --output /src/target/guest
```

The temporary `.cargo` mount hides personal builder settings for this container,
including old `DREAMGPU_GUEST_HOST` defaults. It does not edit your local config.
The Cargo volume caches downloads, and `target/docker-cargo` keeps Linux Rust
artifacts separate from macOS builds. Generated guest files stay in the checkout
after the container exits. The first build downloads the checked Watcom archive
and compiles drivers/translators; subsequent builds reuse those outputs.

The build produces **`target/guest/dreamgpu.exe`** and a ready-to-mount installer
CD, **`target/guest/dreamgpu-setup.iso`**, containing `DREAMGPU.EXE`. Both hashes,
embedded file identities and the import audit are in
`target/guest/installer-manifest.json`. ISO generation runs directly in Rust
on any build host, including Docker, without extra CD authoring tools.

After changes limited to `tools/setup`, rebuild just the installer using the
existing audited packages:

```sh
docker run --rm --platform linux/amd64 \
  --mount type=bind,source="$PWD",target=/src \
  --mount type=tmpfs,target=/src/.cargo \
  --mount type=volume,source=dreamgpu-guest-cargo,target=/root/.cargo \
  -e CARGO_TARGET_DIR=/src/target/docker-cargo \
  dreamgpu-guest-toolchain:16.1.1 \
  cargo run --locked --release -p dreamgpu-build -- \
    installer --root /src --output /src/target/guest
```

Use the full `guest` command again after changes to drivers, translators or
packaged probes. The installer-only command requires both package manifests and
rejects changed payload files whose recorded hashes no longer match. It also
regenerates the ISO from the rebuilt installer.

### Try the installation in a Windows VM

Build the native macOS runtime from the same checkout:

```sh
cargo build --release
```

Start your Windows 98 or 32-bit Windows 2000/XP VM with this runtime and its
DreamGPU adapter enabled. Attach the generated
`target/guest/dreamgpu-setup.iso` to the VM's CD drive, then run
`DREAMGPU.EXE` from the CD in Windows. Use an administrator account on 2000/XP.
You can also copy `target/guest/dreamgpu.exe` into the guest directly.
For a fresh installation, double-click without `/silent`:

1. An installation window appears with the current operation, filenames, an
   activity log and elapsed time. It stays responsive while setup works.
2. If a restart is requested, restart Windows and let setup resume automatically.
   A restart request means installation is still pending. Startup continuation
   runs silently; after logging back in, run `DREAMGPU.EXE /continue` to open the
   status window and see the result.
3. Wait for the explicit installed message. Setup checks the driver and runs
   normal-loader OpenGL, Glide 2 and Direct3D 6/7/8/9 probes before reporting success.
4. Try your application with its normal renderer selection. No app-local graphics
   DLL copies or special OpenGL driver arguments are needed.

Double-clicking a new installer automatically upgrades a completed installation
and clears abandoned extraction files from earlier attempts. `/upgrade` is also
available explicitly. `/continue` resumes pending work with the matching installer. A failure dialog includes
the setup error code, last operation/filename and, for diagnosed file-operation
failures, the Windows error code captured at the failure. Retain those when
reporting a problem. Setup error 24 covers preparation, including reading the
running EXE and checking saved staging state; it does not by itself establish
that disk space or permissions are the cause. Full commands and status meanings are in the
[installer documentation](../tools/setup/README.md).

### Optional SSH builder

`DREAMGPU_GUEST_HOST=<ssh-alias>` delegates guest compilation to a
configured x86-64 Linux host. Native macOS artifacts still build locally. The
SSH cache is dedicated build space, not a consumer checkout. A marked build
cache or empty directory is required before syncing files. Only source inputs
and finished packages are copied; VM fixtures are not involved.

The packages under `target/guest/packages/{win98,windows2000-xp}` contain:

- `drivers/`: the matching display driver and install metadata.
- `application/`: OpenGL, Glide and WineD3D translation libraries.
- `switchers/`: upstream app-local Direct3D/DirectDraw switchers for that OS.
- `tools/`: reusable installers, controls and probes, separate from drivers.
- `LICENSES.txt`: all license texts and attribution records, with labeled sections
  preserving their original paths. Guest packaging combines these before hashing
  the package, so setup extracts one notice file.
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
