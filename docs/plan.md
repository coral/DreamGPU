# DreamGPU: playable, fast retro graphics

This plan replaces the earlier plan on September 13, 2026, following the user's
explicit scope correction. The earlier installer rollback, elaborate recovery,
unattended-installation and exhaustive acceptance requirements were added by the
agent; they were not the user's requirements. They are cancelled, not deferred
release gates. Historical results remain in progress.md and ignored run artifacts.

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

## Milestone 1 — Usable adapter and working installation

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

## Milestone 2 — Fix Mac game-performance bottlenecks

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

## Milestone 3 — Carry the working improvements across hosts and games

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

All three scoped milestones above are implemented. The standard native builds
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
