# Licensing and distribution

The central source, repository and dependency inventory is
[ATTRIBUTION.md](../ATTRIBUTION.md). Independently authored DreamGPU code uses
**GPL-2.0-or-later**. Upstream/copied files retain their own grants and notices;
translation, namespace changes and generated bindings do not change ownership.
Earlier MIT grants remain valid for the versions distributed under those terms.

## Why GPL-2.0-or-later

This choice retains a GPLv2 route for code combined with QEMU and the GPLv2-only
KernelEx file in pthread9x. QEMU's [license policy](../vendor/qemu/LICENSE) includes
GPL-2.0-only portions and describes its emulator as GPLv2. Selecting GPLv3-only
for DreamGPU would remove that route without obtaining additional permissions.

The “or later” option also permits selecting GPLv3 for combinations whose other
components allow it. The FSF explains that GPLv2-or-later permits such a GPLv3
combination; GPLv2-only does not acquire that permission. See the
[FSF GPLv3 compatibility guide](https://www.gnu.org/licenses/quick-guide-gplv3.html)
and [GPL FAQ](https://www.gnu.org/licenses/gpl-faq.en.html).

Apache-2.0 is compatible with GPLv3, but not GPLv2, according to the
[Apache Software Foundation's guidance](https://www.apache.org/licenses/GPL-compatibility.html).
The Cargo graph includes Apache-only dependencies, including winit. For a binary
that incorporates these, select GPLv3 only when every other incorporated
component permits it. MIT-or-Apache dependencies can instead use their MIT
option. There is no blanket claim that all DreamGPU components can be linked
into one GPLv3 or GPLv2 executable.

## Component boundaries and records

The QEMU emulator, host SDK/consumer, guest drivers and guest translator DLLs are
separate build products with different dependency sets. A process/protocol
boundary is not an invented license exception. Evaluate the actual source and
linked contents of each distributed artifact. Preserve corresponding source,
checked patches, build recipes, copyright notices, applicable license texts and
any required offers/relinking materials under the licenses that apply.

[Cargo inventory](../support/attribution/cargo.json) records an all-target,
all-feature development/build/runtime superset. It preserves declared expressions
and collected notice hashes; it does not determine the contents or license of a
particular binary. [Repository inventory](../support/attribution/repositories.json)
records initialized and uninitialized gitlinks separately and lists optional
QEMU wraps. Prebuilt firmware provenance can differ from current source pins.
Native system libraries, compiler runtimes and firmware need artifact-specific
records; the Cargo graph does not inventory them.

Guest package records must retain Wine, noCRT, pthread9x/KernelEx, OpenGLide and
SGI notices alongside DreamGPU's license. A permissive top-level pthread9x notice
does not override its GPL-2.0-only KernelEx file. Open Watcom's compiler license
also does not by itself settle the terms of runtime code incorporated into a
particular output.

## Unresolved upstream notices

The exact pinned OpenGLide `sdk2_glide.h` and `sdk2_3dfx.h` retain historical 3Dfx
proprietary notices. No superseding grant has been established. Retain this
provenance and do not publish a bundled archive containing these copies until
permission is established or permitted definitions replace them. The surrounding
LGPL notice and DreamGPU's GPL declaration do not resolve the issue or establish
binary redistribution clearance. The attribution also identifies optional
firmware source notices whose selected-artifact review remains outstanding.

Microsoft Windows media, installed VM images, proprietary drivers, product keys
and game media are outside DreamGPU's license and source packages. The new
license applies to rights held by DreamGPU's authors, not to these external works.
