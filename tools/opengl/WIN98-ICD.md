# Win98 system ICD diagnostic fixture

`DGICDBT.EXE` is a fixed diagnostic bootstrap, built by the Win98 Cargo/CMake
recipe. It requires Windows 98 and a currently active display driver whose
`OPENGL_GETINFO` escape reports version 2, driver version 1 and ANSI `DGPUICD`.
The Win16 output is 270 bytes; it is distinct from NT's wide-name interface.
The authoritative Win32 call is `ExtEscape` with that explicit output capacity.
On the tested Windows 98 system, generic `Escape` returned -1 while `ExtEscape`
returned the exact descriptor and the normal Microsoft loader rendered correctly.

`DGICDDI.EXE` is the separate read-only inspection target. It records the OS
version and both Win32 `Escape`/`ExtEscape` discovery responses, then starts the
runner without changing registration. Use it to diagnose loader boundaries; a
failed telemetry call alone does not prove the normal Microsoft loader fails.

The bootstrap preserves the exact previous `DGPUICD` value under
`HKLM\Software\Microsoft\Windows\CurrentVersion\OpenGLDrivers` in
`C:\DGICD.BAK`, with a checksum and a flushed write before any registry mutation.
An absent value or the same provider is accepted; another provider is refused.
It registers `dgpuicd.dll`, flushes and reads back the value, then starts
`C:\DGPUBEN.EXE`. Its receipt is `C:\DGICDBT.LOG`. This is diagnostic registration,
not an installed/conforming provider verdict. Reboot recovery revalidates both
the backup and current value before proceeding.

`DGICDBT.EXE /register` performs the same checked registration and readback but
does not launch another runner. Use it when the owned serial runner is already
active. It accepts no configurable guest paths.

`DGICDBT.EXE /restore` restores the original value or absence. It first persists
`C:\DGICD.OFF` so a later boot cannot register it again. A parent key created by
the helper is deleted only when no values or subkeys remain; foreign contents
are preserved. Restoration can resume after interruption. The backup is retained.
The Windows 98 check applies to restoration, but the diagnostic display driver
need not still be active. Driver-file restoration is separate.

Prepare an independent, stopped image with:

```sh
python3 scripts/fixtures/win98-icd.py inputs.json --output target/diagnostic-image \
  --qemu-img target/qemu-build/qemu-img
```

The schema-1 input pins `source`/`source_sha256` and exactly six entries in
`files`: `driver`, `vxd`, `icd`, `bootstrap`, `runner`, and `probe`. Each supplies
`path`, `sha256`, and `before_sha256` (null means the destination must be absent).
The fixed destinations are the installed `WINDOWS/SYSTEM/DGPUMINI.{DRV,VXD}` pair,
`WINDOWS/SYSTEM/DGPUICD.DLL`, and root `DGICDBT.EXE`, `DGPUBEN.EXE`, `DGSYSGL.EXE`.
Both existing driver hashes are mandatory. Existing SetupAPI device binding is
preserved; this utility cannot install a previously absent display driver.

Preparation retains the original disk and copies replaced bytes into
`originals/`. Only the existing, recognized benchmark `WIN.INI` startup entry is
changed to the bootstrap. Microsoft `OPENGL32.DLL` and both registry hives must
remain byte-identical during offline preparation. The runtime fixture must pin
its native binary, firmware and runner, and disable the NIC before continuing.
Run the fixed `sysgl` route only after the expected runner reports ready. The
probe lives at `C:\DGSYSGL.EXE` with no app-local OpenGL library and uses the normal
Microsoft loader/GDI APIs. Preserve bootstrap and probe logs independently when
readiness or pixel verification fails; do not rerun an unchanged failed launch.
