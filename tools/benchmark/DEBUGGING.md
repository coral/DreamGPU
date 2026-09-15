# Fixed startup diagnostics

The ordinary serial controller remains event driven. Its fixed executable allowlist is checked against the actual OS's PE32 subsystem version before `CreateProcess`, avoiding Win98 loader dialogs for newer helpers. Half-Life additionally requires the original `HALF_LIFE` CD with its fixed setup-file markers; a missing retail CD returns `retail-media-missing` before launch or renderer changes.

`DGHLDBG.EXE` is a separate C++23 startup oracle, built by the Cargo guest build. The fixed `hldebug` runner route launches only the authorized retail Half-Life installation under `DEBUG_ONLY_THIS_PROCESS`. It records bounded DLL names/bases, exception metadata, owned thread registers and 32 stack words. It never attaches to an existing process, accepts an executable path, or reads another process's memory. Debug image handles are closed; thread/process debug handles follow the documented `ContinueDebugEvent` lifetime. Its deadline bounds the diagnostic and ends its owned game process. It is not a graphics acceptance or FPS tool.

A missing-CD investigation on Win98 showed the value of the module trace: the launcher loaded system and retail support DLLs but never loaded `hw.dll` or DreamGPU, with no startup exception during the observation. Mounting the original game CD resolved startup and completed the timedemo on both hosts. The null-call dialog observed after the original timeout was secondary cleanup evidence, not proof of a GPU fault.

Actual-header sanitizer checks cover PE header truncation, offset overflow, PE32 version mismatch, original-media identification and wrong/missing media. Optional private Windows DLL export audits check every debugger import; no OS DLL is packaged. Microsoft documents the [debug event loop](https://learn.microsoft.com/en-us/windows/win32/debug/writing-the-debugger-s-main-loop) and [debug handle lifetime](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-continuedebugevent).
# Retail fullscreen reactivation

Cargo builds `DGFOCUS.EXE` from `hl-focus.cpp`. In a ready private Windows 2000
fixture, mount it on the secondary optical drive as `E:\DGDRV.EXE`, then run:

```sh
python3 scripts/diagnostics/hl-focus.py --fixture /absolute/private-fixture --output target/results/fullscreen-return
```

This uses the existing `ntupdate` serial entry as a diagnostic transport; it does
not update a driver. The helper starts one new normal retail Half-Life process,
logs actual Console/Resume child HWNDs, sends their button notification, types
`map c1a0` using keyboard events and performs real guest Alt+Tab away/back.
`-toconsole` and `+map` must not be added: this retail build skips launcher
initialization with those arguments. Existing Half-Life processes are refused.

`C:\DGDRV.LOG` is exported through serial. The helper leaves its game running for
diagnosis. Review the phase log and captured textured game/desktop/game frames;
successful log export alone is not a recovery pass. Captures include host
visibility because an occluded Juke intentionally skips CPU-frame preparation.
These guest-injected keys do not validate Juke's host shortcut routing.

If collection fails after normal console entry, `DGFOCUSR.EXE` continues that
same process from its console without relaunching. Stage it as `E:\DGDRV.EXE`
with `E:\DGFOCUS.PID` containing the recorded PID as exactly four little-endian
bytes. It requires that PID, one matching process at the fixed Half-Life path,
and the owned engine foreground. Do not use this continuation on a process whose
initialization or current console state has not been observed.

`DGFRET.EXE` uses the same PID file to send the normal launcher restore system
command. It records whether Windows restores the engine directly; only if the
launcher remains active does it notify a visible Console/Return button. This
separates taskbar-style restoration from Alt+Tab shell selection.

`DGKEYOBS.EXE`, staged through the same diagnostic entry, is a read-only bounded
foreground and WASD/Shift/Ctrl/Alt/Win key-state observer for **host-origin** input
checks. It creates no window and sends no input. Start it over serial while the
game is active, then inject the host sequence through the owned Juke window.
It finishes after observed key activity ends and all keys stay released for two
seconds, with a 60-second bound if input never arrives. Launch observation and
host injection in one controller at serial STARTED; coordinating them through
separate agent messages can miss the observation interval. A completion PASS
only means the log was collected; key edges and game foreground establish input
behavior. Use the synchronized controller after focusing and capturing the consumer:

```sh
python3 scripts/diagnostics/host-keys.py --fixture /absolute/private-fixture --output target/results/host-keys --guest-pid <recorded-pid> --guest-window <recorded-engine-hwnd>
```

It checks every movement chord, retained game foreground, absence of Alt/Windows
keys and final release. A failed assertion returns a nonzero exit status. The
PID/HWND must describe the current game; do not reuse values from an earlier boot.

For a private Windows 2000 fixture with `C:\DGPUBEN.EXE` already installed, import
this registry file once inside that private copy:

```reg
REGEDIT4

[HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run]
"DreamGPUFixtureRunner"="C:\\DGPUBEN.EXE"
```

For example, put `AUTORUN.REG` on its secondary optical drive and run
`regedit /s E:\AUTORUN.REG`. Subsequent fixture manifests omit
`guest_start_program`; serial READY after login proves automatic startup without
typing into Run after every driver reboot. Keep any existing login trigger the
fixture requires. This is test-image provisioning, not product installer behavior;
do not add the entry to ordinary user disks or adopt a diagnostic image with it.
