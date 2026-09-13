# DreamGPU attribution

Independently authored DreamGPU code is licensed **GPL-2.0-or-later**, unless a
file states different terms. See [LICENSE](LICENSE) and [LICENSES](LICENSES).
This choice does not relicense upstream code, generated interfaces or copied
files. Existing grants for earlier MIT-licensed versions remain available under
their original terms. Copyright and license notices in each source file take
precedence over this summary.

DreamGPU was extracted from [Juke](https://github.com/anderstorpsfestivalen/juke).
The initial working-tree provenance, including unpublished changes, is recorded
in [docs/import-provenance.json](docs/import-provenance.json). The QEMU fork is
based on [upstream QEMU](https://gitlab.com/qemu-project/qemu). Wine9x and the GLU
source descend from [Wine](https://gitlab.winehq.org/wine/wine).

## Direct source repositories

These are all eight top-level git dependencies. Pins below identify the inspected
checkout; [repositories.json](support/attribution/repositories.json) separately
records the parent gitlink and checked-out revision. A repository-wide license
label is not a replacement for its per-file notices.

| Repository and credit | Inspected revision | Use | License evidence |
| --- | --- | --- | --- |
| [QEMU / Fabrice Bellard and QEMU contributors](https://github.com/anderstorpsfestivalen/qemu) · `vendor/qemu` | `b21f8e87d4519ea01acb9a8d21161d551d0d2224` | Native emulator, DreamGPU device and host API adapters; shared fork also retains the separate Juke audio implementation. | GPLv2 for the emulator as a whole; mixed per-file licenses, including GPL-2.0-only portions. [LICENSE](vendor/qemu/LICENSE), [COPYING](vendor/qemu/COPYING). |
| [VMDisp9x / JHRobotics](https://github.com/JHRobotics/vmdisp9x) · `vendor/vmdisp9x` | `718b3d51a1532fe1ba2e133cf76186f1a609d35e` | Win98 display/VxD donor and checked source overlays. | MIT, with original per-file notices. [LICENSE](vendor/vmdisp9x/LICENSE). |
| [ReactOS contributors, including Filip Navara and Hervé Poussineau](https://github.com/reactos/reactos) · `vendor/reactos` | `22fb3bb2c1d8196cf501edbb49b3814739f7b016` | NT display/miniport and interface-header donors; not a build of all ReactOS. | Used display/Bochs code: GPL-2.0-or-later. Header/public-domain exceptions remain per file. Repository has additional licenses. [COPYING](vendor/reactos/COPYING), [display donor](vendor/reactos/win32ss/drivers/displays/framebuf/enable.c), [miniport donor](vendor/reactos/win32ss/drivers/miniport/bochs/bochsmp.c). |
| [qemu-3dfx contributors / startergo fork](https://github.com/startergo/qemu-3dfx-arch) · `vendor/qemu-3dfx` | `5d40e054a1bc8c4b00c41c533c6b84afd9cdc30b` | GL function vocabulary and historical interface reference. | GPLv2 repository notice; used GL vocabulary carries GPL-2.0-or-later. Other files retain their own terms. [LICENSE](vendor/qemu-3dfx/LICENSE), [vocabulary donor](vendor/qemu-3dfx/qemu-1/hw/mesa/mglfunci.h). |
| [Wine authors and JHRobotics](https://github.com/JHRobotics/wine9x) · `vendor/wine9x` | `8ab16c6c0930efc1f9138eddda7b3114d7f31e62` | WineD3D, DirectDraw, D3D8/D3D9 adaptation and switcher sources. | LGPL-2.1-or-later for Wine code; linked support includes the GPL-2.0-only exception below. [LICENSE](vendor/wine9x/LICENSE). |
| [OpenGLide authors, including Fabio Barros, Paul for Glidos and Simon White; qemu-xtra contributors](https://github.com/kjliew/qemu-xtra) · `vendor/qemu-xtra` | `b3deabc7da053ea2c4c61e8f2f21c41d702d7070` | Current OpenGLide translator donor. | LGPL2.1 notices, subject to per-file terms. Historical 3Dfx SDK headers have an unresolved proprietary notice; see below. [LICENSE](vendor/qemu-xtra/LICENSE). |
| [JHRobotics](https://github.com/JHRobotics/patcher9x) · `vendor/patcher9x` | `b6e30d4b5a396dcd453b6c8e6733fd5b5cbce59e` | Pinned Win9x patching/reference tooling; not a grant for patched Windows binaries. | MIT with nested support exceptions. [LICENSE](vendor/patcher9x/LICENSE). |
| [Wine contributors and Silicon Graphics](https://github.com/wine-mirror/wine) · `vendor/wine-glu` | `8f8792fc857ba609b13c6d63839034e0576b0c9f` | Only the SGI mipmap implementation is used by the Glide build. | Used dlls/glu32/mipmap.c: SGI Free Software License B 2.0. Whole Wine repository has other licenses. [Exact source notice](vendor/wine-glu/dlls/glu32/mipmap.c). |

## Pinned Linux graphics provider and launcher

[Mesa](https://gitlab.freedesktop.org/mesa/mesa)26.1.8 is an additional native
source dependency downloaded by Cargo, rather than a git submodule. The exact
release archive, SHA256, modified paths and checked patch identities are in
[support/native/mesa/source.json](support/native/mesa/source.json). It provides
private hardware OpenGL execution for the Linux QEMU child. Mesa's core library
uses MIT terms; GLX and other components retain SGI and other per-file terms.
The pinned archive's `docs/license.rst`, complete `licenses/` tree and source
headers remain authoritative. The runtime package retains these notices and
records separately copied host libraries by package/artifact identity. DreamGPU
patches do not replace original Mesa notices.

The standalone Rust native launcher has its own locked Cargo graph, inventoried
in [cargo-native-launcher.json](support/attribution/cargo-native-launcher.json).
It is separate from the workspace graph below; its recorded dependency license
expressions and notices must accompany the actual linked launcher distribution.
See [the provider recipe](support/native/mesa/README.md) for source preparation
and the exact supported graphics contract.

## Nested source and copied dependencies

| Dependency | Source / pinned revision | License and scope |
| --- | --- | --- |
| fixlink / Jaroslav Hensl | [vendor/vmdisp9x/fixlink](https://github.com/JHRobotics/fixlink) · `a2a74447daea3197255f3a4fb5cfb0c5a453dcc8` | MIT-0; build-time executable fixer. `licence.txt`. |
| noCRT / Jaroslav Hensl | [vendor/wine9x/nocrt](https://github.com/JHRobotics/nocrt) · `d4c45d2acd0c6d5f656e71501d6c779719a640fb` | MIT-0; support code linked by the Wine build. `licence.txt`. |
| pthread9x / mingw-w64 contributors, Lockless Inc., JHRobotics | [vendor/wine9x/pthread9x](https://github.com/JHRobotics/pthread9x) · `9c1ec5a655849f99f1e9c4e3b04609f446c92551` | MIT and BSD-3-Clause in `licence.txt`; **GPL-2.0-only** KernelEx code by Xeno86 in `src/tryentercriticalsection.c`. Preserve all source notices, not just the top-level permissive notice. |
| noCRT / Jaroslav Hensl | [vendor/patcher9x/nocrt](https://github.com/JHRobotics/nocrt) · `0a09a82802ee75717ce055ea7bfafcd3584dd6b0` | MIT-0; this is a different pinned revision from Wine9x noCRT. |
| startergo/qemu-xtra | [vendor/qemu-3dfx/wrappers/extra](https://github.com/startergo/qemu-xtra) · `80b73df4e2752ec01a359a362c44a82bba419128` | LGPL2.1 top-level notice with mixed per-file licenses, including GPL DOSBox-derived material; historical/reference subtree, not the current OpenGLide build source. |
| libmspack / Stuart Caie and contributors | [upstream](https://github.com/kyz/libmspack); copied within the pinned [patcher9x/mspack](vendor/patcher9x/mspack) tree | LGPL2.1 in [LICENCE](vendor/patcher9x/mspack/LICENCE) and source headers. This is a source subtree, not an independent gitlink. |

Copied ReactOS files identify their exact donor and modification status in their
headers. The generated GL vocabulary identifies its qemu-3dfx source; changing
names or translating C to Rust does not remove that provenance. Generated host
OpenGL declarations identify the actual platform SDK/header source in
[gl_api.rs](crates/dreamgpu-host/src/gl_api.rs). macOS OpenGL headers retain their
Apple/SGI notices; Linux declarations use the selected system OpenGL headers.
A generated declaration file is not evidence that an entire platform SDK may be
redistributed. The checked patches and preparation code retain original Wine,
OpenGLide and SGI source notices in prepared build trees.

## QEMU firmware, optional source fallbacks and host libraries

QEMU declares the following nested repositories. Their complete URLs and gitlink
pins are in [repositories.json](support/attribution/repositories.json). Most ROM
source submodules are not initialized by this checkout. QEMU also ships prebuilt
firmware: its [pc-bios/README](vendor/qemu/pc-bios/README) records provenance that
can differ from the current ROM gitlink. For example, the bundled edk2 license
record describes edk2-stable202302 and its included OpenSSL/SoftFloat versions.
Do not assign a current gitlink's license inventory to an older binary without
checking that binary's record.

| Declared source repository | License scope / evidence |
| --- | --- |
| [seabios](https://gitlab.com/qemu-project/seabios.git) · `b52ca86e094d19b58e2304417787e96b940e39c6` | GPLv3/LGPLv3 and retained per-file terms; upstream COPYING and COPYING.LESSER. VGA firmware has its own inherited notices. |
| [SLOF](https://gitlab.com/qemu-project/SLOF) · `47457c9078a217150aa1634f2657e6804fbde467` | BSD-3-Clause; upstream LICENSE. |
| [ipxe](https://gitlab.com/qemu-project/ipxe) · `4bd064de239dab2426b31c9789a1f4d78087dc63` | GPL and per-file alternatives; upstream COPYING requires deriving the applicable license for the selected target using its `.licence` build output. |
| [openbios](https://gitlab.com/qemu-project/openbios) · `e5ac46dd24e6216c36aa80462af25457e7029440` | GPLv2, with per-file notices; QEMU firmware provenance in pc-bios/README. |
| [qemu-palcode](https://gitlab.com/qemu-project/qemu-palcode) · `99d9b4dcf27d7fbcbadab71bdc88ef6531baf6bf` | GPLv2 COPYING text; retain per-file grants. |
| [u-boot](https://gitlab.com/qemu-project/u-boot) · `840658b093976390e9537724f802281c9c8439f5` | GPLv2 aggregate; many files permit later versions, with exceptions. Upstream Licenses/README and per-file SPDX apply. |
| [skiboot](https://gitlab.com/qemu-project/skiboot) · `785a5e3070a86e18521e62fe202b87209de30fa2` | Apache-2.0; upstream LICENCE. |
| [QemuMacDrivers](https://gitlab.com/qemu-project/QemuMacDrivers) · `90c488d5f4a407342247b9ea869df1c2d9c8e266` | Mixed driver/firmware source; no repository-wide grant established by this review. Exact selected source/binary notices remain a release inventory gap. |
| [seabios-hppa](https://gitlab.com/qemu-project/seabios-hppa) · `6d6a72c218f6bf6b0e0b6a8bd26c5c67ba3ad2e0` | SeaBIOS fork; GPLv3/LGPLv3 family and per-file terms. Preserve fork-specific notices. |
| [u-boot-sam460ex](https://gitlab.com/qemu-project/u-boot-sam460ex) · `1e5f4a1607cc6713d27ffe48dd9de84e69cfc1c2` | U-Boot fork; GPLv2-family and per-file exceptions. Exact selected firmware notice review is required before redistribution. |
| [edk2](https://gitlab.com/qemu-project/edk2) · `4dfdca63a93497203f197ec98ba20e2327e4afe4` | BSD-2-Clause-Patent plus historical/per-component terms. Complete bundled notice record: [edk2-licenses.txt](vendor/qemu/pc-bios/edk2-licenses.txt), including OpenSSL/SSLeay and Berkeley SoftFloat notices. |
| [opensbi](https://gitlab.com/qemu-project/opensbi) · `74434f255873d74e56cc50aa762d1caf24c099f8` | BSD-2-Clause plus ThirdPartyNotices; see QEMU firmware provenance. |
| [qboot](https://gitlab.com/qemu-project/qboot) · `8ca302e86d685fa05b16e2b208888243da319941` | GPLv2 LICENSE text; retain per-file version grants rather than inferring “or later” from the license text alone. |
| [vbootrom](https://gitlab.com/qemu-project/vbootrom) · `1c8e9510b22c5b0fd7d7753f08042a4bcbd2939d` | Apache-2.0; QEMU firmware provenance identifies the Nuvoton/ASPEED ROM sources. |
| [libvirt-ci](https://gitlab.com/libvirt/libvirt-ci) · `5176e136ab11e275eb9f57c3d5c80e77af6507cb` | QEMU CI tooling, not guest runtime. Pinned source notice review remains outstanding if this optional repository is distributed. |

The repository ledger also includes every QEMU Meson `.wrap` declaration,
including libslirp, dtc, libblkio, keycodemapdb, Berkeley SoftFloat/TestFloat and
optional Rust crate fallbacks. A declaration is not proof that the fallback was
built: QEMU may select an installed system library instead. Keep the selected
source's license/notice files and Meson dependency/build receipt with any release.
The Cargo ledger below does not cover QEMU's separate Meson Rust dependency graph.

Native system dependencies and platform frameworks are selected by QEMU/Meson
and the host platform (for example GLib, pixman, zlib, libepoxy, EGL/GBM and Apple
frameworks). Their installed package licenses and actual linked artifacts must
be inventoried for the binary being shipped. Neither the source list nor a
successful build establishes blanket redistribution permission for system SDKs.
Firmware is separately licensed software; aggregation is not a claim that its
source can be linked into the GPLv2 emulator.

## Rust dependencies

The direct dependency table covers workspace runtime, platform, build and
example/test dependencies. Versions, features, all **267 resolved external
packages**, repository URLs, authors, declared license expressions, dependency
edges, registry checksums and available crate VCS pins are preserved in
[cargo.json](support/attribution/cargo.json). This all-feature/all-target
normal/build/dev superset is not a binary bill of materials. Multiple versions of
some crates are present transitively.

| Direct package | Repository | Declared license |
| --- | --- | --- |
| `anyhow 1.0.104` | [https://github.com/dtolnay/anyhow](https://github.com/dtolnay/anyhow) | `MIT OR Apache-2.0` |
| `ash 0.38.0+1.3.281` | [https://github.com/ash-rs/ash](https://github.com/ash-rs/ash) | `MIT OR Apache-2.0` |
| `block2 0.6.2` | [https://github.com/madsmtm/objc2](https://github.com/madsmtm/objc2) | `MIT` |
| `bytemuck 1.25.2` | [https://github.com/Lokathor/bytemuck](https://github.com/Lokathor/bytemuck) | `Zlib OR Apache-2.0 OR MIT` |
| `crossbeam-queue 0.3.14` | [https://github.com/crossbeam-rs/crossbeam](https://github.com/crossbeam-rs/crossbeam) | `MIT OR Apache-2.0` |
| `libc 0.2.189` | [https://github.com/rust-lang/libc](https://github.com/rust-lang/libc) | `MIT OR Apache-2.0` |
| `libloading 0.8.9` | [https://github.com/nagisa/rust_libloading/](https://github.com/nagisa/rust_libloading/) | `ISC` |
| `objc2 0.6.4` | [https://github.com/madsmtm/objc2](https://github.com/madsmtm/objc2) | `MIT` |
| `objc2-metal 0.3.2` | [https://github.com/madsmtm/objc2](https://github.com/madsmtm/objc2) | `Zlib OR Apache-2.0 OR MIT` |
| `serde 1.0.229` | [https://github.com/serde-rs/serde](https://github.com/serde-rs/serde) | `MIT OR Apache-2.0` |
| `serde_json 1.0.151` | [https://github.com/serde-rs/json](https://github.com/serde-rs/json) | `MIT OR Apache-2.0` |
| `sha2 0.10.9` | [https://github.com/RustCrypto/hashes](https://github.com/RustCrypto/hashes) | `MIT OR Apache-2.0` |
| `thiserror 2.0.20` | [https://github.com/dtolnay/thiserror](https://github.com/dtolnay/thiserror) | `MIT OR Apache-2.0` |
| `tracing 0.1.44` | [https://github.com/tokio-rs/tracing](https://github.com/tokio-rs/tracing) | `MIT` |
| `uuid 1.26.1` | [https://github.com/uuid-rs/uuid](https://github.com/uuid-rs/uuid) | `Apache-2.0 OR MIT` |
| `wgpu 30.0.1` | [https://github.com/gfx-rs/wgpu](https://github.com/gfx-rs/wgpu) | `MIT OR Apache-2.0` |
| `winit 0.30.13` | [https://github.com/rust-windowing/winit](https://github.com/rust-windowing/winit) | `Apache-2.0` |

For dual MIT/Apache dependencies, selecting the MIT option preserves a GPLv2
combination route. Some resolved dependencies are Apache-2.0-only, including
winit and several graphics/example dependencies. Combinations including those
require an appropriate GPLv3 route and cannot simultaneously incorporate
GPL-2.0-only code without a separate permission. Raw manifest license strings,
including older slash notation and conjunctions such as Unicode notices, are
preserved rather than silently simplified. The `dreamgpu-host` static library
has no external Cargo package dependencies; compiler/runtime components still
retain their own licenses.

## Build and development tools

Tool licenses govern the tools themselves. Any runtime, startup code or headers
copied/linked into output require their own notices and applicable exceptions.
Versions/archive hashes for the guest compiler environment are in
[support/guest/toolchain.json](support/guest/toolchain.json) and
[support/guest/sources.lock.json](support/guest/sources.lock.json).

| Tool / project | Upstream and terms |
| --- | --- |
| Rust and Cargo | [Rust](https://github.com/rust-lang/rust), [Cargo](https://github.com/rust-lang/cargo): MIT OR Apache-2.0, with separately licensed compiler dependencies; [official license policy](https://rust-lang.org/policies/licenses/). |
| GCC and GNU Binutils | [GCC](https://gcc.gnu.org/), [Binutils](https://www.gnu.org/software/binutils/): GPL toolchain with per-component terms. GCC runtime files carrying the [Runtime Library Exception 3.1](https://www.gnu.org/licenses/gcc-exception-3.1-faq.en.html) have additional permissions; the exception is not a grant for arbitrary other files. |
| mingw-w64 | [Source](https://github.com/mingw-w64/mingw-w64): headers/CRT have mixed per-file terms, including public domain, ZPL, MIT and BSD notices. Preserve the selected toolchain's complete notices. |
| Open Watcom | [Source](https://github.com/open-watcom/open-watcom-v2): Sybase Open Watcom Public License and retained third-party terms; pinned release notice controls. Linked runtime pieces require separate inspection; this is not a GPL compatibility assertion. |
| LLVM, Clang, clang-format, clang-tidy | [llvm-project](https://github.com/llvm/llvm-project): Apache-2.0 WITH LLVM-exception and retained component notices. |
| bindgen (optional regeneration) | [rust-bindgen](https://github.com/rust-lang/rust-bindgen): BSD-3-Clause; generated bindings also identify their source headers. |
| CMake | [CMake](https://github.com/Kitware/CMake): BSD-3-Clause and component notices. |
| Meson | [Meson](https://github.com/mesonbuild/meson): Apache-2.0; used by upstream QEMU. |
| Ninja | [Ninja](https://github.com/ninja-build/ninja): Apache-2.0. |
| NASM | [NASM](https://github.com/netwide-assembler/nasm): BSD-2-Clause. |
| Git | [Git](https://github.com/git/git): GPLv2, with per-file terms. |
| Python | [CPython](https://github.com/python/cpython): PSF license family and incorporated notices. Used by automation, this optional inventory tool and upstream build tools; DreamGPU build orchestration is Rust/Cargo. |

## Exceptions and distribution boundaries

The pinned [sdk2_glide.h](vendor/qemu-xtra/openglide/sdk2_glide.h) and
[sdk2_3dfx.h](vendor/qemu-xtra/openglide/sdk2_3dfx.h) contain historical 3Dfx
Interactive proprietary, unpublished-source notices. **No superseding grant for
these exact copies has been established.** The surrounding LGPL repository
notice does not resolve that conflict. Do not relicense these files as DreamGPU
or publish bundled source archives containing them until a grant is established
or permitted interface definitions replace them. This inventory is not clearance
of the affected binary distribution either.

The Wine support library's KernelEx file is GPL-2.0-only. Consequently, a complete
linked Wine adapter must be assessed with that file included; describing every
component as LGPL or asserting a GPLv3-compatible whole would be inaccurate.
Open Watcom runtime incorporation and the explicitly unresolved optional firmware
notices above remain artifact-specific checks.

Windows installation media, installed VM disks, proprietary vendor drivers,
product keys and game media are user-supplied and are not licensed by DreamGPU.
Do not include them in source/license packages. See [docs/licensing.md](docs/licensing.md)
for compatibility rationale and release records, and
[support/attribution/README.md](support/attribution/README.md) for inventory scope
and regeneration. Collected original notice bytes are retained by hash under
[support/attribution/notices](support/attribution/notices); these do not replace
source-level notices or a complete corresponding-source distribution.
