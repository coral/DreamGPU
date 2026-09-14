# DreamGPU

DreamGPU accelerates graphics for Windows 98/2000/XP guests in QEMU on macOS and Linux. It combines guest display drivers and an OpenGL frontend with a native host renderer and a Rust transport/presentation SDK. Glide uses OpenGLide; Direct3D uses WineD3D for Windows through the guest system providers.

DreamGPU works by essentially exposing a "virtual" GPU (depends on how you like to think about it) that forward draw calls to the host. For OpenGL it's quite trivial, we just draw GL, for GLide we use the [qemu-3dfx](https://github.com/startergo/qemu-3dfx-arch) approach and for Direct3D we use WineD3D. End of the day your host GPU ends up painting into a framebuffer, the guest just doesn't know it's actually interacting with a GPU that's 30 years newer than it was designed for.

Yes really, you can play Half Life, UT2004 or Diablo 2 inside a QEMU Windows 98 or Windows 2000 machine at 250 FPS and it just... works? I was surprised myself at first but it's not really that surprising. Noone bats an eye when you run WINE on a Linux box and get better performance than a modern Windows install so why would this be any different really? Sure, we are virtualizing an entire operating system here and translating drawcalls but still. My Mac Studio with an M4 Max is getting > 100 FPS and that's including translating x86 -> ARM lol and then translating GL to Metal. Absolutely insane.

## Is this vibed?

Oh absolutely, top vibed. Not even ashamed of it. Reality is that all the pieces to make this have been out there for years but there just hasn't been energy to put this together and in a "brute-force" way attack this. GPT6 Astra slammed most of this out with **heavy guidance**.

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

## Building

```sh
cargo build --release                            # SDK and native QEMU
DREAMGPU_BUILD=all cargo build --release          # also both Windows guest packages
```

Outputs:

- `target/qemu-build/`: native QEMU binaries and their source/binary manifest.
- `target/guest/dreamgpu.exe`: driver installer for the 98/2000/XP guest

## Licensing

DreamGPU project code is **GPL-2.0-or-later**. See the central [attribution inventory](ATTRIBUTION.md). Third-party components retain their own licenses.
