# DreamGPU execution ledger

Started September 12, 2026. Plan: [plan.md](plan.md).

## Cargo/C++ housekeeping completed (September 12)

The runtime work was paused for the requested build and repository cleanup.

- Cargo `build.rs` now owns native QEMU and guest build sequencing through
  `crates/dreamgpu-build`. `DREAMGPU_BUILD=all cargo build --release` builds
  the host components and both Windows guest packages. Source initialization,
  checked patch preparation, compiler checks, binary audits and packaging are
  Rust; guest compilation is CMake/Make/Watcom. The dependency-free host engine
  is compiled directly by `rustc` in Meson, avoiding recursive Cargo locking.
  Upstream QEMU still requires Meson/Python; no DreamGPU Python recipe is on the
  build path. Juke consumes native Cargo metadata, including custom output paths.
- The full Cargo command passes on the Mac with an ignored local Framework SSH
  builder setting. Native Cargo builds pass on both hosts. Guest outputs are
  `target/guest/packages/win98` and `windows2000-xp`; the six application DLLs
  match byte-for-byte across both OS packages. Driver, runtime, switcher, tool
  and license files are separated. Payload manifests and runner identities work
  with existing fixture automation without treating old acceptance as new proof.
- Formatted all 177 maintained C/C++ files, including headers, test harnesses
  and the explicit QEMU file allowlist. Actual-target clang-tidy passes for NT,
  flat Win98 policy/tools, native QEMU on both hosts, maintained Wine changes,
  and generated/maintained Glide glue. Real segmented Watcom boundaries retain
  their actual compiler/link checks and NE/LE audits; instruction-sensitive
  assembly regions are narrowly protected from line-merging by clang-format.
- Fixed analyzer findings in bounds, casts and source-less Wine color blits;
  repaired a C++ conversion and removed an unintended dynamic compiler-runtime
  dependency from a diagnostic. Preserved exact donor/patch/output identities,
  including significant CRLF patch bytes. No broad guest warning suppressions.
- Removed 16 superseded Python build recipes. Test-only donor patch utilities
  moved under `tests/guest/support`; reusable runtime/evidence automation stays
  in `scripts/`. `scripts/fixtures/tool-media.py` packages an already-built, hash-checked
  tool without invoking a compiler. Audio-only diagnostics moved to Juke.
- Strict workspace Clippy passes for DreamGPU on macOS/Linux and Juke on macOS.
  Validation passed: 71 host +41 SDK unit tests; 8 build/manifest/audit tests;
  26 native GPU tests per host; 163 host automation tests; 27 guest source suites;
  14 tool suites; 3 reusable C/C++ checker tests; 32 focused Juke tests. Actual
  pinned source preparation and all four Win98 patch variants also pass.

Evidence is under `target/housekeeping/`, `target/housekeeping-cpp/run.json`,
`target/housekeeping-final-linux-run.json` and
`target/validation/cargo-guest-build/`. See [build commands](build.md),
[C/C++ quality checks](cpp-quality.md), and [script inventory](../scripts/README.md).
The new guest binaries have build/static-analysis validation. No new game FPS
or guest runtime acceptance is claimed; the paused fixtures remain available.

## Completed source/build work

- Flattened the Windows runtime into `guest/`, separated guest executables into
  `tools/`, and moved compiler/source pins and build support into `support/guest/`.
  Driver/frontend source tests now live in `tests/guest/`. All 77 runtime
  driver/header/license files are byte-identical to their pre-move versions
  (`target/layout-runtime-source-comparison.json`). The 144 controller tests and
  Mac-compatible relocated source tests pass, including the four Glide tests.
  The three Linux-only i686 packing/vector checks and full isolated guest build
  also passed. Historical frozen evidence paths retain
  their original names; moving source does not create new runtime acceptance.

- Saved the agreed plan, including WineD3D for Windows. Initialized public
  coral/DreamGPU. Canonical QEMU fork: anderstorpsfestivalen/qemu;
  original Juke changes preserved as 2bd4c9964d and merged into the same history.
  All native development now goes directly onto the fork's `master` branch;
  consolidated DreamGPU naming commit: fc33a30398. The superseded development
  branches were removed after verifying their commits are ancestors of master.
- Preserved original source hashes and binary diffs under target/import-evidence.
  Original QEMU working changes are preserved in master history as import commit
  c817462ced0790be641029e4b6e9c19d73cc9a66 (same tree as 2bd4c9964d).
- Registered commit-pinned guest dependencies as DreamGPU submodules.
- Extracted generic frame/cursor/desktop/performance contracts, native GPU
  transport, native texture import/composition and shared-memory display/input
  into Rust DreamGPU. Juke uses compatibility adapters and a local path dependency.
- Added standalone viewer and moved diskless native acceptance into DreamGPU.
- Moved QEMU build ownership to scripts/native-build.py; Juke resolves the source
  through Cargo DEP_DREAMGPU_ROOT. All four Mac QEMU architectures built with the
  initial Rust 2D/desktop/cursor validation/execution replacement.
- Imported reusable serial/QMP/benchmark/lifecycle tools; 117 controller tests pass after full tool/test extraction. Old Juke
  command entry points forward through the Cargo-selected checkout.

## Accepted checks so far

- Mac SDK unit tests and native Metal/CGL/desktop/cursor tests passed.
- Linux SDK unit tests and native GPU desktop/cursor/readback tests passed.
- Standalone viewer synthetic presentation passed on Apple M4 Max and Radeon 8060S.
- Juke core/render and QEMU test compilation passed after the first extraction;
  Linux full workspace/all-target compilation passed before the later shmem move.
- Complete C++23 OpenGL frontend and NT driver pair linked with audited legacy
  imports. The Win98 VxD links GCC C++23 packet/result validators into its LE image.
  Ownership/fault-injection, sanitizer and float-bit packing checks passed.
- Corrected Rust executor: 26 diskless QEMU graphics tests passed on Mac (3.67s)
  and Linux (4.11s), including textures, queries, desktop coherence and lifecycle.
- Normal Juke release build passed on Mac through Cargo checkout discovery;
  Linux release build passed with the corresponding local DreamGPU dependency.
- Frozen five-component C++23 guest candidate assembled with hashed license notices:
  target/retro-guest/dreamgpu-cpp23-v1.recipe.json. Package identity
  5022eb1e34a3cefff1460ea1bbfabc7a71954ece6968149241dc69203a543785.
- Archived and hash-verified all 208 original Juke guest source files before
  source ownership cleanup: target/import-evidence/juke-guest-original.tar.gz.
- Full Wine9x source build passed in an isolated Framework build tree.

## Final acceptance and limits

The Rust host port and C++23 guest modernization pass the planned named-guest
acceptance on Mac and Linux. The original XP UTM is unchanged; Juke's importer
created the independent installed source used for XP validation.

| Guest | Mac | Linux |
| --- | --- | --- |
| Windows 98 | 11 activation/graphics/desktop probes; UT D3D and Glide pass | Same 11 probes and both UT renderers pass |
| Windows 2000 | 10 probes; UT D3D and Glide pass | Same 10 probes and both UT renderers pass |
| Windows XP SP3 | 10 probes; UT D3D and Glide pass | Same 10 probes and both UT renderers pass |

The probe sets cover OpenGL, Glide, Direct3D 6–9, windows, modes and resource
lifecycle. All 62 probe results and 12 UT results are checked in
`target/validation/final-acceptance/run.json`. Mac final fixtures are under
`target/fixtures/mac-dgpu-{v5,win98-v5,xp-v5}`; Linux ledgers are under
`target/validation/linux-dgpu-final` and `target/validation/xp-final-v5`.
Original and historical paused fixtures, failed attempts and binary manifests
remain preserved.

Final native checks: 71 Rust host tests, all four Mac QEMU targets, and all 26
native GPU gates on each host pass. The controller suite passes 149 tests.
The XP resize correction also passes actual frontend sanitizer/static-analysis,
pinned import/build and independent ownership-review gates.

Mac Half-Life reports 81.253 FPS on Win2000 (recorded baseline 78.184) and
80.351 on Win98 (baseline 76.069). Linux Win98 reports 129.150 FPS. Linux NT's
first run reports 184.325 versus baseline 206.082; its regression-triggered
sample reports 219.057 without a code change. Both remain recorded; no supported
causal attribution or universal speedup is claimed. The sample and its timing
limits are in `target/validation/linux-nt-v5-perf-analysis`.

Idle captures show bounded maintenance with no redraw loop. Juke uses roughly
0.3–0.7% of one core across the recorded hosts/guests. These are absolute checks;
no historical matched idle baseline exists. Unreal GPU receipt counts are not
engine FPS or input-to-display latency. Tested titles and API probes do not
establish arbitrary game compatibility or general exclusive-fullscreen support.

Detailed source evidence lives in docs/guest-cpp23.md and native build/test
logs under target/. Historical Juke results retain their original artifact IDs.

## Frozen C++23-v1 guest acceptance

Both hosts cold-booted the same independently installed NT disk with exact file
readback hashes; networking was disabled before guest execution. The actual
C++23 runner identity was verified before probes.

- Linux: OpenGL arrays, D3D7/8/9, Glide, lifecycle and display modes pass. Half-Life
  jrgperf reports 199.062 engine FPS. UT Direct3D/Glide pass; zero native GL
  rejections. Dual-process minimize/restore pixels and post-exit resource
  checkpoint pass. These are observed results, not an unmatched speedup claim.
- Mac: same seven probes pass. Half-Life jrgperf reports 82.209 engine FPS.
  UT initializes and precaches, then the harness rejects missing guest foreground
  ownership; clean game exit and zero native GL rejections. Focus automation repair
  is pending; this is not a passing UT game result.
- Active Desktop Recovery was already visible in the original Linux fixture’s
  pre-migration dual-process screenshot. Scoped desktop registry values in the
  original and cold-installed source disks are identical. Evidence under
  target/validation/active-desktop-*; no new graphics fault inferred from that page.
- A pinned Fedora/MinGW/Watcom container built all five components plus runner and
  installer without mounting Juke or the host compiler; package identity
  25e8c6df631bfe3401cf8586b1f2b7298a0aa8bbe8c27a4a2f7ac376ca8d934e.
  This later candidate adds C++23 Win98 ownership state and still needs Win98
  runtime acceptance. Frozen v1 artifacts remain unchanged.

## Shared QEMU fork and audio boundary

- DreamGPU develops on `main`; its shared QEMU fork develops on `master`.
  The submodule pins the exact published commit, with no separate integration branch.
- Audio remains Juke-owned. QEMU's original Juke audio implementation and the
  consumer's `juke` audio arguments/layout names were restored together.
  DreamGPU exposes graphics device `dreamgpu` and display `dreamgpu-shmem`.
- After that correction, all four Mac native targets built and all 26 diskless
  graphics tests passed (3.24 seconds); audio discovery lists `juke`, not `dreamgpu`.

- Published SDK fresh-clone check: 40 library tests passed without initializing
  submodules or checking out Juke (`target/dreamgpu-fresh-sdk.log`).
- Final named Juke release builds passed on Mac and Linux. The WebSocket visibility
  regression passed on Mac; all 144 automation controller tests passed. Linux native
  graphics tests passed (26, 4.30 seconds); the subsequent audio-only rebuild
  restored `juke` in audio discovery before guest deployment.

## Runtime-only guest source layout

- Flattened `guest/windows/` to runtime-only `guest/{nt,win9x,opengl,glide,d3d,include}`.
  Guest installers, probes and automation executables live in `tools/`; compiler
  pins, dependency locks and frontend build helpers live in `support/guest/`.
  Source tests moved to `tests/guest/`; host entrypoints remain in `scripts/`.
- All 77 runtime driver/header/license files are byte-identical to their previous
  locations. The source move does not justify repeating accepted game baselines.
- 144 controller tests and 28 relocated source/tool tests passed on Mac. All three
  i686 packing/vector execution tests passed on Linux. The complete isolated pinned
  guest build and checked package recipe passed: package identity
  `434941268e551727e2e95091345f069605f8fc7cf8f2c8ade7b0d58c0ab7a58d`.

## Renamed Linux NT driver acceptance

An independent offline-installed disk booted the matched named native runtime with
networking disabled before execution. The source updater completed, Windows powered
off cleanly, and a separate cold fixture verified actual activation: PCI device
started, configuration-manager problem 0, service `dgpumini`, display `dgpudisp`,
1024×768×32. Evidence: `target/validation/linux-dgpu-v3/`.

- Arrays, Direct3D7, Glide, window coherence, lifecycle and mode probes pass.
- Half-Life reports 206.082 engine FPS (379 frames / 1.839 seconds); this is a
  recorded result, not a matched performance comparison.
- Direct3D8 initially stopped before drawing on a stale helper assertion for
  `opengl32.dll` despite loading `dgpugl.dll`. Corrected Direct3D8/9 helpers were
  installed through the fixed serial updater; both exact-pixel gates now pass.
  Runtime drivers and the frozen package remained unchanged; no reboot was needed.
- Unreal initialized and produced native GPU frames with zero GL rejections, but
  the automation could not acquire guest foreground ownership from Explorer.
  This initial failure is superseded by the acknowledged-input v7 acceptance below.
- The owned fixture is paused, and original fixtures remain untouched.

## Active implementation batches

- Rust host: finish framebuffer/copy/readback resource ownership, then export and
  QEMU integration ownership, retaining narrow platform ABI calls.
- Win98: activate the final C++23 policy candidate in a private fixture, then run
  desktop and game coverage on both hosts.
- Build provenance: replace remaining source-string transformations with checked
  source patches; preserve generated runtime semantics.
- XP: the user supplied an installed UTM bundle; Juke imported a hash-identical
  private copy after its newer bundle-format support was corrected. Independent
  XP package activation is now being prepared on Linux.

## Continued port and automation acceptance

- Rust typed queries/vector execution: 31 source tests and all 26 actual QEMU GPU
  gates pass on Mac (3.39s) and Linux (4.06s). Query results use raw writes into
  caller output storage; pack-state and error handling survive failed readbacks.
- Rust context/attribute ownership: 35 source tests and 26 Mac native gates pass
  (3.21s) and Linux (4.06s). Rust now owns transactional texture namespace
  setup/teardown, deleted-but-bound/stacked refs and the attribute stack.
- Win98 memory, cursor, GL/window and 2D policy now use bounded freestanding
  C++23 owners. Actual-source sanitizers/static analysis pass on both hosts, and
  normal/diagnostic Watcom/MinGW LE/NE builds pass. Required segmented, VMM, IRQ
  and register adapters remain in C. The final VxD candidate is
  `ba444373ec8bca5064afac00a2ed25beb0692409c60e99dc2e514ac322960633`;
  one private driver installation is in progress. This is not runtime acceptance.
- Mac cold activation and named OpenGL/D3D6/7/8/9/Glide, window/lifecycle/mode
  probes pass. Half-Life: 78.184 engine FPS (379 / 4.848s), recorded without an
  unmatched speedup claim.
- UT foreground helper v7 acquires acknowledged input before launching the game,
  then grants the exact child foreground rights. Both D3D and Glide now pass on
  Linux and Mac; no screenshots or global foreground-policy changes control it.
  Linux validates input acknowledgments and zero GL rejects/dropped capture samples;
  Mac evidence is under `target/fixtures/mac-dgpu-v3/{utd3d,utglide}-focus-v7/`.
  A Mac D3D sample is recorded with its measurement-cost/interval metadata.
- Found a cleanly stopped Win98 source with installed Half-Life and audio/VMM
  prerequisites; private successor prepared while the original paused fixture
  remains untouched. Win98-specific SetupAPI install/activation tools are being
  verified against the actual OS export tables before installing the new candidate.

## Checked upstream source patches

- OpenGLide and its Wine GLU input now use a reviewed patch plus per-file input/
  output SHA-256 identities. Drift or an unrecorded changed file fails before the
  generated source tree is updated. The texture pool includes its maintained
  runtime source directly; the Python string-patching implementation was removed.
- All four actual-source Glide sanitizer tests and the full pinned Linux guest
  build pass. Six patch-contract tests cover drift, output mismatch and path
  escape. Runtime source semantics are preserved; this build-recipe change does
  not justify repeating unchanged game measurements.
- WineD3D and Win98 transformations are also converted and source-equivalent.
  The remaining substitutions generate compiler configuration or normalize recorded
  line endings; maintained source changes are explicit checked patches.

- Rust drawable/copy/readback resource ownership: 39 Rust tests and 26 native GPU
  gates pass on Mac (3.37s) and Linux (3.96s). Allocation rollback, pack-state
  restoration, zero-fill tile bounds and cached pixel/version behavior are covered.
- Rust export transactions: 41 Rust tests and all 26 Mac native gates pass (3.53s);
  Linux also passes all 26 gates (3.91s). Attachment/blit/state rollback and bounded fence
  polling now live in Rust; OS image/fence calls retain narrow adapters.
- Win98 source preparation now uses explicit base/debug/identity patches. All
  four variants produce exactly the previous 167 generated files each; the pinned
  component build passes, producing the exact same VxD SHA-256 as the frozen
  final policy candidate. Wine's normal/diagnostic source equivalence also
  passes (196 normal / 197 diagnostic files), and its full pinned component build
  passes all ten DLL/import/license gates. Generic patch tests now total
  seven, including explicitly recorded CRLF normalization.
- Mac UT D3D sample: the render worker was in its condition-variable wait for
  1142/1171 captured thread samples while CPU0 was mostly executing TCG. These
  thread sample counts locate activity; they are not GPU utilization or engine FPS.
  The accepted NT fixture is paused between tests.

## Win98 cold activation and remaining acceptance

- The final C++23 driver/build-source batch is published on DreamGPU `main` as
  `2047aa1`. QEMU host ports are still a separate active batch.
- Win98 SetupAPI initially exposed an OS difference in HardwareID property type
  and reported size. The installer now selects the exact present device through
  bounded `CM_Get_Device_IDA`; SetupAPI registration passes without touching any
  other device. A clean shutdown and separate cold boot confirm problem 0,
  `DN_STARTED`, `dgpumini.drv` / `dgpumini.vxd`, and OPEN/CLOSE on the loaded
  `DREAMGPU` VxD channel. Linux evidence: `target/fixtures/linux-dgpu-win98-v1/`.
- Linux Win98 WGL/desktop coherence, arrays, lifecycle, modes, window pixels,
  Glide and D3D6 probes pass. D3D7/8/9 currently fail at process creation because
  their helper PE subsystem version is 5.0; this is being corrected and audited
  against actual Win98 exports before those gates run.
- The cleanly stopped installed Win98 disk was copied with SHA-256
  `ee321483642569264bdc1dafe536f764c8b8661511a594aa13aac195d68af565`
  for independent Mac cold activation. A missing Juke font-resource link stopped
  the first Mac app before any VM launch; the corrected private fixture is v2.
- Data dispatch/endian staging also moved to Rust: 43 source tests and 26 native
  GPU tests pass on Mac (3.66s) and Linux (3.87s). Little-endian native payloads
  retain the allocation-free path. Review identified an inherited zero-object-name
  allocation failure gap; checks and injected failures are in the next host batch.
- Framework's canonical DreamGPU directory now has `main` Git metadata tracking
  the published repository. Existing working files, fixtures and artifacts were
  preserved; native source overlays remain explicitly dirty until their batch is
  committed. No separate development branches were created.

- Win98 cold activation and all OpenGL/Glide/D3D6/7/8/9, desktop/window/mode and
  lifecycle probes now pass on both hosts. Corrected D3D helper PE4 headers also
  pass actual Win98 export audits; the runtime drivers were unchanged by that fix.
- Win98 Unreal D3D and Glide both pass on Mac/Linux with helper v8. Live log reads
  use a 32768-byte bound compatible with Win9x window-message parameters; exact
  provider/foreground checks and captured GPU submissions pass with zero rejected
  GL commands. These were correctness runs during builds, not FPS measurements.
  Mac evidence: `target/fixtures/mac-dgpu-win98-v2/`; its guest is paused.
- XP prerequisite supplied by the user: `<private-fixture-directory>/Windows XP.utm`.
  Juke's importer now understands modern Drive/Data/MemorySize and device-array
  metadata, preserves legacy bundles, and rejects missing/escaping image names
  before creating a machine. Two import contract tests and the real import pass;
  Juke `master` commit `116b532` is published. Original and imported disk SHA-256
  match; evidence is `target/validation/xp-utm-import/run.json`. XP package/runtime
  checks are in progress; the old missing-media limitation is resolved.

- Win98 Half-Life startup currently fails on Mac with an illegal-operation dialog;
  no engine FPS was produced. The first host deadline (90s) was shorter than the
  runner's 120s cleanup deadline, so its failure-only text was unavailable at host
  disconnect. Exact fault diagnosis is active on the single Linux controller;
  passed UT/public API gates are preserved and are not being repeated.
- XP identity is confirmed from bounded offline fields: Windows XP 5.1, build2600,
  Service Pack3. Its independent application/driver staging and unattended NT
  installer adaptation are in progress; no original UTM files were modified.

- Grouped Rust image/publication/device ownership batch: 55 Rust tests and all26
  native GPU gates pass on Mac (3.67s) and Linux (4.12s). Native image creation
  rollback, export-slot transitions/early release, immutable DMA snapshot leases
  and zero-object-name allocation failures are covered. Remaining GL outer-batch,
  desktop transaction and queue/reset policy is still being moved out of C.

- Half-Life startup blocker identified: D: still held the Windows installation
  media. An instrumented launch showed no startup exception and no GL module;
  the launcher was waiting for its original retail CD. Timeout cleanup produced
  the secondary null-address fault. With the original Half-Life CUE mounted,
  Linux completes379frames/2.852s (132.897 reported engine FPS); concurrent build
  activity excludes that run from performance comparison. The fixture/controller
  is gaining an explicit retail-CD preflight, and Mac is testing the same media
  correction. No GPU code change was needed for this failure.

- Mac Win98 with the corrected retail CD completes379frames/4.982s:76.069 engine
  FPS in a quiet Mac interval. This is the final C++ guest / frozen-v3-native
  baseline. Its private fixture was later stopped through owned QMP cleanup after
  neither ACPI-button nor legacy guest helper produced a clean QEMU exit; no clean
  Windows shutdown is claimed for that Mac copy. Future guest copies use the
  separately verified clean Linux source, whose SHA-256 is
  `35e5cdcf6b9fe6bbbb006a31fc4828a00e8c0b9810d82098b4096102beea5bc0`.
- XP SetupAPI installation passes; clean guest-shutdown and image checks precede
  a frozen installed source (`d73440a70d6c618a1159610b12fe00a0b61099da0d0943e6f927a1fe77bcbeea`).
  Linux cold activation and all ten graphics/desktop probes pass. Mac cold startup
  reaches the exact serial runner in22.34s; its equivalent probes are running.
- Rust outer framing and desktop transaction batch:62 Rust tests and all26 GPU
  gates pass on Mac(3.50s)/Linux(3.93s). A frozen-source framing comparison matched
  144,992 cases; independent review found no new rollback/lease/aliasing blocker.
- Rust worker output/CPU mapping/reply ownership batch:66 Rust tests and all26 Mac
  GPU gates pass(3.48s). Linux source is captured; its build waits for XP's isolated
  first game capture. Pending-batch/reset/completion ownership is the final policy
  slice being implemented before the complete-native runtime/regression gate.

- Completed Rust submission/reset/completion ownership: 70 Rust tests, all four
  Mac native targets and all 26 diskless GPU gates pass on Mac (3.46 s) and Linux
  (3.87 s). Accepted snapshots, record cancellation, pending/reset disposal and
  bounded completion payloads now have Rust owners. Necessary native OS/QEMU
  adapters are listed in `docs/unsafe-boundaries.md`. Final Juke guest and
  performance acceptance uses separately frozen `target/native-dgpu-v4`; its
  Juke consumer is byte-identical to v3.
- XP cold startup and all ten graphics/desktop probes pass on both hosts. Unreal
  Direct3D also passes on both. Unreal Glide initializes but fails presentation;
  the saved Linux trace contains GL draws and no PRESENT operations. Diagnosis
  targets the guest WGL swap boundary, not host GPU throughput.
- Tooling commit `ad39d41` adds isolated XP directory staging, exact owned XP
  installer-warning handling, bounded startup serial enumeration, a retail-CD
  preflight and process-owned Half-Life debugging. Original XP UTM/media remain
  outside the Git tree and unchanged.

- Independent final review identified an unlocked reset metadata/acknowledgement
  race in the retained C adapter. Correction is in progress before final
  performance adoption; v4's passing Win2000 pixel/window/lifecycle probes stay
  recorded and its unrun performance gate is not claimed. A corrected native
  candidate will use a new v5 identity.
- All 149 host controller tests pass after the combined fixture/tooling changes.
  `docs/automation.md` now documents installed-UTM staging, cold package
  acceptance, fixed serial game commands, media checks and evidence reuse.

- The reset race is fixed and independently reviewed: packet metadata is captured
  under the mutex, and acknowledgement requires the exact generation/epoch/frame
  ticket. Required queue/snapshot calls are unconditional, including release
  builds with assertions disabled. Final source passes 71 Rust tests and all 26
  GPU gates on Mac (3.49 s) and Linux (4.07 s). Frozen native v5 is the adoption
  candidate; v4 evidence remains unchanged.
- The Mac NT v4 guest was cleanly powered off using its known installed
  `C:\DGSTOP.EXE`; QEMU exit status 0 was observed before owned app cleanup.
  This preserves the updated control tools for the v5 cold successor.

- Mac final-v5 NT Half-Life: 379 frames / 4.664 s = 81.253 engine FPS, versus
  recorded v3 78.184. UT Direct3D/Glide and the post-port window/mode/lifecycle
  probes pass. Ten-second idle capture reports Juke 0.30% and QEMU 23.22% of one
  core; no matched historical idle measurement exists, so no idle speedup is
  claimed. Evidence: `target/fixtures/mac-dgpu-v5`.
- Linux final-v5 NT Half-Life reports 184.325 FPS versus v3 206.082 (-10.56%).
  This exceeds the regression trigger. A bounded sample is being collected
  before adoption; passing graphics gates do not override this open regression.

- Mac Win98 final-v5: all 11 activation/graphics/desktop routes and Unreal
  D3D/Glide pass. Half-Life reports 379 / 4.717 s = 80.351 FPS versus recorded
  v3 76.069. The source is the clean complete Linux Win98 successor with the
  same final driver/workload, rather than the unclean stopped historical Mac
  copy. Idle CPU: Juke 0.30%, QEMU 8.06% of one core. Evidence:
  `target/fixtures/mac-dgpu-win98-v5`.
- Linux's regression-triggered sampled follow-up reports 219.057 FPS with no
  source change, so the initial 184.325 slowdown did not reproduce. Both results
  are retained; thread/stack attribution is under review before closing the gate.

- Linux sampling review found no evidenced host-port fix: request-overlap
  host-visible cycle weights were CPU0/KVM 80.13%, GL 13.58%; submission dispatch
  showed ordinary calls/branches, without a large copy/allocation loop. The
  sampled unchanged run exceeded baseline, while the initial slower run remains
  valid evidence of variability. The sample starts after the request, includes
  a trimmed idle tail and lacks an exact timedemo interval; its weights are not
  total CPU utilization. No speculative optimization or extra unchanged run was
  made. Raw exports and analysis: `target/validation/linux-nt-v5-perf-analysis`.
- Linux final NT idle: Juke 0.70%, QEMU 4.99% of one core over 10.01 s; tracing
  records 4 Hz maintenance and no redraw events. This is an absolute idle check,
  not a claimed improvement over an unrecorded baseline.

- Linux final native acceptance is complete. Win98 all 11 graphics/desktop
  probes and Unreal D3D/Glide pass; quiet Half-Life reports 129.150 FPS. The old
  132.897 result ran during builds and is retained only as context. Win98 idle
  reports Juke 0.40%, QEMU 3.30% of one core, with one frame publication and no
  redraw loop. Both NT/Win98 final Linux fixtures are paused; originals remain
  untouched. Complete ledgers and raw JSON/text are on both hosts under
  `target/validation/linux-dgpu-final`.
- XP Glide diagnosis: the current client dimensions change after drawable
  creation; the exact swap error is 24. The correction reuses the existing
  ordered MakeCurrent resize transaction only when dimensions change. Same-size
  swaps add no transport. Independent review and actual frontend ASan/UBSan
  tests cover failed and executed-but-unacknowledged destroy/create operations;
  they cannot publish stale geometry or replay uncertain cleanup. Pinned guest
  build/import checks pass; final XP-v5 runtime proof is in progress.

- Mac final XP-v5 passes all ten activation/graphics/desktop routes and both UT
  renderers with the corrected frontend. All 62 Mac/Linux probe verdicts and 12
  UT verdicts are checked in `target/validation/final-acceptance/run.json`. Final
  Mac and Linux test fixtures are paused; the supplied original UTM is unchanged.
- Final five-component package and controls are assembled on both hosts under
  `target/guest/final-v5`. Portable recipe and source manifests retain all pinned
  component payloads/notices. Package identity:
  `4fccc8caeb1bf54844fb2045d8061b40bb613e66ba8c411d1f9b94bc4ff00d71`.
  Actual ISO extraction verifies all 75 control payload members; all 36 package
  files are verified. The receipt records app-local deployment limits and exact
  accepted artifacts; it does not claim every OS used the identical frontend
  binary before the XP-specific resize correction.
- Framework's canonical checkout is restored to the standalone layout. All seven
  guest source submodules and required nested pins verify clean; QEMU is clean on
  `master` at `225bf1946c`. The 476 obsolete source files and the old QEMU doc/ref
  state were archived before removal. Source archive SHA-256:
  `4178e5c5bac852a57ed11195778b57b8d5c269b3db963bec2eec35bbfa40e145`.
  Evidence: `target/import-evidence/canonical-housekeeping.json`.

- Final normal-checkout audit found Framework Juke still held pre-extraction
  staged source, although all validation used the isolated current consumer.
  Its old source and self-contained QEMU/86Box Git trees were preserved under
  ignored `target/` before canonical Juke was aligned to published `master`.
  Juke commits `fe1112a` / `048c886` make the shared development dependency
  `../../coral/dreamgpu` and document the layout. Cargo metadata resolves the
  canonical DreamGPU directory on both hosts; this removes per-host Cargo edits.

- Canonical Framework Juke `cargo check --workspace --release` passes in 17.99 s
  using its existing release cache and the already validated frozen native
  binaries, without rebuilding unchanged native source. Normal private-repo
  fetches for Juke and 86Box now succeed with the user's specified Framework SSH
  key. Both canonical development trees and submodules are aligned and clean.

## Follow-through audit

The user resumed the plan after extraction acceptance. Three completed items were
too broad: some maintained game tools still compile as C; the Linux timedemo
sample has no exact engine interval; final package runtime proof differs by OS
because Win98/Win2000 predate the XP frontend fix. These are reopened as specific
gates in plan.md. Absolute idle results remain evidence, not a matched historical
comparison. Existing passing runs are retained without relabeling their binaries.

The benchmark comparator now rejects known host/platform/workload/instrumentation
and preconditioning changes, partial resolution evidence and dropped samples.
New captures record duration, warmup, snapshot, probe mode and setup hash. Legacy
missing fields are reported explicitly, without manufacturing equivalence. All
28 benchmark accounting/comparison tests pass, including mixed-instrumentation
and incomplete-capture regressions.
