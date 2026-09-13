Historical benchmark paths in these notes refer to the original Juke evidence tree.
Current build layout and acceptance scope are in [guest runtime](../README.md)
and [execution evidence](../../docs/progress.md).

# Win98 OpenGL bridge audit

Status (September 12): native 2D remains working. The VWIN32 identity and
checked user-memory gates now have real Win98 evidence:
identity (`benchmarks/retro-gpu/win98-identity.json`, historical Juke evidence) and
memory (`benchmarks/retro-gpu/win98-memory.json`, historical Juke evidence). The offscreen GL
channel passed the common typed-query/texture probe and seven native GPU
presents through the Juke IOSurface consumer:
channel evidence (`benchmarks/retro-gpu/win98-gl-channel.json`, historical Juke evidence);
window clipping, primary CPU coherence and public WGL remain incomplete.

The original starting point: the source-built Win98 driver implements native 2D. The separate retail
Half-Life fixture reaches a real software-rendered `c0a0` map using the original
CUE/BIN. Neither result establishes Win98 OpenGL acceleration. This audit records
the remaining guest bridge work; no replacement adapter or host protocol is
needed.

## Reuse the existing frontend and native engine

The common implementation should remain in `../opengl`: WGL context management,
thread ownership, immutable command packing, textures, typed queries, front/back
semantics and readback. The native interface remains PCI `1234:1113` and the
same `gl.h` records, host contexts and export credits used by NT5.

There are three platform boundaries in `../opengl/frontend.cpp`:

| Boundary | Current NT5 entry | Win98 requirement |
| --- | --- | --- |
| Open, submit, query, close | `ExtEscape(DG_ESCAPE)` | A bounded 32-bit VxD request with checked input/output lengths and an OS-derived owner |
| Bind a window | `ExtEscape(WNDOBJ_SETUP)` | A display-driver binding with an authoritative visible region and defined lock/lifetime rules |
| Swap or publish front | `DrawEscape(DG_DRAW_ESCAPE)` | An ordered presentation against that binding, preserving CPU/GPU primary coherence |

`Request`, `JglQuery`, `Bind`, `wglSwapBuffers` and `PublishFront` are the concrete
frontend extraction points. Preserve NT5's current behavior behind its adapter.
Do not scatter Win98 branches through GL packet/state code.

The freestanding helpers in `../nt/include/dg-gl-validate.h`,
`dg-gl-result.h` and `dg-gl-signature.h` are reusable despite their current
directory. They require a 32-bit `ULONG` and the shared constants, not NT kernel
services. Reuse their tests and validation, including function-kind bits,
generation invalidation and driver-stamped client identity. Moving these helpers
to a common directory should be a separate mechanical change coordinated with
the NT driver owner.

## The Win16 escape boundary cannot carry the current request unchanged

The VMDisp9x NE driver's `Control` entry has a 16-bit `UINT function` and far
input/output pointers, with no explicit `cbInBuffer`/`cbOutBuffer` arguments.
Our private escape IDs `0x4a524701` and `0x4a524702` do not fit that function
number. The maximum 65,536-byte command payload plus its 32-byte request header
also exceeds a single Win16 segment. Merely adding those cases to `Control`
would not implement the existing ABI.

The pinned VxD already receives `W32_DEVICEIOCONTROL` and its OS-supplied
`DIOCParams`, including buffer lengths, `hDevice` and `tagProcess`. This is the
candidate 32-bit command entry. Keep the existing request/reply payloads and
their limits, but allocate driver-owned locked command and result buffers, copy
and validate the complete input before DMA, and copy only a matching bounded
result back. Faulting, unaligned or changing caller memory must not become a
host DMA address. Small Win16 controls may coordinate display state; they must
not become an unchecked pointer tunnel for GL batches.

Do not reuse the donor's `OP_FBHDA_*` user memory-map API. Its dispatch includes
unchecked input/output dereferences and returns framebuffer metadata pointers;
those are not the ownership or copy rules of DreamGPU's channel.

## Ownership and completion need Win9x implementations

The existing VxD `DIOC_OPEN`/`DIOC_CLOSEHANDLE` handling maintains one global open
counter. Its `DESTROY_PROCESS` callback performs cleanup only in the donor's
`SVGA` branch. Neither implements per-process GL ownership for DreamGPU's QEMU path.

Allocate an opaque client token on a validated open and bind it to the
OS-supplied process and handle identity. Never accept a process ID from the
request payload as authority. Instrument actual Win98 open/request/close/exit
callbacks first: the relationship between `tagProcess`, `hDevice`, and the
existing `DESTROY_PROCESS` argument must be demonstrated, including reuse.
Resource teardown must share the same serialization and rundown as submission,
mode changes and display access. `DllMain` cleanup alone cannot cover process
termination or a crashed application.

`dg-vxd.c` currently owns one 40-byte native-2D command, IRQ bit 1, a semaphore
and a one-shot watchdog. Add bounded GL DMA/results, IRQ bit 2 and one ordered
ownership mechanism. Preserve the one completion check followed by a sleeping
wait. VMM critical-section contexts already cause the 2D path to decline before
submission; they must not acquire a sleeping GL wait accidentally. Deferred
cleanup may need the pinned VMM event services, whose callback context and
lifetime must be verified on the actual guest.

The existing 2D fault path resets the device and disables further offload. A
reset also destroys GL contexts and a GPU-owned desktop canvas. Once GL exists,
recovery must follow the NT path's explicit coherence/failure rules; silently
resetting and continuing CPU drawing could lose newer pixels.

## Window clipping and CPU access are the largest remaining gate

NT5 uses driver-created `WNDOBJ` state under GDI's window lock. Win98's current
driver has no equivalent binding implemented. `GetClientRect`, a caller's clip
list, and cached user-mode window coordinates are insufficient: another window
can move or occlude the drawable before presentation. The driver must obtain
the visible region and retain its validity for the ordered present. A token
must expire on window destruction, owner exit, display recreation or mode
generation change. HWND reuse must not revive an old binding.

VMDisp9x's `BeginAccess_VXD`/`EndAccess_VXD` callbacks and the DIB engine's
`deBeginAccess`/`deEndAccess` fields are concrete places to investigate primary
access coherence. The current hooks chiefly bracket cursor exclusion and donor
framebuffer access. They do not yet prove that every text, brush, stretch,
screen read, DirectDraw lock or software cursor operation is covered. Some
driver entry points forward directly into the DIB engine.

Before allowing the GPU canvas to own newer desktop pixels, trace those access
paths and their VMM critical-section state. An unsupported CPU access needs an
ordered readback/RETURN before it can read or modify the primary. A path that
cannot legally wait cannot simply fall through to stale CPU memory. Supported
native fills/copies can use the existing ordered desktop operations. When no
canvas is active, keep the validated native-2D/DIB path unchanged. A game-only
swap loop should not introduce a routine full-screen readback.

## Loader and API evidence

The frozen public frontend `target/retro-wgl-v6-front/dgpugl.dll` was checked
against **actual DLL export tables copied read-only from the Win98 fixture**.
All its imports are present: `GDI32`'s `DrawEscape`/`ExtEscape`, the used
`KERNEL32` heap/TLS/critical-section/process/thread functions, and `USER32`'s
window/DC functions. The detailed list is
`target/retro-fixtures/win98-halflife/win98-wgl-import-audit.json`.

That establishes import names, not working Win98 semantics or successful DLL
loading. The current frontend build deliberately targets NT5 and PE subsystem
5.0. A Win98 frontend build needs an explicit supported SDK/PE target followed
by actual `LoadLibrary`, context and thread tests. There is no evidence yet
that the current subsystem header alone causes rejection. Do not register this
evolving public wrapper as a complete system ICD: the donor's `OPENGL_GETINFO`
escape and ICD names are a different integration contract.

## Bounded implementation order and acceptance

1. Add the 32-bit VxD diagnostic channel first, using the shared validators and
   native engine. Prove seven or more offscreen presents recycle export credits;
   then typed query, texture, front/back and readback pixel checks. No windowed
   GPU capability is advertised at this stage.
2. Exercise two processes and multiple threads: stolen tokens, close/double
   close, forced process exit, handle/process reuse and teardown during pending
   work. Check invalid lengths, 64 KiB boundaries, inaccessible buffers, result
   capacities and generation changes. Require no busy polling or leaked host
   resources.
3. Establish the driver-owned visible-region and primary-access contracts on
   Win98 before enabling window presentation. Cover complex overlap, moving and
   resizing windows, destruction, mode changes, software cursor, DIB CPU writes
   and screen reads. Host operations must observe one order throughout.
4. Run the same public WGL pixel oracles and desktop performance gates used on
   NT5, followed by the original retail Half-Life OpenGL renderer. The existing
   software checkpoint is a clean fixture for this final step, not GPU evidence.

The missing clipping and legal-wait contracts are why this audit does not add a
speculative Win98 presentation implementation or an alternate host GPU path.

## Observed Win98 OS contracts (September 12)

The source-built identity probe completed with four processes, duplicate
handles, normal/forced exits and twelve handle reuse cycles. Its serial log
returns completion/errors directly. The LE resident module name must match the
DDB name: the former `qemumini` / `DREAMGPU` mismatch prevented `CreateFile`
from reaching our callback. Both now use `DREAMGPU`, with a space-padded DDB.
The binary audit enforces this [DDK build contract](https://techshelps.github.io/MSDN/WIN95DDK/HTML/S2305.HTM).

`DIOC_OPEN` arrives with a zero handle, so private channel OPEN allocates the
opaque client after VWIN32 has assigned `hDevice`. Requests use OS-supplied
`tagProcess` plus `_GetCurrentContext()` and the per-process handle. Duplicate
handles close independently. In this fixture inherited-handle requests were
rejected before reaching the VxD. Normal and forced process exits both delivered
`DIOC_CLOSEHANDLE` in the retiring process context with critical count zero.
`DESTROY_PROCESS` instead carries the public PID and executes under a critical
section in another process context. Its current context is never an owner key.

The checked memory adapter validates bounded private ranges, checks committed
pages, pins a global alias, and examines the original user PTEs for present/user
and (for outputs) writable access. Full input is copied before output mutation.
Thirteen real IOCTL cases passed, including an unaligned 65,568-byte buffer
spanning eighteen pages, readonly input, readonly output rejection, inaccessible
pages, nulls, kernel addresses, overflow and an uncommitted boundary. The global
alias is also the address used for unlock; see the original
[WinPcap VxD adapter](https://github.com/wireshark/winpcap/blob/master/Packet9x/VXD/Lock.c)
and [DOSLIB VMM service definitions](https://github.com/joncampbell123/doslib/blob/master/windows/w9xvmm/dev_vxd_dev_vmm.h).

These observations do not establish pending-GPU forced-exit rundown, a legal
window clipping lock, or coverage of every DIB primary access path. Keep those
gates explicit before enabling public windowed GL.

## Window implementation and observed DDI route

The source-built access oracle records actual `BeginAccess`/`EndAccess` pairs
for text, lines, fills, GetPixel, DIB upload/read, stretching and cursor access.
All observed calls had VMM critical count zero. The explicit native screen-copy
path bypasses DIB access and therefore needs its own coherence guard. Evidence:
access (`benchmarks/retro-gpu/win98-access.json`, historical Juke evidence).

A compatible bitmap reaches the display BitBlt entry with GDI-clipped source
and destination coordinates: two disjoint clips and four rectangles around an
occluding window. A DIBSection may bypass that entry, and a memory-DC Control
escape is rejected. Those unsuccessful routes are recorded, not repeated:
clipping (`benchmarks/retro-gpu/win98-clipping.json`, historical Juke evidence).

The implemented bridge uses an owned compatible bitmap containing a small
binding tag, copied by SetBitmapBits. Real GDI delivered the exact tag bytes to
the display driver: tag evidence (`benchmarks/retro-gpu/win98-bitmap-token.json`, historical Juke evidence).
The tag is not authority by itself: the VxD checks the binding's client token,
OS address-space owner, generation and retained native image. It accepts no
user-supplied desktop clip list or bitmap/DMA address. The frontend holds a
private DC through each swap and uses an OS window property to invalidate
bindings on HWND destruction. The source bitmap contains no rendered image.

`dg-window-vxd.c` shares the GL channel mutex and sleeping completion path.
It retains a bounded native present, seeds the GPU primary when needed, draws
each GDI clip and explicitly discards the retained export after the GDI call.
CPU access uses ordered READBACK then RETURN. A fault or an illegal critical
access writes the same native FAULT_STOP doorbell as NT and halts the guest CPU
without returning to stale VRAM or polling. Native cursor takeover is required
before accepting a window binding, avoiding a routine cursor-driven frame
readback. Source build, host tests and the real public WGL window oracle pass;
window evidence (`benchmarks/retro-gpu/win98-window.json`, historical Juke evidence) records exact
pixels, CPU coherence, clipping, occlusion, destruction and resize. This is not
yet a supported-game or FPS claim.

`scripts/diagnostics/win98-probe.py` prepares an independent disk, injects the exact
source-built package offline, launches through WIN.INI and waits for a serial
terminal result. It disconnects networking before executing guest instructions,
dismisses initial logon once from a driver startup event, and closes its owned
processes on completion/failure. Evidence directories cannot be overwritten.

Current window bring-up failures and exact package hashes are retained in
the window ledger (`benchmarks/retro-gpu/win98-window-progress.json`, historical Juke evidence).
The first real native cursor run exposed a segmented-pointer bug: Win16 local
stack addresses use SS, so cursor helpers taking those addresses need explicit
far pointers. The build now rejects Watcom W112 truncation warnings. The fixed
package enables native cursor ownership and completes window binds/presents.
Retained-image cleanup now works without an active desktop through IPC version
2 detached DISCARD. Diskless native and actual Win98 empty-clip tests pass.
The scheduler guard rejects held critical claims or real VMM reentry; CF alone
is a VM priority boost and is no longer treated as a held critical section.
The v8 public pixel oracle passed once and its evidence is retained.

The accepted public window result above is on macOS. A transferred independent
Linux KVM fixture reached the shared PE4.0 serial controller in44.208 seconds,
but its first public probe stopped before creating its worker thread. The
probe passed a null thread-ID output pointer, which Win9x rejects with error87.
The corrected probe and normal driver now pass on both Mac and Linux, with
exact pixels and complete ownership/lifecycle checks. The normal driver emits
one bounded startup marker and avoids an unnecessary VxD entry while the CPU
owns the desktop. No per-frame debugport logging remains in that package.

Retail Half-Life requires two launcher details beyond its command line: the
existing registry EngineType must select OpenGL(2), and EngineGLDriver is loaded
from the application's `gldrv` directory. The shared benchmark controller now
copies its installed DreamGPU frontend to that private directory, sets and reads
back the exact typed settings, and fails before launch if either step fails.
The earlier completed software-renderer timedemo is explicitly excluded from
native GPU performance. The next game attempt carries bounded owned-process
module and window evidence, so an absent engine log does not require UI
navigation. See game bring-up (`benchmarks/retro-gpu/win98-halflife-progress.json`, historical Juke evidence).

The original retail Half-Life native OpenGL timedemo completed on Linux with
427 successful retained presents and379 benchmark frames. Its diagnostic-driver
6.171 FPS result is recorded in game evidence (`benchmarks/retro-gpu/win98-game.json`, historical Juke evidence);
it is not production performance acceptance. A production checkpoint launch
returned Windows loader error1157 before creating the game; a fixed dependency
loader oracle now identifies the failing module instead of retrying gameplay.

The bulk-readback candidate negotiates 64 KiB only when the native device
advertises `DG_CAP_GL_BULK_READBACK`; old native devices retain their 512-byte
limit. The VxD owns sixteen contiguous result pages and clears only the
requested result capacity. User output pinning includes the full 48-byte reply
header even when the buffer starts at an unaligned address. The actual-source
allocator, capability and page-bound tests pass. The extended public oracle
reads a 256 by 64 texture and checks every RGBA byte plus unaligned guard bytes;
the real Linux Win98 gate passes with `win98-bulk-v1`.
Bulk evidence (`benchmarks/retro-gpu/win98-bulk.json`, historical Juke evidence) pins the exact
package and output; this is a correctness result, not a game FPS claim.

## Diagnostic system ICD discovery

Cargo also builds a separate `diagnostics/icd/drivers/win98/dgpumini.drv`.
`prepare.rs` copies the already prepared VMDISP tree into `vmdisp9x-icd`,
applies the hash-checked `support/guest/win9x/patches/icd.json` transaction,
and adds `dg-icd16.h`. CMake keeps all Watcom scratch objects in that separate
tree. The diagnostic pair uses the exact production VxD; the production
`control.c` and driver are not patched by this transaction.

The diagnostic `Control(OPENGL_GETINFO)` returns version 2, driver version 1 and
ANSI name `DGPUICD`, matching the proposed Win98 registry value under
`HKLM\Software\Microsoft\Windows\CurrentVersion\OpenGLDrivers`.
Its ABI follows pinned VMDISP `control.c` at
`718b3d51a1532fe1ba2e133cf76186f1a609d35e`: two 32-bit fields and 262 ANSI bytes,
270 bytes total. Watcom compile-time checks verify that layout against the
donor struct and verify a 32-bit far output pointer with 16-bit near pointers.
The donor's 532-byte buffer comment does not describe this struct and must not
be confused with NT's wide-name response. Null and segment-crossing output
ranges reject before `_fmemcpy`; caller buffer capacity is a loader ABI
precondition because `Control` supplies no length.

`tests/guest/win9x/test_icd16.py` tests the actual writer under sanitizers,
including segment endpoints and untouched trailing bytes, and applies the
actual checked donor patch. The pinned Watcom diagnostic target builds. These
are source/build gates; this diagnostic adapter does not establish system
loader activation or complete OpenGL coverage, and does not register an ICD.
The small C helper is retained specifically for Watcom's 16-bit far-pointer ABI;
it contains no allocated resources or flat rendering policy.
