> **Scope reset — September 13, 2026:** The user rejected the agent-added rollback,
> perfect-uninstall, unattended-installation and smoke-test campaigns. They are
> cancelled. The authoritative milestones are now in [plan.md](plan.md): working
> accelerated games, Mac Direct3D/Glide/OpenGL performance, accurate adapter
> capacity, and the XP Display Settings/Properties crash. Earlier outstanding
> checklists below are historical and must not restart cancelled work.

# DreamGPU execution ledger

Started September 12, 2026. Plan: [plan.md](plan.md).

## September 14 — execution resumed

The user resumed [plan.md](plan.md). Fullscreen graphics recovery and captured
movement chords pass, and normal-image adoption is complete. Prior performance
and display-mode measurements are retained; they will not be rerun unchanged.

The previous local `target/follow-through` fixtures, helpers and receipts are no
longer present. Juke's normal Windows 2000 disk and DreamGPU's native QEMU binaries
remain; the consumer executable was rebuilt successfully. The exact previous
recovery installer survived in Framework's dedicated Cargo guest cache and was
recovered to `target/follow-through/recovered-740/` (SHA `74064e43…`), with its NT
package. It can be used without waiting for the independent Wine patch rebuild. The
September 13 handoff below is historical evidence, not a list of artifacts
currently available locally. Prepare a private copy of the stopped normal image
and verify its installed stack before testing; do not use the missing fixture's
hashes as the identity of a new run.

The pending Wine analyzer patch is complete, including map-failure cleanup,
conversion-failure cleanup and common fill validation. This is housekeeping work, not an explanation for the
Half-Life focus failure. Runtime preparation and patch validation run independently.
Framework SSH uses the existing `coralmodern` identity; its prior agent-only key
availability did not survive the session change.

The retained focus helper is now `tools/benchmark/hl-focus.cpp`, built as
`DGFOCUS.EXE` by the existing guest CMake/Cargo tool target. It starts a normally
initialized retail process, discovers its real enabled Console/Resume controls,
uses the child HWND in button notifications, types the map through key events,
and logs mode/window/foreground phases. It exports through the existing serial
probe; generated media and run data stay in `target/`. The private fixture has an
additional helper CD drive so further diagnostics do not require a reboot.

The C/C++ checker also had a concrete empty-diff bug: it emitted line zero for
unchanged borrowed files, which LLVM rejects. It now emits a positive range
beyond EOF. Existing checker tests and a real LLVM invocation over an unchanged
donor with a diagnostic pass; this keeps unchanged upstream warnings outside
our maintained-line filter while retaining explicit owned-header coverage.
Evidence: `target/follow-through/quality-filter-20260914/report.json`.

Wine patch `0008` and all manifest identities now replay exactly (eight patches,
21 files). The final Cargo guest release build passes, producing installer
`79b45eef7063da19157c10e2e20520a127761d6a4c7bc9bed10dd7f19deffef8`.
Both OS packages contain WineD3D SHA `2cd38c95…`. Target-aware LLVM 22 analysis
passes for the affected surface and transform-header paths, with owned-line and
header filters; no diagnostics were suppressed to claim completion. This is
source/build/static validation, not new game performance or runtime acceptance.
The OpenGL recovery investigation uses the separately recovered `74064e43…`
package. Receipt: `target/follow-through/wine-cleanup-20260914/receipt.json`.

Recovery package `74064e43…` is activated in the new private fixture: installer
continuation reached terminal phase 2 with exit 0. Reboot automation now uses a
fixture-only HKCU Run entry for `C:\DGPUBEN.EXE`; serial readiness was observed
without the manual Run-dialog bootstrap. The retained provisioning recipe is
`tools/benchmark/DEBUGGING.md`. The original normal disk remains unchanged.
Actual recovery now passes in the same Half-Life process (PID 924): textured
800×600×16 gameplay, Alt+Tab away to the 1280×1024×32 desktop, then ordinary
launcher `SC_RESTORE` returning directly to live textured gameplay. Composed output
fills the render area. The first Alt+Tab-back left Explorer selected, so it was
not a rendering-recovery failure or a successful application-selection test.
No forced mode, drawable recreation or console notification was needed. The
helper's initial post-restore exit condition incorrectly looked for launcher
controls after the engine was already active; that condition was fixed without
repeating the observed successful transition.

Evidence: `target/follow-through/fullscreen-recovery-20260914/recovery-acceptance.json`
and `return-existing-process/001-rendered.png` in the same directory.

The real host-input check also passes: plain WASD, Shift+WASD and Ctrl+Shift+WASD
went through macOS Quartz → Juke → the guest. A read-only observer recorded all
12 movement chords, game foreground throughout, no Alt/Windows bits and final
release of every key (31 state-change samples). Existing Juke release/reconciliation
fixes are accepted for these captured chords. The historical Search incident's
exact cause is not established by this result.

The first observer attempt was invalid: input arrived after its observation
window ended because separate controller/agent turns consumed the interval.
The retained `scripts/diagnostics/host-keys.py` now injects synchronously from the
serial STARTED callback; the observer exits after activity plus a released-key
settling interval, with a bounded no-activity timeout. No screenshot or menu loop
is involved. Evidence: `host-input-synchronized/{acceptance.json,host-events.json,guest/engine-output.txt}`
under the same recovery evidence directory. Existing Half-Life protocol and
C/C++ checker tests pass (29 tests).

Final source checks pass on Mac: strict all-target/all-feature Clippy for both
workspaces, Rust formatting for both, clang-format for 318 maintained C/C++ files,
and the 29 relevant existing Python checks. Juke's ordinary build passed earlier
in this run. The final guest Cargo build includes all retained focus/observer tools
and passes in 10.85 seconds; current installer SHA is
`aa538306d9b5424e06b1f66ff5fca07be0c4536f4d3d0bfe64c11694afc09067`.
Its NT display drivers and OpenGL payload match the recovered runtime installer;
WineD3D and Glide binaries differ, so this build is not represented as a new
full-stack game validation. Source-check receipt and explicit package comparison:
`target/follow-through/quality-final-20260914/{checks.json,package-difference.json}`.
Targeted LLVM 22 clang-tidy also passes with the exact current configuration:
all three Half-Life focus helper modes, the input observer, and affected Wine
surface/stateblock sources including expanded owned-header coverage. Earlier
cached-config analysis was superseded by these exact-config checks; no additional
source corrections were needed. Commands, source/config hashes and per-TU logs
are recorded in `quality-final-20260914/receipt.json` under the same evidence root.

The normal-image adoption tool was still limited to the old schema-1 Python
package format. It now accepts Cargo schema 2 using its declared compact UTF-8
identity encoding and validates the accompanying build receipt against the
payload inventory. Existing payload hash and unexpected-file checks remain in
force. Six adoption tests pass, including schema-2 acceptance and tampering.
Guest cleanup recorded normal game exit, absent fixture autostart and a restored
1280×1024×32 desktop before clean shutdown; no runtime process holds the source
disk. Cleanup receipt: `fullscreen-recovery-20260914/normal-finalize/shutdown.json`.

Normal adoption is complete. Juke's `files/machines/win2000/machine.toml` selects
`.dreamgpu-adoptions/20260915T041802Z-a8e5ca0b/disk.qcow2`, a standalone copy of the
cleanly stopped verified guest (SHA
`631c0da3b25f7295d36a79409d0d7d8b1d39f0f2f54ecb342ca860e9f08da0da`).
The original selected disk still hashes to `3df51b66…`; presentation settings and
the Half-Life CUE at IDE index 2 are preserved. No diagnostic optical media or
private runner autostart is carried into ordinary use. The installed stack remains
the actual recovery-tested `74064e43…` package, not the newer separately built
Wine/Glide payload. VM and game were left stopped after clean shutdown.

Receipt: `target/follow-through/fullscreen-recovery-20260914/normal-adoption.json`;
full provenance is beside the adopted disk. All current plan checkboxes are
complete. No unchanged performance rerun, new lifecycle campaign, commit or push
was performed. Normal use resumes with Juke's usual `cargo run` and Windows 2000
slot; the current game-recovery result specifically covers ordinary taskbar-style
restore, not a claim that every Alt+Tab shell-selection sequence was tested.


## Paused handoff — September 13 evening

**Runtime work is stopped. Fullscreen return to gameplay is not fixed/accepted.**
The private VM shut down through `C:\DGSTOP.EXE`; its QMP socket and owned Juke
process disappeared. No unrelated process was signaled. The normal Windows 2000
disk/config were not changed or replaced during this recovery investigation.

Next session: reuse the stopped updated fixture, inspect the real launcher
Console/Resume control's state and notification route, then attempt one faithful
reactivation. The last real map renders correctly and desktop restoration works;
the attempted return leaves the engine hidden/minimized. Do not rerun installer
setup or repeat the old performance benchmarks to resume this work.

All paths below are relative to DreamGPU unless marked Juke. These are local,
ignored artifacts under `target/`, not files promised by a clean Git clone.

| Item | Location / identity |
| --- | --- |
| Shutdown receipt | `target/follow-through/fullscreen-focus-v1/night-stop.json` |
| Resume fixture | `target/follow-through/fullscreen-focus-v1/handoff-v4/` (`run.json` has launcher/native/firmware settings) |
| Stopped disk | `target/follow-through/fullscreen-focus-v1/handoff-v4/machines/win2000/disk.qcow2` |
| Disk SHA-256 | `aeafd120981f966b278ff3efa86ab6a5d55f34fa68b9446255d0a6b3ea45b23d` |
| Installed recovery package | `target/guest/dreamgpu.exe`, SHA `74064e43724843de8be56964a636a6f15163c51dfd2887c3655b0a1f56702cf0` (matched on both hosts before cleanup) |
| Frozen consumer | `fullscreen-focus-v1/juke-handoff`, SHA `a64c5313ce488fa8dcbb7987af1f97a20c0e44b0e41ca61ae9512b7c031b3047` |
| Frozen QEMU | `fullscreen-mouse-native-v1/qemu-system-i386`, SHA `2b013907a3a33dc9ea768a8e13e62b17ec33c808ea25b568cde105b027be7629` |
| Meaningful run | `fullscreen-focus-v1/transition-v4/{capture.json,outcome.json,log-export-110s/engine-output.txt}` |
| Process samples | `fullscreen-focus-v1/transition-v4/state-export/{engine-output.txt,analysis.json}` |
| Normal source record | `fullscreen-focus-v1/normal-source-record.json`; normal disk SHA at clone `3df51b66217b56e17b02d155ababd68d1b787fa0db4bc67062031042154f7cfd` |

Abbreviated evidence paths beginning `fullscreen-` above are under
`target/follow-through/`. Normal Juke still selects
`files/machines/win2000/.retro-adoptions/fullscreen-mouse/disk.qcow2` and retains
the Half-Life CUE. That is the earlier launch/input adoption, **not** the latest
recovery package or diagnostic disk. Frozen binaries identify the observed run;
tonight's source cleanup is separate and does not retroactively change it.

When work resumes, use `scripts/fixtures/fixture.py prepare` with the stopped
`handoff-v4` disk as the source and its recorded binary/firmware settings, writing
to a new run directory. Then use `start` on that newly prepared fixture. The
controller deliberately refuses to restart a completed run in place; do not
overwrite its receipt or log. This reuses the installed driver state without
another installation. It starts paused so networking is disabled before `cont`;
the existing runner is `C:\DGPUBEN.EXE`. Refresh runtime PIDs/HWNDs from the new
process; recorded ones are no longer valid. Guest logs can be exported through
the existing serial probe without a reboot or disk copy. See the final ledger
section for failed automation assumptions that must not be repeated.

## Evening cleanup — September 13

Runtime investigation is paused independently of these source checks.

- `cargo fmt --all -- --check` passes for Juke and DreamGPU. DreamGPU's standalone
  native launcher formatting also passes.
- `cargo clippy --workspace --all-targets --all-features -- -D warnings` passes
  for Juke on Mac. DreamGPU passes the same command on Mac and Linux with
  `DREAMGPU_BUILD=sdk`; its standalone launcher also passes on both hosts with
  the required runtime manifest/directory/program compile-time values. Logs and
  the Linux source comparison are under `target/housekeeping-night-20260913/`;
  `receipt.json` records Linux checks and `juke-checks.json` records Juke checks.
- Fixed explicit unsafe-block requirements around owned fd/mmap operations and
  collapsible Rust conditions. Formatting covers the existing workspace changes.
  Existing staged/unstaged work is preserved; no commit or push was made.
- The final Wine analyzer findings were carried into September 14 and fixed;
  see the current execution section above. Earlier passing format/native/guest
  checks remain historical results, not newly rerun checks.

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


### System installation follow-through

The NT5 system-loader experiment is now part of the Cargo-built guest tools.
It uses an isolated synthetic DLL name, a distinct original marker in System32,
and a temporary LocalSystem service owning a named image section. The serial
`ntloader` request checks normal system loading before publication, implicit
imports and both forms of LoadLibrary during publication, then service removal
and original loading afterward. No Microsoft runtime or protected DLL cache is
modified. The PE5 link, exact diagnostic-only import audit and target-aware
clang-tidy pass. Runtime results are recorded separately; compilation does not
establish this as the production Direct3D route.

The public DirectDraw 2D probe is also included in the normal guest build. It
checks the actual COM implementation module, completed copy/fill/color-key work,
visible pixels and object recreation. The Win98 switcher source now has checked
publication and native-library path fixes with actual-source sanitizer and
legacy-link evidence. Independent system-provider installation and matched 2D
runtime measurements remain in progress.

The next 21-function OpenGL batch is accepted on both hosts. Edge/index arrays
close ArrayElement's dependency gap; polygon stipple, logic/index and accumulation
API behavior use the real advertised RGBA visual. The visual has zero accumulation
bits, so accumulation operations correctly report the specified no-buffer error.
Coverage is now 146 existing functions, 151 adapters and 39 missing operations,
with four retained native-provider limitations. Both hosts pass the changed
fixed-state GPU oracle and 99 host tests; actual guest source tests, legacy links
and targeted static analysis pass. Exact identities are retained in
`target/follow-through/fixed-state-acceptance.json`. Evaluators are the next group.

### NT5 named-image loader result

Windows 2000 loads the alternate synthetic provider through implicit imports and
bare LoadLibrary, but an absolute System32 path loads the original disk image.
The owned service and section are removed, and original loading is restored.
The first short-deadline attempt is retained as failed; instrumented service
startup with the longer bounded deadline completes and gives the actual loader
result. This eliminates named sections as the complete system Direct3D route.
It does not justify replacing the protected runtime or claiming XP support.
Exact artifacts and raw logs are in
`target/follow-through/nt-system-loader-v1/acceptance.json`.

The donor's XP instructions replace both protected runtime and cache files.
Microsoft's [WFP replacement documentation](https://learn.microsoft.com/en-us/windows/win32/wfp/detecting-file-replacement)
confirms that catalog validation can restore the original from cache or media.
A durable installer cannot treat a successful copy as provider activation.

### Normal system Direct3D probe variants

The existing D3D6/7/8/9 pixel workloads now share C++23 sources between diagnostic
and normal system-loading variants. New DGSYS6/7/8/9 helpers load the public API
DLL name from a clean application/current directory, verify the returned COM
vtable belongs to the system Wine provider, and check system GPU dependency
locations. They do not preload a private OpenGL DLL to influence resolution.
The C++ conversion replaces inactive-union function-pointer access with a bounded
representation copy and uses the common freestanding memory implementation.

Both Cargo-built packages pass legacy linking and import auditing. The actual
loader-policy sanitizer tests and 23 serial-controller tests pass. Maintained
tool links now treat linker warnings as failures, so a missing entry point cannot
silently produce a runnable-looking executable. Frozen helpers and controller
identities are in `target/follow-through/system-d3d-probes-v1/`. Actual system
Direct3D rendering acceptance is still pending.

All 23 evaluator functions now pass their guest and both-host native gates.
The advertised order is an honest bounded eight; checked map/grid payloads and
query results retain context ownership. A valid terminal INT_MAX mesh is handled
with equivalent widened-index native primitives instead of overflowing a provider
loop. Coverage is 146 existing functions, 174 adapters and 16 missing operations.
Display lists and selection/feedback remain; the four known native-provider
limitations stay documented separately from missing DreamGPU implementation.
Evidence: `target/follow-through/evaluator-acceptance.json`, including 103 host
tests on each host, guest sanitizer checks and target-aware compilation/analysis.

### System Direct3D loading and actual driver lifecycle

The frozen Win98 global Wine candidate now passes ordinary Direct3D6, 7, 8 and
9 loading from clean application/current directories. These are the same C++23
HAL pixel workloads as the diagnostic probes, with actual system DLL paths and
Wine COM object ownership checked. The receipt is
`target/follow-through/system-d3d-probes-v1/win98-acceptance.json`. The first
attempt exposed a doubled separator in the probe's root-directory path check;
Win98 returned access denied. The fixed join is covered by actual-source loader
policy tests. A separate controller startup timeout happened while that fixture
was paused for another measurement; it executed no graphics test. Future quiet
windows must start after READY, not interrupt the bounded boot deadline.

The accepted runtime uses the earlier frozen native frontend. Wine discovery
leaves GL_INVALID_ENUM before the workload, while draw/presentation errors are
zero. This receipt establishes the normal-loader route and pixels, not full GL
coverage, performance or installer activation. No passed unchanged API test was
repeated.

The Windows 2000 installer driver journal now completes actual install, cold
boot, started-device verification, exact previous-driver restore, another cold
boot and restored-device verification. The first attempt found that CopyFile
preserved READONLY on private originals, preventing the flush reopen; the
corrected transaction temporarily clears and restores those private attributes.
Evidence is under `target/follow-through/driver-lifecycle-nt-v2`. Win98 reached a
real SetupAPI Insert Disk prompt despite local payloads; its source-list/queue
handling is being corrected before further mutation. These driver mechanics do
not yet establish full system provider installation.

### Windows 2000 normal system Direct3D route

A separate stopped-source copy now boots the owned Wine switchers in System32
and passes all four ordinary Direct3D6/7/8/9 pixel probes. The native DDRAW is
preserved as DDSYS and the original DLL cache is unchanged. The new read-only
`ntruntime` tool records SHA256 and actual `SfcIsFileProtected` results:
SFCDisable is zero, system DDRAW is protected but remains the candidate, and the
cache contains the exact original. D3D8/9 were absent in this source and are not
on this Windows 2000 protected-file list. The receipt is
`target/follow-through/nt-global-d3d-v1/acceptance.json`.

This corrects our overly broad assumption that protected replacements inevitably
get restored at boot. It establishes a usable system route, not the production
installer or resilience to an explicit OS restore/update. WFP reacts to directory
notifications and verifies catalogs, as described by [Microsoft](https://learn.microsoft.com/en-us/windows/win32/wfp/detecting-file-replacement).
The next concrete implementation gate is a scoped deferred replacement and
rollback via reboot. Microsoft documents `AllowProtectedRenames` as a consumed
one-boot setting for [Server 2003 restore operations](https://learn.microsoft.com/en-us/windows/win32/vss/backing-up-and-restoring-system-state-under-vss);
that documentation is not proof for Windows 2000/XP, so the exact target guests
must execute the transaction. No persistent SFC disabling or cache overwrite is
part of this proposed route. The earlier protected-DLL prohibition was our
implementation assumption, not a requirement from the user.

### Deferred system replacement and driver acceptance

Windows 2000 now executes the real protected-DDRAW install/restore cycle using
`MoveFileEx(DELAY_UNTIL_REBOOT | REPLACE_EXISTING)` and the consumed one-boot
`AllowProtectedRenames` value. The diagnostic refuses foreign queues or existing
permission, records original/candidate hashes before enqueue, and verifies the
exact native pending pair. After clean shutdown/cold boot the desired hash is
present; after the reverse queue and another clean boot the original hash is
restored. Both times the queue and permission value are absent afterward. WFP
policy and original cache stay unchanged. The source compiles/audits and both
compiled variants pass target-aware clang-tidy. Evidence:
`target/follow-through/nt-boot-rename-v1/acceptance.json`.

This closes mechanism feasibility on Windows 2000; the production installer
still needs durable multi-file queue ownership, interruption recovery and
cancellation. The diagnostic is deliberately fixed to one runtime and never
claims that enqueuing alone is an installation success. The production adapter
is now being integrated independently from the driver journal. XP uses the same
frozen helper on a separate prepared copy; its result remains pending.

Existing-driver lifecycle acceptance is now complete on both Windows 2000 and
Windows 98, with four fixed command receipts and cold device checks. Win98's
source-media issue was resolved using a temporary process-local source list and
bounded queue callback; same-INF pending replacements require inspecting the
queued bytes before classifying the node as original. The final accepted path
needs no media dialogs or repeated binding. Source adapters cover224 injected
mutation failures plus these target-specific cases. Exact receipts are
`target/follow-through/driver-lifecycle-nt-v2/acceptance.json` and
`target/follow-through/driver-lifecycle-win98-v3/acceptance.json`. First install
from stock/unbound VGA remains a distinct gate.

### XP system runtimes and the first-install distinction

XP passes all four ordinary system Direct3D6/7/8/9 HAL pixel probes. Its protected
DDRAW also passes the real deferred install and restore cycle through clean
boots, including exact original hash restoration and automatic removal of the
queue/one-boot permission. SFCDisable stays zero and the original cache is
unchanged. D3D8/9 are protected on XP; in this feasibility fixture they were
coldstaged before the first boot and remain the candidate. The full multi-file
installer transaction still requires its own acceptance. Exact receipt:
`target/follow-through/xp-boot-rename-v1/acceptance.json`. A stale native path was
rejected before preparation; the corrected manifest uses the already accepted
frozen native image rather than attributing a mutable build path to an old hash.

The next first-install case is explicitly unbound PCI plus VGA fallback, as found
in the pristine XP UTM source. It has no prior display INF node. Driver records
now distinguish stock, DreamGPU and unbound bindings without fabricating INF
identity; V2 retains the V1 byte layout and can still decode old stock/owned
records. The existing224 mutation-failure checks remain passing. Unbound capture
is not yet activated: it needs durable ownership of newly published OEM INF/PNF
files and a checked device-removal/re-enumeration path. Removing a new package
must never delete a pre-existing or shared OEM package.

### NT first-install ownership integration (in progress)

The driver journal now distinguishes stock, existing DreamGPU and genuinely
unbound NT PCI devices. V2 retains the 11,076-byte V1 layout and reads old
stock/owned records; V1 cannot encode an absent prior binding. Detection requires
actual driver property, service property and driver-key absence. SetupAPI sets
are refreshed rather than reusing devnode data after removal. Actual-source
ASan/UBSan passes the existing 224 mutation failures plus V1 loading, malformed
unbound records, registry-error distinction and changed-adapter identity checks.

First-install mutation remains guarded pending integration of owned OEM INF/PNF
publication/removal and its recovery journal. The pristine imported XP UTM disk
will be the runtime first-install source; earlier XP tests with an existing
DreamGPU binding do not satisfy that gate.

### Complete ICD dispatch and Win98 deferred-file adapter

The final display-list batch now passes 113 Rust host tests on each host, strict
Clippy, 35 Mac/33 Linux native GPU tests, guest sanitizers, pinned GCC16 and
three-translation-unit target-aware tidy. Immutable list payloads, replay/resource
ownership and deferred compile errors are implemented. Coverage is 146 direct
exports plus 190 typed adapters, zero missing slots; four prior native-provider
edge cases remain documented. Frozen native binaries and source hashes are in
`target/follow-through/gl11-native-v1` and `lists-acceptance.json`. Production
driver packaging is now being consolidated around the verified ICD path.

The Windows98 reboot adapter uses actual WININIT.INI DOS short-path pairs,
preserves unrelated sections/comments/rename entries, and supports repeated NUL
deletions. Checksummed private before/after receipts precede public renames;
recovery validates exact bytes, including the boot-consumed WININIT.BAK when
interrupted between publication and completion. Tests exposed and fixed a staging
retry that preserved bytes but lost hidden/system attributes. ASan/UBSan now
passes 59 injected mutation failures, cancellation, replacement/deletion/reboot,
consumption before completion, foreign queue collisions and corrupted receipts.
Pinned GCC16 analyzer/Werror and explicit header-filtered target-aware tidy pass.
Actual Win98 mechanism and final installer execution remain pending.

### Windows98 deferred file replacement mechanism accepted

An independent Win98 fixture now passes the actual WININIT sequence: queue an
existing-file replacement, an existing-file deletion and an originally absent
file installation; clean shutdown/cold boot; verify all exact desired identities;
queue reversal; clean shutdown/cold boot; verify original identities and the new
file's absence. Receipt: `target/follow-through/win98-boot-queue-v1/acceptance.json`.
No display driver or public API runtime was changed by this synthetic mechanism
test. The final fixture is paused. The frozen mechanism binary precedes later
competing-writer/alias-path hardening, covered by separate actual-source fault
tests; final installer acceptance still needs the latest coherent package.

The initial controller lacked the selected fixed route and rejected the request
before guest helper execution. A newer frozen controller resolved that. The
NT-only shutdown helper was unsuitable for Win98; the fixture's existing Win98
JRGSTOP helper completed both actual clean poweroffs. Automation remains fixed
serial requests and QMP program launch, with no screen/menu decisions.

### Whole-installation coordinator and normal activation verification

The installer now has separate per-generation runtime executable/proof-helper
copies, so new packages do not overwrite a prior generation's recovery tools.
Six exact-hash ordinary-loader probes run from the authenticated private tools
directory, with matching clean current directory and verified system providers.
Successful helper bits and child PIDs are durable; retries skip acknowledged
passes. Failed GPU proofs report failure, not a fabricated pending reboot.

The top-level policy sequences driver then providers, reverses that order for
removal, persists direction/phase before component work, and owns a separate
DreamGPU.Setup RunOnce continuation. Its pure actual-source policy gate passes
64 interruption cases and 40 pending/error cases, with GCC16/analyzer/tidy. The
concrete Win32 coordinator and main routing are being integrated and tested.
Readable setup messages and a normal Windows restart offer replace raw JSON
message boxes; /silent and machine-readable receipts remain available.

### Continued system-installer integration

The pristine XP fixture now has actual first-install, cold driver activation and
cold restoration to its original unbound state. The intermediate removal returned
a conflict after successful service deletion because XP exposes the marked
service as disabled. The source now accepts that transition only for an already
owned deleting service; the actual baseline was restored and the fixture shut
down cleanly. See `target/follow-through/xp-first-install-v2/runtime-ledger.json`.
The classifier fix will be covered by the coherent installer run, without repeating
the unchanged standalone cold-boot proof.

The global adapter passed 95 actual Win32 seam interruptions, covering operation
epochs, private resume-executable identity and foreign RunOnce refusal. Driver
generation provenance and provider preparation recovery are being integrated
separately; those component internals are not inferred from global seam tests.

Added fixed complete-installer serial operations and a bounded cold-boot controller
(`scripts/fixtures/system-install.py`). The acceptance helper pins the executable,
reads every checksummed global record without journal repair, records phase/epoch
and provider generation, and distinguishes pending from terminal completion.
The first helper target compile passed pinned GCC16/Pentium III/no-SSE with
`-fanalyzer`; final schema/helper/runtime acceptance follows the coherent package.

### First complete installer runtime, September 13

The frozen combined installer at
`target/follow-through/system-installer-initial-v1/inputs/dreamgpu.exe`
has SHA256 `2c8de4fc90bb9f2844a5cb2a2ad108be32a116b388ddb08a85b1962a4850ae30`.
It includes both production display/ICD packages, the GL1.1 frontend, all six
normal-loader proofs and global schema3 coordination. Pinned GCC16 compilation,
PE4/import/payload audits and complete installer/header clang-tidy pass. Current
all-target/all-feature Rust Clippy also passes after three new toolchain lint
fixes in native test array decoding.

The first untouched-XP run completed driver installation and its cold-boot
activation, then stopped with ownership conflict29 at global provider phase1.
The controller preserved the actual guest error, and the owned fixture was
cleanly powered off for exact journal/filesystem inspection. This is an identified
integration failure, not complete system activation. Evidence lives under
`target/follow-through/system-installer-xp-v1/install-cycle`. Investigation uses
the failed disk and source snapshot rather than repeating the same install.

The combined ICD/frontend source contract now passes actual Drv-to-frontend
format, thread, context-sharing, swap and resize tests. Its source-only receipt is
`target/follow-through/icd-frontend-contract-v1/acceptance.json`; no production
GL binary changed for this test.

### XP complete system activation accepted

The same frozen `2c8de4fc...` installer now completed its remaining provider boot
and all six normal-loader tests on XP with the frozen GL1.1 Linux native
`1733ead1...`. Evidence:
`target/follow-through/system-installer-xp-v1/provider-resume-cycle/operation-0`.
System Microsoft `opengl32.dll` loaded system `dgpuicd.dll` and checked 8,192
pixels/two swaps; system Glide2 checked 2,304 pixels/eight swaps; Direct3D6/7/8/9
checked 1,024/2,560/512/512 pixels and presentation/cleanup. The global installer
returned activated0 only after these checks. This proves initial installation
and API activation, not yet uninstall, changed-driver upgrade or other OSes.

The earlier XP conflict was caused by three unrelated Plug-and-Play pending
temporary-file deletions. Read-only inspection verified that all 13 DreamGPU
runtime replacements remained privately staged and original public DLLs intact.
After a cold boot consumed Windows' queue, the same installer queued its own
files successfully. The source now defers disjoint valid OS queues with a bounded
private wait receipt; ambiguous/colliding paths remain conflicts and foreign
queue/permission bytes are unchanged. The corrected source passed fault injection,
GCC16 analysis and target-aware tidy; the accepted executable predates that
classification improvement. No unchanged complete install was repeated.

Win98's first acceptance helper stopped before launching setup due to its mutex
preflight. The helper now uses the same CreateMutex-based API as setup and logs
the failing gate explicitly. With that helper corrected, Win98 setup reaches
post-stage preparation but exits27 before a global journal is created. Its exact
private/runtime state is being inspected before another installer change. A
40-second poweroff bound was too short on the XP runtime-replacement boot; the
automation now waits up to 120 seconds and still refuses forced reset.


### Complete Windows 2000 activation and XP removal recovery

The original full `2c8de4fc...` installer now completes unattended Windows 2000
installation through two clean cold boots. All six normal-loader GPU checks
pass before activated0, GLOBAL phase2. Exact receipts and helper output:
`target/follow-through/system-installer-win2000-v1/install-cycle`. The activated
disk is retained separately for normal game and upgrade tests.

XP initial uninstall restored providers, removed the owned device/OEM package,
and marked its service for deletion, then misclassified a stale devnode lookup
as exit26 while the kernel still held the service. The recorded stopped failure
was continued through one additional cold boot, without repeating uninstall;
`system-installer-xp-v1/uninstall-final-cycle` returns removed13, phase5. Source
regression tests reproduce and fix the pending-unload classification. Independent
final baseline hash/registry inspection follows the terminal receipt.

Win98's post-stage exit27 was a fixture provenance failure: the selected source
already contained an unjournalled Wine system namespace. No public runtime bytes
were changed. The corrected native-restored source is
`win98-system-ddraw/rollback-v1-staged.qcow2`, SHA256
`38b45c2dc2d312266e824ca54b5418b93254b95bd5627a47980dcc98a326e41b`.
With that baseline, initial setup requests its driver boot successfully. The
next startup stalls at Updating System Settings: read-only evidence shows driver
activation succeeded and 12 provider replacements were queued, but Windows 98
repeated the newly re-registered RunOnce continuations before Explorer started.
The driver/runtime startup adapters are being corrected to use a next-login
continuation with durable ownership, without this same-boot loop.

The combined `8367af72...` candidate passes GCC16, PE4/import/resource audits,
full-header clang-tidy and analyzer checks. It contains driver generations,
repair, initial extraction recovery/cancellation and foreign NT queue deferral.
It predates the newly identified Win98 startup correction; its source and receipt
remain frozen and must not be relabelled as the corrected candidate.


### Mac normal-system API acceptance

A stopped copy of installer-activated XP was staged with the six unchanged
normal-loader helpers in its root directory, then cold-booted on Mac with the
GL1.1 native binary `f35ce5c4...`. All six actual OpenGL/Glide2/Direct3D6–9
pixel/presentation probes passed; evidence is
`target/follow-through/system-installer-xp-mac-v1/acceptance.json`.
The source activated disk remained unchanged. No installation was repeated,
no API DLL was placed beside an executable and the NIC was disabled before
continuing the guest. These results cover the exact initial package providers,
not the later installer's newly added lifecycle transitions.

### System-wide follow-through: normal games, recovery, and desktop sampling

The initial complete installer activated all six API proofs on XP and Windows
2000. The same activated XP providers pass OpenGL, Glide2, and Direct3D6/7/8/9
on the Mac host. XP initial removal now has an independent exact-original audit:
all18 global entries restored, owned service/OEM package removed, and the PCI
device reenumerated unbound. Current8367 upgrade and provider-only repair pass
on independent XP copies. Repair retained the driver generation and repeated
only the affected DirectDraw-dependent proofs; it did require three D3D6 attempts
before completion, so this is not an exactly-once execution claim.

An interrupted8367 rollback exposed Windows regenerating a borrowed PNF cache.
The fix authenticates the borrowed INF and leaves its derived PNF alone. A new
executor recovery handoff remains necessary to finish the old pending journal
without overwriting frozen executors or erasing payload provenance.

Win98 startup continuation now uses an owned persistent Run entry, avoiding the
RunOnce rearming loop. Its next boot revealed historical Wine DDRAW bytes in
SYSBCKUP restoring themselves over the captured Microsoft public DLL. The new
installer journals existing matching cache/public pairs; it neither invents
absent cache entries nor touches NT caches. Combined6382 is under actual Win98
and NT lifecycle acceptance; successful source checks are not activation.

Mac normal system-loading Unreal Direct3D and Glide both pass with the original
game renderer and no app-local providers. The full640x480 D3D client image is
retained. Half-Life's normal launch completed379 frames in5.387s (70.35fps), but
its current receipt lacks actual ICD module evidence; do not count that number
as confirmed hardware-path acceptance. The runner is acquiring that evidence.

The current-native system-installed desktop pair retained all16 acknowledgements,
zero trace drops, and240Hz. Copy/scroll/repaint and idle CPU regressed despite
improved fill. Sampling identified repeated empty VGA dirty-snapshot TLB resets,
with both virtual CPUs otherwise sleeping. A narrow QEMU fix passes actual-body
atomic/bitmap boundary tests and builds on Linux/Mac. One changed Mac capture is
underway against the saved same-source installed run. Evidence lives under
`target/follow-through/{system-desktop-mac-v1,dirty-snapshot-v1}`.

Win98 v5/6382 now reaches activated exit0 after cold2 with all six API pixel
proofs. Cold1's explicit helper raced the real startup continuation and returned
busy28; the automatic executor independently completed driver verification and
queued13 owned renames. The controller followed that durable evidence into the
required queue-consumption boot, without replaying installation. Current6382 XP
baseline uninstall also reaches restored exit13 after two clean boots; final
readonly restoration audit and fresh reinstall are pending.

The Mac empty-dirty-snapshot candidatee60d reduced idle QEMU CPU28.35→8.63%
of one core, copy mean11.11→10.45ms, and repaint19.44→14.01ms. Fill and
scroll remain mixed; no universal parity claim. A separate sampled scroll run
shows most remaining VGA time in the unconditional post-snapshot vCPU rendezvous.
A follow-on change skips that fence only for an empty captured TCG snapshot,
preserving real dirty writes and non-TCG listener callbacks. Its actual MemoryRegion
wrapper plus bitmap-body ASan/UBSan test passes; independent review/build/runtime
checks remain in progress.

The follow-on empty-snapshot rendezvous fix91a7Mac/b4c2Linux passes both-host
native builds and actual-source sanitizer/bitmap gates plus independent race
review. The saved changed Mac run reduces idle QEMU CPU again to5.68% of one
core (pre-install7.65%). Fill8.83ms, copy10.28ms, scroll10.23ms and repaint16.97ms
are completed16-acknowledgement means with zero dropped records and240Hz. The
remaining short desktop distributions overlap and some captures contain guest
background CPU bursts; this is not universal latency-parity evidence. All original
and changed measurements remain in dirty-fence-v1/comparison.json.

Win986382 baseline uninstall now has an independent exact-original audit: all14
global files and both driver files restored, native DDRAW public/cache botha278,
and absent D3D8/9 caches still absent. The installed source82f5 and restoredb9b2
are cleanly stopped and preserved. XP6382 fresh reinstall after removal also
reaches terminal activation after two clean boots; readonly identity audit is
underway. Next coherent installer adds authenticated pending-rollback recovery,
bounded mutex ownership handoff, and exact-original Win98 cache repair.

The final native copy trace contains349 complete inline submissions, no errors,
no continuation work, and763.6MB processed. Median native batch elapsed179us,
work164us, and non-work8us; this separates actual 2D execution from the roughly
10ms input-to-visible-ack metric. The diagnostic trace adds logging overhead and
is not a latency benchmark. Evidence: dirty-fence-v1/macos-copy-native-trace.

Diskless native tests exposed a harness issue with relocated frozen binaries:
firmware lookup still depended on QEMU build-directory placement. The harness
now passes an explicit firmware directory (or DREAMGPU_FIRMWARE_DIR override).
The original missing-BIOS failures are preserved separately from rerun results.

Normal Half-Life hardware-path acceptance now passes on Mac: system
opengl32/dgpuicd/dgpugl loaded in the exact owned hl/hw process, with native GPU
draws and presented drawable frames.379 frames/5.408s=70.083fps. Retail engine
GL strings are absent, explicitly recorded; actual module and host execution
evidence establish the route. The completed run is retained, not replayed.

Fully installer-activated Win986382 source774d passes all six API pixel proofs
on the final91a7 Mac native as well as an ordinary system DirectDraw 2D caller
(exact copy/color-key/primary pixels, two object lifecycles,1024 completed blits).
Its clean app directories contain no replacement provider DLLs. Normal games
are now running against this same source.

Win98 Mac normal Half-Life also passes hardware-path proof:379 frames/5.351s
=70.833fps with system ICD modules plus native draw/presentation evidence.
The `sysddrawnative` comparison invocation on this same installed Wine public
provider correctly refuses its native-provider assertion; this was an invalid
baseline configuration, not a product failure, and yields no native timing.
Do not use it for a performance comparison or weaken the provider assertion.

Normal game rendering now passes on both hosts (Win98 and Windows2000):
system ICD Half-Life with native hardware-path proof, and system Direct3D/Glide
Unreal Tournament with full client bounds. Linux W2K cleanly shut down. Win98
shutdown after these games fails on both hosts; those private guests are paused,
not clean, and must not become fixture sources. The preserved installer-only
Win98 sources had clean shutdowns and remain unchanged.

The Win98 fault maps to KRNL386 ordinal535, VWin32_BoostThreadGroup. Linux
faults dereferencing a null thread lookup result; Mac faults in VMM priority
adjustment reached by the same function. This does not yet identify a product
bug. Both Glide test receipts reveal forced TerminateProcess cleanup after a
2-second close deadline; their graphics proofs pass, but graceful game exit was
not established. The automation is being corrected to close the actual retail
viewport, wait a bounded interval, and fail its clean-exit gate on termination.
No speculative driver patch is justified by this evidence.

The maintained DGSTOP artifact used here is NT5-only and was rejected by the
Win98 loader. That failed invocation is retained separately; the subsequent
legacy PE4 shutdown helper reached the kernel fault. The common shutdown tool
also needs a correctly targeted Win98-compatible build.

XP corrected-executor recovery is complete and audited: new b1f8 installer
resumes the preserved8367 failed rollback without rewriting old G/D/T executors,
payloads or backups. All13 providers and two driver files match immediate-before
images; recovery receipt binds the old/new executors and all owned startup and
NT queue/permission entries are absent. Recovered disk32d1 is cleanly stopped.
Evidence: system-installer-xp-recovery-v1/acceptance.json.

The final-native desktop comparison now uses91a7 for both pre-install and
installed states, with the same app/probe/display. All16 acknowledgements,
zero dropped records,240Hz and clean pre-install shutdown. Before→installed
mean ms: fill7.84→8.83, copy9.59→10.28, scroll9.86→10.23, repaint16.81→16.97;
idle QEMU core%5.18→5.68. The sampled empty-dirty reset/rendezvous regression is
removed. Remaining short fill/copy distributions do not establish universal
parity. Saved comparison: dirty-fence-v1/matched-final-native.json.

The first corrected Glide exit diagnostic still failed its graceful-exit gate:
WM_CLOSE on the exact viewport did not stop UT within15s. This time its subsequent
Win98 shutdown completed cleanly, so forced termination alone is not sufficient
to explain the original cross-host shutdown fault. Original W2K Glide runs also
used forced cleanup while W2K shut down normally. Historical rendering results
remain valid; their graceful-exit claims are withdrawn.

Retail Window.dll disassembly provides a concrete automation fix: its owned
WLog's ID_LogFileExit command calls the engine's appRequestExit. A hash-bound
helper now uses that exact engine command, with module-path/owner validation and
pre-force window/thread/log diagnostics. Runtime verification is in progress;
no speculative GL/VxD patch has been made.

Installer GUI automation is implemented as a hash-bound sysui route using the
actual /continue flow and the existing durable outcome verifier. It reads only
the owned standard dialog, checks exact result text/button identity, declines
reboot through verified default IDNO, and requires the corresponding process
exit status. Foreign/reused windows, hung controls and unknown dialogs fail.
Twelve actual-adapter sanitizer cases, PE4 GCC16 compilation and target-aware
clang-tidy pass. Guest UI acceptance is next on an independent activated copy.

Linux's corrected retail-engine quit completes the original three-game history
(Half-Life → UT D3D → UT Glide) and clean Win98 shutdown. The first Mac attempt
was an invalid deployment: root copied new UT helpers to C:\ while the fixed
runner uses C:\SIERRA\Half-Life. Its wrong-path receipt remains preserved and
that private guest shut down cleanly; it is not evidence for the changed helper.
Both FAT/NT stagers now validate explicitly declared probe hashes against the
actual runner's launch paths before disk work. Seven path/identity cases pass.
The corrected Mac helper readback targets the actual launch locations.

The interactive UI fixture also exposed a startup-control race before the guest
was resumed: QMP's Unix socket pathname existed before it accepted connections.
Readiness now retries only read-only query-status within the existing deadline;
network disable and cont remain outside that loop and run once. Three new cases
cover transient readiness, deadline failure, and never retrying a mutation.
All29 fixture tests pass. No guest install was repeated for this controller fix.

### Corrected full game exit history, both hosts

The exact-path corrected engine-quit helpers complete Half-Life → UT Direct3D →
UT Glide and a no-force Win98 shutdown on both hosts. Mac ledger:
`target/follow-through/win98-graceful-exit-v1/macos-history-v2.json`; Linux:
`history.json` in the same evidence family. This closes the changed lifecycle
gate, without repeating or relabelling historical graphics/FPS evidence. The
original faulted disks stay preserved and are not clean fixture sources.

Interactive W2K setup reached the exact system-wide installed success dialog.
The first sysui helper failed before dismissal with Win32 error1421; the failure
boundary needs control diagnostics. One Return dismissed that known dialog and
the private guest then shut down cleanly. This is an observed product UI result,
not an automated UI gate pass. Diagnostic helper9722 adds owned button IDs and
explicit failure stages; it retains the exact7afe installer and fixed operation.

The isolated Mesa26.1.8 hardware proof fixes integer INDEX_OFFSET precision and
fractional negative PixelZoom: direct EGL and unchanged DreamGPU GPU oracles
pass on the Framework's radeonsi hardware. Cargo/private runtime integration is
in progress; no system Mesa replacement or global library-path change is used.
Legacy 1D borders remain a separate unaccepted implementation until sampling,
copy, query and lifetime oracles pass.

### Automated installer UI acceptance

Owned-control diagnostics established that Windows2000's sole MB_OK control is
ID2 rather than ID1. Corrected helper452485 checks the sole visible button's
class, owner, ID and exact OK label, posts the actual command, observes process
exit0 and verifies terminal GLOBAL activation. It passes16 actual Win32 adapter
sanitizer cases, GCC16 C++23/PE4 plus analyzer, and target-aware LLVM22 tidy.

The guest produced a successful receipt; the controller initially expected
`sysui` instead of the real `/continue` operation's `sysresume` receipt. The fixed
parser requires dialog action, exact installer exit and continuation receipt to
agree. Its26-test controller suite passes. Reprocessing the preserved successful
bytes closes this gate without repeating the guest operation. Evidence:
`target/follow-through/installer-ui-v1/ui-acceptance.json`.

### Default private Linux Mesa runtime accepted

Cargo now builds the pinned Mesa provider and an immutable runtime containing
its actual QEMU ELF, linked libraries, vendor descriptions, source-package
identities and notices. Linux `native_dir` is `qemu-build/bin`; Rust launchers
authenticate their runtime before replacing themselves with the real QEMU ELF.
Ninja's original outputs remain unchanged. Relocation, byte/symlink tampering,
actual process ownership and standalone BIOS boot/clean quit are tested. Juke's
Vulkan environment is unaffected.

Final launcherdd6efd / runtime8ece47 passes44 launcher/fixture/sampler checks,
Cargo native builds, both Rust Clippy gates and three native GPU gates with no
manual provider environment: exact extreme integer transfer, fractional/negative
pixel zoom, and normal drawable export/release. Evidence and frozen inputs:
`target/follow-through/gl11-runtime-v1/acceptance.json`. Hardware acceptance is
Framework AMD radeonsi; other compiled hardware drivers are not runtime-tested.
The private provider is not a replacement system display-driver installation.
The border-texture proof remains separately reviewed and is not in this receipt.

### Final verification and desktop trace decomposition

Strict Mac workspace/all-target/all-feature Clippy and formatting pass. All212
host automation tests pass (two platform-specific skips) after removing a stale
registry test overload and fixing the runtime identity test's expected canonical
Mac `/private/var` path. Neither fix changes production registry or process logic.

The benchmark summarizer now joins probe counters to the exact acknowledged
frame generation and records input→publication→acquisition→ACK stages. Missing,
ambiguous, dropped or out-of-order chains yield no derived stage result. Its24
source tests pass. Reprocessing the saved final-native matched captures preserves
all16 chains per scenario in `dirty-fence-v1/matched-frame-stages.json`.

The installed-minus-before mean publication delay is +1.024ms fill, +1.883ms
copy, +0.495ms scroll and +1.127ms repaint. Frame-worker acquisition stays near
60–80µs; later renderer preparation is similar or lower after installation.
Publication includes guest execution and producer refresh, so this identifies
which portion needs investigation without claiming it is GPU execution time.
The earlier native copy trace remains the actual native-work measurement.
No new benchmark run or speculative copy-path change was made for this analysis.

### NT startup ownership correction

The attempted corrected-executor recovery had not run when a private W2K guest
stalled before Explorer. Actual vCPU PCs at60/100s mapped to frozen7afe file
hashing and SHA256 compression. The old continuation was repeatedly executing;
a missing login key was not the cause, and fixture-only autologon did not fix it.
Original failed disks and unchanged journal/log identities remain preserved.

Candidate292be uses persistent NT Run/schema3 for new GLOBAL startup receipts,
while historical schema1 remains RunOnce with exact retirement semantics. GLOBAL
owns component startup only after verifying its own durable arm; missing/foreign
registration blocks driver mutation. New standalone NT driver journals use
Run/version4; legacy key before-images are not reinterpreted. The combined source
passes fault gates, GCC16 analyzer and LLVM22 tidy and is frozen on both hosts
under `system-installer-startup-owner-v1/inputs`. Actual recovery now passes on an independent predecessor copy: terminal rollback12, exact18 original provider images, two prior driver files and OEM2 binding restored, pending queues consumed, and all DreamGPU startup registrations removed. A subsequent baseline uninstall returns13 after one clean boot and preserves the original OEM inventory. Both final disks are cleanly stopped. Win98 upgrade and immediate rollback also pass exact14-provider/two-driver/cache audits. A separate controlled SYSBCKUP-only repair reaches failure26 and remains under investigation; these successful lifecycle results do not close that repair gate.


### Desktop source and configuration comparison

`desktop-state-audit-v1/assessment.json` records a read-only comparison of the
stopped before/installed fixtures. All48 nonempty miniport function bodies and
all PE sections match. The original17 display-hook mappings and GDI hot paths
match; changes cover ICD hooks, driver hook count, and associated binding code.
All220 service startup configurations match. Scoped video, priority and startup
settings do not identify a cause for the saved publication delay. Actual runtime
background activity remains unmeasured. The first disassembly extractor was
rejected for empty instruction arrays; the corrected receipt validates nonempty
bodies and symbol cardinality. No speculative driver change follows this audit.

The next discriminating performance trace must separate input delivery, guest
handler/work, native 2D completion and refresh publication without adding disk or
serial I/O inside the measured guest work. Existing measurements remain valid;
this source comparison does not establish universal desktop parity.


User observation during Mac XP UT acceptance: Direct3D shading/shadows looked
better than Glide but perceived performance was poor. Added a deferred profiling
and optimization pass to plan.md; no pivot or FPS claim. Mac XP D3D and Glide
ordinary-provider/engine-exit acceptance passes on native0b105f, followed by
clean guest shutdown (`xp-normal-system-games-v1/macos-shutdown.json`).


### Windows 98 cache repair accepted

The controlled SYSBCKUP-only drift case caught a missing cache-path predicate in
`seed_repair`: assessment and capture allowed the exact known-original cache,
but verifier seeding omitted it. The narrow allowlist fix keeps NT cache refusal.
Actual verifier fault tests fail before the fix and pass afterward. Candidate
17e68b repairs the independent drifted Win98 copy after one clean boot, with all
14 providers, two drivers and caches audited. VERIFY.JRN advances completed mask
51→63: only affected Direct3D6/7 proofs rerun, retaining valid unrelated proofs.
The failed292 case and original source remain preserved. Final combined guest
frontend packaging follows; this receipt identifies installer17e68b only.


### Final source quality sweep

Both Mac texture-control GPU oracles pass on frozen native0b105f, including
actual border uploads, split signed updates and exact readback. All118 Rust host
tests and strict workspace Clippy pass. Complete maintained C/C++ formatting
passes after five whitespace-only fixes, mirrored before final package capture.

The full parallel workspace suite exposed a test isolation defect: the global
trace recorder legitimately captures transport events from other concurrent
tests. The flow test now checks its uniquely named cross-thread events and
rejects stale-session events without assuming unrelated event absence. No
production recorder behavior changes. The corrected full workspace suite passes,
as do216 host automation tests (two platform-specific skips). Exact logs are
under `gl11-border-macos-v1/`.


### Scope correction applied

All three agents stopped installer acceptance/recovery and conformance campaigns.
No unfinished recovery source edits existed beyond accepted0aaf; that investigation
was read-only. All owned installer/game VMs are stopped or merely staged. Removed
the unused new border diagnostic and CMake target, and the unfinished desktop QPC
header/probe changes/decoder/tests. Completed production graphics fixes and saved
profiles remain. The final grouped Mac native build completed; no further
conformance runs are scheduled.

New assignments: NT adapter metadata and Properties crash; Mac UT Direct3D
profiling/optimization; shared native OpenGL/Glide performance. The D3D performance
pass is now active priority, superseding its earlier deferred status.

## Game performance after the scope reset (September 13)

The existing Mac UT Direct3D workload was sampled once during its measurement
phase. CPU0 spends 1,066/1,192 samples executing TCG while the native renderer
waits for work in 1,065/1,172 samples. This points to guest-side translation and
command production as the first substantial improvement, not GPU saturation.
Evidence: `target/follow-through/mac-ut-d3d-perf-v1/profile/qemu.sample.txt`.

The native implementation now avoids a cancellation mutex per command, reuses
validated context/drawable lookup within batches, and restores only the current
attributes affected by an array packet. Focused checks and the Linux build pass.
The isolated cancellation timing is not a game FPS result; the changed game
candidate will include the WineD3D vertex-array improvement.

Existing game automation now summarizes submission throughput and received GPU
frame intervals from its existing counters and traces. Phase boundaries use the
same `CLOCK_MONOTONIC` clock as native traces: Python's Mac monotonic clock has a
different sleep offset. Old captures without matching boundaries remain usable
for counter totals but are not misrepresented as measured frame pacing. Saved
Mac baseline counter summaries are 55.25 D3D and 15.71 Glide presents/second; these
are submission rates, not engine-reported FPS or physical display measurements.
No unchanged baseline was replayed.

### Changed native OpenGL game on Linux

The direct Half-Life `jrgperf` timedemo completed once: 379 frames, 1.996 seconds,
**189.844 engine-reported FPS**. The actual process loaded system
`opengl32.dll`, `dgpugl.dll` and `dgpuicd.dll`; native draw commands and GPU
drawable presentation were recorded without capture drops. This proves the
accelerated game route and gives a current throughput result. It is not a
matched speedup claim against an unchanged baseline. The unchanged Windows 2000
game image used the existing renderer-proof runner, without new helper setup.

The Linux candidate contains the native command/array improvements and actual
64 MiB framebuffer default. Its ELF is `8c5444e6…`, immutable runtime
`68d75f2a…`. Mac candidate `97c1a2de…` contains the same source changes.
Evidence: `target/follow-through/game-opengl-performance-v1/game/run.json`.

### Performance candidates: measured results, including rejected changes

- Corrected Mac WineD3D candidate `80362e92…` uses 99,800 array draws instead
  of the scalar vertex workload. Commands per present fell about 72%, and the
  measured transport bytes fell from 115.1 MB to 80.3 MB. Its sampled result is
  55.42 presents/s; this is not a demonstrated improvement over the saved
  unsampled 55.25 presents/s. The remaining guest CPU cost needs guest-PC
  attribution. An earlier candidate did not engage because Wine's synthetic
  multitexture flag was mistaken for actual multiple texture units; its failed
  result is retained instead of replaying the baseline.
- Glide `b92389bf…` rendered correctly on Mac XP and Linux Win98, but the cache
  was rejected as a performance candidate. Mac throughput was 15.01 presents/s
  versus the saved 15.71; texture allocation/deletion increased sharply. The
  actual log records only 115 retained-image hits in 7,821 misses, so raw snapshot
  hashing/storage is being removed rather than expanded. The next change keeps
  identical-download/range improvements and restores efficient storage reuse.
- The Linux Win98 run recorded 102.61 received GPU frames/s with correct full
  client rendering, no capture drops and no native GL rejections. Its selected
  old benchmark helper failed the post-measurement capture/exit handshake, so
  the automated overall result remains failed. One in-engine console `quit`
  and the existing guest shutdown helper closed it normally; no game replay
  was used for cleanup. Use the already-built current helper in the next
  changed-candidate image. This is not a driver crash or a new lifecycle task.

Evidence: `wine-vertex-perf-v1/`, `glide-texture-performance-v1/`,
`glide-texture-game-linux-v1/` under `target/follow-through/`.

### Accepted simpler Glide and packaged game results

The corrected Glide implementation removes raw snapshots, FNV hashing and
retired-content searching. It keeps exact active conversion keys, identical
download skips, precise invalidation, and compatible GL storage reuse. Mac XP
presentation throughput increased from 15.71 to19.80/s (+26%). TexSubImage updates
per present fell from132.5 to104.4; the measured run allocated413 textures and
deleted none. Full rendering and normal game exit passed.

The actual packaged Win98 Glide binary passed its current helper on Linux:
1,385 measured native GPU frames in about10 seconds (138.50/s), median7.757ms,
p9510.156ms, no rejects or capture drops. It allocated435 textures and deleted
none. Normal game exit and OS shutdown completed. This uses KVM and is not a
Mac/TCG speed comparison. Evidence: `glide-texture-game-linux-v2/acceptance.json`.

The current WineD3D array/surface path also passed on Linux XP using KVM:3,640
measured native frames in10.0027 seconds (363.90/s), median2.587ms,p953.719ms.
The authoritative frame shows full rendering with the existing shading and
effects. Evidence: `wine-vertex-game-linux-v1/game/run.json`.

The combined Cargo guest build passed and delivered `target/guest/dreamgpu.exe`
and both OS packages on both hosts. Installer SHA`d2366fd9…`; accepted NT display
and miniport binaries are included. Receipt:
`system-installer-performance-v1/inputs/acceptance.json`. No extra installer
lifecycle or smoke campaign was performed.

The next Direct3D target comes from complete guest-PC sampling: byte-copy and
byte-fill helpers account for47.4% of frontend instructions (331.5 million
of698.8 million). The earlier37% estimate counted only their hottest loop blocks
and omitted entry/tail work. These are instruction counts, not cycle shares.
Memory helpers are the next production optimization; separate Glide attribution
and exact x87 feasibility work remain bounded performance investigations.


### Accepted Mac Direct3D memory/CPU improvement

The grouped integer word/chunk memory helpers and guarded x87 PC24 arithmetic
passed the normal Mac UT Direct3D run at **65.07 presents/s**, up from the saved
55.42 (+17.4%). Native received-frame median/p95 gaps fell from18.299/23.233ms to
16.109/20.181ms. This is submission/frame-delivery throughput, not engine FPS or
physical scanout. The textured/shaded scene remained intact; normal game exit
and Windows shutdown completed. No unchanged baseline was repeated.

The CPU change is restricted to basic x86 arithmetic with exact binary32
operands and interior-normal results at the game's existing24-bit precision and
nearest-even rounding. Result bits and current-operation exception flags match
the original software arithmetic; all other cases fall back. Generic SoftFloat,
transcendentals and game precision remain unchanged. The actual-source
comparison passed1,050,704 cases. Bounded memory accesses passed sanitizer and
i686 machine-code checks without SSE or x87 in the helpers.

Evidence: `guest-memory-performance-v1/acceptance.json`,
`x87-pc24-candidate-v1/manifest.json`. Candidate native`ea3b189f…` and frontend
`6a633248…` are measured together; no per-fix speedup is inferred.

The remaining Glide target is now measured: packed16 expansion accounts for
40.25% of recorded guest instructions. The existing packet format can carry
those pixels directly, eliminating guest expansion and reducing transfer bytes.
This change is being integrated with the accepted CPU/memory work before final
outputs are refreshed.


### Packed texture forwarding: Linux game result and refreshed outputs

The paired packed16 frontend/native passed the normal Win98 UT Glide workload
on Linux/KVM at184.31 presents/s, up from138.50 (+33.1%). Median/p95 received-frame
gaps fell from7.757/10.156ms to5.536/7.327ms. Full640×480 rendering, native command
acceptance and normal game exit passed; Windows then shut down normally. This
is measured frame delivery, not engine FPS or physical scanout. The changed
frontend includes the accepted word/chunk helpers. Evidence:
`packed16-game-linux-v1/acceptance.json`.

The combined Cargo builds passed and refreshed `target/guest/dreamgpu.exe`
(SHA`c2b4bae4…`) and both Windows packages on Mac/Linux. Paired frontend`a277e72a…`
uses direct packed16 pixels and requires updated native support. Linux native
ELF`db8437f8…`/runtime`dd28aaaf…` includes the guardedPC24 helper and packed16
validation. All480 frozen payload files match the standard outputs on bothhosts.
Receipt: `system-installer-performance-v2/inputs`. No installer lifecycle work.


### Packed texture forwarding: Mac result

The paired Mac Glide candidate passed at **33.99 presents/s**, up from19.80
(+71.7%), and more than twice the original15.71. It sent50.61 texture updates
per present instead of104.41, and688,157 bytes per present instead of991,945
(30.6% less). The changed group contains packed16 forwarding, word/chunk memory
helpers and guardedPC24 arithmetic; individual speedups are not inferred. Actual
system-provider use, full game rendering/capture and normal engine exit passed.
Receipt: `packed-texture-performance-v1/macos-game/run.json`.

These measured improvements are enabled by default. Mac native`7eccc017…` and
frontend`a277e72a…` form the final paired build. Mac remains below high-refresh
rates; these results do not claim200FPS or eliminate TCG cost. The scoped plan
milestones are complete with this limitation recorded. Strict workspace Clippy
passes on bothhosts; three test iterator expressions were updated for the newer
Linux Clippy lint without changing runtime behavior. Final logs:
`target/follow-through/performance-final-clippy.log` on eachhost.

Mac Glide also completed normal Windows shutdown; all task-owned VMs/builds
are stopped. Final grouped receipt, mirrored on bothhosts:
`packed-texture-performance-v1/macos-acceptance.json`.


## Next Mac performance/display pass started

The user approved maximizing throughput beyond the firstpass,256MiB framebuffer
and selectable rates through120Hz.120Hz is not an FPS cap or stopping target.
Active tasks are now at the top of plan.md; prior completed milestones are saved
evidence. Subagents own fresh attribution, typedarray/translation changes and
guestdisplaytiming, while root owns monitor pacing and integration.


### Physical-monitor refresh implementation

The shared-memory display now accepts the consumer window monitor rate through
a small host-control message. Juke supplies its current monitor rate; SDK keeps
the value across native reconnects and only queues changed values. This is
independent of Windows-selected refresh and GPU rendering throughput. Removed
the global-main-display/first-DRM-card guess. Desktop deadlines retain fractional
periods and skip missed deadlines without a catch-up loop.

Actual-source clock checks pass underASan/UBSan onMac/Linux for59.94,60,75,85,100,
120,144,240Hz over10,000deadlines and long stalls. SDK connection/deduplication
checks pass. Native integration build and changed-window run await the grouped
source handoff. No guest frame-rate cap was added.


### Fresh profiles and next grouped candidate

Current Mac profiles (`mac-current-profiles-v1/`) captured bothUTpaths with
complete guestPCtables and hostCPU samples. The native renderer still waits
roughly95% of sampled time. D3Dfrontend work includes158.8M memcpy,145.2M Draw
and69.7M component-conversion instructions; Glide includes358.1M memcpy and
267.4M Draw. Instruction counts identify volume, not cycle shares.

The compact typed-array/direct-packet implementation now removes fixedfloat
expansion and the scratch-to-packet copy. Shared guest/kernel/native admission
is updated together. Glide's supported RGB565/ARGB4444/1555 uploads now bypass
guest conversion; chromakey expansion remains where required. The current
Glide profile measured255.3M instructions in Convert565to5551 alone.

Named x87single-load/store helpers are another measured cost. Exact load/zero/
store guards passed360,912 actual-source result/exception comparisons across
rounding modes and precisions. The grouped candidate will also compile native
code atO3, retaining strict floating-point semantics and existing hardening.
None of these source-level results is yet a new gameFPSclaim.

The old NTwindow harness expected pixel-format0 after production switched to
format1 in commit1b06caf; corrected that stale assertion. Its source suite now
passes, including newcompact kernel validation.

### Combined build ready for game measurements

Both native builds succeeded and were frozen in `combined-native-v1/`: Mac
`0d81e810…`, Linux launcher `62808e0e…` with execution binary `15761615…` and
immutable private runtime `77a1b2e…`. The new release Juke consumers are Mac
`2bc83502…` and Linux `b811af52…`; both send the actual window-monitor rate.

The coherent Cargo guest build passed. A final review caught signed-normalized
GL 1.1 component semantics: retain the exact conversion for signed color/normal
attributes while still writing directly into compact packet storage. The measured
unsigned-byte colors and float attributes remain on the direct path. This narrow
fix is being incorporated before package capture. The native compact-array pixel
check has passed on Mac; Linux and actual game measurements follow.

The final guest capture is ready on both hosts under
`display-performance-v3/inputs/` (485 verified files). The single installer is
`d0dc4ec1…`; frontend `91e58798…`, NT miniport/display `5e72a4be…`/`1f819995…`,
Wine `2cf1ede2…`, Glide `f7e5a483…`. Standard guest outputs match this capture.
Both native GPU compact-array checks passed. Independent stopped XP and Windows
2000 images now contain the coherent driver pair and frontend, avoiding a mixed
old-kernel/new-packet test. Actual game and display checks are underway.

### First combined Mac results

Unreal Tournament Direct3D passed at **69.87 submitted frames/s**, up from 65.07
(+7.38%). Median frame gap improved from 16.109 to 14.816 ms; p95 from 20.181 to
18.54 ms. Glide passed at **40.14 submitted frames/s**, up from 33.99 (+18.07%),
with about 10.26% fewer submitted bytes per frame. Both used the normal system
providers and completed normal engine exit with no rejected or dropped native
commands. These are grouped changes, not isolated attribution to one optimization.
Receipts: `mac-compact-performance-v1/`.

XP's actual Adapter page reports **256 MB**, and Properties opens successfully
and reports the device working. The refresh helper result and Windows 98 checks
remain in progress.

A further bounded CPU pass is now implementing exact canonical finite comparisons
and zero/identity/cancellation cases excluded by the existing guarded PC24 helper.
The saved profiles still contain 127/248 D3D/Glide arithmetic-helper samples and
18/53 comparison samples; these include already-fast paths, so they do not predict
the new optimization's FPS gain. Special values, exceptional cases and uncertain
rounding continue through the existing implementation.

XP refresh validation passed on the combined default-256 MiB native. The guest
enumerated 60/75/85/100/120 Hz; changing to 120 succeeded, and both Windows mode
readback and the device timing query reported 120. Interrupt-backed begin/end
waits completed and restoring 60 succeeded. The diagnostic flushes a log after
each wait, so its aggregate elapsed time is not a clean refresh-frequency
measurement. No continuous timer or busy polling was introduced.

### OpenGL attribution completed

Ordinary system-ICD Half-Life completed with native GPU-path proof and a bounded
CPU sample in `combined-opengl-v1/macos-game/`. CPU0 had 1,030 samples: 664 in
translated execution, 89 in named x87 helpers and 220 in guest CPU event waits.
CPU1 slept throughout. The renderer waited for work in 924/1,009 samples (91.6%).
These are per-thread stack shares, not cycle counts. The observed launch window
includes loading; exact engine frame timestamps remain unavailable.

The sampled run reported 379 frames / 5.767 seconds (65.717 FPS). It is attribution
evidence, not a clean speed comparison against the saved unsampled baseline.
Half-Life issued roughly 4.8 million native GL records during the enclosing launch
window, including 439 finishes and 758 matrix queries. No busy-loop replacement
or weakened finish semantics was introduced. The guest then shut down normally.

The additional exact x87 group passed **2,287,008 result/full-exception-flag
comparisons per host** against the existing arithmetic and comparison routines.
Checks cover all four rounding modes and three precision controls, signed zeros,
normal cancellation, wider-value fallback, denormals, invalid encodings, NaNs and
infinities. Independent source review found no blocker. The final Mac native is
frozen as `combined-native-v2/` (`25c9fd96…`). No guest package changed in this
second group; the prior Mac game results are the saved comparison baseline.

### Final native game results and full framebuffer check

Mac Direct3D reached **72.324 submitted frames/s**, another 3.51% above the first
combined group's 69.87. Final Glide was **39.940**, versus 40.136 in that group
(−0.49%); the last CPU change has no demonstrated Glide gain. Its overall gain
against the previous 33.99 baseline remains about 17.5%. Exact receipt:
`x87-exact-game-v1/`.

The final unsampled Mac Half-Life run passed at **379 / 4.952 seconds = 76.537
engine FPS**, versus the saved 70.083 baseline (+9.21%). System-provider and native
GPU evidence passed, followed by clean shutdown. Receipt:
`final-opengl-macos-v1/`.

Linux Direct3D passed at **382.896 submitted frames/s** (382.889 received GPU
frames/s), versus 363.90 (about +5.2%). Median GPU gap improved from 2.587 to
2.374 ms; p95 increased slightly from 3.719 to 3.832 ms, so the gain is throughput,
not uniformly improved pacing. This is Linux KVM, not Mac TCG. No dropped or
rejected native commands were observed.

Windows 98 successfully mapped the full **268,435,456-byte framebuffer** and
checked/restored offscreen words at 128 MiB, 255 MiB and the final DWORD. Its
refresh-mode registration check still requires the changed configuration to be
reloaded; 120 Hz is not yet accepted on this guest.

Final Linux Glide passed at **190.718 submitted frames/s**, versus the saved
184.313 (+3.48% for the grouped changes), with correct captured rendering,
normal system providers, no rejected/dropped commands and clean shutdown.
Receipt: `final-glide-linux-v1/acceptance.json`.

The Linux D3D receipt also records different camera/scene phases between the
current and saved runs. Its throughput comparison is descriptive, not isolated
causal proof of a 5.2% optimization. No unchanged baseline was repeated.

Normal Juke release builds and strict DreamGPU workspace Clippy pass on both
hosts. Linux's first concurrent build checks collided in a shared QEMU configure
directory; serial checks repaired the generated build state and passed. The
frozen native executables and private Linux runtime were unaffected.

The final all-component Cargo build changed only two bytes in Win98's minidriver
VERSIONINFO file-date field compared with the measured guest capture. All other
runtime/tool payload bytes match. The resulting installer is `2c28ea52…`; package
manifests record its exact generated identity. This timestamp-only difference is
not a driver implementation change.

Final Linux Half-Life passed at **379 / 1.798 seconds = 210.763 engine FPS**,
versus the saved 189.844 (+11.02%). The ordinary system OpenGL provider and native
GPU path passed; no CPU sampler or guest-PC plugin was enabled. The bounded
Windows 2000 rate tool was then launched after normal game exit, before clean
shutdown, so its actual 120 Hz result can be read from the stopped guest log.
Receipt: `final-opengl-linux-v1/`.

The stopped Windows 2000 diagnostic log confirms all five 1024×768×32 rates,
Windows and MMIO 120 Hz readback, eight begin/end interrupt waits, restoration to
60 Hz, and `RESULT failures=0 rates_mask=31`. Its extraction was read-only and
temporary raw/loop resources were cleaned. All task-owned game VMs are stopped.
Windows 98's refresh-selector interface remains under investigation; its capacity
and 60 Hz graphics path are already accepted.

### Windows 98 refresh-selector cause identified

Tracing the exact guest's `DESKCP16.DLL` located the hide condition: the Adapter
page requires the active monitor's nonzero parsed `MaxResolution` before reading
refresh-rate lists. The Default Monitor lacks that metadata. The installed mode
lists and default-60 registry entries were correct; the earlier GDIINFO field
hypothesis was rejected without editing production code.

The required fix is truthful virtual-monitor capability reporting through the
normal DDC/monitor path. DreamGPU currently disables standard VGA EDID; directly
turning on QEMU's shared helper would use the wrong containing-structure cast.
Any native EDID implementation must own its storage, prefer 60 Hz, advertise the
supported range through 120 Hz and preserve existing resolution choices. This
monitor is virtual and independent of the consumer's physical display.

The owned EDID implementation and native builds now pass on both hosts. The
descriptor has valid checksums, preferred 1024×768 at 60 Hz, a 60–120 Hz range,
and detailed 3840×2160 / 3200×2400 modes preserving NT resolution extrema. It
uses DreamGPU-owned storage at BAR2+0, avoiding the incompatible shared VGA
container cast. The actual changed Windows 98 cold-detection check is pending.
Frozen native identities are in `final-edid-native-v1/manifest.json` on each host.

Final standard guest outputs now match across hosts: 480 files including the
installer, payloads and manifests; identity inventory SHA `f19637de…` in
`final-packages-v1/manifest.json`. The single installer is SHA `2c28ea52…`.
This preserves the recorded timestamp-only Win98 minidriver distinction from
the measured capture. No unchanged game baseline or game test was repeated for
the later virtual-monitor metadata change.

Changed cold detection now finds the DreamGPU Plug and Play Monitor. Windows
98's built-in MONITOR.INF installed it through the normal new-hardware wizard,
and Adapter Properties now visibly exposes the previously missing refresh-rate
selector, defaulting to 60 Hz. No fixture registry override was needed. Actual
120 Hz device readback and restoration are the remaining check.

The independent readback caught a real remaining defect: selecting and confirming
120 Hz updates Windows 98's Adapter page but leaves native timing at 60 Hz. The
monitor EDID and populated `MaxResolution` are accepted; the mode propagation
path still needs correction. UI-only success is not a refresh-rate pass.

The propagation cause is now concrete: the donor makefile compiles shared
`vxd_fbhda.obj`, `init.obj` and `modes.obj` without `QEMU`, excluding the new
conditional rate code from the production driver. The fix is to compile scoped
QEMU objects and link them into the DreamGPU target. The uninitialized-register
finding was an earlier hypothesis, not the established cause of UI120/device60.

The scoped Win98 objects compile and link successfully after removing the
donor's obsolete QEMU-only `DWORD ReadDisplayConfig` declaration; the actual
implementation and callers use `void`. The all-component Cargo retry passes
(`final-all-rate-macos-build-v2.log`). Standard outputs match across hosts:
480-file inventory SHA `093104df…` in `final-packages-v2/manifest.json`, installer
SHA `820f0b33…`. The changed guest rate check is running with identical VxD code
and a minidriver differing from that package only in its resource timestamp.

That build correction was necessary but not sufficient: the live read-only
Win16 diagnostic returned VDD success with maximum 120, minimum 0, and flags
`0x0280` (`MONITOR_INFO_NOT_VALID | REFRESH_RATE_MAX_ONLY`). The donor discarded
the explicit selected rate together with invalid monitor-range information.
The remaining correction gives a supported explicit max-only rate precedence;
invalid ranges and unsupported selections must still use constrained behavior.

### Performance/display pass complete

Windows 98 now passes with the exact standard-package minidriver `e6841050…`
and VxD `14c08c63…`. Normal Windows selection changes device timing between
60 and 120 Hz. The final helper blocks report 120/120/60 Hz, each with eight
begin-blank and one end-blank interrupt waits and zero failures. The guest was
restored to 60 Hz and cleanly shut down. Receipt:
`display-edid-win98-v3/acceptance.json` on both hosts.

The active monitor has the exact EDID, no Config Manager problem, hardware
maximum 3840×2400 and the Win98 built-in monitor driver's maximum 1600×1200,
covering every maintained Win98 INF mode. All five selectable refresh rates are
present. Focused actual-source checks cover explicit supported selections,
unsupported positive selections, invalid ranges and the 60 Hz default.

Final `DREAMGPU_BUILD=all` Cargo build passed in 14.98 seconds using the Linux
guest builder. Both hosts have matching 480-file inventories, SHA `cb633485…`,
and the combined installer `target/guest/dreamgpu.exe`, SHA `44e7a5ef…`.
Evidence: `final-packages-v3/manifest.json`. The provider comparison in the same
directory accounts for every changed loaded byte as an assertion source path;
executable instructions remain identical to the measured providers. WineD3D
changes only checksum and nonloaded symbols. No unchanged game was rerun.

Native builds, strict Rust Clippy and consumer release builds pass on both hosts;
the final display changes passed their focused source and actual guest checks.
All task-owned VMs and builds are stopped. The six accepted performance results
and their practical limits are in plan.md and
`performance-display-summary-v1/acceptance.json`. The Mac remains limited by
translated guest work; 120 Hz remains a selectable scanout rate, never an FPS cap.

### Half-Life fullscreen and mouse follow-up (in progress)

The actual stopped Windows 2000 image contains old `jrg` system drivers and
app-local graphics providers. Its retail Half-Life settings request fullscreen
800×600×16, and its normal shortcut has no arguments. The retail engine ignores
a failed 16-bit mode change, explaining the borderless game within the larger
desktop. Current NT modes previously offered only 32-bit storage.

The matched NT driver/native implementation now supports real RGB565 primary
storage. Native GPU composition remains 32-bit; bounded conversion occurs at
CPU-primary ownership boundaries. Existing 32-bit transfers retain memcpy.
The matched private kernel interface is version 7; desktop word 8 specifies
16 for RGB565 and zero for the existing 32-bit protocol.

The actual native GPU/VRAM check passes on Mac and Linux through 32→16→32,
GPU drawing, CPU patching, readback, work-quantum boundaries and guard bytes.
The first check failed because its reused frame waiter hardcoded 64×32; after
correcting that test helper, the same native binaries pass. Evidence is in
`nt16-primary-v1/{macos-native-test-v2.log,linux-oracle-v2.log}`.

Captured mouse movement now activates the existing relative PS/2 pointer instead
of accumulating deltas into USB tablet coordinates that ignore guest recentering.
Buttons are released before device switches. Juke uses raw relative motion in
both Locked and Confined modes, leaves failed grabs uncaptured, and ignores
unsupported buttons instead of synthesizing left clicks. Source routing sanitizer
and Rust input tests pass; actual guest recenter and game validation are pending.

Actual-image activation found the old-install migration guard: driver-status.log
reports `supported original driver pair result=3`; the original service is
`jrgmini`, which the current installer does not capture. The existing exact-PCI
`DGDRV` update helper is being used for this one migration before system setup.
No rollback schema or new installer lifecycle support is being added.

Review before game launch also caught two remaining 32-bit-only guards in the
actual NT display ICD callbacks (`DrvDescribePixelFormat` and
`DrvSetPixelFormat`). These must admit the new 16-bit primary; the OpenGL
backbuffer remains truthful RGBA8. The guest package is being rebuilt with that
correction before attempting the game. Native mode-cycle evidence is unchanged.

### Actual Windows 2000 fullscreen/input fix complete

The current paired driver was installed with the existing exact-PCI update
helper, followed by the existing system installer. Installer `8f33bb52…`
completed phase 2 with exit 0. The installed display `779c9f0b…` and miniport
`368fadbf…` match the standard Cargo package. No installer source or rollback
schema changed for the legacy migration.

Actual retail Half-Life now changes native scanout to 800×600×16. The composed
frame contains the complete game without desktop borders; Juke's physical window
remains 2048×1536 throughout. Ten small relative movements produce a modest camera
turn. Console `quit` exits normally and restores 1280×1024×32 at 60 Hz. That
original desktop setting was restored after the driver update's 1024×768 default.

The separate actual guest recenter check passes: twelve injected `(3,2)` motions
become eight coalesced Windows messages totaling exactly `(36,24)`, maximum `(6,4)`.
There are two button downs, two ups, and no held button after capture release.
The read-only post-shutdown audit confirms normal `default` system OpenGL selection,
original fullscreen preferences and absence of the six verified old app-local
providers. Original user disk SHA `5cff1692…` stayed unchanged.

The private fullscreen helper's strict window-geometry predicate **did not pass**
and therefore collected no module list. Its normal-exit/restoration receipt is
valid; it is not used to claim a geometry or module pass. The independent native
VBE readback, actual composed game frames and fixed host-window measurements
establish the requested visible result. No unchanged game was rerun to satisfy
that diagnostic's extra predicate.

Normal Juke's `files/machines/win2000/machine.toml` now selects the verified image
`.retro-adoptions/fullscreen-mouse/disk.qcow2` (SHA `916a0234…`), retaining the
Half-Life CUE attachment. Debug/release builds pass on Mac; normal release builds
and strict Clippy pass on both hosts. Native RGB565 mode-cycle and actual ICD
callback sanitizer checks pass on both hosts. All task-owned VMs are stopped.
Evidence: `target/follow-through/halflife-win2000-userdisk-v1/acceptance.json`.


### 2026-09-13: fullscreen reactivation follow-up — paused, unresolved

The authoritative current handoff is at the top of this ledger. This is a new
failure after the earlier successful launch/normal-exit check. The user's trigger
involved guest movement keys; shortcut handling is deferred until graphics
recovery works. The shutdown screenshot naming CSC Notifications does not, by
itself, establish a DreamGPU fault.

Implemented guest changes distinguish a retired binding from a temporarily
unavailable clip before submission. A retired binding gets one rebind/retry;
missing or overflowed DC clips preserve the context and submit no kernel work.
Uncertain native presentation failures remain fail-closed. Neither GL commands
nor possibly submitted swaps are replayed. Actual frontend/window sanitizer
checks passed on Mac and Linux. Evidence:
`target/follow-through/wgl-binding-recovery-v1/`, including
`clip-recovery.json` and `final-package-receipt.json`.

Juke now marks the VM's CPU framebuffer dirty after the renderer accepts
ReturnCpu/Reset, including retried queue-full submissions, and requests redraw
for the active VM. Source review found that consuming a dirty notification during
GPU ownership could otherwise lose the final CPU update; coherence checks remain.
The first apparent runtime example was **not evidence of this race**: the host
was occluded. Foregrounding it immediately aligned raw/rendered 1280×1024 output.
Observers now record host visibility.

Diagnostic attempts and their limits:

| Attempt | What actually happened | What it establishes |
| --- | --- | --- |
| Initial helper | SetForegroundWindow was refused; game stayed active | Rendering only, no recovery transition |
| v2 | Real Alt+Tab, but `-toconsole`/`+map` bypassed launcher initialization | Not a faithful normal launcher lifecycle |
| v3 | Normal launcher entry; WM_CHAR map input was ignored | No actual gameplay recovery tested |
| v4 | Real keyboard map input, textured `c1a0`, Alt+Tab away and back | Desktop restores; engine resume still unresolved |

In v4, `focus_away=1`, `focus_back=1`, `engine_resumed=0`. The host was unoccluded;
raw and rendered desktop output agreed at 1280×1024. Three later main-thread
samples show USER32/MFC message waiting; engine modules remain loaded. There was
no observed blocked GL/Wine call. Sampling cannot establish whether the earlier
resume handler ran or returned early. The helper posted menu-form WM_COMMAND
with `lParam=0`, while the executable binds real child controls with DDX. A normal
button notification carries the child HWND. Inspect that route before blaming a
provider for the helper's failed resume.

The read-only state/export bootstrap can itself change launcher foreground, so
use the v4 phase log for the failed-return foreground state. All twelve temporary
thread suspensions were balanced; no thread was left suspended. No further game
input or driver change followed the user's request to pause.
