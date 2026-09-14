# Ownership and unsafe boundaries

Rust reduces the amount of code that must uphold manual memory invariants. It does
not make QEMU guest memory, platform graphics drivers, C++ or an FFI pointer safe by
itself. This document identifies the remaining contracts and the checks protecting
them. See [architecture.md](architecture.md) for the data flow and
[progress.md](progress.md) for executed runtime evidence.

## Guest memory is not a Rust allocation

The QEMU big lock does **not** establish exclusive ownership of guest RAM: TCG CPU
execution can release that lock while accessing mapped VRAM. Never form a Rust
`&mut [u8]` over guest VRAM merely because a device callback holds the lock.

The 2D engine receives an immutable command snapshot, the VRAM size and owned
progress/output fields. Safe Rust validates the complete batch and computes
bounded integer source/destination ranges. One native transfer callback per chunk
performs the RAM operation and dirty tracking using QEMU's existing memory semantics.
Rust receives no VRAM pointer. The callback must not reenter the engine or mutate
the borrowed command/progress storage.

This boundary preserves the existing scheduling and callback count; it does not
serialize all guest CPUs. Emulated-DMA concurrency remains a QEMU/native-memory
contract. Safe references to a **host-owned copied snapshot** are permitted once
its complete allocation, initialization and immutability have been established.

## Host command ABI

`dreamgpu-host` is a `no_std` Rust static library. Heap storage uses explicit,
fallible host allocation/free callbacks; Rust owns the resulting texture, context
state and read-cache allocations. FFI entry points return protocol error codes
and retain no command pointers or callback tables after return.
Host invariant failures abort rather than unwinding into C. `#[repr(C)]` records
and generated platform function-pointer types define the calling boundary.

Callers must provide valid allocations of the documented size, alignment where
required, and disjoint writable outputs. A null check cannot prove that a non-null
pointer names a live allocation. Checks of guest-controlled lengths happen before
a slice or index is constructed from those lengths.

Validation responsibilities are layered:

- QEMU captures and checks the outer batch and exact record lengths before dispatch.
- Rust validates function classes, argument shapes, texture limits, vertex/index
  payloads, reserved padding and query result sizes.
- Resource lookup verifies process ownership, context/drawable existence and limits.
- Native execution verifies platform capabilities and reports errors.

Do not remove the outer validation because the executor is Rust. Callbacks without
an argument-length parameter still depend on the validated immutable record contract.
C++ exceptions and Rust unwinding must never cross these interfaces.

Arithmetic checks must also hold in debug builds. Check row/stride relationships
or use checked arithmetic before computing a potentially overflowing end address;
rejecting a malformed record after an overflowing expression is too late.

## Registry callbacks and native GL

The render worker owns context/drawable creation and deletion. Output/completion
workers access publication fields through the existing synchronization. Rust uses
raw field accesses for this shared registry and does not retain a reference into
it across a platform callback that can inspect the registry.

Creation/deletion callbacks drain pending publication before resource slots are
reassigned. Drawable destruction uses its last **published** epoch/generation
cutoff; the global allocation epoch is not a substitute. Native image handles can
outlive the originating drawable, so image/context ownership and release completion
must be preserved independently.

Context sharing is scoped to the guest client. Failed allocation must leave the
registry slot available. Closing one client must release its resources without
invalidating another client's shared namespace or reusing stale IDs as live exports.

Platform function tables must contain the required entries for the current context.
Generated bindings establish ABI layout; they do not prove a context is current,
that a function is supported, or that an opaque pointer is still alive.

Client vertex arrays require special care. GL may retain the configured pointer in
context state even though a draw consumes client data before returning. The executor
saves/restores the client-array state before releasing its immutable payload and
restores affected current attributes. Every required function is checked before
installing pointers, so an unsupported function cannot cause an early return with
a dangling array pointer still configured. Full index and padding validation remains
a precondition of the execution entry point.

Compact array packets carry seven fixed-width descriptors and aligned, tightly
packed attribute sections. No guest address or stride crosses the transport.
The frontend captures directly into reserved packet storage; the kernel and Rust
host independently validate types, component counts, lengths and padding. Signed
GL 1.1 normalized color/normal inputs retain their original conversion into float
sections. Native GL consumes other supported component types directly. Display
lists retain owned immutable packet data, and pointer/current-state restoration
finishes before that data is released.

## Render resources and export transactions

The render worker exclusively owns `ContextState`, texture namespaces, internal
framebuffer attachments and the texture read cache. Context initialization allocates
all default objects before publishing references or accounting. Attribute entries
retain texture objects independently of names, so deletion followed by reuse of the
same numeric name cannot redirect a saved binding. A failed native attribute pop
preserves the saved entry. Teardown clears published state before allocator/cache
callbacks and releases each retained reference once. Native context refcounts and
completion-context selection still use the QEMU/platform adapter.

The host allocator need not zero memory. Rust explicitly initializes objects and
zero tiles, and copies complete immutable payloads into endian-staging buffers.
No slice is constructed over an uninitialized native query output. Readback writes
use raw pointers with the validated output capacity. The one weak texture read-cache
identity is invalidated by version changes or object destruction; its allocation
is released on failure, final tile, replacement or platform teardown. Pack-state
restoration occurs before an error escapes the read operation.

Internal drawable storage is initialized before use, including both color buffers
and depth/stencil. Allocation-only textures use GPU clears when attachable; legacy
formats and deleted-but-bound objects use a bounded zero tile of at most 64 KiB.
Never rebind a deleted texture's old name merely to initialize, read or export it.

Export owns temporary GL attachments and restores framebuffer, read-buffer,
scissor and attachment-binding state on every return path. IOSurface and EGL
binding/fence callbacks receive a synchronous stack descriptor and may update only
native image handles and context references. Rust retains no borrow of that mutable
C image/context across a callback. Pixel orientation changes with one GPU blit;
export does not introduce CPU readback. On macOS the completion worker uses a
zero-time fence query and a sleeping 100-microsecond wait, bounded to five seconds;
it consumes the fence on success or failure. Native OS image/descriptor lifetime
and published producer leases remain independent contracts below.

`NativeImage` owns its allocation and partially created OS handles. The Rust owner
initializes null handles and invalid descriptors before the platform creator runs;
any failure invokes destruction of the exact partial state. Destruction first clears
the live record, then gives the OS adapter a temporary owned snapshot. The adapter
must not retain that snapshot. Image destruction requires output/completion work to
be drained; this ownership rule does not permit reading or copying a concurrently
used image record.

Export slot transitions run under the existing engine mutex. Claiming a free slot
moves it to rendering; only a complete packet can move to pending. Publication marks
it published and sending before unlocking for native transport. A matching early
release records pending credit, and the send completion applies that credit after
the transport stops reading the image. Stale epoch/generation releases do nothing.
Failed sends and cancellation free credit without inventing a published cutoff.
Generation exhaustion fails instead of reusing a stale identity. Slot scans use raw
state fields rather than an exclusive borrow of a whole array whose published
packets can still be read by the output worker.

## CPU mappings and GPU leases

`FrameAllocation` and `GpuImage` are unsafe traits because their implementers provide
raw/native allocation descriptions. Their owners must keep allocations mapped,
initialized and stable for the promised lifetime. An allocation owner and a producer
read lease are different: retaining a cached mapping must not monopolize its slot.

`FrameLease` protects immutable CPU pixels until the last reader finishes. Native
imports retain the producer lease through every GPU submission that references it.
Completion callbacks release leases, not just Rust texture wrappers. Releasing an
image when commands are submitted instead of when they finish permits the producer
to overwrite pixels still in use.

Native handle import validates dimensions, format, offset/stride bounds and ownership:

- IOSurface imports own the transferred Mach right and retained surface. Producer
  work is complete before publication on this path.
- DMA-BUF imports own descriptors, match the negotiated physical GPU, observe a
  supplied ready fence or an established completion guarantee, and execute the
  required foreign ownership transitions.
- CPU mappings are checked against actual descriptor size and protocol layout.

Closing a descriptor does not by itself prove that the GPU has stopped accessing
its allocation. Kernel/native resource lifetime and producer credit both matter.

## Reliable queues, reset and cancellation

Desktop updates are ordered and bounded. They cannot use a lossy latest-frame
mailbox. Epoch/sequence validation prevents stale resource references and CPU
handoff frames from becoming current after reset or a newer desktop seed.

A readback reply is one-shot: success, failure or cancellation must complete the
waiting request exactly once. Completion callbacks must be nonblocking, including
during disconnect and shutdown. Transport shutdown wakes sleeping receive/release
workers; it must not depend on another guest frame or a visible window.

Resource diagnostics may temporarily clone leases. Keep them out of presentation
hot paths and drop snapshots before asserting that resources were released.

## Guest C++ and the remaining native code

C++23 guest code retains a restricted runtime, explicit failure returns, move-only
resource owners and legacy calling conventions. Modern syntax does not establish
memory safety. Keep ownership transfers visible, validate sizes/overflow, and run
host sanitizer/static-analysis tests against the actual maintained implementation.

Guest process death, driver unload and failed initialization require explicit cleanup;
ordinary scope destructors do not cover every lifecycle. Interrupt/kernel paths must
respect their allocation and execution constraints. Win98's 16-bit segmented ABI
remains a small trusted native adapter; a flat pointer is not a valid substitute
for a far pointer or register-call contract.

Upstream QEMU/Wine/OpenGLide and remaining CGL/EGL platform implementation are still
native trusted code. Their licenses and ownership rules survive a language rewrite.
Do not describe the complete stack as memory-safe until those boundaries support
that claim.

## Evidence and review requirements

The tests cover immutable leases across resize/drop, slot-credit release independent
of mapping caches, descriptor transfer, fragmented records, bounded shutdown,
process/resource cutoffs, exact cursor algebra, desktop ordering and native GPU
import/completion. The diskless QEMU suite tests the production device-to-SDK boundary;
OS/game fixtures test additional driver and compatibility behavior.

The GL validator replacement was compared once against the frozen prior C
implementation: 529,456 deterministic metadata, scalar, array/texture and query
checks matched. The original comparison artifacts and source hashes are recorded
under `target/validation-parity/`. A supplementary 4,097 function-boundary calls
and an unknown-function/null-pointer case verify the preserved no-read scalar
helper behavior; the outer validator still rejects unsupported functions.
Its checked-in focused tests cover boundary and
accepted cases; the comparison is migration evidence, not a second production path
or proof of all possible inputs.

When changing an unsafe boundary, document the allocation owner, concurrent readers,
completion condition, cancellation behavior and required caller validation. Test the
specific invariant affected. Do not repeat unrelated successful GPU/game suites as
a substitute for reviewing the memory contract.

## Device DMA snapshot transactions

`crates/dreamgpu-host/src/device.rs` owns cursor and GL submission snapshots with
fallible allocation callbacks. DMA must initialize the entire allocation before
reporting success. It must not retain the destination pointer. A failed DMA,
record validator, query destination check or queue submission releases the snapshot
exactly once. Only a successful queue callback transfers ownership to the worker;
the submitting Rust transaction never accesses that allocation again. Cursor
shape validation finishes before replacing the retained cursor pixels. Cursor
movement does not allocate or copy a shape.

The outer GL validator callback must establish complete bounded records and forward
progress, including the fixed query payload. Later Rust record traversal and query
metadata rely on this validated immutable snapshot. Diagnostics only read record
pointers during their callback. Request and callback records are copied into the
transaction before callbacks; no exclusive borrow of the QEMU device crosses a
callback. Query destinations remain integer guest addresses; QEMU checks writable
RAM and performs DMA when completing the request.

The 2D capture path DMA-writes directly into the existing private command snapshot,
then invokes the same Rust batch validator used by execution. Saved-state checking
validates cursor dimensions/pixels and 2D command/progress bounds before indexing.
The VMState field layout, register behavior, completion interrupts and scheduling
quanta remain unchanged. QOM registration, VMState declarations, RCU RAM translation,
PCI DMA, BH/IRQ operations and actual VRAM memory operations remain native QEMU
adapters. They do not expose guest RAM as Rust references.

`cargo test -p dreamgpu-host device::` exercises snapshot allocation/read/validation/
queue failures, ownership transfer, invalid coherence recognition, query result
leases, transactional cursor updates and restored progress bounds. Callback test
fixtures provide host allocation and I/O; production Rust supplies the policy.

The worker output ring stores at most 96 GPU frames and 64 desktop records inline.
Its pending credits include the record currently being sent, so popping work cannot
bypass consumer backpressure. QEMU holds the engine mutex for every ring and lease
transition; sends operate on a copied stack record after unlocking. CPU export
mappings are writable only after their previous consumer lease has been released.
Rust clears mapping ownership before native deallocation and initializes storage
before publication, using raw pointers rather than references to shared bytes.
Readback replies transfer an incoming descriptor only when epoch, sequence and token
match the outstanding request. Cancellation keeps that identity to recognize a late
reply; the receiver closes ignored/rejected descriptors. Finishing a request transfers
its descriptor exactly once to the native mmap/close adapter, even on failure.

The actual Rust worker tests cover FIFO wrap, queued plus in-flight credit limits,
stale CPU releases, shape changes and allocation failure, exact return packets,
reply duplication/cancellation and bounded mapped result layouts.

Accepted command batches now have a Rust owner through submission, execution, reset
and completion. Failed admission leaves the immutable DMA allocation with the
caller; successful admission transfers it. Taking work removes it from the pending
slot before unlocking. Reset disposes only pending work; an executing batch checks
cancellation between validated records. Native query callbacks write to bounded
inline or fallibly allocated output without borrowing the entire batch. Successful
completion moves the bulk allocation to a bounded 16-entry ring; failed completion,
ring eviction and final shutdown free it exactly once. Polling transfers ownership
to the QEMU DMA adapter. Reset completions use the same bound. Both C and Rust assert
the native completion/batch layouts; these structures are internal host ABI, not
guest wire records. Native trace and error-report callbacks cannot retain the
immutable record pointer or reenter batch execution.

The remaining C boundary is explicit: QEMU QOM/MMIO/VMState registration, PCI DMA
and RCU RAM operations, BH/IRQ scheduling, mutex/condition/thread operations, Unix
socket ancillary descriptor transfer, Mach ports, memfd/mmap, CGL/EGL/GBM/IOSurface
creation and destruction, and generated native GL function table construction.
Render-thread adapters coordinate native current-context selection and flushing,
publication drains before OS handle destruction, and error reporting. The Rust
registry owns client/context/drawable admission, lookup, epoch and command routing;
Rust owns texture/default-object references, storage accounting, GL execution,
framebuffer/export operations, publication state, desktop transactions, batches,
result ownership and bounded queues. The C platform context wrapper still provides
its native reference count and monotonic serial used by cross-context GPU fences;
these are native handle lifetime mechanics, not a second guest resource registry.

Reset I/O uses a generation/CPU-epoch/frame ticket copied while holding the engine
mutex. The native worker sends that immutable snapshot after unlocking. On return,
Rust clears and acknowledges the reset only if the entire ticket still matches the
pending request. A newer reset stays pending, and the worker processes it before
claiming any new batch. The state-transition test explicitly interleaves a reset
between snapshot and acknowledgement, including reused generations with changed
CPU metadata, and verifies exactly one acknowledgement of the latest request.

Bitmap and DrawPixels use a render-worker-owned immutable image transaction per
context. Eight descriptor words identify shape, format, type, byte count, exact
next offset, flags and a non-reusable transaction ID. Bitmap FIRST carries four
raw float parameters outside image progress. Guest packing completes all address
and length checks before the first chunk; host admission independently bounds
shape arithmetic and reserves at most 64 MiB, with a separate 64 MiB aggregate
CPU staging limit across contexts. Native GL receives the fully assembled image
once, with temporary canonical unpack state restored afterward. Guest pixel
transfer, raster position, clipping and zoom remain native GL state.

No native draw or Bitmap raster movement occurs for an incomplete or malformed
worker transaction. Ownership is detached before the single native call, so an
error or lost acknowledgement cannot replay it. This does not promise rollback
of arbitrary native GL errors after invocation. Matching malformed continuations,
explicit abort, incompatible same-context commands, drawable changes/destruction,
context destruction and reset release retained allocation credit once. Wrong IDs
and duplicate FIRST do not release another transaction. Stateless rejection on
the BQL side does not mutate render-worker state: its bounded pending allocation
remains owned until worker cancellation or teardown. Flush, Finish, unchanged
MakeCurrent and transport batch boundaries can occur between chunks. Other
contexts may execute independently, sharing only the aggregate staging budget.

The C bridge checks the render-owned active bit before constructing a memory
callback record on ordinary scalar/query paths. All transaction mutation remains
in Rust. The callback record and commit snapshot are local immutable values;
allocator callbacks cannot reenter the owner or retain their pointers. Tests
exercise exact budget saturation across two contexts, allocation/API failures,
malformed offsets/descriptors/flags, abort and repeated teardown, native-error
non-replay, and actual GPU pixels before/after multi-packet commit. Native tests
also cover all scalar component widths, fractional negative zoom, zero-area
Bitmap movement, invalid raster positions, transfer, and depth/stencil attachment
behavior for DrawPixels and CopyPixels.

For DrawPixels with BITMAP index data, the allocation is charged at its expanded
width-times-height size. Packed rows are received unchanged, then unpacked backward
in that allocation into exact unsigned-byte indices 0 and 1 at commit. This avoids
a reproduced Mesa native bitmap-index unpack hang while retaining the same index
maps, transfer, raster and fragment operations in the single native DrawPixels
call. It does not render pixels on the CPU. Ordinary Bitmap retains packed native
input. Expanded images over 64 MiB are rejected before allocation and guest pointer
access; zero-width images do not iterate over their potentially large row count.

Texture residency queries carry three logical texture names and return a bounded
five-byte valid/aggregate/per-object result. The render worker resolves all names
inside the owning namespace before invoking native AreTexturesResident; native
object names never enter guest output. There is no persistent query cache or
extra ownership state. The guest validates its full input and stages all replies
before publishing a false-result residence array; an all-resident result leaves
the caller's array unchanged. Priority requests contain bounded name/float pairs;
zero and non-object names are ignored as specified, and valid names are translated
before the real native priority call without rebinding texture state.

CopyTexImage1D and CopyTexSubImage1D use the existing texture version, fence and
allocation owner. A 1D definition conservatively accounts eight bytes per texel
for the full GL1.1 format set through RGBA16; existing restricted 2D definitions
retain their prior accounting. Width includes border texels. Subcopy validation
uses the actual native level border and the owned level width, allowing the signed
border offsets while rejecting out-of-range writes before the copy. Allocation
metadata changes only after the native definition succeeds. Exact GPU tests cover
both borders, signed framebuffer source coordinates, RGBA16 storage, unchanged
1D/2D bindings, real priority clamping, and bounded residency replies.

A native provider may discard legacy 1D border texels despite a successful copy.
After a 1D definition, the resource owner queries actual native width and border
before publishing its size and accounting; it never substitutes requested values
for missing native storage. Mac's exact border oracle passes. Linux Mesa26.1.8
currently reports width4/border0 for width6/border1, reproduced by a standalone
EGL program without DreamGPU. That border conformance failure remains open;
borderless copies, residency and priority have separate acceptance gates.

## Virtual display timing

The guest's selected 60/75/85/100/120 Hz mode drives a coherent virtual scanout
sample, separate from physical monitor pacing and GPU submission throughput.
Timing queries and begin/end waits use fixed 16-byte requests and 32-byte replies.
The NT display escape routes to its miniport; Windows 98 uses the existing VxD
DeviceIoControl boundary, avoiding a sleeping Win16 display escape.

A waiter owns one sequence and one one-shot virtual-clock timer. Completion raises
an interrupt and wakes a kernel event/semaphore; it does not poll or hold the GL
render lock. Cancellation, mode change and teardown resolve the owned request.
An idle display has no continuously armed vblank timer. Consumer monitor hints
only schedule desktop refresh deadlines, with fractional periods retained and
missed deadlines skipped rather than replayed in a busy loop.

### RGB565 primary storage

NT 16-bit modes use actual RGB565 VRAM, masks, stride and mapped extent. The
matched version-7 miniport/display request carries the primary depth; native
validation checks it against the active VBE mode and desktop epoch. Legacy
32-bit commands keep format word zero. The GPU compositor and exported frames
remain 32-bit regardless of primary depth.

`dreamgpu_primary16_transfer` borrows disjoint native-owned buffers only for the
call. QEMU validates VRAM row extents before each chunk, converts between two-byte
RGB565 and four-byte compositor pixels, and retains the existing bounded work
quantum. No pointers survive the call. The unchanged 32-bit branch uses memcpy.
The native mode-cycle test checks exact GPU/CPU pixels, conversion across a work
quantum inside a row, guard bytes, and return to a fresh CPU-owned frame.
