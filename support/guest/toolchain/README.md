# Standalone guest build

The validated compiler is Fedora MinGW GCC/G++ 16.1.1-1.fc44 targeting
`i686-w64-mingw32`. `Containerfile` pins the official Fedora 44 amd64 base
digest, compiler, binutils, headers, CRT and winpthreads packages. Native build
utilities are installed from that Fedora release. This is a compiler/API pin,
not a claim that the entire operating-system image is bit reproducible.
Package resolution fails if a pinned version disappears; update the recipe
deliberately with new validation, or supply an archive mirror containing the
same RPMs. It never silently substitutes a newer cross compiler.

On x86-64 Linux, with the pinned compiler installed:

```sh
DREAMGPU_BUILD=all cargo build --release
```

On macOS, use the [local Docker build and VM test workflow](../../../docs/build.md#local-docker-build-on-macos).
It includes first builds, installer-only rebuilds, and transferring the result to
Windows. The container must use `--platform linux/amd64`, including on Apple Silicon.

On Linux, the same build can run inside the toolchain container:

```sh
podman build -t dreamgpu-guest-toolchain:16.1.1 -f support/guest/toolchain/Containerfile support/guest/toolchain
podman run --rm --userns=keep-id -v "$PWD:/src:Z" -e DREAMGPU_BUILD=guest dreamgpu-guest-toolchain:16.1.1
```

An optional SSH builder can also compile the guest components while native QEMU
builds locally. Set up the alias normally in `~/.ssh/config`, then run:

```sh
DREAMGPU_GUEST_HOST=builder DREAMGPU_BUILD=all cargo build --release
```

The remote build uses `~/.cache/dreamgpu/cargo-guest` by default. An absolute
`DREAMGPU_GUEST_ROOT` selects a different dedicated cache. Rust orchestration
copies only build sources, verifies all public dependency pins and copies back
the finished packages. It does not boot or modify any VM.

Outputs are `target/guest/packages/win98` and
`target/guest/packages/windows2000-xp`. CMake keeps intermediate builds and
compilation databases in `target/guest/build/{nt5,win98}/`.

Open Watcom is downloaded from the URL in `sources.lock.json` and checked
against SHA256 `be75ec9f0cd9ee9ebf3af9a096092dd988f3c016599a415724c455d81205f71d`.
`DREAMGPU_TOOL_CACHE` reuses the verified archive. The Win98 NE/LE adapters use
Watcom because current C++ compilers do not emit those segmented executable ABIs;
flat drivers, policy and frontend code use C++23. No SSE2, exceptions or modern
C++ runtime is deployed into the guests.

Cargo calls Rust preparation/audits and CMake recipes. There is no Python build
entry point. Upstream Wine uses Make and QEMU uses Meson; the latter still needs
Python as an upstream build dependency.

## Updating source patches

OpenGLide/Wine GLU, WineD3D and the Win98 donor use explicit patches under
`support/guest/{glide,d3d,win9x}/patches/`. Each manifest records the original and
patched SHA-256 of every changed file and the patch identity. The helper checks
all inputs, applies patches in an isolated staging directory, and validates the
complete output before copying it into the generated build tree. Recorded CRLF
normalization preserves the old Windows donor build semantics. Unexpected source
drift fails instead of silently skipping a string substitution.

When updating a pinned source, review and rebase the patch, deliberately update
its input/output identities, and run the relevant component build and actual-source
tests. Do not regenerate expected hashes merely to silence a mismatch. Normal and
diagnostic variants have separate checked transformations; generated compiler
configuration remains ordinary build configuration. The package manifest carries
patch identities alongside compiler, source and artifact hashes.
