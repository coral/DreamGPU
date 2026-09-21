# Windows guest installer

`DREAMGPU_BUILD=all cargo build --release` produces one
`target/guest/dreamgpu.exe`. Run it **inside the Windows guest**. It detects
Windows 98 or 32-bit Windows 2000/XP, validates the DreamGPU PCI adapter and
embedded payloads, installs the display driver and system graphics providers,
and offers a restart when needed. macOS and Linux use the same guest installer.

Interactive setup opens an installation window immediately. It shows the current
operation, filenames being checked/extracted/installed, a scrolling activity log,
and elapsed time while driver operations and graphics tests run. The window stays
responsive during installation. Setup failures include an error code and the last
operation; completion and restart requests remain explicit dialogs. Closing setup
is disabled while it is working. `/silent` suppresses the window and dialogs.

The executable embeds the supplied `icons/DreamGPU.ico` for Explorer and
shortcuts. The installer window and result/restart dialog title bars, taskbar
and Alt+Tab use the legacy icon set on Windows 98/2000 and the XP set on XP,
with separate small and large icons. Icon changes are included in the installer
manifest and picked up by the installer-only rebuild.

The normal provider paths are:

- OpenGL: Microsoft system `opengl32.dll` and GDI load `dgpuicd.dll`.
- Glide2: the system `glide2x.dll` uses the DreamGPU OpenGL frontend.
- DirectDraw and Direct3D6–9: system providers use the packaged WineD3D
  translation libraries and DreamGPU acceleration.

Games use their ordinary renderer selection. Installation does not require
copying DLLs beside games, a custom OpenGL driver argument or patched imports.
Glide1/3 and a native Direct3D HAL are not advertised. API coverage and measured
native-provider limitations are recorded in [the plan](../../docs/plan.md).

## Status

System provider activation is working on Windows 98, Windows 2000 and XP.
Current work is game rendering/performance and usable adapter properties;
installer rollback, unattended installation and exhaustive lifecycle campaigns
are explicitly outside the [current plan](../../docs/plan.md).

The implementation details below describe existing code, not remaining tasks
or release requirements. Disk images and emulator states are sufficient for
our development recovery. [Progress](../../docs/progress.md) retains historical
results and current game measurements.

## Commands and results

Double-clicking the installer runs installation or reconciles its owned current
installation. It reports completion only after driver verification and six
normal-loader tests: OpenGL, Glide2 and Direct3D6/7/8/9. A restart request is
pending work, not installed success. An owned startup entry resumes the recorded
operation. New transactions use a persistent owned Run receipt on both OS
families. New startup entries use `/continue /startup`: completion is confirmed,
and a remaining restart or setup failure opens an actionable dialog. Concurrent
startup callbacks are suppressed while continuation or its result dialog is open.
Another installer already running does not produce a duplicate warning. Restarts
always require confirmation. Explicit `/silent` still suppresses all UI, including
when combined with `/startup`. Historical receipts retain their original silent
command and exact registry ownership; an existing pending installation can still
be resumed interactively with its retained `setup.exe /continue`.

Setup currently completes and verifies the display-driver phase before starting
graphics-library replacement. If both phases require boot-time changes, this
ordering takes two Windows restarts. This is an installer sequencing choice, not
a Windows requirement that every update needs two restarts. The first restart
prompt explains the possibility, and startup continuation requests another when
needed. Pending work is not reported as an activated graphics update.

| Command | Meaning |
| --- | --- |
| `dreamgpu.exe /silent` | Install with JSON receipts and process exit status, without UI |
| `dreamgpu.exe /continue` | Resume the durable current operation |
| `dreamgpu.exe /recover` | Use a corrected executor to finish an authenticated pending rollback or removal |
| `dreamgpu.exe /upgrade` | Install a new owned generation, retaining the immediate prior state |
| `dreamgpu.exe /repair` | Repair exact known-original runtime drift; reject unknown replacement bytes |
| `dreamgpu.exe /rollback` | Restore the immediate prior generation, or cancel incomplete initial staging |
| `dreamgpu.exe /uninstall` | Restore the captured first-install baseline, or cancel incomplete staging |
| `dreamgpu.exe /stage` | Diagnostic private extraction/preparation only; does not activate graphics |

`/silent` can accompany an operation. Repair preserves the verified driver and
reruns only affected API proofs. Uninstall removes only owned files, device,
service, INF and registry changes; borrowed original components remain intact.
A removal already in progress must finish rather than reverse into installation.

Key exit statuses are 0 activated, 10 staged, 11 pending reboot, 12 rolled back,
13 removed, 17 staging cancelled, 20 unsupported OS, 21 missing/ambiguous device,
22 invalid payload, 23 arguments, 26 operation incomplete, 27 preparation failed,
28 installer busy, 29 ownership conflict and 32 UI startup failed (installation
not started). Driver-only diagnostic statuses
14–16 do not report full graphics activation.

## Ownership and recovery

The C++23 Win32 boundary uses checked lengths, bounded allocations, RAII,
SHA256-verified payloads and no dynamic C++ runtime, exceptions or RTTI. The
installer targets PE4 and imports APIs available on the selected legacy OS.
The Cargo audit specifically checks that `CM_Get_Device_IDA` resolves from
CfgMgr32, since Windows 98 does not export it from SetupAPI.

Running setup normally starts fresh extraction: after checking the new payloads
and acquiring the installer lock, it deletes `%WINDIR%\DGSETUP.NEW`,
`DGSETUP.JRN` and `DGSETUP.CAN`, including incomplete or mismatched state left by
an earlier build. This cleanup is repeatable after interruption and never
traverses directory links. No manual file renaming or old installer is required
for abandoned extraction.

Explicit `/continue` and `/rollback` still use a ticket binding extraction to
the exact installer, OS and payload catalog. Only a complete catalog is
published as `%WINDIR%\DreamGPU`. Running a new EXE over a completed installation
automatically selects upgrade; that path replaces the installed components
while retaining the original Windows driver/runtime before-images. The installed
tree is not disposable extraction scratch space.

Guest packages contain one `LICENSES.txt` with all license texts and attribution
records in labeled sections. The individual source notices are combined before
the package manifest is hashed and embedded.

Within that private tree, GLOBAL coordinates driver and provider transactions.
Independent immutable generations retain both the original baseline and the
immediate predecessor. Before-images and intent records are flushed before
public mutations. The initial driver transaction supports prior DreamGPU,
Microsoft VGA, an unbound NT5 PCI device, and the Win98 legacy
`qemumini.drv`/`qemumini.vxd` pair. Other prior drivers are rejected before public
changes. Legacy migration uses a version 5 journal with the existing byte layout:
it retains the exact compatible INF and both legacy binaries in place, captures
verified private backups, and requires the new DreamGPU destinations to be absent.
Rollback reselects that original INF without copying files, verifies the restored
pair and started device, and removes only the recorded DreamGPU images. Changed
originals, backups, or unexpected binding pairs stop recovery with a conflict.
The currently loaded driver protocol cannot
report a resident build hash, so verification makes no such claim.

NT5 protected-runtime replacement uses an owned boot rename transaction without
disabling Windows File Protection or replacing its original cache. A disjoint
valid OS rename queue defers installation to a reboot; overlapping or ambiguous
entries remain conflicts. Win98 uses its checked boot replacement transaction.
Startup entries retain their own before-images and exact installer identities.
Both OS families use a persistent owned Run value for new pending transactions,
avoiding RunOnce re-registration during the same startup. GLOBAL is the sole
startup owner for managed components; missing or foreign coordinator registration
blocks component mutation. Historical receipts keep their exact before-images
and retirement semantics. Completion restores the captured startup baseline.
The coordinator installs the driver before providers and restores providers
before removing the driver. Retained private journals/originals support recovery;
this is interruption recovery through Windows APIs, not a claim of atomicity
under arbitrary power loss or hostile concurrent disk changes.

## Automated acceptance

`scripts/fixtures/system-install.py` drives fixed hash-pinned helpers over serial,
records global phase/epoch/generation receipts and performs bounded clean cold
boots. It disables guest networking before execution and never forces a reboot
or repeats a completed operation. Every cold successor uses an independent copy
of the stopped private disk. Failed operations retain their original errors.

The fixed `sysinstall`, `sysresume`, `sysupgrade`, `sysrollback`, `sysremove`,
`sysrepair` and `sysrecover` routes accept no arbitrary guest command. Their helpers require the
exact installer SHA256. Normal-loader proofs verify actual loaded system-module
paths and pixel/presentation results with no neighboring provider DLLs.
The `sysui` route runs the real interactive `/continue` operation through
`DGSETUI.EXE`. It reads only the authenticated installer's owned standard dialog,
validates the known message and button text/ID, declines a requested reboot, and
checks its process exit against the durable continuation receipt. The sole NT5
OK button uses control ID2; both its ownership and exact label are checked.
Unknown dialogs fail the gate. This uses control messages, not screen coordinates.

Actual policy/Win32 adapter tests inject failures into extraction, journals,
copy/rename, registry state, reboot queues, driver generations and recovery.

For an installer-only incremental build from already audited OS packages:

```sh
cargo run -p dreamgpu-build -- installer --root . --output target/guest
```

This rebuilds the installer resources and verification header together; it does
not rebuild unchanged drivers or translators. Runtime receipts always identify
the exact executable tested.
