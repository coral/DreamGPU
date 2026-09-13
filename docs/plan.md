# DreamGPU implementation plan

Agreed September 12, 2026. Execution authorized. This document records the agreed
scope; `docs/progress.md` records implementation evidence and remaining work.

## Build and source housekeeping (completed)

- [x] Make Cargo/build.rs the public build entry point and sequence native and guest components in Rust.
- [x] Build Windows 98 and Windows 2000/XP packages with CMake and the pinned legacy toolchains.
- [x] Keep incremental compiler outputs and checked source preparation; support a dedicated SSH guest-builder cache on macOS.
- [x] Remove superseded Python build recipes and retain useful runtime/testing automation.
- [x] Format all maintained C/C++ and run target-aware clang-tidy, retaining real Watcom ABI checks.
- [x] Fix strict Clippy findings in DreamGPU and the Juke consumer.
- [x] Validate native builds and diskless GPU behavior on macOS/Linux; audit both guest packages.

Commands and limitations: [build.md](build.md), [cpp-quality.md](cpp-quality.md).
Runtime/performance tasks below retain their prior evidence status; housekeeping
builds do not replace game/guest acceptance.

## Objective and implementation policy

Extract the working retro graphics stack into `github.com/coral/DreamGPU`, rooted
in its own checkout. DreamGPU owns its
implementation, dependencies, builds and automated tests. Juke consumes the local
checkout and remains the primary integration and performance verification app.

Support accelerated OpenGL, Glide and Direct3D for QEMU Windows 98/2000/XP guests
on macOS and Linux. Preserve responsive desktop use, bounded memory consumption,
existing performance gains and sleeping idle workers. 200 FPS is an ambition,
not a universal requirement or an achieved result for CPU-emulation-bound games.

- Modern Rust: host rendering, command processing, resource management, QEMU
  integration and presentation SDK.
- C++23: maintained 32-bit guest frontend, driver logic and helpers.
- Minimal C/assembly: Win98's required 16-bit and native ABI adapters.
- Existing upstream languages: pinned QEMU, WineD3D and OpenGLide dependencies
  with explicit patches. Their wholesale rewrite is outside this migration.
- No rust9x.

## Repository, dependencies and interfaces

Create a root Cargo library package named `dreamgpu` alongside its workspace.
Separate protocol, host execution, QEMU integration and presentation internally.

DreamGPU owns the GPU device, required QEMU graphics/performance patches, native
execution and IOSurface/DMA-BUF export, frame leases, ordered desktop/cursor/CPU
display integration, guest drivers/frontend/translators, dependency and toolchain
pins, guest packaging, probes, reusable fixture control, sampling and benchmarks.
A minimal standalone viewer proves independence from Juke.

Juke retains VM management, UI, input/audio integration and CRT effects. Extract
the CPU-display anchor and cursor coupling from its shared-memory backend.
Preserve unrelated QEMU functionality Juke uses and identify it separately.

Juke's root manifest uses:

```toml
[workspace.dependencies]
dreamgpu = { path = "../../coral/dreamgpu" }
```

Consuming crates use `dreamgpu.workspace = true`. The same relative layout resolves
the canonical checkout on Mac and Framework without per-host edits. Build metadata ties
the SDK, native QEMU artifacts and guest-package manifest to the same checkout.

- Move required QEMU source ownership into DreamGPU; remove Juke's duplicate
  ownership after migration.
- Commit-pin substantial dependencies as submodules. Keep only needed imported
  fragments, preserving exact provenance.
- Replace fragile source-string substitutions with explicit patches/fork commits.
- Preserve current QEMU performance and compatibility fixes.
- Commit directly on DreamGPU `main` and the shared QEMU fork's `master`; Juke
  stays on `master`. Do not create separate integration/development branches.
  Pin published QEMU commits in DreamGPU's submodule.
- Keep audio in Juke. The shared QEMU fork may retain its existing `juke` audio
  backend; DreamGPU owns graphics, not the consuming application's audio backend.
- Preserve per-component licenses and SPDX notices; no umbrella license is needed.
  Resolve identified Glide SDK-header provenance before publishing affected copies.
- Never commit credentials, product keys, Windows images, game media or generated
  guest binaries. Keep 86Box and unrelated Juke assets outside DreamGPU.

Move generic frame, desktop and graphics-error types into DreamGPU, with no
`juke-*` dependency. Preserve device identity, wire layouts, opcodes and guest DLL
exports during extraction. Generate Rust and C-compatible guest definitions from
one canonical protocol description.

SDK contracts include GPU selection, image descriptions, leases, ordered desktop
batches, cursor updates, acknowledgements, reset/fault events and instrumentation.
Use narrow bindgen bindings plus audited wrappers for C interfaces; use cxx where
retained host C++ benefits. Guest/host communication stays the device protocol.

## Rust host and C++23 guest implementation

Port command validation, resource tables, scheduling and platform rendering to
Rust; keep QEMU registration, DMA and lifecycle bindings small. Preserve execution
outside QEMU's global lock, IOSurface on Mac, DMA-BUF on Linux, current GPU-copy
and synchronization semantics, separate complete frames and ordered desktop
operations, bounded queues/resources and sleeping idle workers.

Own contexts, textures, mappings, descriptors, Mach rights and fences explicitly.
Producer allocations cannot be reused before consumer GPU completion. Guest
commands are untrusted: preserve immutable snapshots, overflow checks and DMA
bounds. Rust references must not assert exclusivity over concurrently mutable
guest/QEMU memory.

Pin verified GCC 16.1.1 and existing Open Watcom tooling. Supply a reproducible
Linux guest-builder recipe. Mac builds host components natively and may invoke
the configured Linux guest builder. Use C++23, Pentium III, established user-mode
SSE1 math with SSE2 disabled, existing external floating-point conventions,
controlled runtime/imports, and no guest exceptions/RTTI. Do not introduce kernel
floating-point/SIMD without explicit support and verification.

The C++23 compiler probe passed; actual PE32, NT-driver and Win98 VxD linkage and
execution are early gates, not assumed successes. Retain minimal NE entry points,
segmented-pointer conversions and register thunks; compile flat 32-bit logic with
the modern compiler and validate the VxD linkage.

Require move-only RAII owners, explicit borrowed views, checked lengths,
overflow-safe arithmetic, fallible allocations and bounded containers. Span alone
does not enforce bounds. Handle failed initialization, forced process termination
and driver unload. No Rust/C++ unwinding crosses ABI boundaries.

Run Clang analysis and sanitizer tests on actual maintained code. Fuzz lengths,
offsets, strides, clipping, handles and lifecycle transitions. Use formal contracts
selectively on critical remaining C routines. Document FFI/legacy trusted boundaries.

## Direct3D through WineD3D for Windows

Use the existing pinned Wine9x/WineD3D integration:

```text
Game: DirectDraw / Direct3D 6-9
  -> app-local Windows translation DLLs -> WineD3D
  -> DreamGPU OpenGL frontend -> QEMU device/host renderer
  -> host GPU -> DreamGPU presentation SDK -> Juke
```

Windows translation DLLs execute inside the Windows guest; host Wine is not
required. Preserve accepted Win2000 D3D probes and UT D3D on both hosts without
claiming arbitrary games or Win98/XP support from that evidence.

- Import pinned translator/runtime sources and existing fixes into DreamGPU.
- Build common WineD3D libraries and Win98/NT5 switchers from source.
- Route OpenGL/WGL through the DreamGPU frontend, preserving native DirectDraw
  switching needed by ordinary 2D behavior.
- Deploy DLLs beside supported games with original-file preservation and hashes;
  keep Windows system graphics DLLs unchanged.
- Retain measured GPU front-buffer and read-only mapping fixes.
- Implement missing frontend behavior from deterministic probes/game traces;
  expose accurate capabilities and reject unsupported operations explicitly.
- Keep normal rendering/presentation GPU-backed and genuine CPU reads/writes
  coherent. New integration/helpers use C++23; upstream Wine retains its language.

Run exact Direct3D6/7/8/9 pixel/resource/presentation probes and preserved UT D3D.
Finish Win98 game verification on Linux/Mac, then NT5 package acceptance on an
installed XP fixture when available. Cover windows, clipping, resize, modes,
cleanup and return to desktop. Exclusive-fullscreen game support needs its own
evidence. Record native rejections/errors, readbacks, transfers, waits and cleanup.
Additional titles enter the compatibility list only after exact automated tests;
public D3D8/9 probes do not prove arbitrary D3D8/9 game compatibility.

## DreamGPU naming and public identity

Added September 12 during implementation at the user's request. `dgpu` is the short form for constrained names. DreamGPU must
make sense to a reader who has never used Juke. Migrate maintained source names,
header guards, symbols, driver/device names, build tools, package paths and docs
to DreamGPU. For example, the public transport header guard becomes
`DREAMGPU_TRANSPORT_H`; internal Juke and generic retro-gpu names are not the
new project's public identity. Juke appears as a consumer/integration example.

Do this as one coordinated source and consumer migration after freezing current
validation evidence. Inventory textual ABI separately from unchanged numeric wire
layouts: QEMU device/config names, guest 8.3 DLL and tool filenames, loader paths,
trace/QOM names, package manifests and automation must agree. Update Juke's normal
integration with the new names. Frozen historical artifacts/results keep their
original identities; do not rewrite old measurement records to claim new binaries.
Upstream copyright and attribution retain their original text.

- [x] Map source, header/symbol, device, guest filename, tooling and docs names.
- [x] Rename maintained implementation and canonical generated interfaces.
- [x] Update build/package recipes, probes and Juke consumer configuration.
- [x] Run source/build, diskless GPU and guest deployment checks for new identities.
- [x] Explain standalone usage first and Juke integration separately.

## Execution checklist

- [x] Preserve tracked/untracked/submodule state, source identities and artifacts;
  preserve paused fixtures and evidence.
- [x] Scaffold repository, Cargo workspace, inventories/notices; save this plan.
- [x] Extract working sources/dependencies/automation preserving provenance/ABI.
- [x] Connect Juke locally and unify SDK/native/guest artifact discovery.
- [x] Build independently and exercise standalone viewer/probe harness.
- [x] Prove C++23 DLL, NT and Win98 VxD integration, imports/relocations/floats/ISA.
- [x] Port host validation/ownership, execution, platform and QEMU integration.
- [x] Modernize maintained guest ownership; retain necessary legacy adapters.
- [x] Package WineD3D and app-local deployment with source pins and fixes.
- [x] Preserve NT coverage, finish Win98 Direct3D games and execute XP gates.
- [x] Verify relevant graphics/desktop/lifetime/performance through Juke on both hosts.
- [x] Remove superseded production code and duplicate source/build ownership.
- [x] Document architecture/builds/unsafe boundaries/updates/coverage and link Juke.

### Remaining implementation and acceptance gates

- [x] Rust SDK, native transport/presentation, host 2D planning, GL validation,
  scalar/array execution, process registry and texture lifetime/budgets.
- [x] Remaining typed GL queries, readback/pack state and vector execution in Rust.
- [x] Remaining attributes, framebuffer attachments, clear/copy execution and
  native platform resource ownership in Rust; keep necessary OS/QEMU ABI glue narrow.
- [x] Win98 checked memory policy, immutable copying and pin ownership in C++23.
- [x] Audit/port the remaining maintained flat Win98 logic; document the precise
  segmented/register/interrupt adapters that must retain their legacy ABI.
- [x] Convert OpenGLide/Wine GLU source edits to checked patches and verify the
  pinned guest build and actual-source tests.
- [x] Replace remaining fragile build-time source substitutions with explicit
  checked patches or pinned upstream-fork commits. WineD3D and Win98 variants
  preserve generated source bytes; component builds pass.
- [x] Named Linux NT driver installation/cold activation and OpenGL/D3D7/8/9/Glide,
  window/lifecycle/mode checks; record Half-Life result.
- [x] Equivalent named Mac NT acceptance using the independent updated disk.
- [x] Reliable unattended Unreal foreground acquisition; verify D3D and Glide
  on both hosts after the helper change.
- [x] Named Win98 candidate activation and relevant desktop/OpenGL/Glide/Direct3D
  game checks on Mac/Linux using preserved fixtures.
- [x] Direct3D6 exact-pixel gate on Mac and Linux.
- [x] Installed XP fixture/runtime acceptance.
- [x] Matched workload/desktop/idle checks for the completed port batches; preserve
  accepted evidence and sample any >5% regression before adoption.

Completed acceptance is recorded in [progress.md](progress.md). Game comparisons
retain both the Linux NT first-run slowdown and the unchanged sampled follow-up;
no causal claim is made. Desktop correctness and bounded idle behavior pass. Idle
CPU figures are absolute because no matched historical idle baseline was recorded.
The final package receipt lives at `target/guest/final-v5/receipt.json`; rebuilding
from source remains the supported way to reproduce the package.

Source organization: `guest/` contains only runtime drivers/frontends and shared
contracts, `tools/` contains guest executables, `support/guest/` contains build
inputs, and `tests/guest/` contains driver source checks. No `guest/windows/` layer.

Use subagents with explicit file ownership and one controller per runtime fixture.

## Automation, acceptance and completion

### Follow-through after extraction acceptance

The extraction gates above preserve their recorded results. The broad completed
checklist did not close these more specific gaps; track them independently:

- [ ] Compile the maintained Unreal setup/control tools as C++23 with scoped
  resource ownership and legacy OS import/ISA gates.
- [ ] Arm sampling before guest process execution and stop at the observed result;
  record actual event times and distinguish process lifetime from an unavailable
  exact engine timing interval.
- [ ] Deploy the final packaged frontend to Win98/Win2000 application directories,
  preserving original DLLs/hashes without reinstalling game assets; run targeted
  acceptance of those exact bytes on Mac and Linux.
- [x] Reject benchmark comparisons with known host/workload/instrumentation drift
  or dropped samples; report missing comparison evidence explicitly.
- [ ] Establish matched desktop/idle evidence before claiming preservation of
  those metrics. Existing absolute idle measurements remain valid but do not
  supply a historical matched baseline.

### Measurement rules

Carry serial/QMP request IDs, READY signals, process INSTANCE checks, bounded waits
and owned cleanup into build/probe/benchmark/lifecycle commands taking explicit
manifests and fixture paths. Runs record revisions/dirty identity, binaries,
package/compiler/workload hashes, host/display configuration, measurement
boundaries, results, samples and cleanup. Direct launch/timedemos control games;
screenshots are automatic evidence, never a repeated model-directed menu loop.

Required checks: Mac/Linux native builds and Juke path dependency; malformed
commands/stale handles/reset/bounded queues; OpenGL/Glide/D3D oracles; desktop
clipping/cursor/modes/coherence; failures/process exit/VM shutdown; concurrent
processes/minimize/VM switching; resource counts returning to defined steady state;
Half-Life and UT workloads; matched performance and sleeping idle behavior.

Use accepted measurements when configurations match. A >5% slowdown in a matched
metric requires sampling and resolution before adoption. Engine FPS, native
presents and physical display latency are different metrics.

- One successful run satisfies its gate. Repeat for a change, failure or gap.
- Inspect traces, group justified fixes, then benchmark the changed candidate.
- Do not recreate cancelled baselines or compare against software rendering.
- Launch guests paused, disable NIC through QMP, then resume.
- No builds/unrelated loads during measurement. Preserve original disks and use
  independent fixtures. Never copy a live source disk or mix KVM/TCG snapshots.
- Never gain reported FPS through busy loops, deeper queues or changed emulated
  speed. Accepted implementations become the normal path.

Completion requires independent DreamGPU builds, Juke local consumption, relevant
acceptance for replaced paths and removal of duplicate production implementations.
Use Win2000 as initial reference; resume preserved Win98 instead of reinstalling.
XP runtime requires an installed fixture and remains unverified until it passes.
Carry incomplete compatibility/performance work forward with its actual evidence;
extraction and compilation do not complete missing runtime acceptance.
