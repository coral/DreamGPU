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


## Follow-through after the source release

- DreamGPU now publishes on `master`; the initial public history is the single
  `release` commit. The canonical Framework checkout uses the same branch and
  QEMU gitlink. Local private-data ignore additions remain user-owned.
- Cargo's full Mac/native plus Linux guest-builder sequence passes after SPDX,
  copied-source provenance and package-notice changes (31.77 seconds). Both
  OS package manifests verify every payload and include the central attribution
  ledger and original notice bytes. The six application DLLs match across OS
  packages. Native Linux and the current Mac Juke consumer also build.
- The current OpenGL frontend, OpenGLide and WineD3D DLL hashes differ from the
  previously deployed package; the three remaining Wine DLLs are unchanged.
  Runtime acceptance of the new bytes is in progress using clean stopped source
  disks and independent successors. Earlier passing results are retained.
- The C++23 Unreal source conversion is present. The build audit found that its
  helper-specific PE-version, imported-API and scalar-instruction checks were
  lost during build-system migration; restoring those checks is required before
  closing that follow-through gate.

Current run evidence lives under `target/follow-through/`; builds and acceptance
remain distinct, and runtime measurements run only after build activity ends.


### Current acceptance and newly expanded installation goal

- Linux current application packages pass 20 public probes and four Unreal
  activity/provider/lifetime checks across NT and Win98. Both fixtures are paused.
  Saved scene artifacts preserve a visible D3D limitation; these checks are not
  a universal visual-correctness oracle. Mac NT passes ten public probes; the
  user's captured partial-client rendering is open. Mac Glide's next attempt
  aborted before game launch because the helper observed held Shift input.
- Real Linux Half-Life sampling now validates the control protocol, including two
  QMP endpoints and perf's NUL-terminated acknowledgement variant. The single
  actual timedemo captured 1,263 events. It is an instrumented diagnostic, not a
  comparable FPS baseline. Exact engine-frame boundaries remain unknown.
- Relocated fixture QEMU can now use an explicit firmware directory and per-file
  SHA256 manifest. Preparation and startup reject missing, changed or symlinked
  firmware before launch; 25 fixture tests pass.
- The user expanded the product goal to a single guest-run `dreamgpu.exe` that
  detects 98 versus 2000/XP and installs system-wide graphics. The plan now tracks
  ICD, Direct3D system integration, installer lifecycle and normal-loader tests
  independently of historical app-local package acceptance. Driver/API discovery
  work is required; wrapping the old package in an executable is insufficient.

### Unified installer and normal-loader foundation

Cargo now sequences construction of a PE4 C++23 `dreamgpu.exe` embedding both
audited guest payloads. The first foundation artifact is 14,381,522 bytes, SHA256
`bee26d192b1b94804b20e2604c1c0afcc4478045686935f46991a1662248a2b6`.
OS/device preflight, integrity checks and owned diagnostic staging are present;
the default command exits with `provider_not_ready` before writes. This is not
a completed system installation. Persistent file/registry lifecycle work and
actual system provider activation remain open. Evidence is under
`target/follow-through/installer-foundation/`.

The diagnostic ICD's typed 336-slot adapter builds and passes actual-source
sanitizer checks, but 150 operations remain explicitly unsupported. A new
`DGSYSGL.EXE` probe exercises the actual system OpenGL/GDI loading path, requires
the system-directory DreamGPU ICD, rejects game-local API providers and checks
8,192 exact pixels. GCC16.1.1 legacy-target compilation with `-fanalyzer -Werror`
passed. The fixed serial `sysgl` route uses the existing strict probe-result
parser; all 23 controller tests pass. Its first isolated Windows loader run is
pending and no full OpenGL conformance or installed-driver success is claimed.

### Unreal clipping diagnosis and candidate

The native trace establishes correct 640×480 export/final viewport dimensions.
Wine's backbuffer offscreen mode first rendered a 1024×768 desktop primary into
the smaller physical drawable, truncating the image before the final translated
blit. The checked second Wine patch routes oversized offscreen blits through the
existing framebuffer-to-texture fallback, preserving the required coordinates.
The actual-source sanitizer oracle verifies 1,048,576 primary texels, including
the full offset 640×480 copy and unchanged surrounding pixels. This is source
verification; the corrected game's runtime acceptance is still pending.

Both guest packages build with the new Wine and the strengthened D3D7 far-edge
pixel probe. The clean aggregate build initially exposed an omitted diagnostic
ICD dependency; Cargo's explicit `dreamgpu-guest` target now includes every
installed diagnostic on both OS families. The actual MinGW build also caught
and resolved a `D3DRECT` union-initializer warning in the new probe. Final build
passes in 6.17s. Frozen payloads and a hash-verified receipt live under
`target/follow-through/backbuffer-candidate-v1/`. Both contain Wine SHA256
`e6a4126fa9608317975a56ecf8fd2ba2a86047e62ec37902339b02e94f2f8f0a`
and D3D7 probe SHA256
`4888d8253957a47156c0142179fc47fbc6cc8054a620fd53bb9fac54b7028ee2`.

Current script automation: 173 tests pass in 2.999s. Strict workspace/all-target
Clippy passes in 1.12s. The canonical Linux Juke release now builds against the
current DreamGPU dependency (27.45s, SHA256
`b254677216ba2524868245b48f9646f9dc52ff3da36b7965cec138e84ab4c269`);
this build does not relabel earlier frozen-executable runtime evidence.

### Bounds fix accepted on both hosts

The same frozen Wine/probe candidate now passes the real D3D7 test on Mac and
Linux: 1,280 exact render-target pixels plus 1,280 exact desktop-visible pixels,
including distinct right, bottom and corner colors. One corrected UT D3D run per
host fills the entire 640×480 client. Mac records 626 GPU receipts; Linux records
3,902; both have zero native rejections. The prior Linux right/bottom edge bands
are absent. These are correctness results, not matched FPS comparisons (window
positions differ from earlier captures). Geometry tracing was disabled.

Receipts and full-client images:
`target/follow-through/mac-wine-backbuffer-acceptance-v1/` and
`target/follow-through/linux-wine-backbuffer-acceptance-v1/`. The Linux fixture is
paused with networking disabled. The owned Mac diagnostic process groups were
subsequently stopped for quiet desktop/idle measurements; their guest disks and
evidence are preserved and are not clean-shutdown source fixtures. Reusable
trace instructions and the bounded collection limit are documented in
[geometry-diagnostics.md](geometry-diagnostics.md).

### Matched desktop evidence and sampled fill optimization

The Mac pair uses the same cold disk (SHA256
`b8e9a29ed2884f05429e5b2056a5dbbf8eb883a3029daeb00611afb6803196f1`),
fixed desktop probe, firmware, display placement and verified 240 Hz. Every
desktop scenario has 16 acknowledged/submitted batches and zero dropped trace
records. Copy mean latency is roughly unchanged, but fill/scroll/repaint means
are higher in the current native pair; parity is not claimed. Idle total CPU is
36.86 versus 35.02 percent of one core; most belongs to the running Windows guest.
All reports, comparison-condition checks and stage joins are retained under
`target/follow-through/matched-desktop-v1/`. Both Juke and QEMU binary identities
differ in that pair, so it does not isolate a QEMU regression. The baseline's
48 Rust host source hashes match preserved commit `c892b86`, confirming that
it already contains the completed host ownership port despite its pre-commit
QEMU revision string.

A separate candidate CPU diagnostic sampled both QEMU and Juke during the same
fill replay. Both tools completed inside the replay, but heavy sampling caused
an acknowledgement mismatch; the harness correctly rejected latency acceptance.
The stacks still identify the fill callback as a useful current hotspot. The
old callback's 47 ARM64 instructions are identical in both compared binaries,
so its cost is not evidence that the port introduced the observed difference.
Stacks and bounded scope are in `target/follow-through/desktop-fill-sampled-v1/`.

The host fill callback now splits validated 16-bit and 32-bit cases outside the
loop, allowing constant-stride vectorization while retaining unaligned
little-endian writes, exact work budgets and dirty-region marking. The rebuilt
Mac binary uses 64-byte vector stores; expanded actual-QEMU tests pass 39
pixel-format/width cases, including vector tails, row padding and guard bytes.
Native build passes in 5.95s. The frozen candidate SHA256 is
`0a0592c6871358ac7da46ca03020b7fbd69c599d1a5cbab0ea0d7df1cfd3a49a`.
Linux native build and all 39 boundary cases also pass. The corresponding Linux
QEMU is `44833ea492810bc7471445c34934472542be93550d619ffa7b8550d526d6ee58`;
the corrected receipt is `linux-constant-fill-verification-v2.json`. An earlier
incorrect source-copy attempt is retained as invalid evidence.

One changed-candidate Mac capture uses the same Juke, guest, probe and display;
only the native binary changes. Mean fill-batch latency improves from 26.14 to
10.61 ms and CPU from 22.04 to 14.75 percent of one core. Copy/scroll/repaint means
are 13.17/12.56/15.90 ms, versus 10.71/13.36/16.84 before; those mixed distributions
do not establish universal parity. All 16 batches per scenario are acknowledged
and submitted, with zero dropped records. Idle CPU is 35.97 versus 35.02 percent.
Reports are in `target/follow-through/matched-desktop-vectorized-v1/`; no unchanged
baseline was rerun.

### Real Windows loader and installer lifecycle milestones

The first diagnostic ICD now passes the actual Microsoft Windows 2000 system
OpenGL loader: GDI enumerates hardware format 1 and loads system `dgpuicd.dll`;
8,192 exact pixels and two user-ICD swaps pass. Its discovery request needs a
532-byte response with the driver name at offset 8. The implementation accepts
both this observed layout and the 520-byte ReactOS layout, zeroing the complete
output. Evidence is in `target/follow-through/system-icd-probe-v4/`. The resulting
independent fixture was shut down cleanly and checked before reuse; its disk
SHA256 is `c3550c96147783ab7c2c5186123f2aa3acc53a51c4957eb5981d41e73fc854de`.
This basic loader result does not prove full GL1.1 coverage or production setup.

The installer now has bounded checksummed journaling, durable intent-before-write
records, immutable original backups, interrupted-write reconciliation and owned
file/registry rollback. Actual Win32 adapter tests cover 108 syscall failure
boundaries. A single independent Windows 2000 run checks default refusal (exit
30), diagnostic staging (10) and rollback (12), preserving 12 global DLL hashes
and four ICD registry values. Installer SHA256:
`38bdc9b5107057f389adedaf26ff07c91affa8d7649aa749d76369d1bf218563`.
The helper's final PASS prefix differed from the host parser contract; the
original rejected run is retained, with a separate verdict based on its exact
observed records. The helper prefix is corrected for future builds; installation
actions were not repeated. See `target/follow-through/installer-lifecycle-runtime/`.
Activation, reboot completion, real driver binding rollback and NT5 global
Direct3D integration remain open; default setup still refuses incomplete providers.

### Seventy additional OpenGL operations

The diagnostic dispatch now includes 39 numeric color/normal/rectangle variants,
seven fixed-state aliases and all 24 raster-position variants. Actual frontend
sanitizer tests verify legacy integer normalization, payloads, Begin/End errors,
rectangle order and context state. Raster commands preserve double precision
through typed host calls; the native oracle checks transformed and clipped raster
positions, associated state, bounded queries and malformed commands. All 73 host
Rust tests, strict Clippy and all 27 native GPU tests pass; the native suite passes
on both Mac and Linux. The first distance assertion was corrected to respect the
specification's permitted eye-axis approximation; its failed log is preserved.

The pinned GCC16.1.1 diagnostic DLL build and clang-tidy pass. Its SHA256 is
`279f648da6eb09e98e41b056cabf9abf0e81008f3ac48b23e9ebe96479836142`.
Coverage is 146 direct functions, 110 adapters and 80 explicit unsupported slots;
the development version and production registration gate are unchanged. The
normal-loader result above belongs to its earlier frozen candidate. New source
checks do not relabel that runtime evidence. Exact source/binary hashes and logs
are in `target/follow-through/raster-acceptance.json` and
`target/follow-through/system-icd-evidence/`.

### Normal system Glide loading and combined build

The first normal system Glide run passes on an independent Windows 2000 fixture:
`DGSYSGR.EXE` launches from `C:\\` with no neighboring providers, verifies actual
`C:\\WINNT\\system32\\glide2x.dll` and `dgpugl.dll`, checks 2,304 exact GPU pixels,
eight swaps and same-window reinitialization/cleanup. The original clean ICD
fixture stays unchanged; the new fixture is paused with networking disabled.
Evidence is in `target/follow-through/system-glide-runtime-v1/`. Offline staging
establishes provider loading, not automatic installer activation or removal.

The combined `DREAMGPU_BUILD=all` Cargo release build passes in 51.51s, producing
native QEMU, both OS payloads and `dreamgpu.exe` SHA256
`71b0ce63c263c4accfae3f23b65d5afe6f1892141e44f200a0cb948950aa8190`.
All-feature/all-target workspace Clippy passes with warnings denied; build-crate
tests pass (13 run, two explicitly ignored). Source formatting passes for 206
maintained files after correcting three formatting-only differences. The build
receipt records exact manifests under `target/follow-through/combined-build-receipt.json`.
Subsequent client/pixel/Win98-discovery changes require their own build checks;
this receipt is not silently updated to cover them.

### Windows 98 installer loading and rollback

The fixed lifecycle helper now selects 98/2000/XP and checks fourteen global
graphics files, including the Win98 driver pair, plus the OS-specific ICD values.
Its first Win98 attempt failed before installer startup with loader error 31.
An import audit against DLLs read from the stopped original guest identifies the
single missing export: MinGW resolved `CM_Get_Device_IDA` from SetupAPI, whereas
this Windows 98 image exports it from CfgMgr32. All other imports exist. The
source link order now resolves CfgMgr32 first, and Cargo rejects a wrong or absent
module/function pairing. The focused regression test passes.

With the same embedded packages, corrected installer SHA256
`6228037c97974a9748a5e200ad244be973b8fb79835091931e70193e54c6fff9`
passes one fresh Win98 lifecycle run: default refusal leaves no private state,
staging records six original-file identities, and rollback preserves fourteen
graphics files and the Win98 ICD value. Original guest bytes remain unchanged;
both diagnostic fixtures are paused with networking disabled. The first failure
and corrected result are retained under `target/follow-through/win98-setup-runtime-v1/`
and `win98-setup-runtime-v2/`. Private actual-OS DLLs and the import receipt remain
ignored under `win98-setup-import-audit/`; they are not redistributable payloads.
Legacy-target helper compilation and clang-tidy pass. This closes loader/staging
mechanics on Win98, not system-provider activation or XP runtime acceptance.

### Client state, pixel state and Win98 discovery

The next guest batch adds all fourteen interleaved array layouts, a bounded
per-context client stack preserving borrowed pointers and pixel-store state, nine
pixel-transfer/map APIs, and CopyPixels. Actual-source tests cover failed-address
validation, legacy integer normals, stack masks/limits/context isolation, original
typed map values and publication: only color copies to FRONT publish immediately.
Pinned guest compilation and target clang-tidy pass. Coverage has 146 direct
entries, 124 adapters and 66 explicit rejections; ArrayElement, Linux integer
pixel transfer and CopyPixels depth/stencil retain explicit partial-validation
notes. The diagnostic version and production installation gate stay unchanged.

Mac passes 77 host Rust tests and 28 native GPU tests. The Linux suite passes its
other 27 tests but exposes Mesa's INT_MAX INDEX_OFFSET rounding through float;
the typed transport preserves the value, while the native driver returns INT_MIN.
A separate pixel oracle with an exactly representable offset passes the remaining
transfer/map/export/zero-initialization/color-copy/scissor checks. The original
failure is retained and full-range conformance is not claimed. Native transfer
state is neutralized only for internal zero initialization, with scoped restoration
and no use of guest attribute-stack capacity. Evidence is in
`target/follow-through/pixels-copy-acceptance.json`.

Win98 diagnostic ICD discovery also builds through a separate prepared source
and object tree. The actual Watcom compiler verifies the donor's 270-byte Win16
ANSI descriptor, far-pointer width and name offset; this is distinct from NT's
532-byte wide-character request. The checked patch reports DGPUICD and reuses the
exact production VxD. Actual-source boundary tests and preparation checks pass;
guest registration/loading is still open. See `target/follow-through/win98-icd-discovery/`.

The combined native/guest/installer Cargo build for this checkpoint passes in
18.34s and strict all-feature/all-target Clippy passes. The later Win16 provenance
comment update does not change code semantics. Bitmap and DrawPixels now have a
coordinated implementation task for bounded immutable image assembly; they must
draw once at commit, preserving fractional raster/zoom semantics and releasing
staging budgets on errors, context loss and reset.

### XP lifecycle acceptance and tighter controller diagnosis

The same corrected installer and helper used on Win98 now pass the first actual
XP lifecycle attempt: default refusal, six-entry staging and rollback preserve
fourteen graphics files and four ICD values. The independent XP fixture reaches
the expected controller identity automatically in 12.13s and is paused afterward.
Evidence is in `target/follow-through/xp-setup-runtime-v3/`.

Two earlier startup attempts never executed the installer. Offline diagnosis
found `CreateFileCOM1` access denied: XP was launching its older controller from
the All Users Startup directory, while only the root copy had been updated.
The fixture's explicit file manifest now replaces the actual startup executable;
no GUI bootstrap is needed. Both failed starts and their logs remain retained.
Readiness timeout diagnostics now include the last observed identity instead of
discarding it; a Unix-socket protocol regression test covers the stale-controller
case. The original clean XP disk remains unchanged.

### Recovery dispatch and bounded pixel-image assembly

Installer continuation now authenticates the existing journal before selecting
the recovery operation. An owned rollback or removal can resume while new GPU
providers remain unavailable; a new installation or upgrade still cannot begin.
Tests invoke the production runtime gateway and Win32 store under ASan/UBSan on
both OS-family paths, including 68 injected recovery syscall failures, corrupt
receipts and blocked installation paths with zero mutations. Provider readiness
has not changed, and this source-level recovery gate does not replace an actual
installed-driver reboot/upgrade/uninstall test.

Bitmap and DrawPixels now assemble a bounded immutable image before issuing one
native draw. Stream IDs, offset/order checks, cancellation, context teardown and
a shared 64 MiB staging budget prevent partial-image replay or unbounded memory.
An inactive stream returns cheaply before crossing the Rust image guard on
ordinary rendering commands. Guest sanitizer and host state-machine tests cover
malformed chunks, failed publication, allocation failures and context isolation.

Native acceptance exposed two Linux provider issues. Fractional negative
PixelZoom loses or miscolors pixels in a direct EGL reproduction without any
DreamGPU code; the same exact oracle passes on Mac. DrawPixels with packed stencil
bits also hangs the Linux driver. The latter is being addressed by expanding
packed bits into exact 0/1 bytes inside the original bounded allocation before
one native submission, with guest preflight charging the expanded byte count.
The correction passes the packing, transfer, typed pixel, depth and stencil
assertions on both hosts. Mac passes all 29 native GPU tests. Linux's changed
image test completes in 0.29s and fails only the retained three-pixel fractional
zoom assertion; its other 28 tests passed before this correction. The direct EGL
reproduction and original driver failure remain recorded. Neither issue is hidden
by weakening pixel expectations or claiming complete GL1.1 coverage.

All 55 native source hashes match across hosts in
`target/follow-through/pixel-image-acceptance.json`. Host Rust tests now total 88
passing tests, including 11 stream adversarial cases; strict Clippy passes.
The combined Cargo native/both-guest/installer build passes in 17.20s, followed by
workspace all-feature/all-target Clippy and maintained C/C++ formatting checks.
Its exact outputs are recorded in `combined-build-v3-receipt.json`. The installer
still refuses production activation while the remaining provider contracts are
incomplete; these successful builds do not change that status.

### Texture completion and controller provenance

The next four guest adapters implement real texture residency/priority and 1D
image copies. Residency stages results until the full call succeeds and leaves
the caller array unchanged when all textures are resident. Subimage validation
queries the actual level extent and border, including legal negative border
offsets. Full GL1.1 base/sized 1D formats use conservative eight-byte-per-texel
accounting; the existing 2D format policy remains unchanged. Guest sanitizer,
legacy GCC and compile-database clang-tidy checks pass. Coverage now has 130
adapters, 60 explicit gaps and five documented partial operations.

Host Rust tests total 94 passing tests with strict Clippy. Mac's focused native
1D border/RGBA16/subcopy/priority/residency oracle passes, as does the existing
2D signed/padded-copy check. Linux passes borderless copies and real
priority/residency, but its driver strips requested legacy borders: width 6 and
border 1 become width 4 and border 0 without a native error. A direct EGL
reproduction confirms the same result without DreamGPU. Resource metadata now
queries the actual native extent/border before publication, preserving accurate
ownership and accounting. Both 1D copy entries retain this explicit partial
coverage; no fake texture dimensions are reported.

The exact cross-host source and native results are recorded in
`target/follow-through/texture-control-acceptance.json`. The final combined Cargo
build passes in 10.41s, followed by strict workspace Clippy and maintained C/C++
formatting. `combined-build-v5-receipt.json` records the native, both guest package
and combined installer identities. The preceding combined build caught the
updated system probe's missing freestanding memory-helper link; adding the
existing CMake MEMORY support fixes it without introducing a guest CRT dependency.

Fixture receipts now record separate preparation/startup hashes for the four
core controller modules. A controller correction no longer needs to be inferred
from native/guest binary identities, and preparation provenance is retained.
The existing 26 fixture tests pass after this metadata change.

### Windows 98 normal system OpenGL loader

The first actual Win98 system-loader proof now passes: Microsoft
`C:\WINDOWS\SYSTEM\OPENGL32.DLL` loads the registered
`C:\WINDOWS\SYSTEM\DGPUICD.DLL`, selects accelerated GDI format flags `0x25`,
renders all 8,192 expected pixels and completes two swaps. The single serial
request completes in 0.115s; it is a correctness test, not an FPS comparison.
Its raw log SHA256 is
`0448ab0664ab045f44d8eec014c300c8025426bd5664ed12ed083d4ce4c02fe7`.

Two earlier attempts remain explicitly failed: an older controller omitted the
copied QEMU binary's firmware path before guest execution, then the bootstrap
refused registration when ordinary Win32 Escape returned no descriptor. A
read-only diagnostic isolated the correct Win32 thunk: ExtEscape returns version
2, driver 1 and the expected ANSI name with a 270-byte output. The Win16 driver
needed no change. A fixed registration-only helper then reused the already-ready
fixture without spawning another serial controller or rebooting.

The helper preserves the exact previous registry record before mutation and
refuses deleting a parent containing unrelated values or subkeys. Its sanitizer
tests cover restoration, foreign entries, conflicts and corrupt backups. A
separate `/restore` invocation on the same guest also passes: the original absent
ICD value is restored under the existing parent, the disabling marker persists,
and the original checksummed backup stays byte-identical. The fixture is paused
with networking disabled. Raw registration, pixel and restoration receipts are
recorded in `target/follow-through/win98-system-icd-v1/acceptance.json`.
This closes Win98's basic system loader and diagnostic registry restoration
gates; production provider readiness, full GL1.1 compatibility and complete
installed-driver lifecycle remain open.

Final compile-database clang-tidy passes for the normal bootstrap, read-only
inspection variant and system OpenGL probe. The inspection variant now logs its
active-driver result explicitly instead of leaving it unused; this diagnostic
logging change does not repeat or replace the accepted runtime proof. The final
combined Cargo rebuild passes in 9.23s, with exact outputs in
`target/follow-through/combined-build-v6-receipt.json`. Native bytes are unchanged
from the preceding checkpoint; the combined installer is rebuilt with the final
guest tools. Existing strict Clippy and full formatting results are retained,
and the changed helper passes its targeted formatting check.
