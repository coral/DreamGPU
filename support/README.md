# Build support

Cargo's `build.rs` calls the Rust `dreamgpu-build` crate. It initializes exact
source pins, stages checked patches, builds native QEMU, invokes guest CMake,
audits the resulting executables, and packages each guest OS separately.

```sh
cargo build --release
DREAMGPU_BUILD=all cargo build --release
```

Guest compilation requires the [pinned Linux toolchain](guest/toolchain/README.md).
On macOS, set `DREAMGPU_GUEST_HOST` to your configured SSH builder.

`guest/cmake/` contains cross-compilation recipes; `guest/*/patches/` contains
reviewed donor transformations. Source and toolchain identities are in
`guest/sources.lock.json` and `guest/toolchain.json`.

Python remains available for source tests, serial/QMP automation and evidence
analysis. It does not sequence DreamGPU compilation. Upstream QEMU's own Meson
build continues to require Python.
