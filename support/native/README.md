# Private Linux rendering runtime

Cargo builds pinned Mesa into `qemu-build/mesa/install`, then publishes native
launchers in `qemu-build/bin`. Ninja's real QEMU outputs remain in `qemu-build`.
Each launcher authenticates a content-addressed `qemu-build/runtime/<sha256>`
containing its real ELF, Mesa, linked dependencies, GLVND vendor descriptions,
source-package identities and license texts. The manifest hashes all members.

Launching QEMU needs no environment opt-in. The Rust launcher sets library and
vendor search paths only in the QEMU child and then replaces itself with the
recorded real ELF. Juke and its Vulkan loader retain their original environment.
GLVND retains installed vendor selection; proprietary implementations and the
kernel/DRM/display server remain platform facilities. The ELF interpreter remains
a hash-checked platform dependency. This is a private application runtime, not a
replacement host display-driver installation.

A copied launcher first looks for the same runtime beside its new `bin` directory,
then uses its recorded original runtime path. A present but altered adjacent
runtime fails instead of falling back. `--dreamgpu-runtime` verifies the closure
and prints its actual execution path and hash. Cargo's native manifest includes
this mapping; fixture manifests using a launcher must retain its `runtime` and
`execution_path` fields. Sampling continues to require the exact real ELF,
application parent and owned QMP socket.

The hardware driver set is explicit in `crates/dreamgpu-build/src/mesa.rs`.
Hardware acceptance currently covers AMD radeonsi on Framework Linux; building
other drivers does not establish their runtime acceptance. Mesa's existing
compatibility version and capabilities are not overridden. No software Gallium
renderer is built. The package inventory supports RPM and Debian metadata; its
initial end-to-end runtime gate uses Fedora. Kernel and proprietary driver
compatibility across machines is not implied by copying the package.

Build dependencies include upstream Meson/Ninja and the selected Mesa drivers'
LLVM, Clang, CLC, SPIR-V, DRM, X11, Wayland and GLVND development packages. Mesa's
upstream Python generators remain upstream build dependencies, as with QEMU;
DreamGPU's orchestration and launcher are Rust. Expat uses Mesa's pinned upstream
Meson wrap, with its source and patch hashes recorded in `mesa/source.json`.

`dreamgpu-build mesa --root <checkout> --output <private-prefix>` and
`dreamgpu-build native-runtime --root <checkout> --output <native-output>` are
bounded build/package entry points for an already-built native ELF. The normal
`native`/`all` Cargo flow performs both automatically. The standalone launcher
Cargo workspace and target directory avoid recursively locking the parent build.
