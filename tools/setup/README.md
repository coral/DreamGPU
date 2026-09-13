# Windows guest installer

`DREAMGPU_BUILD=all cargo build --release` produces one
`target/guest/dreamgpu.exe`. Run it **inside the Windows guest**. It detects
Windows 98 or 32-bit Windows 2000/XP, validates the DreamGPU PCI adapter and
embedded payloads, installs the display driver and system graphics providers,
and offers a restart when needed. macOS and Linux use the same guest installer.

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

The complete installer has activated all six normal-loader GPU checks on
Windows 98, Windows 2000 and XP. Current Win98 and XP baseline removal and XP
fresh reinstall have exact-original audits. XP also recovered a preserved failed
rollback using the corrected installer while retaining its original executables
and backups. Final composed upgrade/rollback/removal gates and Win98 shutdown
after games remain in progress. [Progress](../../docs/progress.md) records exact
artifacts and limits; a build alone does not establish runtime acceptance.

## Commands and results

Double-clicking the installer runs installation or reconciles its owned current
installation. It reports completion only after driver verification and six
normal-loader tests: OpenGL, Glide2 and Direct3D6/7/8/9. A restart request is
pending work, not installed success. An owned startup entry resumes the recorded
operation (Run on Windows 98, RunOnce on NT5).

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
28 installer busy and 29 ownership conflict. Driver-only diagnostic statuses
14–16 do not report full graphics activation.

## Ownership and recovery

The C++23 Win32 boundary uses checked lengths, bounded allocations, RAII,
SHA256-verified payloads and no dynamic C++ runtime, exceptions or RTTI. The
installer targets PE4 and imports APIs available on the selected legacy OS.
The Cargo audit specifically checks that `CM_Get_Device_IDA` resolves from
CfgMgr32, since Windows 98 does not export it from SetupAPI.

A checksummed ticket binds initial extraction to the installer, OS and complete
payload catalog before creating `%WINDIR%\DGSETUP.NEW`. Exact owned partial
copies can resume; cancellation records its direction before deleting anything.
Only a complete catalog is published as `%WINDIR%\DreamGPU`. Unknown files,
foreign edits and corrupt complete records are rejected. A crash before the
first ownership ticket is fully written fails closed; unproven files are not
silently adopted.

Within that private tree, GLOBAL coordinates driver and provider transactions.
Independent immutable generations retain both the original baseline and the
immediate predecessor. Before-images and intent records are flushed before
public mutations. The initial driver transaction supports prior DreamGPU,
Microsoft VGA and an unbound NT5 PCI device; unsupported prior drivers are
rejected before public changes. The currently loaded driver protocol cannot
report a resident build hash, so verification makes no such claim.

NT5 protected-runtime replacement uses an owned boot rename transaction without
disabling Windows File Protection or replacing its original cache. A disjoint
valid OS rename queue defers installation to a reboot; overlapping or ambiguous
entries remain conflicts. Win98 uses its checked boot replacement transaction.
Startup entries retain their own before-images and exact installer identities.
Windows 98 uses a persistent owned Run value while work is pending, avoiding
RunOnce re-registration during the same startup; completion restores its baseline.
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

The fixed `sysinstall`, `sysresume`, `sysupgrade`, `sysrollback`, `sysremove` and
`sysrepair` and `sysrecover` routes accept no arbitrary guest command. Their helpers require the
exact installer SHA256. Normal-loader proofs verify actual loaded system-module
paths and pixel/presentation results with no neighboring provider DLLs.
Actual policy/Win32 adapter tests inject failures into extraction, journals,
copy/rename, registry state, reboot queues, driver generations and recovery.

For an installer-only incremental build from already audited OS packages:

```sh
cargo run -p dreamgpu-build -- installer --root . --output target/guest
```

This rebuilds the installer resources and verification header together; it does
not rebuild unchanged drivers or translators. Runtime receipts always identify
the exact executable tested.
