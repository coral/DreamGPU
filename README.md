# DreamGPU

DreamGPU accelerates graphics for Windows 98/2000/XP guests in QEMU on macOS and Linux. It combines guest display drivers and an OpenGL frontend with a native host renderer and a Rust transport/presentation SDK. Glide uses OpenGLide; Direct3D uses app-local WineD3D for Windows.

## Source layout

- [`guest/`](guest/README.md) contains runtime display drivers, the OpenGL
  frontend, protocol headers and translator adaptations.
- [`tools/`](tools/README.md) contains programs that run inside a guest for
  installation, diagnostics, graphics probes and automated game tests.
- [`support/guest/`](support/README.md) contains compiler/source pins and guest
  build support. [`scripts/`](scripts/README.md) provides runtime automation
  and evidence analysis; Cargo owns compilation and guest packaging.
- `tests/guest/` contains driver and frontend source tests; `src/`,
  `crates/dreamgpu-host/` and `vendor/qemu/` contain the SDK and native host.

## Library and standalone viewer

```toml
[dependencies]
dreamgpu = { path = "../dreamgpu", features = ["presentation"] }
```

```sh
cargo test --features presentation
cargo run --features presentation --example viewer -- --smoke
cargo run --features presentation --example viewer -- /tmp/dgpu.sock
```


## Build the native device and guest package

Cargo owns source initialization, native builds, cross compilation and packaging:

```sh
cargo build --release                            # SDK and native QEMU
DREAMGPU_BUILD=all cargo build --release          # also both Windows guest packages
```

Outputs:

- `target/qemu-build/`: native QEMU binaries and their source/binary manifest.
- `target/guest/packages/win98/`: Win98 display drivers, OpenGL, Glide, WineD3D,
  corresponding license notices and separately grouped tools.
- `target/guest/packages/windows2000-xp/`: NT5 display drivers and the same API
  implementations, with tools kept separate from runtime drivers.
- `target/guest/dreamgpu.exe`: combined guest installer, with an adjacent
  `installer-manifest.json` recording payload identities. System-wide activation
  is under development; the current installer refuses incomplete providers.
  See [installer status](tools/setup/README.md) and the [remaining plan](docs/plan.md).

## Licensing

DreamGPU project code is **GPL-2.0-or-later**. See the central [attribution inventory](ATTRIBUTION.md), [license details](docs/licensing.md). Third-party components retain their own licenses.
