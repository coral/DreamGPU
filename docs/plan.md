# DreamGPU plan

Updated September 14, 2026. **Current milestones completed.**
Fullscreen graphics recovery and captured movement chords pass, and ordinary
Juke now selects the verified Windows 2000 image. See [progress.md](progress.md)
for evidence, exact artifacts and limits. Further performance work starts from
a new measured bottleneck rather than repeating the completed baseline pass.

## Goal and boundaries

Install `dreamgpu.exe` inside a Windows 98, 2000 or XP QEMU guest and use ordinary
system OpenGL, Glide and WineD3D Direct3D with good visuals and high performance
on macOS and Linux. DreamGPU remains independent; Juke is its development consumer.
Keep the desktop responsive and idle workers asleep. Rendering should run as fast
as possible: 120 Hz is a selectable guest refresh rate, **not an FPS target or cap**.

The user cancelled rollback, perfect uninstallation, unattended installation,
Windows 98-to-XP migration and broad smoke/fault-test campaigns. They are not
release gates or deferred milestones. Preserve completed useful code; do not
extend that work or reactivate historical checklists.

## Milestone 1 — System graphics and display capabilities: completed

- [x] Cargo owns native and guest build sequencing, with Rust host integration,
  maintained C++23 guest code and pinned source/license provenance.
- [x] A single guest installer selects the Windows 9x or NT payload; normal system
  OpenGL, Glide 2 and WineD3D Direct3D 6–9 loading works without per-game DLL copies.
- [x] Actual framebuffer capacity is 256 MiB; native texture storage has a separate
  256 MiB budget. XP reports the capacity and Adapter Properties opens normally.
  A real `DrvGetModes` overrun was fixed; the user's original Properties crash
  was not reproduced, so its cause is not claimed as proven.
- [x] Windows 98/2000/XP expose 60/75/85/100/120 Hz, defaulting to 60 Hz.
  Real device timing and interrupt-backed sleeping waits were checked. Win98
  monitor metadata and explicit-rate selection defects were corrected.
- [x] Builds and packaged outputs work on both development hosts.

Build usage: [build.md](build.md). Prior display evidence:
`target/follow-through/display-edid-win98-v3/acceptance.json` and
`performance-display-summary-v1/acceptance.json`.

## Milestone 2 — Measured game performance: completed pass

- [x] Profile actual Mac game workloads and separate translated guest execution,
  packing/conversion, native submission and presentation costs.
- [x] Reduce redundant WineD3D command traffic and frontend byte loops; use compact
  typed arrays and native packed Glide textures. Preserve rendering semantics.
- [x] Optimize exact eligible x87 operations without approximations; keep QEMU's
  full software path for unsupported precision, special values and exceptions.
- [x] Measure the changed games on Mac and Linux with saved baselines.

| Host | Workload | Saved baseline | Latest accepted |
| --- | --- | ---: | ---: |
| Mac | UT Direct3D | 65.07 | 72.32 |
| Mac | UT Glide | 33.99 | 39.94 |
| Mac | Half-Life OpenGL | 70.08 | 76.54 |
| Linux | UT Direct3D | 363.90 | 382.90 |
| Linux | UT Glide | 184.31 | 190.72 |
| Linux | Half-Life OpenGL | 189.84 | 210.76 |

UT numbers are submitted GPU frames/s in a fixed window; Half-Life numbers are
engine-reported timedemo FPS. Linux D3D camera phases varied, so that comparison
is descriptive. Mac uses TCG and Linux uses KVM: these are not equivalent GPU
benchmarks. Current-pass samples showed native rendering workers mostly waiting
for guest work; Mac's translated command production remains the named limit.
These results are saved evidence, not a claim that all further performance work
is finished. Start another pass from a specific slow-game profile.

## Milestone 3 — Real fullscreen and relative mouse: launch/exit completed

- [x] Support real NT RGB565 16-bit primary scanout alongside the fast 32-bit path.
- [x] Admit 16-bit primary modes in the ICD while keeping the GL backbuffer RGBA8.
- [x] Route captured raw mouse motion through relative PS/2, in Locked and Confined
  modes; correctly release buttons and avoid tablet coordinates during capture.
- [x] Update the ordinary Windows 2000 image from old `jrg`/app-local providers to
  the system stack. Preserve the Half-Life CD attachment.
- [x] Observe actual retail Half-Life at 800×600×16, a fixed host-window size,
  bounded mouse movement and normal quit restoring 1280×1024×32.

Evidence: `target/follow-through/halflife-win2000-userdisk-v1/acceptance.json`.
This covered launch and normal exit, **not Alt+Tab recovery**.

## Milestone 4 — Fullscreen reactivation and captured input: completed

The user observed black output and a launcher at the top left after switching
between Half-Life and other guest windows. Juke must follow the real guest monitor
mode; do not stretch an arbitrary small game window to impersonate a mode change.

- [x] Fix retired WNDOBJ bindings poisoning healthy GL contexts: one pre-submission
  rebind, temporary not-ready for missing/overflowed clips, no GL command replay
  or retry of an uncertain native swap. Actual-source sanitizer checks pass on
  Mac and Linux; the runtime-verified installer is `74064e43…`.
- [x] On accepted Juke ReturnCpu/Reset, mark the CPU frame dirty and request active
  redraw. This closes a source-identified lost-notification possibility; the
  previously occluded screenshot does not prove that race.
- [x] Verify actual gameplay → desktop → gameplay in the same retail Half-Life
  process: textured 800×600×16 → 1280×1024×32 desktop → textured 800×600×16.
  The successful return used normal launcher `SC_RESTORE`, equivalent to taskbar
  restoration. No forced mode, crop, context recreation or console notification
  was needed. Alt+Tab-back had left Explorer selected; that attempt did not test
  resumed game rendering and is not recorded as an Alt+Tab-selection pass.
- [x] Send plain WASD, Shift+WASD and Ctrl+Shift+WASD through the real Mac host-input
  path into captured Juke. The guest observer saw every chord, retained Half-Life
  foreground, saw no Alt/Windows keys, and ended with all keys released. This
  accepts current captured movement behavior; it does not prove the cause of
  the historical Windows Search incident.
- [x] Make the verified correction usable in ordinary Juke. Removed fixture-only
  runner autostart, quit and shut down cleanly, then selected the verified disk
  through the existing adoption mechanism. User data, presentation settings and
  Half-Life CD are preserved; diagnostic optical media is absent. Normal config
  selects `.dreamgpu-adoptions/20260915T041802Z-a8e5ca0b/disk.qcow2`.
  The original disk is unchanged.
- [x] Finish pending Wine map/fill analyzer corrections and replay patch identities;
  Cargo guest build and targeted LLVM analysis pass. These failure-path fixes
  are independent of the OpenGL recovery result; no new performance claim.

Current evidence is under `target/follow-through/fullscreen-recovery-20260914/`:
`recovery-acceptance.json`, `host-input-synchronized/acceptance.json` and
`normal-adoption.json`.
The September 13 local fixtures were absent when execution resumed; a private
copy of the stopped normal disk was used with the recovered exact installer.
Historical receipts in progress.md do not imply those old files remain available.

## Execution and automation rules

1. Use existing run/profile receipts; group evidence-supported fixes, then measure
   the changed candidate. One successful run is enough; do not repeat for reassurance.
2. Use serial/QMP and direct game launch. No click → screenshot → wait → click loop.
   Lifecycle testing must preserve the real launcher initialization; for this
   retail Half-Life, `-toconsole` and `+map` bypass it. Console map commands require
   real keyboard injection; `WM_CHAR` was ignored.
3. Record `get_window_state` with captures. An occluded host intentionally skips
   CPU-frame preparation; stale screenshots in that state are not recovery failures.
4. Export guest diagnostic logs over the existing serial probe without rebooting
   or copying disks. A successful diagnostic export is not a successful game resume.
5. Use stopped private disk copies, disable guest networking before execution and
   coordinate exclusive VM ownership. Preserve normal user disks and media.
6. Use subagents for bounded independent work. Keep builds in Cargo and tests
   proportional to changed behavior. No broad new infrastructure or test campaign.
7. Preserve visuals, latency and idle sleeping; no busy loops, deeper queues,
   reduced quality or altered workloads to inflate performance numbers.
8. Keep this checklist current; progress.md preserves history without restoring
   cancelled scope. Work on master, no unsolicited branches, commits or publication.
