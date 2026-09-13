# Fixed UT99 Glide acceptance

These source-built Win32 helpers install and run the original CityIntro scene without menu navigation, screenshot decisions, a shell, or networking. Foreground setup uses one verified input transaction in a helper-owned window. They are fixture tools, not a general installer.

Prepare private media outside Git:

```sh
python3 tools/unreal/prepare.py --output target/retro-media/ut99-dreamgpu-v5
DREAMGPU_BUILD=guest cargo build --release
```

The preparation command verifies the original `UT_GOTY_CD1.ISO` SHA-256 `e184984ca88f001c5ddd52035d76cd64e266e26c74975161b5ed72366c74704f`. It retains System, Textures, Sounds, Music and Help and the complete original Maps set. Its manifest records every retained file's source hash. The media, generated configuration and build outputs stay under `target`; no original game files belong in Git.

Put the prepared `UT99` folder at the fixture CD root. Install `DGUTSET.EXE` and `DGUT.EXE` in `C:\SIERRA\Half-Life` using the fixed fixture installer. The serial runner's whitelist exposes:

- `PROBE <id> utsetup`: `DGUTSET.EXE`, result `C:\DGUTSET.LOG`, outer timeout 500 seconds.
- `PROBE <id> utlogs`: `DGUTLOG.EXE`, result `C:\DGUTLOG.LOG`, bounded existing engine/Glide logs, no game execution.
- `PROBE <id> utglide`: `DGUT.EXE`, result `C:\DGUT.LOG`, outer timeout 75 seconds.

Setup copies fixed `E:\UT99` to `C:\UT99` with a 480-second deadline checked between files. A single blocking filesystem copy can finish after that deadline; the outer runner bounds the process. Repeating setup skips same-size files only in this pinned original-media tree. This is a reuse optimization, **not a guest hash-readback claim**. DreamGPU's changing `dgpugl.dll`, `glide2x.dll` and benchmark configuration are always refreshed. If setup times out, its copy counters/log identify the completed work and the next setup resumes.

UCC decompresses each original map through an owned process, with a bounded wait and cleanup. Setup validates each resulting Unreal package magic and returns a bounded UCC log tail on failure. `DGUT.INI` selects Glide for both windowed/game rendering, 640×480 fullscreen inside the guest, first-run version 436, no sound, no direct draw, no capture and no ServerActors. Host tests also keep the virtual NIC down.

The game command is fixed:

```text
C:\UT99\System\UnrealTournament.exe CityIntro.unr INI=DGUT.INI USERINI=DGUSER.INI ABSLOG=C:\DGUTENG.LOG -nosound -noframecap -log
```

The helper detects owned critical-error windows without input, deletes the previous engine log, waits up to 50 seconds for explicit `Bringing Level CityIntro.` and `Game engine initialized` evidence from the engine file or its owned log window, and verifies that its owned game's `dgpugl.dll` and `glide2x.dll` come from `C:\UT99\System`. The stock file writer buffers 4096 bytes despite its unbuffered flag. The original `-log` option exposes `WLog` and its `WEditTerminal` child, which receive the engine log directly. The helper reads only that owned control with a 100 ms `SendMessageTimeout`, without typing commands or inspecting pixels. It also requires a successful `wglSwapBuffers` marker after the most recent Glide context open; an earlier loading-screen frame cannot satisfy that gate. It hides the log window before measurement. It measures a ten-second running interval, then closes that process's windows and, if needed, terminates only the owned process handle. Its result includes a bounded fresh engine-log tail, readiness time and measured interval. It does not infer game FPS from elapsed time: host GPU presentation tracing supplies FPS and latency measurements.

A successful source build or configuration check does not establish game rendering. Keep the first successful live run and its binary hashes, engine log and host trace; do not repeat old controls or return to menu automation.

The Mac source setup gate copied 568 files and decompressed CityIntro in 16.3 seconds. The initial game fixture exposed a missing `Help/Logo.bmp`; keep the original Help directory because the retail engine loads these startup bitmaps before renderer initialization. The revised setup reused installed assets and added the 14 original Help files. Live game acceptance remains tracked separately.

The stock engine also loads `Entry.unr` before the requested scene. Preparation therefore retains all original maps, and setup decompresses them in a single bounded pass; avoiding omitted-asset retries is worth the roughly 55 MB extra media.

The retail Glide renderer requires fullscreen for its reported Voodoo board. The original `GlideDrv.dll` SetRes branch at RVA `0x2c19–0x2c63` accepts a windowed request only for SST96/Voodoo Rush (type 1); OpenGLide reports Voodoo (type 0). The first actual engine log confirmed successful DreamGPU context/TMU initialization before this rejection. The fixture requests guest fullscreen rather than misreporting a different board. Juke remains an ordinary host window. The launcher removes the fixed fixture `Running.ini` marker only after its preceding owned process has been cleaned up, preventing prior handled failures from opening the stock recovery wizard.

The NT driver window-ownership regression was independently reproduced by closing and reopening Glide on the same HWND, then fixed by comparing the real kernel process identity rather than a retired GL client token. The private NT kernel interface is version 6; install its matching miniport/display pair and cold-activate it before this game gate. Mac acceptance passed 1024 exact GPU pixels and eight swaps including the reopen. That passing API control is separate from real-game readiness.

A texture-heavy game startup sample is not steady gameplay. The first traced startup exposed pixel-store query rejections despite successful executed GL batches. Acceptance must inspect validation rejections as well as execution status, and must establish map readiness before making frame-rate claims. `scripts/benchmarks/game.py` combines the fixed guest command with actual host GPU receipts, subsequent submissions, dropped-sample accounting and native rejection evidence; its whole-attempt activity count is not game FPS.

The first Mac real-game gate passed with the pixel-store query fixes: `target/retro-fixtures/mac-pixelstore-v1/utglide-ready14-01/run.json` records explicit CityIntro loading, app-local providers, 3.281-second readiness, ten seconds of owned game execution, 46 distinct GPU frame receipts, and zero dropped samples/native GL rejections. These are compatibility/activity facts, not an engine FPS result. Its subsequent profile included an old powered-off fixture app still rendering, so it cannot support percentage comparisons with a quiet candidate.

The current helper verifies the owned `WWindowsViewportWindow` is foreground with a bounded state check, then signals fixed optional measurement events around its ten-second interval. The runner forwards those phases; the host obtains one actual Juke-rendered frame and native counter boundaries from them. After replacing the runner, verify its exact build identity before submitting setup or game commands: the old runner can otherwise accept a job just before installation stops it, leaving a running helper that blocks its own executable replacement.

Foreground setup runs before game creation. A serial command has no Windows foreground permission; forwarding `AllowSetForegroundWindow` from an ineligible runner cannot create that permission. The helper creates a temporary window, verifies the target with `WindowFromPoint` and its process identity, inserts one tagged mouse move/down/up transaction through `SendInput`, and waits up to one second for the actual down/up messages and foreground ownership. Held buttons/modifiers, a foreign target, partial input, missing acknowledgments, and unexpected foreground state fail explicitly. Partial insertion that includes a button-down attempts the matching release. The helper restores the cursor and destroys/unregisters its temporary window on completion or failure.

Only after receiving input does it create the game suspended, grant permission to that exact child PID, and resume it. The temporary window remains alive until the unique game viewport accepts foreground activation. This order matters: fullscreen Glide captures mouse input after initialization, so attempting the transaction afterward cannot reach the helper window. The readiness gate verifies the game HWND, PID and thread and acknowledges its separate input queue with bounded `WM_NULL`. No input queues are attached and no global foreground policy is changed.

Microsoft documents the [foreground and last-input conditions](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow), [input insertion semantics](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput), and [bounded activation acknowledgment](https://devblogs.microsoft.com/oldnewthing/20161118-00/?p=94745). `test_focus.py` and `test_focus_input.py` exercise the actual helper headers under sanitizers, including ownership changes, unresponsive targets, held input, partial insertion, deadline and cleanup paths.

Linux acceptance for the final launch order is recorded privately in `target/validation/linux-dgpu-focus-v7/`. Both original UT Direct3D and Glide completed their ten-second intervals, proved input down/up acknowledgments and exact game foreground ownership, and produced host GPU submissions with zero dropped capture samples or native GL rejections. The Glide process required the existing bounded owned-process termination fallback during cleanup; Direct3D closed gracefully. Frame receipt counts are activity evidence, not engine FPS.

Win98 needs a smaller live log read: its `SendMessageTimeout` wParam is limited to 16 bits, as documented in Microsoft's [Windows 95 Programmer's Guide](https://www.bitsavers.org/pdf/microsoft/windows_95/Programmers_Guide_to_Microsoft_Windows_95_1995.pdf). Passing the former 65,537-character capacity truncated it to one and concealed readiness while the game was already rendering. The shared helper now uses a 32,768-character buffer with a compile-time bound, and reports individual map/engine/renderer readiness flags on failure. The corrected helper passed both Direct3D and Glide on Win98 on Linux and macOS; evidence stays in the private fixture ledgers.

The maintained helper sources compile as C++23 using the pinned MinGW GCC toolchain. Files, process/thread handles, snapshots, events, and directory enumerations have noncopyable scoped owners; early returns close resources while preserving the triggering Win32 error. The C ABI entrypoint calls a scoped body before `ExitProcess`, so normal completion runs destructors without a CRT. Fatal `Die` paths terminate the helper after the existing bounded child cleanup; they do not require exception unwinding.

The build enforces GCC static analysis with warnings as errors, PE subsystem 4.0, the existing Win98 API allowlist, and scalar Pentium III code without XMM/YMM/ZMM registers. It links no C++ runtime, exceptions, RTTI, or static-initialization support. `test_handles.py` exercises the actual owner under ASan/UBSan and Clang analysis; the focus, input handoff, and fresh failure-reader tests also compile the actual helper headers as C++. Game invocation, serial phase events, deadlines, and installed filenames retain the existing automation contract.
