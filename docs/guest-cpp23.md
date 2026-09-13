# Guest C++23 migration

DreamGPU builds guest packages from this checkout. It never needs Juke sources,
a Juke configuration, or a running guest. Source dependencies are pinned under
`vendor/`; Cargo initializes and verifies the pinned donor checkouts when building guest packages.

The currently validated Linux cross compiler is GCC 16.1.1 (Fedora MinGW
16.1.1-1.fc44), targeting Pentium III. `support/guest/toolchain.json` records the policy;
build manifests record the actual compiler. Win98 NE/LE adapters still use the
Open Watcom release and checksum in `support/guest/sources.lock.json`.
The [standalone toolchain recipe](../support/guest/toolchain/README.md) documents the exact
Linux packages and one-command candidate build.

## Implemented C++23 paths

- All ten OpenGL/WGL production units compile as C++23. `guest/opengl/frontend.cpp` keeps the public stdcall exports and immutable C ABI.
  A move-only scoped context owner now frees failed allocations automatically;
  successful and uncertain native creation explicitly transfer ownership into
  the existing bounded context table.
- The complete NT5 miniport/display, installer and diagnostic sources compile as C++23.
  `guest/nt/miniport/transport.cpp` owns failed pool initialization with RAII.
  `guest/nt/display/window.cpp` owns its semaphore lock and presentation allocation
  with RAII. Every callback return and clipping failure unlocks automatically.
  The engine callback boundary still explicitly releases the semaphore before
  calls that may re-enter GDI.
- `guest/include/ownership.hpp` is freestanding: no standard library allocation,
  exceptions, RTTI, static constructors, atexit or C++ runtime dependency.

- `tools/benchmark/runner.cpp` and its installer compile as C++23. The launched game
  process has a scoped handle owner; bounded termination still occurs explicitly.
- Win98 links six freestanding GCC C++23 policy objects into the Watcom LE VxD:
  `packet32.cpp` validates actual command/result bytes; `owner32.cpp` manages
  bounded process/context/handle tokens and reference retirement;
  `memory32.cpp` validates private spans/PTEs and owns pinned aliases plus
  immutable overlap-safe staging; `cursor32.cpp` owns the bounded cursor stream
  and capacity-checked pixel conversion; `channel32.cpp` owns GL requests,
  typed results, retirement, window bindings, retained frames and desktop
  coherence; `blt32.cpp` checks rectangles, offsets and primary metadata.
  These files live under `guest/win9x/`.
- Win98 channel transactions use move-only pin, semaphore and DMA owners.
  Client references release under the same interrupt exclusion as acquisition.
  Failed allocation/capture paths release every acquired resource; successful
  captures preserve overlapping buffers and unaligned returned-count storage.
  Native timeout and retained-frame retirement preserve the requirement for
  confirmed completion before freeing GPU-visible storage.
- The Win98 C boundary passes only fixed-width scalar values and POD pointers
  through explicit cdecl callbacks. The compiler/link gate rejects unexpected
  runtime symbols and permits only the declared internal policy-object links.
  Both normal and identity-diagnostic packages pass the real Watcom linker and
  NE/LE audits. Runtime acceptance remains specific to the installed package.
- OpenGLide compiles with C++23 while retaining its upstream implementation.

These are production translation units. Remaining owned C in the Win98 runtime
implements the segmented OS entrypoints and VMM/register boundary described
below. Game setup helpers compile as C++23; some public probes remain C. Upstream Wine remains C;
its maintained adaptations are explicit source patches rather than a language
rewrite of the upstream translator.

## Compile and source gates

On a Linux host with the cross compiler:

```sh
DREAMGPU_BUILD=guest cargo build --release
python3 tests/guest/opengl/test_frontend_packing.py
```

On either development host with Clang and sanitizers:

```sh
python3 tests/guest/opengl/test_frontend.py
python3 tests/guest/nt/test_gl.py
python3 tests/guest/win9x/test_owner.py
python3 tests/guest/win9x/test_memory.py
python3 tests/guest/win9x/test_cursor.py
python3 tests/guest/win9x/test_channel.py
python3 tests/guest/win9x/test_blt.py
DREAMGPU_STATIC_ANALYSIS=1 CXX=clang++ python3 tests/guest/opengl/test_frontend.py
DREAMGPU_STATIC_ANALYSIS=1 CXX=clang++ CC=clang python3 tests/guest/nt/test_gl.py
```

The source tests execute the production frontend, NT window code and Win98
policy implementations. The Win98 memory, cursor, channel and blit scripts also
run Clang static analysis. They
cover transport rejection, allocation failure, uncertain completion, x87/float
bit preservation, ownership, GDI clipping, callback locking and cleanup. The
linked driver and frontend import allowlists prohibit accidental C++ runtime
or unsupported operating-system dependencies.

The cross-compiled DLL imports only KERNEL32, USER32 and GDI32. The display
driver imports only Win32k, and the miniport's existing NT5 allowlist remains
active. Linking succeeds without libstdc++ or a user-mode CRT.

Successful source/build gates do not claim new guest runtime acceptance. The
new package must pass the standalone automated guest probes before replacing the recorded
accepted package. No guest process is launched by these build commands.

## Direct3D package

`DREAMGPU_BUILD=guest cargo build --release` builds the pinned Wine9x/WineD3D DLLs
and existing Win98/NT5 switching variants. WineD3D imports `dgpugl.dll`
directly. App-local deployment retains native DirectDraw switching and avoids
replacing Windows system DLLs. The same Cargo build includes the bounded
public D3D6/7/8/9 probes under each OS package’s `tools/` directory. The reviewed adaptations live in
`support/guest/d3d/patches/base/`; optional bounded copy instrumentation lives in
`support/guest/d3d/patches/diagnostic/`. Each manifest records the pinned upstream
commit, patch hash, and every changed file's exact input/output hashes.
`crates/dreamgpu-build/src/prepare.rs` applies the production patches before
CMake compilation; build manifests retain patch and helper provenance. Generated
compiler configuration and frontend import definitions remain separate build
inputs. `tests/guest/d3d/test_wine_patches.py` exercises the actual pinned source
for both variants, including diagnostic source-ID and output identities.

The Wine package includes the nested nocrt MIT-0 and pthread9x MIT/BSD notices,
source-specific notices (including KernelEx GPL2), and the GPL2 license text.
The packager requires these notices and verifies their build-manifest hashes.

## Memory and legacy boundaries

C++23 alone does not make this code memory safe. Scoped owners guarantee
specific cleanup paths; raw driver buffers, shared-memory mappings and the
upstream graphics implementations still require validation and review.

- Guest ABI headers expose fixed-width POD data and C exports. Kernel callers
  validate ranges, alignment, sizes and ownership before using guest memory.
  No C++ objects, references, exceptions or standard containers cross the ABI.
- The NT kernel pool and GDI semaphore wrappers do not turn driver callbacks
  into general user-mode C++. They preserve existing IRQL/context and reentry
  rules; semaphore release around reentrant GDI calls is explicit.
- Win98's NE files (`dg-call16.c`, `dg-blit16.c`, `dg-cursor16.c`,
  `dg-window16.c`) use actual segmented DDI/far-pointer and register-call ABIs.
  Modern MinGW does not emit that Windows 16-bit ABI. The small layout predicate
  in `dg-cursor.h` is shared with this NE capture and therefore stays compatible
  with Watcom; the 32-bit consumer compiles it as C++.
- `dg-memory-vxd.c` contains naked VMM entry and page callbacks plus DIOC
  marshaling. `dg-gl-vxd.c` contains critical/reentry checks, flags/CLI, atomic
  IRQ/watchdog semaphore completion, fatal HLT, and the cdecl service table.
  `dg-window-vxd.c` only marshals DIOC/PCRS scalar fields into the C++ core.
- `dg-vxd.c` retains the VPICD descriptor, naked ISR/EOI, PCI/register access,
  device DMA lifetime, sleeping watchdog wait, and the authoritative framebuffer
  snapshot/publication boundary. `dg-cursor-vxd.c` retains VMM contiguous-page
  allocation, cursor registers/completion and uncertain-DMA ownership; it no
  longer assembles or converts cursor pixels. These adapters call VMM services
  with Watcom's required register conventions instead of exposing them to GCC.
- `dg-identity-vxd.c` is diagnostic-only VMM identity/critical tracing and
  port-I/O text formatting. It accepts no user buffer, allocates no storage and
  submits no GPU work. The channel service table binds before IRQs are enabled;
  callbacks cannot unwind or retain request pointers.
- An uncertain native GPU completion retains ownership rather than freeing a
  potentially live resource. Cleanup after process death must wait for legal
  adapter context and confirmed host cleanup. Timeout is not proof of release.
- Upstream Wine and OpenGLide keep their own allocation and concurrency models.
  App-local translation preserves the native system DirectDraw installation;
  bundled switchers are not silently deployed as system DLL replacements.

Clang analysis and ASan/UBSan tests cover maintained code that can run on the
host. Actual i686 compiler/link/import gates cover legacy targets. Neither
substitutes for cold-boot guest probes, mode switches, lifetime tests and the
same-package game benchmarks on both hosts.
