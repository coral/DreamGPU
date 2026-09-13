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
- Commit directly on DreamGPU `master` and the shared QEMU fork's `master`; Juke
  stays on `master`. Do not create separate integration/development branches.
  Pin published QEMU commits in DreamGPU's submodule.
- Keep audio in Juke. The shared QEMU fork may retain its existing `juke` audio
  backend; DreamGPU owns graphics, not the consuming application's audio backend.
- Independently authored code uses GPL-2.0-or-later. Preserve upstream licenses,
  SPDX notices and copied-file provenance; see ATTRIBUTION.md.
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

Use the existing pinned Wine9x/WineD3D integration, installed through the
system-provider work below. Earlier application-local packages remain diagnostic
fixtures, not the final installation model:

```text
Game: DirectDraw / Direct3D 6-9
  -> system Windows translation providers -> WineD3D
  -> DreamGPU OpenGL frontend -> QEMU device/host renderer
  -> host GPU -> DreamGPU presentation SDK -> Juke
```

Windows translation DLLs execute inside the Windows guest; host Wine is not
required. Preserve accepted Win2000 D3D probes and UT D3D on both hosts without
claiming arbitrary games or Win98/XP support from that evidence.

- Import pinned translator/runtime sources and existing fixes into DreamGPU.
- Build common WineD3D libraries and Win98/NT5 switchers from source.
- Route OpenGL/WGL through the DreamGPU frontend and verify ordinary DirectDraw
  2D correctness and performance through the same installed provider.
- Install providers once per guest through the owned system transaction, retaining
  original files and exact rollback records. Resolve NT5 protected-runtime loading
  explicitly; application-local DLL deployment does not satisfy this milestone.
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

## System-wide installation (new required product goal)

The user clarified that the deliverable is one **`dreamgpu.exe` installer run
inside the Windows guest**, automatically detecting Windows 98 versus Windows
2000/XP. Cargo builds the two internal OS payloads and the combined executable.
Separate `dreamgpu-9x.exe` / `dreamgpu-nt.exe` may be diagnostic build artifacts;
the normal user flow is install, reboot when required, then launch a game.

The installed DreamGPU device supplies ordinary desktop acceleration plus
OpenGL, Glide and Direct3D through the system interfaces applications normally
load. No game-directory DLL copying, renamed private renderer, patched game
import table, custom `-gldrv` argument, or per-title setup is required for the
normal path. This supersedes the earlier app-local-only deployment policy;
existing app-local artifacts and results remain historical validation evidence.
Automatic API discovery does not imply support for every game's requirements.

The implemented route combines the Windows OpenGL ICD, system Glide2, and
WineD3D-backed DirectDraw/Direct3D6–9 providers. It does not advertise a native
Direct3D HAL. The ICD has all 336 GL1.1 dispatch slots; measured native-provider
limitations remain documented below. The assembled installer has activated all
six normal-loader GPU checks on XP, Windows 2000, and now Windows98 using
combined6382. Complete lifecycle acceptance, normal games across both hosts, and
matched desktop performance remain gates.

- [x] Finish the OS-specific system-loading design: OpenGL ICD/pixel-format
  integration, global Glide discovery, DirectDraw and Direct3D entry points.
  Full production installation now passes all six API pixel gates on98/2000/XP;
  separately listed OpenGL compatibility limits and lifecycle gates remain.
- [x] Implement the required system driver/API integration with the current
  bounded graphics transport and host acceleration.
- [x] Build one legacy-compatible installer with two OS payloads directly through
  Cargo/Rust orchestration and CMake; no Python build recipes.
- [x] Detect the supported Windows version and DreamGPU PCI device; validate
  payloads and prerequisites before modifying the installation.
- [ ] Implement driver activation, reboot completion, upgrade/repair, failure
  rollback and uninstall with explicit ownership of files and registry entries.
- [ ] Preserve existing desktop performance, normal DirectDraw behavior and
  unrelated system components. Restore previously owned installation state on
  failed upgrades or uninstall.
- [ ] Test unattended installation, reboot, upgrade, rollback and removal on
  independent 98/2000/XP fixtures; use machine-readable completion receipts.
- [x] Launch normal OpenGL/Glide/Direct3D programs from clean application
  directories containing no DreamGPU/Wine replacement DLLs. Verify actual loaded
  provider identities and pixels, not merely activity or successful loading.
- [x] Resolve the reported Unreal partial-client rendering/black margins and
  validate the former texture bands on both hosts with the corrected Wine path.
- [ ] Verify relevant APIs and representative games on both Mac and Linux hosts,
  including normal desktop use before/after games and no idle busy looping.

### System installer work breakdown (in progress)

**OpenGL and driver integration**

The ICD has 336 typed non-NULL slots: 146 existing functions and 190 adapters,
with no missing dispatch entries. Four implemented entries retain documented
native-provider limitations. Windows 2000 and Win98 normal system loading pass
basic pixel/swap gates with frozen earlier inputs; final coherent production
registration and installer activation are being integrated. Dispatch coverage
alone is not a formal complete OpenGL conformance claim.

- [x] Implement 39 numeric color/normal/rectangle variants and seven fixed-state
  aliases, with actual frontend sanitizer tests and legacy-target compilation.
- [x] Implement all 24 raster-position variants with typed native execution and
  raster-state queries. Both hosts pass the actual transform/clip/state oracle
  and the 27-test native GPU suite recorded for that batch.
- [x] Implement interleaved arrays and a bounded per-context client stack,
  preserving borrowed pointers and pack/unpack state. Actual-source tests cover
  all 14 interleaved layouts, stack limits and context isolation.
- [x] Finish ArrayElement's edge/index-array dependencies, including typed index
  values and canonical edge flags in a validated extended vertex layout. Actual
  guest-source and both-host native GPU tests pass. The same group implements
  polygon stipple, logic/index state and accumulation API behavior for the
  advertised zero-accumulation-bit RGBA visual; no unsupported visual is invented.
- [x] Implement nine pixel transfer/map operations with typed bounded payloads
  and exact native state queries. Neutralize guest transfer state only around
  internal texture zero initialization, restoring it without borrowing guest
  attribute-stack capacity. Guest transfers retain their intended behavior.
- [x] Complete CopyPixels native/guest integration and both-host color-copy GPU
  gates, preserving signed coordinates, read/draw surfaces, transfer/scissor state
  and FRONT publication. Independent depth/stencil pixel oracles now also pass
  on both hosts, alongside no-color-publication tests.
- [ ] Resolve the Linux Mesa extreme integer INDEX_OFFSET precision failure.
  The typed transport preserves INT_MAX, but Mesa converts it through float
  internally and returns INT_MIN. Preserve the failed oracle and do not invent
  shadow query state that disagrees with drawing. Remaining pixel checks pass
  using an exactly representable offset; full-range conformance stays open.
- [x] Implement and validate Bitmap/DrawPixels bounded immutable image assembly
  with a single committed draw. Guest/host implementation and adversarial tests
  now cover ordering, cancellation, context loss and the shared 64 MiB staging
  limit. Packed DrawPixels expands bits to native byte values within that same
  budget to avoid a reproduced Linux driver hang; this changes packing, not
  rendering. Mac passes all 29 native GPU tests; Linux completes the new typed,
  packing, transfer and depth/stencil assertions but retains the separate native
  fractional-zoom failure below. The 64 KiB packet limit does not split draws.
- [ ] Resolve Linux fractional negative PixelZoom coverage. A direct native EGL
  reproduction without DreamGPU produces the same missing/wrong pixels under
  Mesa 26.1.8; preserve the exact failing oracle and distinguish this provider
  failure from transport acceptance. Mac passes the corresponding pixel check.
- [x] Implement texture residency/priority and 1D image-copy operations. Guest
  adapters pass legacy compilation, sanitizer and clang-tidy checks. Mac and
  Linux pass borderless copy, real priority/residency and existing 2D-copy GPU
  checks. Residency publishes caller output atomically. Native texture metadata
  uses queried dimensions/borders after allocation, with conservative accounting.
- [ ] Resolve Linux legacy 1D texture borders. Direct EGL reproduces Mesa
  accepting width 6/border 1 but reporting width 4/border 0 without an error.
  Mac passes exact border pixels; retain Linux's failed border assertion and
  mark both 1D copy operations partial rather than fabricating border state.
- [x] Implement all 23 evaluator APIs with context-owned, bounded control grids,
  accurate MAX_EVAL_ORDER=8 and typed map/grid/query handling. Native GPU gates,
  103 host tests on both hosts, guest sanitizer and legacy link/analysis pass.
  Valid terminal INT_MAX meshes use widened-index native primitives to avoid
  an opaque native loop overflow; ordinary meshes retain native EvalMesh.
- [x] Complete selection/feedback with retained context-owned native buffers,
  bounded readback and single render-mode transitions. Both host native oracles,
  107 Rust host tests, strict Clippy and guest sanitizer/tidy checks pass.
- [x] Complete the eight display-list operations, including immutable copied
  commands/arrays/images, late-bound nested lists and logical texture ownership.
  Correctly defer ordinary compile-only errors until replay. Final source passes
  113 host tests on each platform, 35 Mac and 33 Linux native tests, guest
  sanitizers, pinned GCC16 and explicit target-aware tidy. The four previously
  recorded native-provider edge cases remain explicit limitations.

- [x] Implement the Windows `Drv*` ICD entry points and the exact 336-slot GL1.1
  dispatch ABI: 146 existing exports plus 190 typed adapters, zero missing slots.
  Keep the four measured native-provider limitations explicit; this is not a
  claim of formal complete OpenGL conformance.
- [x] Validate the ICD adapter's pixel-format/context/thread/sharing/swap behavior
  against actual frontend code. A combined actual-body harness now joins the
  Drv callbacks to the real frontend, covering format adoption, thread ownership,
  namespace sharing/lifetime and failure paths, swap and resize under sanitizers.
  DrvCopyContext remains explicitly unsupported; sharing requires a fresh,
  never-current destination. Production OS loading remains a separate gate.
- [x] Complete NT pixel-format/display DDIs and Win98 `OPENGL_GETINFO` integration,
  then verify native `opengl32.dll`/GDI loading on each supported Windows version.
  Production installers now activate the real system loader on all three guests;
  advanced context/pixel-format compatibility limits remain documented.
- [x] Build checked Win98 diagnostic discovery in an independent source/object
  tree. Actual Watcom compilation verifies the 270-byte Win16 ANSI response and
  far-pointer boundary; reuse the exact production VxD. Real Win98 system-loader
  execution remains separate from this build/ABI gate.
- [x] Complete the automated Win98 normal-system ICD proof with frozen inputs
  and an independent disk. The diagnostic bootstrap verifies the active Win16
  discovery interface, preserves the exact prior registry value, supports
  restoration and refuses foreign provider/key contents. Its actual-source
  sanitizer tests pass. The actual Microsoft system loader now loads the system
  ICD and passes 8,192 exact pixels/two swaps. Win32 ExtEscape with a 270-byte
  ANSI output succeeds; ordinary Escape did not reach the expected thunk. Keep
  this diagnostic registration separate from production installer readiness.
- [x] Verify actual Win98 diagnostic registry restoration and bootstrap disabling
  after the accepted normal-loader run, retaining the exact original value/key
  identity and unrelated entries. One restoration invocation verifies the original
  absent ICD value under the existing key, a durable disabling marker and the
  byte-identical original backup. This does not constitute full driver removal.
- [x] Add a fixed `sysgl` automation route and a normal-loader probe using system
  `opengl32.dll`, GDI pixel-format/swap calls, system `dgpuicd.dll` identity and
  8,192 exact red/green pixel checks. The probe rejects neighboring API DLLs;
  legacy-target compilation passes. Windows 2000 now passes the real system
  loading test: native GDI format 1, system `opengl32.dll`/`dgpuicd.dll`, 8,192
  exact pixels and two swaps. Its actual `OPENGL_GETINFO` request is 532 bytes
  with the name at offset 8; callback evidence confirms user-ICD swaps. This
  closes the basic loader gate, not full GL1.1 coverage or production activation.
- [x] Register system providers and validate ordinary loading without `-gldrv`
  or special DLL names. Normal Half-Life now proves system ICD modules and native
  GPU execution on both hosts and both game fixtures; six API proofs pass on
  all three supported guest versions. Known GL edge limits remain listed above.

**DirectDraw, Direct3D and Glide**

Current implementation work targets one coherent Wine DirectDraw provider for
ordinary 2D and 3D callers, with exact 2D pixels and completed-work performance
tests. Preserving desktop performance does not require constructing a native-to-
Wine COM migration layer: ordinary GDI keeps its existing driver path. Do not
claim native DirectDraw behavior is preserved solely from caller-name routing.

- [x] Harden the Win98 switcher's initialization/publication and native-library
  loading; validate routing with ordinary unknown 2D clients, not only registered
  game names or immediate-caller heuristics.
- [x] Profile and remove the proven Wine CPU-blit overhead. Bounded contiguous
  copies avoid tiny repeated memcpy calls; a current CPU map no longer acquires
  a redundant GL context and repeats Win98 window/DC atom lookups. Actual
  system DirectDraw correctness passes and 1,024 completed blits take 1.390 ms,
  versus 17.107 ms before and 3.197 ms in the frozen native offscreen result.
  Native primary-surface failure remains recorded; this does not imply universal
  desktop parity. Complete exact replacement rollback below.
- [x] Implement and verify the Win98 system installation transaction using the
  preserved native runtime/registration contract identified in the donor source.
  Own the relevant SYSBCKUP copies with their public runtime files: Windows can
  restore a higher-version backup after setup. The old fixture contained a Wine
  DDRAW cache despite its restored native public DLL, explaining the v4 conflict.
  Correct that historical contamination only in a separate staged baseline and
  verify installer-owned cache/public restoration through real cold boots.
  Win98 v5 with6382 now activates all six APIs after its second cold boot;
  all14 captured files match desired, and baseline removal independently
  restores all14 originals plus both driver files, including native cache/public
  DDRAW. Exact-original cache repair is in the next source candidate.
- [x] Implement NT5 system Direct3D activation with owned protected-runtime
  boot transactions. The historical donor installation
  replaces Windows File Protection-managed runtime files and cache copies; a
  durable install must address this explicitly. Actual Windows 2000 cold staging
  now passes normal loaders without changing the original cache or disabling
  protection. Windows 2000 now also passes exact protected-DDRAW deferred
  install/restore through reboot, with one-boot AllowProtectedRenames scoped
  to an otherwise empty queue; both queue and permission are consumed.
  XP now passes the same exact protected-DDRAW install/restore mechanism,
  with queue/permission consumed and original bytes restored. Integrate durable
  production multi-file queue recovery/cancellation is implemented. Full installer
  activation passes all six normal-loader probes on Windows2000 and XP; XP
  baseline removal/reinstall and corrected-executor recovery have exact-original
  audits. The latest composed Windows2000 upgrade/rollback/removal gate remains
  tracked separately. No native Direct3D HAL is advertised.
- [x] Test an NT5 named-image loader route before adopting it: a temporary owned
  service publishes only a synthetic marker DLL section, with a distinct synthetic
  system-directory original. Check ordinary imports, bare/absolute LoadLibrary,
  then section/service removal and original-provider restoration. No Microsoft
  runtime or KnownDLL registry entry is changed by this feasibility probe. Only
  actual loader results can justify using the route for WineD3D providers.
  Windows 2000 uses the alternate image for imports and bare LoadLibrary but
  bypasses it for an absolute system path. Cleanup restores original loading.
  This route is rejected as complete system integration; no XP support is inferred.
- [x] Test ordinary system DirectDraw and Direct3D6/7/8/9 loader paths
  with no private providers beside the executable. The normal 2D/3D provider
  is Wine, with preserved original-runtime ownership rather than COM migration.
  The four shared C++23 pixel probes now build as normal-loader variants,
  verifying actual COM vtable ownership and system dependency locations; their
  serial routes and source-policy checks pass. Actual Win98 normal system
  loading now passes all four versions: D3D6 checks 1,024 target/front pixels,
  D3D7 checks 2,560, and D3D8/9 each check 512 GPU pixels plus presentation.
  Windows 2000 also passes all four normal-loader probes after cold staging,
  with protected DDRAW candidate identity intact, SFCDisable=0 and unchanged
  original DLL cache. XP also passes all four normal-loader probes. The protected-DDRAW
  online deferred-rename mechanism passes install/restore on both NT versions;
  complete installer-owned multi-file activation now passes on all three guest
  versions. Current6382 XP baseline uninstall and fresh reinstall complete two
  clean boots each; both exact final-state audits pass.
- [x] Install the supported Glide2 provider globally with collision/ownership
  checks. Full installation now passes the system Glide2 pixel oracle on all
  three guests, and baseline uninstall restores its captured original. Current
  Glide2 results do not establish other Glide APIs.
- [x] Inventory Glide1/Glide3 source, ABI/build differences and missing behavior
  separately in `docs/glide-support.md`. These remain explicit compatibility
  extensions; the current system installer must not advertise them.
- [x] Build shared C++23 Glide probe code for the existing diagnostic and a normal
  system-loader `DGSYSGR.EXE` test; require clean executable/current directories
  and verified actual system Glide/OpenGL dependency paths. Legacy-target builds
  and actual loader-policy sanitizer tests pass. One independent Windows 2000
  run also passes normal system loading and exact pixels, with no neighboring
  providers; installer-driven registration/removal and other OS gates remain open.

**Build and installer implementation**

- [x] Implement a separate durable driver-binding journal: capture exact prior
  compatible INF node, bind through existing checked helpers, own RunOnce
  continuation, verify started device/channel after reboot, and restore prior
  binding and owned files. Cover existing DreamGPU upgrades and stock Microsoft
  VGA as distinct required cases; untouched stock files remain verified in place.
  Driver verification remains distinct from complete OpenGL/Glide/Direct3D
  installation success. Windows 2000 now passes binding, post-reboot device
  verification, restoration and post-reboot verification of its exact prior INF
  and driver files. Win98 now passes the full install/verify/restore/verify
  cycle too, including local source-media resolution and same-INF pending
  replacement reconciliation. Stock/unbound VGA remains open. The NT first-install path now records an
  explicit unbound state in a byte-compatible V2 journal (V1 stock/owned records
  remain readable), checks actual binding/service/key absence, and refreshes
  SetupAPI device data after removal. Sanitizers cover absence versus registry
  errors and adapter identity changes, in addition to 224 existing mutation
  boundaries. The untouched XP UTM first-install fixture now passes binding,
  cold activation and cold return to its exact unbound baseline, including owned
  OEM INF/PNF and service removal. XP normalizes a service marked for deletion to
  disabled; accept that only inside the already-owned deleting phase. The final
  classifier change is source-tested and will receive coherent installer coverage.
  Do not invent a prior VGA INF or delete borrowed packages.

- [x] Add a PE4 C++23 bootstrap, built by Cargo/Rust/CMake, embedding both audited
  OS packages. Detect Windows 98 versus 32-bit 2000/XP and PCI `1234:1113`.
  Default installation now sequences the driver and system providers, and reports
  success only after all six normal-loader proofs. The earlier foundation's
  `provider_not_ready` behavior remains historical evidence, not current behavior.
- [x] Add bounded payload parsing and integrity checks, owned file/registry state,
  transaction journal, activation/reboot continuation and machine-readable receipts.
- [x] Implement bounded, checksummed file/registry journaling, immutable originals,
  interrupted-write reconciliation and conflict detection. Actual Win32 adapter
  tests cover 108 injected syscall failure boundaries. One Windows 2000 staging
  and rollback run preserves 12 global DLL hashes and four ICD registry values;
  this validates lifecycle mechanics, not driver activation.
- [x] Extend the fixed lifecycle probe to all three supported OS versions and
  the Win98 driver pair. Actual Windows 98 execution caught an unavailable
  SetupAPI import before startup; resolve it from CfgMgr32 and enforce that
  module/function pairing in the Cargo audit. The corrected single Win98 run
  passes default refusal, staging and rollback with 14 global files and the
  OS-specific ICD registry value unchanged. The same corrected installer/helper
  also passes XP: 14 files and four ICD values unchanged. Its actual All Users
  Startup controller copy is now updated by the fixture manifest; replacing only
  the root copy had left an older process holding COM1. Full initial XP activation
  now passes all six system probes through the installer. Upgrade, repair and
  complete removal acceptance remain separate gates.
- [ ] Complete repair, upgrade, interrupted-install recovery and uninstall;
  include baseline uninstall after a completed upgrade rollback: restored parent
  bytes remain installed even though the failed target journal says failed.
  Preserve first originals and authenticate the restored state; do not relabel
  the failed generation or require another install merely to uninstall.

  retain original unrelated files and restore owned prior state correctly.
  The top-level intent coordinator now sequences driver then providers for
  installation, and providers then driver for removal. Its independent global
  RunOnce closes gaps between component continuations; durable epochs distinguish
  full installation from driver-only diagnostics. Changed-driver generations,
  baseline versus immediate-parent restoration, reinstall-after-removal, known
  original-runtime repair, initial extraction recovery/cancellation and durable
  provider preparation are now implemented and fault-tested. The current combined
  candidate is frozen at `target/follow-through/system-installer-current-v1/inputs`
  (SHA256 `8367af7279f26c821e4082b86fbedca6b2e2dc8a32a6ba3d91d566cff4151715`).
  XP now passes the new candidate upgrade through one clean cold boot, advancing
  GLOBAL epoch/provider generation1 to2. Repair, rollback and removal of this
  upgraded installation are being checked on independent copies. XP's first uninstall exposed a
  stale removed-device query while its service awaited kernel unload; the current
  candidate fixes that classification. Preserve the original failure receipt.
- [x] Present readable installer completion/error messages and a normal Windows
  restart offer. Keep /silent machine-readable receipts and ensure a successful
  status requires the driver plus all six ordinary-loader GPU checks. UI code is
  implemented and passes the combined legacy compile/analysis gate. Automated
  /silent execution is accepted on XP; interactive UI remains a manual smoke gate.
- [x] Correct recovery dispatch so an owned rollback/removal can resume even
  while new providers are unready. Authenticate/load the journal before routing
  continuation; keep installation and upgrade gated. Actual Win32 gateway tests
  cover both OS families and 68 injected recovery syscall failures, including
  blocked installation paths with zero mutations and corrupt-receipt refusal.
- [x] Gate installer success on driver activation and six ordinary-loader GPU
  proofs. Full installation passes on Windows98/2000/XP; staging alone cannot
  report complete activation. Exact GL compatibility limits remain explicit.
- [ ] Test install/boot/normal games/desktop/uninstall on independent fixtures,
  including wrong OS/device, missing files, corrupt payloads and interrupted writes.
  Fixed serial `sysinstall`, `sysresume`, `sysupgrade`, `sysrollback`, `sysremove`
  operations run hash-pinned helpers and record the checksummed global phase.
  `scripts/fixtures/system-install.py` sequences one operation across a bounded
  number of clean cold boots, cloning stopped private disks and retaining every
  receipt. A pending reboot is not an activation result. Win98 startup now uses an owned
  persistent Run entry while NT retains RunOnce, preventing Win98 from executing
  re-armed continuations repeatedly in the same startup. The changed Win98
  candidate (`519c3e42...`) is source-tested and compiled; its fresh baseline
  runtime gate is underway. Finish actual acceptance
  using this loop, without screenshot/menu navigation.

**Outstanding rendering and verification**

- [x] Convert the maintained Half-Life benchmark launcher from its historical
  app-local minidriver configuration to the game's ordinary system OpenGL mode.
  Verify this retail build's default driver selector before changing its persisted
  renderer setting. Remove only hash-identified fixture-owned old private DLLs;
  do not replace them with a system DLL copied into the game directory. Preserve
  existing receipts as historical evidence and record actual system ICD loading
  for the new normal-game run. The selector is proven from the actual retail
  launcher: literal `default` selects system `opengl32.dll`; nondefault names get
  a `gldrv\` prefix. Both NT/FAT stagers now support hash-bound removal of owned
  app DLLs. The normal UT helpers also reject neighboring providers and require
  actual system module paths; their setup no longer recopies private providers.
  Source/build gates pass; the normal-game runtime gate remains below.
- [x] Run Half-Life and both UT renderers through normal system loading on
  installed Win98/Windows2000 fixtures and both hosts. Hardware-path Half-Life
  and full-client UT rendering pass. The Win98 Glide graceful-exit and subsequent
  guest shutdown gates remain open separately below; forced cleanup is not a
  successful engine exit.

- [x] Fix the traced Unreal client-area clipping: Wine's `ORM_BACKBUFFER`
  fallback draws a desktop-sized 1024×768 intermediate into a physical 640×480
  backbuffer, truncating it before the correctly translated final 640×480 blit.
  Native export dimensions and final viewport are correct. Provide correctly
  sized offscreen storage; preserve required screen/client translation. Require
  a full-client pixel/geometry check after the fix. The first trace exceeded the
  harness artifact limit and establishes diagnosis, not benchmark acceptance.
  The checked Wine patch now routes oversized offscreen blits through the existing
  framebuffer-to-texture copy. Actual-source sanitizer tests verify 1,048,576
  primary texels, and both OS packages build. A new D3D7 probe checks distinct
  far-edge colors in both render-target and GDI front-buffer pixels. On Mac and
  Linux it passes all 2,560 pixel checks; one corrected UT run per host fills the
  full 640×480 client with zero native GL rejections. No matched FPS claim.
- [x] Reassess the recorded D3D edge bands with the coordinate fix. The corrected
  Linux UT image has no prior right/bottom bands; distinct-color front-buffer
  edge checks pass. Both hosts retain full-client screenshots.
- [x] Finish current Mac application-package acceptance. New system-installed
  Win98 passes seven ordinary API/2D probes plus Half-Life and both Unreal paths;
  Mac NT normal Glide and D3D also pass. These later receipts close the historical
  held-Shift launch failure without treating that failure as a game result.
  Exact installer lifecycle/final-candidate acceptance remains separate.
- [x] Rebuild the canonical Linux Juke executable with the current DreamGPU
  dependency after measurement. Release build passes in 27.45s and reports the
  correct QEMU path; SHA256 is recorded in the build receipt. Earlier runtime
  results still identify the frozen executable actually used for each test.
- [x] Collect matched desktop/idle captures from the same clean guest bytes,
  workload, probe, host display and instrumentation. Use the accepted post-port
  native runtime as baseline; do not recreate cancelled pre-extraction baselines.
  Both runs have 16 acknowledged batches per desktop scenario, zero dropped
  trace records and verified 240 Hz. Current fill/scroll/repaint means are higher;
  this is evidence to investigate, not a desktop performance acceptance.
- [ ] Resolve the desktop performance signal before claiming parity. Candidate
  QEMU/Juke CPU stack captures complete; sampling perturbed acknowledgement counts,
  so their timings are not performance evidence. The existing scalar fill loop is
  a concrete hotspot, but its instructions match the accepted baseline and do not
  establish the cause of the earlier delta. Split validated pixel-size cases into
  constant-stride loops; Mac assembly now uses vector stores and all 39 QEMU fill
  boundary cases pass. The changed candidate reduces mean fill-batch latency
  from 26.14 to 10.61 ms, with all acknowledgements and no drops. Other desktop
  distributions remain mixed; no universal parity claim. Linux native build and
  all 39 boundary cases also pass. Preserve saved measurements; do not repeat them.
  The current-native before/after system-installation pair has all16 workload
  acknowledgements, zero dropped records and240Hz, but higher copy/scroll/repaint
  means and idle CPU after installation. Sampling shows sleeping virtual CPUs
  and active VGA polling repeatedly resetting TLB dirty state for empty snapshots.
  Implement and verify skipping that reset only when no dirty bits were cleared;
  retain hardware dirty-log clearing and all actual dirty updates. The narrow
  change passes actual-body atomic/bitmap tests and builds on both hosts;
  Final changed native91a7Mac/b4c2Linux preserves real dirty updates and also
  avoids empty-snapshot TCG rendezvous. Both builds, actual-source sanitizer
  gates, independent race review and mixed native GPU checks pass. Installed
  idle CPU fell28.35%→5.68%; the matched final-native pair is recorded below.
- [ ] Rebuild and run final relevant formatting, Clippy, target-aware native/guest
  checks after these changes settle. Carry exact payload hashes into acceptance;
  do not relabel older results as evidence for rebuilt binaries.

Installer acceptance is independent of the earlier extracted-package acceptance.
The reported Unreal clipping is now closed by the corrected candidate's explicit
client-edge checks on both hosts. Older whole-attempt activity oracles remain
historical evidence and do not independently establish visual correctness.

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

- [x] Compile the maintained Unreal setup/control tools as C++23 with scoped
  resource ownership and legacy OS import/ISA gates. Restored final PE4/API/scalar
  checks in Rust; all five existing helper binaries and actual-source tests pass.
- [x] Arm sampling before guest process execution and stop at the observed result;
  record actual event times and distinguish process lifetime from an unavailable
  exact engine timing interval. One real Linux capture verifies profiler ACK order
  and 1,263 event timestamps; two preflight failures aborted while suspended.
- [ ] Deploy the final package through the system installer on Win98/Win2000/XP,
  preserving original system files and removing only fixture-owned old app-local
  overrides. Run targeted ordinary-loader/game acceptance of those exact bytes
  on Mac and Linux without reinstalling game assets.
- [x] Reject benchmark comparisons with known host/workload/instrumentation drift
  or dropped samples; report missing comparison evidence explicitly.
- [ ] Establish matched desktop/idle evidence before claiming preservation of
  those metrics. Compare the same current native/Juke/probe and Windows 2000
  baseline before versus after system-provider installation. Retain the earlier
  native-version comparison separately; do not recreate cancelled historical
  baselines or attribute source/config drift to the installer.

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
Installed XP and Windows 2000 now have complete initial system-installer/API
activation evidence. Current-generation upgrade/repair/removal and Win98 complete
activation retain their separate outstanding gates.
Carry incomplete compatibility/performance work forward with its actual evidence;
extraction and compilation do not complete missing runtime acceptance.

### Active lifecycle follow-through (2026-09-13)

- [x] Complete Win98 clean game exit and shutdown on both hosts. The corrected
  original Half-Life → UT Direct3D → UT Glide sequence passes on Mac and Linux,
  including no-force guest shutdown. Preserve the historical paused fault disks;
  they are not clean sources. Ledgers: win98-graceful-exit-v1/history.json and
  macos-history-v2.json.
- [x] Correct UT automation: authenticate the loaded retail Window.dll and owned
  WLog, request the engine's File/Exit command, bound graceful exit and report
  forced termination as failure. Host deadline exceeds the guest cleanup bound.
  Exact-path helper preflight prevents accidentally running an older game-local tool.
- [x] Build and exercise the shared PE4 shutdown helper on Win98 and NT.
- [ ] Complete final coherent installer recovery and the composed
  upgrade → rollback → baseline uninstall workflow, with exact-original audits.
- [x] Record the pre-install desktop counterpart on the same final native91a7
  as the saved installed run. All16 acknowledgements and zero dropped records
  at240Hz; clean shutdown recorded. Evidence: dirty-fence-v1/matched-final-native.json.
  Before→installed means in ms: fill7.84→8.83, copy9.59→10.28, scroll9.86→10.23,
  repaint16.81→16.97. Idle QEMU core%5.18→5.68. This closes identity matching,
  not universal performance parity or the remaining fill/copy distribution gap.

- [ ] Exercise the interactive installer through bounded owned-dialog automation
  as well as /silent. The new sysui helper authenticates the installer, runs its
  fixed /continue flow, reads only its standard dialog controls, declines restart
  using verified default IDNO, and compares the actual process exit with the
  displayed result. Foreign/reused windows and unknown dialogs fail closed.
  Twelve actual-adapter ownership/lifetime/outcome sanitizer cases pass; target
  build and target-aware analysis pass. The actual W2K success dialog is observed;
  automated control dismissal failed and is under diagnosis, not yet accepted.
