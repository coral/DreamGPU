# DreamGPU: maximize Mac performance and improve display modes

This plan replaces the earlier plan on September 13, 2026, following the user's
explicit scope correction. The earlier installer rollback, elaborate recovery,
unattended-installation and exhaustive acceptance requirements were added by the
agent; they were not the user's requirements. They are cancelled, not deferred
release gates. Historical results remain in progress.md and ignored run artifacts.

## Completed implementation — performance and display pass

Approved September 13: maximize Mac game throughput. 120 Hz is the maximum
selectable Windows refresh, not an FPS cap or a completion target. Preserve game
quality, responsiveness and sleeping idle workers. The first-pass measurements
below remain the baseline.

- [x] Finish current Mac D3D, Glide and OpenGL attribution. Current D3D/Glide
  guest-PC profiles and the OpenGL host sample are recorded. Name guest packing/conversion,
  translated CPU helpers, driver submission, GPU execution and presentation costs.
- [x] Accept compact typed-array packets and direct capture into reserved packet
  storage. Focused source checks, both-host native pixel checks and changed games
  pass. Signed GL 1.1 normalization semantics are preserved.
- [x] Accept the grouped performance changes: compact arrays, native packed Glide
  textures, exact x87 single-load/store conversions and optimized native builds.
  First changed Mac results: D3D 65.07 → 69.87 submitted frames/s; Glide
  33.99 → 40.14. A further exact x87 zero/identity/cancellation and finite-compare
  pass addresses remaining named helper work. Move expensive guest work into host
  handlers where useful. No approximations,
  deeper frame queues or busy loops.
- [x] Verify the actual 256 MiB framebuffer in Windows 98 and NT, including NT
  reporting and Properties. XP reports 256 MB and Properties opens; Windows 98
  mapped all 256 MiB and checked/restored words at 128 MiB, 255 MiB and the final
  DWORD. The native texture budget remains separately 256 MiB.
- [x] Verify 60/75/85/100/120 Hz in Windows 98/2000/XP, with 60 Hz as default.
  Shared display timing and context-free driver queries are implemented, including
  sleeping interrupt-driven waits. XP and Windows 2000 actual checks pass.
  Windows 98 control-panel tracing proved that missing active-monitor maximum
  resolution metadata hides the selector before it reads the valid mode lists.
  Truthful virtual-monitor DDC now passes cold detection, with preferred 60 Hz,
  maximum 120 Hz and preserved resolution choices; the selector appears normally.
  Corrected the Win98 build rules that excluded the setter and the donor guard
  that discarded explicit selected rates with invalid monitor-range flags.
  Actual device readback now follows dynamic 120 → 60 → 120 Hz selection.
- [x] Validate fractional refresh deadlines using the monitor containing the
  consumer window. Source checks pass on both hosts. Guest refresh, physical
  presentation and uncapped game rendering remain independent.
- [x] Measure changed games on each host, including Linux Direct3D. Preserve
  visuals and desktop responsiveness. Use saved baselines and serial/QMP commands.
- [x] Produce matching native, guest and installer outputs; finish focused checks
  and strict Clippy, and record actual results and remaining named bottlenecks.

Current accepted measurements:

| Host | Workload | Saved baseline | Current |
| --- | --- | ---: | ---: |
| Mac | UT Direct3D | 65.07 | 72.32 |
| Mac | UT Glide | 33.99 | 39.94 |
| Mac | Half-Life OpenGL | 70.08 | 76.54 |
| Linux | UT Direct3D | 363.90 | 382.90 |
| Linux | UT Glide | 184.31 | 190.72 |
| Linux | Half-Life OpenGL | 189.84 | 210.76 |

UT numbers are submitted GPU frames/s over the fixed measurement window;
Half-Life numbers are engine-reported timedemo FPS. Linux D3D camera phases
varied, so its comparison is descriptive rather than isolated causal attribution.
Mac uses TCG and Linux uses KVM; cross-host ratios are not a like-for-like GPU test.
All game runs completed with normal providers, hardware rendering and clean exit.

The remaining Mac limit is translated guest command production. Current-pass
profiles show native render workers waiting for work for roughly 92–95% of their
samples; guest array capture/copies, game execution and exact x87 operations are
the named costs addressed here. These samples are attribution, not a prediction
of attainable FPS or a post-change GPU utilization benchmark. The final exact
x87 group improved Mac D3D another 3.5%, with no demonstrated additional Glide
gain. Further optimization should begin from a new profile of a specific slow
game; 120 Hz does not mark performance completion or impose a rendering limit.

Measured native builds are frozen under `target/follow-through/combined-native-v2/`;
the final virtual-monitor metadata is in `final-edid-native-v1/`. The
all-component Cargo build passes on Mac with the Linux guest builder. Strict
workspace Clippy and normal Juke release builds pass on both hosts. XP and Windows
2000 verified all five refresh rates, actual 120 Hz timing and sleeping waits.
Windows 98 mapped all 256 MiB and now applies the selected rate to the device.
Its final packaged drivers passed actual 120 Hz selection, interrupt-backed
waits and restoration to 60 Hz, followed by clean shutdown. The final receipt is
`display-edid-win98-v3/acceptance.json` on both hosts.
Final standard guest outputs match across hosts (`final-packages-v3/`), including
`target/guest/dreamgpu.exe`, SHA `44e7a5ef…`. Provider PE comparisons account for
rebuild metadata changes without repeating the accepted game measurements.

Ownership: SDK agent handles profiles, translation and Mac Direct3D; GL agent
handles compact arrays, Glide and native pixel checks; guest agent handles display
drivers and timing; root handles monitor pacing, OpenGL/Linux games, integration
and documentation. VM measurements on each host are exclusive. No rollback,
uninstall, unattended installation, smoke campaigns, repeated unchanged baselines
or new branches.

## Vision

Install DreamGPU inside a Windows 98, 2000 or XP QEMU guest and play games using
ordinary system OpenGL, Glide and Direct3D. Games should look correct and run
fast on macOS and Linux. Juke is the development consumer; DreamGPU remains an
independent Rust/C++ project. A responsive desktop and usable display settings
are part of a working GPU.

Performance work is now the priority. The user observed that UT Direct3D on Mac
has better shading/shadows than Glide but runs poorly. Preserve those visuals
while fixing the actual cost. High refresh rates, roughly 200 FPS where the guest
and game permit it, are the ambition; do not invent FPS or disguise a CPU limit.

## Completed first pass — historical baseline

The following milestones record the implementation before the active pass above.
Their 64 MiB figures and earlier performance numbers are historical. Current
capacity is 256 MiB, and current results are recorded in the active checklist and
progress.md.

### Milestone 1 — Usable adapter and working installation

- [x] Cargo builds the native runtime and both guest OS payloads; a single
  dreamgpu.exe detects the guest OS and installs system graphics providers.
- [x] Ordinary OpenGL, Glide 2 and WineD3D Direct3D 6–9 loading is implemented.
  Existing working installs and game images are the starting point.
- [x] Open XP Adapter Properties successfully with the changed driver on Mac.
  The reported crash was not reproduced on the inspected baseline. Fixed a real
  `DrvGetModes` output-buffer overrun, without claiming it was the observed crash's
  proven cause. The changed UI stays open and reports the device working properly.
- [x] Correct adapter identity and video-memory capacity. QEMU now allocates a
  real 64 MiB framebuffer by default on both hosts. The NT driver bounds mode
  sizing/reporting by the actual PCI aperture and displays readable adapter
  metadata. The native texture budget remains separate from framebuffer memory.
- [x] Ensure the resulting guest driver is straightforward to install and use.
  The updated single installer and both OS packages are built at `target/guest/`
  on both hosts. Installer behavior is unchanged from the working installation.
  No further installer lifecycle work is required.

Completion: the user can install/use the GPU, inspect adapter properties without
crashing, and see accurate supported capabilities. No rollback, perfect removal,
unattended installation or Windows 98→XP migration requirement.

### Milestone 2 — Fix Mac game-performance bottlenecks

- [x] Profile the existing automated UT Direct3D workload on Mac. Use saved
  results first; collect an actual CPU/GPU sample where the current evidence is
  missing. Separate guest/emulation, WineD3D, native rendering and presentation.
- [x] Fix the dominant avoidable costs together: redundant state/queries,
  synchronization, copies, render-target handling, translation overhead or other
  hotspots actually shown by the workload. Keep shading/shadows and full-client
  rendering correct.
- [x] Apply relevant shared improvements to Glide and OpenGL. Profile their
  real game path when it differs; do not assume the D3D bottleneck applies to both.
- [x] Measure the changed candidate once with the same direct-launch workload,
  settings, resolution and host display. Record actual game FPS/frame pacing
  where available and CPU/GPU costs. Whole-attempt frame counts are not game FPS.
- [x] Make the faster working path the default and remove superseded slow paths
  when they are no longer needed. No extra opt-in performance mode.

Completion: substantial trace-supported improvements to real games, preserved
visuals, and concrete before/after evidence. Do not stop at collecting a profile
or reporting that a game launches while avoidable dominant overhead remains.

Current evidence and implementation:

- The Mac UT Direct3D sample spends 1,066/1,192 CPU0 samples in TCG; the
  renderer waits for work in 1,065/1,172 samples. Guest command production is
  the first bottleneck, rather than host GPU saturation.
- WineD3D's common transformed-vertex immediate path now uses bounded client
  arrays, preserving RHW, color, secondary color and UVs. Measured commands per
  present fell from about 3,786 to 1,052, but the sampled run's 55.42 presents/s
  does not beat the saved unsampled 55.25 result convincingly. Attribute remaining
  guest CPU work using guest-PC hot blocks; do not claim completion from fewer
  commands alone.
- Native batch context lookup, cancellation checks and array state restoration
  have been streamlined and built on Mac and Linux. The changed Direct3D game measurement has completed;
  command traffic fell substantially. The subsequent grouped memory/CPU change established the frame-delivery gain below; the native cleanup alone is not credited with that gain.
- Linux Half-Life with the changed native renderer completed 379 frames in
  1.996 seconds: 189.844 engine-reported FPS. The normal system OpenGL provider
  and native GPU command/presentation evidence passed. This is a current result,
  not a matched before/after speedup claim.
- Glide's first retained-content cache was rejected: only 115 recovered images
  across 7,821 misses on Mac, and allocation/deletion churn cancelled any upload
  reduction. Remove raw snapshots/hashing/retired-content search. Keep exact
  active conversion state, identical-download skips, precise invalidation and
  efficient compatible-storage reuse. The corrected candidate improved Mac
  throughput 26% (15.71→19.80 presents/s), cut updates per frame 21%, and eliminated
  deletion churn. The packaged Win98 build also passed on Linux at 138.50 received
  GPU frames/s with full rendering and normal game exit.

Further measured improvements:

- Guest-PC attribution identified byte-at-a-time frontend `memcpy`/`memset` as
  47.4% of frontend instructions (331.5 million of 698.8 million). Bounded integer word/chunk helpers
  replace these loops without SSE2, overreads, or changed overlap semantics.
- The Mac game selects 24-bit x87 precision. Basic arithmetic now uses native
  floating point only when operands/results permit exactly the original result
  and exception flags. Full precision, special values, extreme exponents and
  other rounding modes retain QEMU's exact software path. Generic SoftFloat and
  transcendental operations are unchanged. Actual-source differential checks
  passed 1,050,704 cases; this does not change the game's selected precision.
- Together, these changes improved Mac Direct3D submission throughput from
  55.42 to 65.07 presents/s (+17.4%). Received-frame median/p95 gaps improved
  from 18.299/23.233 ms to 16.109/20.181 ms. The textured/shaded scene and normal
  game exit passed. This measures the grouped changes, not isolated effects.
  Evidence: `guest-memory-performance-v1/acceptance.json`.
- Glide guest-PC profiling found packed-texture expansion consumes 40.25% of
  recorded guest instructions. Forward packed 16-bit pixels using the existing
  format/type packet metadata and native GPU support. The paired candidate
  improved Mac Glide from19.80 to33.99 presents/s (+71.7%), and Linux Glide
  from138.50 to184.31 (+33.1%). The Mac group includes the accepted memory/CPU
  improvements; this is not isolated packed-texture attribution. New frontends require the matching updated native runtime; existing
  byte-pixel packets remain supported. No mixed-version migration project.

### Milestone 3 — Carry the working improvements across hosts and games

- [x] Build the changed code on Mac and Linux; use the same supported DreamGPU
  device/profile configuration on both Juke instances.
- [x] Exercise the actual changed paths in representative OpenGL, Glide and
  Direct3D games using the existing 98/2000/XP images. Reuse successful evidence
  for unaffected combinations; do not build an exhaustive test matrix.
- [x] Fix observed rendering, stability or major frame-pacing problems. Normal
  desktop interaction must remain responsive and idle workers must sleep.
- [x] Put the working outputs at the documented Cargo package paths and update
  concise usage/performance notes with actual results and remaining limitations.

Completion: a useful accelerated GPU for retro games on both hosts, with the XP
properties issue fixed and the measured performance improvements enabled normally.

## Delivered results and practical limits

The first-pass milestones below were implemented; the active checklist above supersedes their completion status. The standard native builds
and `target/guest/dreamgpu.exe` (SHA `c2b4bae4…`) include the accepted changes on
both hosts. The installer still runs inside the guest. Current build/runtime
pairing is explained in [build.md](build.md).

| Workload | Host / execution | Recorded result |
| --- | --- | --- |
| UT Direct3D | Mac / TCG | 55.42 → 65.07 presents/s; +17.4% |
| UT Glide | Mac / TCG | 15.71 → 19.80 → 33.99 presents/s; +116% overall |
| UT Direct3D | Linux / KVM | 363.90 received GPU frames/s |
| UT Glide | Linux / KVM | 138.50 → 184.31 presents/s; +33.1% |
| Half-Life OpenGL | Linux / KVM | 189.844 engine-reported FPS |

Submission/received-frame rates are not physical scanout or engine-reported
FPS. Mac and Linux use different execution engines, so their numbers are not
cross-host speedup comparisons. Mac games remain below the high-refresh ambition;
the measured guest/TCG cost has been reduced, not eliminated. The XP Properties
page now works with the changed driver and reports actual64MiB capacity; the
original reported crash was not reproduced, so its exact cause remains unknown.

Further performance claims require a new game profile showing the next dominant
cost. Nothing here reopens cancelled installer, rollback or smoke-test work.

## Not part of this work

- System rollback, corrected-executor recovery and transactional lifecycle design.
- Perfect uninstall, exhaustive upgrade/removal/interruption/fault campaigns.
- Unattended installation or upgrading Windows 98 into 2000/XP.
- Smoke-test campaigns, general OpenGL certification or speculative API edge cases
  unrelated to a real failure in supported games.
- New desktop-QPC experiments chasing a small synthetic latency difference while
  games are slow. Preserve completed useful instrumentation; cancel the new campaign.
- Rewriting working subsystems or building more benchmark infrastructure without
  a concrete need to diagnose or improve the current games.

Completed working code need not be torn out merely because its original task was
out of scope. Stop extending it. Remove unfinished, unused additions and do not
let previous checklists or historical documentation reactivate cancelled work.
Disk images and emulator states provide recovery for development.

## Execution and automation rules

1. Use subagents for concrete independent work: XP driver/properties, Mac
   WineD3D performance, and shared OpenGL/Glide/native performance. Coordinate
   shared files and VM ownership; keep the root focused on integration/results.
2. Use the existing direct-launch/serial/QMP game loop. No menu-click/screenshot/
   wait cycle. Screenshots can document rendering or a specific crash.
3. Read existing run.json/profile evidence. Group fixes, then measure the changed
   candidate. A successful run is sufficient; do not repeat it for reassurance.
4. Use targeted compiler checks and tests only where they answer a real risk in
   changed code. No new broad smoke/fault/lifecycle acceptance gate.
5. Disable guest networking during these tests. Use independent copies of stopped
   disk images; never copy a live disk. Keep original images and user media intact.
6. Do not change game quality, guest speed or workload to inflate FPS. Avoid busy
   loops and deeper queues that trade latency for a better-looking number.
7. Keep Rust for host/QEMU integration, C++23 for maintained guest code, Cargo for
   builds, pinned upstream sources and existing license/provenance records.
8. Update this plan and progress.md with actual completed work. Historical partial
   results are evidence, not authority to expand the user's requirements.
