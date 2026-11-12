/* SPDX-License-Identifier: GPL-2.0-or-later
 * Freestanding C++23 GL/window policy. All OS callbacks use a fixed cdecl
 * table; no callback unwinds. Entry serializers and interrupt exclusion retain
 * the original ordering. User buffers enter only through checked pinned aliases.
 */
#include <stdint.h>
#include <stddef.h>
typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef uint8_t BYTE;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#ifndef __cdecl
#define __cdecl DREAMGPU_CDECL
#endif
#include "gpu.h"
#include "gl.h"
#include "channel32.h"
#include "dg-owner.h"
#include "dg-refusal9.h"
#include "dg-function-cache.h"
#include "packet32.h"
#include "dg-gl-queue.h"
#include "dg-gl-bulk.h"
#include "../include/ownership.hpp"
static const DG9_CHANNEL_SERVICES *api;
extern "C" void DREAMGPU_CDECL DreamGpuChannelBind(const DG9_CHANNEL_SERVICES *services) {
    api = services;
}
static void *copy_bytes(void *out, const void *input, size_t bytes) {
    auto *dst = static_cast<BYTE *>(out);
    const auto *src = static_cast<const BYTE *>(input);
    for (size_t i = 0; i < bytes; ++i)
        dst[i] = src[i];
    return out;
}
static void *fill_bytes(void *out, int value, size_t bytes) {
    auto *dst = static_cast<BYTE *>(out);
    for (size_t i = 0; i < bytes; ++i)
        dst[i] = static_cast<BYTE>(value);
    return out;
}
#define memcpy copy_bytes
#define memset fill_bytes
#define DgVxdRegisters() api->Registers()
/* Nonzero only when the calling thread itself owns the VMM critical section
 * or the VMM is reentered (dg-critical9.h): it must not sleep on dg_gl_mutex
 * or a completion. Another thread's claim is plain contention and blocks. */
#define Dg9GlCritical() api->Critical()
#define Dg9GlDisableInterrupts() api->DisableInterrupts()
#define Dg9GlRestoreInterrupts(flags) api->RestoreInterrupts(flags)
#define _GetCurrentContext() api->CurrentContext()
#define Create_Semaphore(count) api->CreateSemaphore(count)
#define Destroy_Semaphore(value) api->DestroySemaphore(value)
#define Wait_Semaphore(value, flags) api->WaitSemaphore(value)
#define Signal_Semaphore(value) api->SignalSemaphore(value)
#define Dg9CursorTakeoverSafe() api->CursorSafe()
#define Dg9Primary(w, h, s, o, b) api->Primary(w, h, s, o, b)
#define Dg9PrimaryGpuOwned(owned) api->PrimaryOwned(owned)
#define DgIdentityText(text) api->TraceText(text)
#define DgIdentityField(text, value) api->TraceField(text, value)
#define DgIdentityChar(value) api->TraceChar(value)
static ULONG Dg9GlRead(void *, ULONG reg) {
    return api->Read(reg);
}
static void Dg9GlWrite(ULONG reg, ULONG value) {
    api->Write(reg, value);
}
static BOOL Dg9LockUser(DWORD address, DWORD bytes, BOOL write, DG9_USER_LOCK *lock) {
    return DreamGpuLockUser(api->Memory, NULL, address, bytes, write, lock);
}
static void Dg9UnlockUser(DG9_USER_LOCK *lock) {
    DreamGpuUnlockUser(api->Memory, NULL, lock);
}
struct pin_deleter {
    void operator()(DG9_USER_LOCK *lock) const noexcept {
        Dg9UnlockUser(lock);
    }
};
using pin_owner = dreamgpu::unique_owner<DG9_USER_LOCK, pin_deleter>;
struct semaphore_deleter {
    void operator()(DWORD *handle) const noexcept {
        if (*handle)
            api->SignalSemaphore(*handle);
        *handle = 0;
    }
};
using semaphore_owner = dreamgpu::unique_owner<DWORD, semaphore_deleter>;
struct dma_deleter {
    void operator()(BYTE *pages) const noexcept {
        api->Free(pages);
    }
};
using dma_owner = dreamgpu::unique_owner<BYTE, dma_deleter>;
struct page_deleter {
    void operator()(BYTE *pages) const noexcept {
        api->Memory->Free(nullptr, pages);
    }
};
using page_owner = dreamgpu::unique_owner<BYTE, page_deleter>;
static void Dg9WindowRetire(DWORD client);
static DG9_OWNER_TABLE dg_gl_owners;
static BOOL dg_gl_initialized, dg_gl_faulted;
static DWORD dg_gl_mutex, dg_gl_physical, dg_gl_result_physical;
static ULONG *dg_gl_commands;
static BYTE *dg_gl_result;
/* Bulk results (dg-gl-bulk.h): DG_GL_BULK_PAGES fixed system pages, not
 * contiguous, and one contiguous page listing their frames for the device.
 * Driver-owned for the VxD's life like dg_gl_result; never a caller's pages. */
static BYTE *dg_gl_bulk;
static ULONG *dg_gl_bulk_list;
static DWORD dg_gl_bulk_physical;
static DG_GL_LIMITS dg_gl_limits;
static DG9_FUNCTION_CACHE dg_gl_function_cache;
/* A client lease releases under the same IRQ exclusion as acquisition.
 * reset() before retirement drain preserves result/cleanup ordering. */
class client_reference final {
    DWORD token_{};

  public:
    client_reference() noexcept = default;
    client_reference(const client_reference &) = delete;
    client_reference &operator=(const client_reference &) = delete;
    ~client_reference() noexcept {
        reset();
    }
    void acquired(DWORD token) noexcept {
        token_ = token;
    }
    void reset() noexcept {
        if (!token_)
            return;
        const auto flags = api->DisableInterrupts();
        Dg9OwnerRelease(&dg_gl_owners, token_);
        api->RestoreInterrupts(flags);
        token_ = 0;
    }
};
static ULONG Dg9GlWordsNative(void *, ULONG function) {
    Dg9GlWrite(DG_GL_REG_QUERY_FUNCTION, function);
    return Dg9GlRead(NULL, DG_GL_REG_FUNCTION_WORDS);
}
static ULONG __cdecl Dg9GlWords(void *, ULONG function) {
    return Dg9FunctionCacheWords(&dg_gl_function_cache, function, Dg9GlWordsNative, NULL);
}
/* Queued transport (gl.h): one RAM page and up to eight 128 KiB packet
 * buffers. dg_gl_build is the buffer the next submission is written in: a
 * free queue buffer, or dg_gl_commands for the register transport. */
static DG_GL_QUEUE dg_gl_queue;
static BOOL dg_gl_queue_supported, dg_gl_build_queued;
static ULONG *dg_gl_build;
static ULONG dg_gl_build_status, dg_gl_result_bytes, dg_gl_result_type, dg_gl_queue_sequence;
/* dg_gl_caps2 is DG_GL_REG_CAPS2 (gl.h): read with CAPS while the host offers
 * the GL transport, zero otherwise; hosts without it read zero there. */
static ULONG dg_gl_caps, dg_gl_caps2, dg_gl_caps_version, dg_gl_caps_generation;
static BOOL dg_gl_caps_valid;
/* ResultCapacity is the RESULT_CAPACITY word (DG_GL_RESULT_PAGES for the
 * bulk list) and ResultPhysical its destination; both zero without a query. */
struct DG9_SUBMIT {
    ULONG Token, Stream, Async, ResultCapacity, ResultPhysical;
};
static ULONG Dg9QueueRead(void *, ULONG reg) {
    return api->Read(reg);
}
static void Dg9QueueWrite(void *, ULONG reg, ULONG value) {
    api->Write(reg, value);
}
static void Dg9QueueBarrier(void *) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
static int Dg9QueuePrepare(void *) {
    return api->QueuePrepare();
}
static int Dg9QueueWait(void *) {
    return api->QueueWait();
}
static void Dg9QueueFinish(void *) {
    api->QueueFinish();
}
static DG_GL_QUEUE_OPS Dg9QueueOps(void) {
    return DG_GL_QUEUE_OPS{nullptr,         Dg9QueueRead, Dg9QueueWrite, Dg9QueueBarrier,
                           Dg9QueuePrepare, Dg9QueueWait, Dg9QueueFinish};
}
/* A current queue page names the engine generation without an MMIO read;
 * engine reset clears it before any other register changes. */
static ULONG Dg9Generation(void) {
    return DgGlQueueCurrent(&dg_gl_queue) ? dg_gl_queue.Generation
                                          : Dg9GlRead(NULL, DG_REG_GENERATION);
}
/* CAPS and GL VERSION are invariant for a realized device; reread them after
 * every OPEN (a restored desktop may run on another host) or reset. */
static ULONG Dg9Caps(ULONG generation, BOOL force) {
    if (force || !dg_gl_caps_valid || dg_gl_caps_generation != generation) {
        dg_gl_caps = Dg9GlRead(NULL, DG_REG_CAPS);
        dg_gl_caps_version = Dg9GlRead(NULL, DG_GL_REG_VERSION);
        dg_gl_caps2 = (dg_gl_caps & DG_CAP_GL_TRANSPORT) ? Dg9GlRead(NULL, DG_GL_REG_CAPS2) : 0;
        dg_gl_caps_generation = generation;
        dg_gl_caps_valid = TRUE;
    }
    return dg_gl_caps;
}
static void Dg9QueueEnsure(void) {
    if (!dg_gl_queue_supported || dg_gl_build || DgGlQueueCurrent(&dg_gl_queue))
        return;
    const auto ops = Dg9QueueOps();
    DgGlQueueReap(&dg_gl_queue);
    if (!(Dg9GlRead(NULL, DG_GL_REG_STATUS) & DG_STATUS_BUSY))
        DgGlQueueEnable(&dg_gl_queue, &ops, Dg9GlRead(NULL, DG_REG_GENERATION));
}
/* Win9x requests are pinned in place (dg-memory.h), never copied by the OS:
 * a request and its packet must fit DG9_MEMORY_MAX_BYTES. The queued transport
 * therefore keeps the register transport's 64 KiB packets here; NT negotiates
 * DG_ESCAPE_QUEUE_MAX_BYTES because its escape buffers are copied by GDI. */
#define DG9_GL_PACKET_BYTES DG_ESCAPE_MAX_BYTES
static_assert(sizeof(DG_ESCAPE_REQUEST) + DG9_GL_PACKET_BYTES <= DG9_MEMORY_MAX_BYTES,
              "every negotiated Win9x packet must be pinnable");
static_assert(DG9_GL_PACKET_BYTES <= DG_ESCAPE_QUEUE_MAX_BYTES);
static ULONG Dg9LocalBytes(void) {
    return DG9_GL_PACKET_BYTES;
}
static ULONG *Dg9Build(void) {
    if (dg_gl_build)
        return dg_gl_build;
    Dg9QueueEnsure();
    dg_gl_build = dg_gl_commands;
    dg_gl_build_queued = FALSE;
    dg_gl_build_status = DG_ESCAPE_OK;
    if (DgGlQueueCurrent(&dg_gl_queue)) {
        const auto ops = Dg9QueueOps();
        int result;
        void *buffer = DgGlQueueBuffer(&dg_gl_queue, &ops, &result);
        if (buffer) {
            dg_gl_build = static_cast<ULONG *>(buffer);
            dg_gl_build_queued = TRUE;
        } else if (!result) {
            dg_gl_faulted = TRUE;
            dg_gl_build_status = DG_ESCAPE_TIMEOUT;
        } else {
            dg_gl_build_status = DG_ESCAPE_HOST; /* engine reset discarded work */
        }
    }
    return dg_gl_build;
}
static ULONG Dg9GlSubmit(ULONG bytes, DG_ESCAPE_REPLY *reply, const DG9_SUBMIT *submit = nullptr) {
    ULONG *commands = Dg9Build();
    const BOOL queued = dg_gl_build_queued;
    dg_gl_build = NULL;
    dg_gl_result_bytes = dg_gl_result_type = 0;
    if (dg_gl_faulted)
        return DG_ESCAPE_STOPPED;
    if (dg_gl_build_status != DG_ESCAPE_OK) {
        reply->DeviceError = DG_GL_ERROR_GENERATION;
        return dg_gl_build_status;
    }
    if (!queued) {
        const auto status = api->Submit(dg_gl_physical, bytes, reply);
        if (status == DG_ESCAPE_TIMEOUT)
            dg_gl_faulted = TRUE;
        if (status == DG_ESCAPE_OK && submit && submit->ResultCapacity) {
            dg_gl_result_bytes = Dg9GlRead(NULL, DG_GL_REG_RESULT_BYTES);
            dg_gl_result_type = Dg9GlRead(NULL, DG_GL_REG_RESULT_TYPE);
        }
        return status;
    }
    if (bytes > dg_gl_limits.MaxBytes)
        return DG_ESCAPE_UNSUPPORTED;
    const auto ops = Dg9QueueOps();
    ULONG sequence = ++dg_gl_queue_sequence;
    if (!sequence)
        sequence = ++dg_gl_queue_sequence;
    const ULONG index = DgGlQueuePublish(
        &dg_gl_queue, &ops, bytes, sequence, commands[7], submit ? submit->Token : 0,
        submit ? submit->Stream : 0, submit && submit->ResultCapacity ? submit->ResultPhysical : 0,
        submit ? submit->ResultCapacity : 0, submit && submit->Async);
    if (submit && submit->Async)
        return DG_ESCAPE_OK;
    DG_GL_QUEUE_RESULT outcome = {};
    const int waited = DgGlQueueWaitEntry(&dg_gl_queue, &ops, index, &outcome);
    if (!waited) {
        dg_gl_faulted = TRUE;
        return DG_ESCAPE_TIMEOUT;
    }
    if (waited < 0) {
        reply->DeviceError = DG_GL_ERROR_GENERATION;
        return DG_ESCAPE_HOST;
    }
    reply->DeviceError = outcome.Error;
    reply->CompletedSequence = sequence;
    dg_gl_result_bytes = outcome.ResultBytes;
    dg_gl_result_type = outcome.ResultType;
    return outcome.Status & DG_STATUS_ERROR ? DG_ESCAPE_HOST : DG_ESCAPE_OK;
}
static void Dg9GlDrain(DG_ESCAPE_REPLY *reply) {
    DWORD token, flags;
    DG_ESCAPE_REPLY cleanup = *reply;
    for (;;) {
        flags = Dg9GlDisableInterrupts();
        token = Dg9OwnerTakeRetired(&dg_gl_owners);
        Dg9GlRestoreInterrupts(flags);
        if (!token)
            break;
        Dg9WindowRetire(token);
        ULONG *commands = Dg9Build();
        memset(commands, 0, DG_GL_HEADER_BYTES);
        commands[0] = DG_GL_CLOSE_CLIENT;
        commands[1] = DG_GL_HEADER_BYTES;
        commands[2] = token;
        commands[7] = reply->Generation;
        /* A retired client's cleanup completion is not this caller's result
         * identity, including when the caller just completed a typed query. */
        if (Dg9GlSubmit(DG_GL_HEADER_BYTES, &cleanup) != DG_ESCAPE_OK)
            break;
        /* Synchronous: the token's earlier entries are reaped; drop its
         * unreported failures with it. */
        DgGlQueueDropClient(&dg_gl_queue, token);
        flags = Dg9GlDisableInterrupts();
        Dg9OwnerFinishRetired(&dg_gl_owners, token);
        Dg9GlRestoreInterrupts(flags);
    }
}
static BOOL Dg9GlInitialize(void) {
    if (dg_gl_initialized)
        return !dg_gl_faulted;
    dma_owner commands(static_cast<BYTE *>(api->Allocate(16, &dg_gl_physical)));
    dma_owner results(static_cast<BYTE *>(
        api->Allocate((DG_ESCAPE_MAX_RESULT_BYTES + 4095) >> 12, &dg_gl_result_physical)));
    if (!commands || !results || !dg_gl_physical || !dg_gl_result_physical ||
        (dg_gl_physical & 4095) || (dg_gl_result_physical & 4095))
        return FALSE;
    dg_gl_commands = reinterpret_cast<ULONG *>(commands.release());
    dg_gl_result = results.release();
    /* Queued transport memory, only when the host offers it. Contiguous
     * pages may be scarce on Win9x: fewer than two buffers keeps registers. */
    const auto ops = Dg9QueueOps();
    if (DgGlQueueSupported(&ops)) {
        DWORD physical = 0;
        void *page = api->Allocate(1, &physical);
        if (page && physical && !(physical & 4095)) {
            dg_gl_queue.Page = static_cast<volatile ULONG *>(page);
            dg_gl_queue.PagePhysical = physical;
            while (dg_gl_queue.BufferCount < DG_GL_QUEUE_MAX_BUFFERS) {
                void *buffer = api->Allocate(DG9_GL_PACKET_BYTES >> 12, &physical);
                if (!buffer)
                    break;
                if (!physical || (physical & 4095)) {
                    api->Free(buffer);
                    break;
                }
                dg_gl_queue.Buffers[dg_gl_queue.BufferCount].Memory = buffer;
                dg_gl_queue.Buffers[dg_gl_queue.BufferCount].Physical = physical;
                ++dg_gl_queue.BufferCount;
            }
            dg_gl_queue.BufferBytes = DG9_GL_PACKET_BYTES;
        } else if (page) {
            api->Free(page);
        }
        dg_gl_queue_supported = dg_gl_queue.Page && dg_gl_queue.BufferCount >= 2;
    }
    Dg9OwnerInitialize(&dg_gl_owners);
    dg_gl_initialized = TRUE;
    return TRUE;
}

static void Dg9BulkRelease(void) {
    if (dg_gl_bulk)
        api->Memory->Free(nullptr, dg_gl_bulk);
    if (dg_gl_bulk_list)
        api->Free(dg_gl_bulk_list);
    dg_gl_bulk = nullptr;
    dg_gl_bulk_list = nullptr;
    dg_gl_bulk_physical = 0;
}
/* The bulk buffer, once, when the host offers bulk reads. Its pages are fixed
 * for the VxD's life, so the frames read here stay valid; any failure leaves
 * bulk off (MaxResultBytes stays at the contiguous limit). */
static void Dg9BulkEnsure(void) {
    const DG9_MEMORY_SERVICES *memory = api->Memory;
    if (dg_gl_bulk || !(dg_gl_caps2 & DG_CAP2_GL_BULK_READ) || !memory || !memory->Allocate ||
        !memory->Free || !memory->CopyPtes)
        return;
    DWORD physical = 0;
    dma_owner list(static_cast<BYTE *>(api->Allocate(1, &physical)));
    if (!list || !physical || (physical & 4095))
        return;
    page_owner pages(static_cast<BYTE *>(memory->Allocate(nullptr, DG_GL_BULK_PAGES)));
    const auto linear = reinterpret_cast<uintptr_t>(pages.get());
    if (!pages || (linear & 4095))
        return;
    /* The page table entries land in the list page, then become frames. */
    auto *frames = reinterpret_cast<ULONG *>(list.get());
    if (!memory->CopyPtes(nullptr, static_cast<DWORD>(linear >> 12), DG_GL_BULK_PAGES, frames))
        return;
    for (ULONG i = 0; i < DG_GL_BULK_PAGES; ++i) {
        const ULONG pte = frames[i];
        if (!(pte & 1) || !DgGlBulkSetFrame(frames, i, pte & ~4095UL))
            return;
    }
    dg_gl_bulk = pages.release();
    dg_gl_bulk_list = reinterpret_cast<ULONG *>(list.release());
    dg_gl_bulk_physical = physical;
}
/* Copy a bulk result's tail into the caller's output after its pinned first
 * window, one pinned window at a time (DG9_MEMORY_MAX_BYTES bounds a pin). */
static BOOL Dg9CopyOut(DWORD address, const BYTE *data, DWORD bytes) {
    for (DWORD done = 0; done < bytes;) {
        const DWORD count = bytes - done < 65536 ? bytes - done : 65536;
        DG9_USER_LOCK window{};
        if (!Dg9LockUser(address + done, count, TRUE, &window))
            return FALSE;
        pin_owner window_pin(&window);
        memcpy(window.Address, data + done, count);
        done += count;
    }
    return TRUE;
}

extern "C" int DREAMGPU_CDECL DreamGpuGlControl(const DG9_CHANNEL_CONTROL *params, DWORD *result) {
    DG9_USER_LOCK input{}, output{}, returned{};
    pin_owner input_pin(&input), output_pin(&output), returned_pin(&returned);
    DWORD held_mutex = 0;
    semaphore_owner mutex_lease(&held_mutex);
    client_reference client_lease;
    DG_ESCAPE_REQUEST request;
    DG_ESCAPE_REPLY reply;
    DG9_OWNER_KEY owner;
    DG9_HANDLE_KEY handle;
    ULONG status = DG_ESCAPE_INVALID, token = 0, query = 0;
    DWORD flags, pinned = 0;
    BOOL bulk = FALSE;
    BOOL acquired;
    if (!DgVxdRegisters())
        return FALSE;
    if (params->CloseHandle && dg_gl_initialized) {
        handle.Tag = params->tagProcess;
        handle.Handle = params->hDevice;
        /* Retirement is nonblocking even if the OS closes a handle in an
         * exceptional critical context. Pending requests retain references.
         * The next legal entry drains retired objects before accepting work. */
        flags = Dg9GlDisableInterrupts();
        Dg9OwnerRetireHandle(&dg_gl_owners, handle);
        Dg9GlRestoreInterrupts(flags);
        if (!Dg9GlCritical() && dg_gl_mutex) {
            Wait_Semaphore(dg_gl_mutex, 0);
            memset(&reply, 0, sizeof(reply));
            Dg9QueueEnsure();
            reply.Generation = Dg9Generation();
            Dg9GlDrain(&reply);
            Signal_Semaphore(dg_gl_mutex);
        }
        return FALSE; /* preserve the mini-VDD's ordinary handle accounting */
    }
    if (params->dwIoControlCode != DG_ESCAPE)
        return FALSE;
    *result = DG9_ERROR_INVALID;
    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    memset(&returned, 0, sizeof(returned));
    memset(&reply, 0, sizeof(reply));
    if (!DgVxdRegisters() || params->lpOverlapped || params->cbInBuffer < sizeof(request) ||
        params->cbOutBuffer < sizeof(reply) ||
        params->cbOutBuffer > sizeof(reply) + DG_ESCAPE_BULK_RESULT_BYTES || !params->tagProcess ||
        !params->hDevice)
        return TRUE;
    /* Retryable refusal (dg-refusal9.h): this thread owns the VMM critical
     * section or the VMM is reentered, so it may not sleep on the GL mutex or
     * a completion. Nothing is pinned, locked or read yet: the request did
     * not execute, and the frontend resends it. Another thread's claim does
     * not refuse (dg-critical9.h). */
    if (Dg9GlCritical()) {
        *result = DG9_ERROR_BUSY;
        return TRUE;
    }
    /* A bulk reply is longer than one pin may be: pin the window holding the
     * reply header now and the rest only to copy a result into it. */
    pinned =
        params->cbOutBuffer < DG9_MEMORY_MAX_BYTES ? params->cbOutBuffer : DG9_MEMORY_MAX_BYTES;
    if (!Dg9LockUser(params->lpInBuffer, params->cbInBuffer, FALSE, &input) ||
        !Dg9LockUser(params->lpOutBuffer, pinned, TRUE, &output) ||
        !Dg9LockUser(params->lpcbBytesReturned, sizeof(DWORD), TRUE, &returned))
        goto done;
    memcpy(&request, input.Address, sizeof(request));
    if (request.Version != DG_ESCAPE_VERSION || (request.Flags & ~DG_ESCAPE_SUBMIT_ASYNC) ||
        request.Bytes > DG9_GL_PACKET_BYTES ||
        request.Bytes != params->cbInBuffer - sizeof(request) ||
        request.ResultCapacity > DG_ESCAPE_BULK_RESULT_BYTES ||
        request.ResultCapacity > params->cbOutBuffer - sizeof(reply) ||
        ((request.Flags & DG_ESCAPE_SUBMIT_ASYNC) && request.ResultCapacity) ||
        (request.Operation != DG_ESCAPE_SUBMIT &&
         (request.Bytes || request.ResultCapacity || request.Flags || request.Stream)))
        goto done;
    if (!dg_gl_mutex) {
        DWORD candidate = Create_Semaphore(1);
        if (!candidate)
            goto done;
        flags = Dg9GlDisableInterrupts();
        if (!dg_gl_mutex) {
            dg_gl_mutex = candidate;
            candidate = 0;
        }
        Dg9GlRestoreInterrupts(flags);
        if (candidate)
            Destroy_Semaphore(candidate);
    }
    Wait_Semaphore(dg_gl_mutex, 0);
    held_mutex = dg_gl_mutex;
    reply.Version = DG_ESCAPE_VERSION;
    reply.FunctionWords = (ULONG)-1;
    Dg9QueueEnsure();
    reply.Generation = Dg9Generation();
    reply.MaxResultBytes = DgGlResultLimit(
        Dg9Caps(reply.Generation, request.Operation == DG_ESCAPE_OPEN), dg_gl_caps2, !!dg_gl_bulk);
    if (dg_gl_caps_version != DG_GL_VERSION || !(dg_gl_caps & DG_CAP_GL_TRANSPORT) ||
        !DgGlLimitsRefreshLocal(&dg_gl_limits, reply.Generation,
                                request.Operation == DG_ESCAPE_OPEN, Dg9LocalBytes(), Dg9GlRead,
                                NULL)) {
        status = DG_ESCAPE_UNSUPPORTED;
        goto unlock;
    }
    Dg9FunctionCacheRefresh(&dg_gl_function_cache, reply.Generation,
                            request.Operation == DG_ESCAPE_OPEN);
    if (request.ResultCapacity > reply.MaxResultBytes)
        goto unlock;
    if (!Dg9GlInitialize()) {
        status = DG_ESCAPE_RESOURCES;
        goto unlock;
    }
    if (request.Operation == DG_ESCAPE_OPEN) {
        /* Initialization may have just allocated the queue: enable it, then
         * negotiate packet size against the transport actually in use. */
        Dg9QueueEnsure();
        reply.Generation = Dg9Generation();
        if (!DgGlLimitsRefreshLocal(&dg_gl_limits, reply.Generation, TRUE, Dg9LocalBytes(),
                                    Dg9GlRead, NULL)) {
            status = DG_ESCAPE_UNSUPPORTED;
            goto unlock;
        }
        reply.DeviceError = DgGlQueueCurrent(&dg_gl_queue) ? DG_ESCAPE_FEATURE_QUEUE : 0;
        Dg9BulkEnsure();
        reply.MaxResultBytes = DgGlResultLimit(dg_gl_caps, dg_gl_caps2, !!dg_gl_bulk);
    }
    reply.MaxBytes = dg_gl_limits.MaxBytes;
    reply.MaxRecords = dg_gl_limits.MaxRecords;
    Dg9GlDrain(&reply);
    owner.Process = params->tagProcess;
    owner.Context = (DWORD)_GetCurrentContext();
    handle.Tag = params->tagProcess;
    handle.Handle = params->hDevice;
    if (request.Operation == DG_ESCAPE_OPEN) {
        if (request.Client || request.Function)
            goto unlock;
        flags = Dg9GlDisableInterrupts();
        reply.Client = Dg9OwnerClaim(&dg_gl_owners, owner, handle);
        Dg9GlRestoreInterrupts(flags);
        status = reply.Client ? DG_ESCAPE_OK : DG_ESCAPE_RESOURCES;
        goto unlock;
    }
    token = request.Client;
    flags = Dg9GlDisableInterrupts();
    acquired = Dg9OwnerAcquire(&dg_gl_owners, token, owner, handle);
    Dg9GlRestoreInterrupts(flags);
    if (!acquired) {
        status = DG_ESCAPE_OWNER;
        goto unlock;
    }
    client_lease.acquired(token);
    reply.Client = token;
    if (request.Operation == DG_ESCAPE_CLOSE) {
        flags = Dg9GlDisableInterrupts();
        Dg9OwnerRetireToken(&dg_gl_owners, token);
        Dg9GlRestoreInterrupts(flags);
        status = DG_ESCAPE_OK;
    } else if (request.Operation == DG_ESCAPE_CAPABILITIES && !request.Function) {
        reply.FunctionWords = Dg9Caps(reply.Generation, TRUE);
        /* The second word, unchanged; older drivers leave DeviceError zero. */
        reply.DeviceError = dg_gl_caps2;
        status = DG_ESCAPE_OK;
    } else if (request.Operation == DG_ESCAPE_QUERY) {
        reply.FunctionWords = Dg9GlWords(NULL, request.Function);
        status = DG_ESCAPE_OK;
    } else if (request.Operation == DG_ESCAPE_SUBMIT && !request.Function) {
        DG_GL_QUEUE_FAULT fault;
        DgGlQueueReap(&dg_gl_queue);
        /* A stream's deferred failure is reported before anything else of
         * its client runs; this request is not executed. */
        if (request.Stream && DgGlQueueTakeFault(&dg_gl_queue, token, 0, &fault)) {
            reply.FunctionWords = fault.Stream;
            reply.DeviceError = fault.Error;
            reply.CompletedSequence = fault.Sequence;
            status = DG_ESCAPE_DEFERRED;
            goto unlock;
        }
        ULONG *commands = Dg9Build();
        if (dg_gl_build_status != DG_ESCAPE_OK) {
            status = dg_gl_build_status;
            dg_gl_build = NULL;
            goto unlock;
        }
        if (!dg_gl_build_queued && request.Bytes > DG_ESCAPE_MAX_BYTES) {
            dg_gl_build = NULL;
            goto unlock;
        }
        memcpy(commands, (BYTE *)input.Address + sizeof(request), request.Bytes);
        if (!DreamGpuValidatePacket(commands, request.Bytes, token, reply.Generation, &dg_gl_limits,
                                    Dg9GlWords, NULL)) {
            dg_gl_build = NULL;
            goto unlock;
        }
        query = commands[0] == DG_GL_QUERY;
        if (query != (request.ResultCapacity != 0)) {
            dg_gl_build = NULL;
            goto unlock;
        }
        /* Larger results go to the bulk pages through their list. Only the
         * bytes the device reports are ever copied out, so those 4 MiB are
         * not cleared per query; small results keep the cleared buffer. */
        bulk = query && DgGlBulkResult(request.ResultCapacity);
        if (bulk && !dg_gl_bulk) {
            dg_gl_build = NULL;
            goto unlock;
        }
        if (query && !bulk)
            memset(dg_gl_result, 0, request.ResultCapacity);
        const ULONG result_physical = bulk ? dg_gl_bulk_physical : dg_gl_result_physical;
        const ULONG result_word = query ? DgGlResultWord(request.ResultCapacity) : 0;
        if (query && !dg_gl_build_queued) {
            Dg9GlWrite(DG_GL_REG_RESULT_ADDR_LO, result_physical);
            Dg9GlWrite(DG_GL_REG_RESULT_ADDR_HI, 0);
            Dg9GlWrite(DG_GL_REG_RESULT_CAPACITY, result_word);
        }
        const DG9_SUBMIT submit = {request.Stream ? token : 0, request.Stream,
                                   (request.Flags & DG_ESCAPE_SUBMIT_ASYNC) != 0, result_word,
                                   query ? result_physical : 0};
        status = Dg9GlSubmit(request.Bytes, &reply, &submit);
        if (status == DG_ESCAPE_HOST && request.Stream &&
            reply.DeviceError == DG_GL_ERROR_SKIPPED) {
            if (!DgGlQueueTakeFault(&dg_gl_queue, token, request.Stream, &fault)) {
                fault.Stream = request.Stream;
                fault.Error = DG_GL_ERROR_SKIPPED;
                fault.Sequence = reply.CompletedSequence;
            }
            reply.FunctionWords = fault.Stream;
            reply.DeviceError = fault.Error;
            reply.CompletedSequence = fault.Sequence;
            status = DG_ESCAPE_DEFERRED;
        } else if (query && status == DG_ESCAPE_OK) {
            reply.ResultBytes = dg_gl_result_bytes;
            reply.ResultType = dg_gl_result_type;
            if (!DreamGpuValidateResult(reply.ResultType, reply.ResultBytes, request.ResultCapacity,
                                        bulk ? dg_gl_bulk : dg_gl_result)) {
                reply.ResultBytes = reply.ResultType = 0;
                status = DG_ESCAPE_HOST;
            }
        }
    }
unlock:
    client_lease.reset();
    if (dg_gl_initialized)
        Dg9GlDrain(&reply);
    {
        /* Still under the mutex: no later query replaces the result first.
         * The part beyond the first window is copied before the reply is
         * written, so a window that cannot be pinned fails the request. */
        const BYTE *data = bulk ? dg_gl_bulk : dg_gl_result;
        DWORD head = reply.ResultBytes;
        if (head > pinned - sizeof(reply)) {
            head = pinned - sizeof(reply);
            if (!Dg9CopyOut(params->lpOutBuffer + pinned, data + head, reply.ResultBytes - head)) {
                status = DG_ESCAPE_INVALID;
                reply.ResultBytes = reply.ResultType = 0;
                head = 0;
            }
        }
        reply.Status = status;
        memcpy(output.Address, &reply, sizeof(reply));
        if (head)
            memcpy((BYTE *)output.Address + sizeof(reply), data, head);
    }
    {
        const DWORD count = sizeof(reply) + reply.ResultBytes;
        memcpy(returned.Address, &count, sizeof(count));
    }
    mutex_lease.reset();
    *result = 0;
done:
    return TRUE;
}

/* Called after the native device reset has drained its DMA worker. A wait
 * still unwinding during OS teardown retains its storage until VMM teardown;
 * it must never wake into freed command memory. */
extern "C" void DREAMGPU_CDECL DreamGpuGlShutdown(void) {
    dg_gl_faulted = TRUE;
    if (api->WaitPending()) {
        api->Signal();
        return;
    }
    if (dg_gl_commands)
        api->Free(dg_gl_commands);
    if (dg_gl_result)
        api->Free(dg_gl_result);
    Dg9BulkRelease();
    /* The device reset preceding shutdown disabled the queue page. */
    for (ULONG i = 0; i < dg_gl_queue.BufferCount; ++i)
        api->Free(dg_gl_queue.Buffers[i].Memory);
    if (dg_gl_queue.Page)
        api->Free(const_cast<ULONG *>(dg_gl_queue.Page));
    memset(&dg_gl_queue, 0, sizeof(dg_gl_queue));
    dg_gl_queue_supported = FALSE;
    dg_gl_build = NULL;
    dg_gl_commands = NULL;
    dg_gl_result = NULL;
    dg_gl_initialized = FALSE;
    if (dg_gl_mutex)
        Destroy_Semaphore(dg_gl_mutex);
    dg_gl_mutex = 0;
}

#include "dg-window9.h"
static struct DG9_WINDOW {
    DWORD Token, Client, Context, Drawable, Width, Height, Generation;
    DWORD Slot, EpochLow, EpochHigh, FrameLow, FrameHigh;
    BOOL Retained;
} dg_windows[DG9_WINDOW_SLOTS];
static DWORD dg_window_next = 1;
static BOOL dg_desktop_active;
static DWORD dg_desktop_width, dg_desktop_height, dg_desktop_stride, dg_desktop_bpp;

static void Dg9DesktopFault(ULONG status) {
    dg_gl_faulted = TRUE;
    api->Fatal(status);
    __builtin_unreachable();
}
extern "C" void DREAMGPU_CDECL DreamGpuPrimaryFault(void) {
    Dg9DesktopFault(DG_ESCAPE_STOPPED);
}
/* Every record names the primary format; the device checks it against the
 * active VBE mode. Zero keeps the original XRGB8888 records byte-identical. */
static ULONG *Dg9DesktopRecord(ULONG op, ULONG client, ULONG drawable, ULONG bpp) {
    ULONG *commands = Dg9Build();
    memset(commands, 0, 96);
    commands[0] = DG_GL_DESKTOP;
    commands[1] = 96;
    commands[2] = client ? client : 0xffffffffUL;
    commands[4] = drawable;
    commands[7] = Dg9Generation();
    commands[8] = op;
    commands[8 + DG_DESKTOP_PRIMARY_BPP / 4] = bpp == 16 ? 16 : 0;
    return commands + 8;
}
static ULONG Dg9WindowSubmit(ULONG bytes) {
    DG_ESCAPE_REPLY reply;
    ULONG status;
    const ULONG *commands = Dg9Build();
    const ULONG opcode = commands[0], operation = commands[8];
    memset(&reply, 0, sizeof(reply));
    reply.Generation = commands[7];
    status = Dg9GlSubmit(bytes, &reply);
    if (api->TraceText) {
        if (status != DG_ESCAPE_OK) {
            DgIdentityText("DGWINDOWERROR");
            DgIdentityField("opcode", opcode);
            DgIdentityField("operation", operation);
            DgIdentityField("status", status);
            DgIdentityField("device", reply.DeviceError);
            DgIdentityField("sequence", reply.CompletedSequence);
            DgIdentityChar('\n');
        }
    }
    return status;
}
static void Dg9CohereLocked(void) {
    ULONG *r, status;
    if (!dg_desktop_active)
        return;
    if (dg_gl_faulted)
        Dg9DesktopFault(DG_ESCAPE_STOPPED);
    r = Dg9DesktopRecord(DG_DESKTOP_READBACK, 0, 0, dg_desktop_bpp);
    r[4] = dg_desktop_width;
    r[5] = dg_desktop_height;
    r[11] = dg_desktop_stride;
    status = Dg9WindowSubmit(96);
    if (status != DG_ESCAPE_OK)
        Dg9DesktopFault(status);
    r = Dg9DesktopRecord(DG_DESKTOP_RETURN, 0, 0, dg_desktop_bpp);
    r[4] = dg_desktop_width;
    r[5] = dg_desktop_height;
    r[11] = dg_desktop_stride;
    status = Dg9WindowSubmit(96);
    if (status != DG_ESCAPE_OK)
        Dg9DesktopFault(status);
    dg_desktop_active = FALSE;
    Dg9PrimaryGpuOwned(FALSE);
}
extern "C" void DREAMGPU_CDECL DreamGpuPrimaryAccess(void) {
    if (!dg_desktop_active)
        return;
    /* A CPU access that cannot wait for READBACK/RETURN cannot fall through
     * to stale VRAM either. Only this thread's own critical section or VMM
     * reentry stops it now; a claim by another thread blocks instead of
     * halting the guest. */
    if (Dg9GlCritical() || !dg_gl_mutex)
        Dg9DesktopFault(DG_ESCAPE_STOPPED);
    Wait_Semaphore(dg_gl_mutex, 0);
    Dg9CohereLocked();
    Signal_Semaphore(dg_gl_mutex);
}
static struct DG9_WINDOW *Dg9WindowFind(DWORD token, DWORD client) {
    unsigned i;
    if (!token || !client)
        return NULL;
    for (i = 0; i < DG9_WINDOW_SLOTS; ++i)
        if (dg_windows[i].Token == token && dg_windows[i].Client == client)
            return &dg_windows[i];
    return NULL;
}
/* DISCARD touches no primary pixels, but the device still checks its format
 * word, so it names the live primary rather than the last seeded desktop. */
static ULONG Dg9WindowDiscard(struct DG9_WINDOW *window, DWORD bpp) {
    ULONG *r, status;
    if (!window->Retained)
        return DG_ESCAPE_OK;
    r = Dg9DesktopRecord(DG_DESKTOP_DISCARD, window->Client, window->Drawable, bpp);
    r[10] = window->Slot;
    r[12] = window->EpochLow;
    r[13] = window->EpochHigh;
    r[14] = window->FrameLow;
    r[15] = window->FrameHigh;
    status = Dg9WindowSubmit(96);
    if (status == DG_ESCAPE_OK)
        window->Retained = FALSE;
    return status;
}
/* Called while the same GL mutex drains a retired client's native contexts. */
static void Dg9WindowRetire(DWORD client) {
    DWORD width, height, stride, offset, bpp = 0;
    unsigned i;
    for (i = 0; i < DG9_WINDOW_SLOTS; ++i) {
        if (dg_windows[i].Client != client)
            continue;
        /* An unsupported live depth keeps the original zero format word. */
        if (dg_windows[i].Retained && !Dg9Primary(&width, &height, &stride, &offset, &bpp))
            bpp = 0;
        if (Dg9WindowDiscard(&dg_windows[i], bpp) != DG_ESCAPE_OK)
            Dg9DesktopFault(DG_ESCAPE_STOPPED);
        memset(&dg_windows[i], 0, sizeof(dg_windows[i]));
    }
}
extern "C" int DREAMGPU_CDECL DreamGpuWindowControl(const DG9_CHANNEL_CONTROL *params,
                                                    DWORD *result) {
    DG9_USER_LOCK input{}, output{}, returned{};
    pin_owner input_pin(&input), output_pin(&output), returned_pin(&returned);
    DWORD held_mutex = 0;
    semaphore_owner mutex_lease(&held_mutex);
    client_reference client_lease;
    DG9_WINDOW_REQUEST request;
    DG9_WINDOW_REPLY reply;
    DG9_OWNER_KEY owner;
    DG9_HANDLE_KEY handle;
    struct DG9_WINDOW *window;
    DWORD flags, width = 0, height = 0, stride = 0, offset = 0, bpp = 0;
    unsigned i;
    BOOL acquired = FALSE, critical;
    ULONG status = DG_ESCAPE_INVALID;
    if (params->dwIoControlCode != DG9_WINDOW_IOCTL)
        return FALSE;
    *result = DG9_ERROR_INVALID;
    critical = Dg9GlCritical();
    if (api->TraceText) {
        flags = Dg9GlDisableInterrupts();
        DgIdentityText("DGWINENTRY");
        DgIdentityField("init", dg_gl_initialized);
        DgIdentityField("cursor", Dg9CursorTakeoverSafe());
        DgIdentityField("critical", critical);
        DgIdentityChar('\n');
        Dg9GlRestoreInterrupts(flags);
    }
    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    memset(&returned, 0, sizeof(returned));
    memset(&reply, 0, sizeof(reply));
    if (!dg_gl_initialized || !dg_gl_mutex || !DgVxdRegisters() || params->lpOverlapped ||
        params->cbInBuffer != sizeof(request) || params->cbOutBuffer != sizeof(reply))
        return TRUE;
    /* Same retryable refusal as DreamGpuGlControl: before any pin, mutex,
     * owner or window state, so the request provably did not execute. */
    if (critical) {
        *result = DG9_ERROR_BUSY;
        return TRUE;
    }
    if (!Dg9LockUser(params->lpInBuffer, sizeof(request), FALSE, &input) ||
        !Dg9LockUser(params->lpOutBuffer, sizeof(reply), TRUE, &output) ||
        !Dg9LockUser(params->lpcbBytesReturned, sizeof(DWORD), TRUE, &returned))
        goto done;
    memcpy(&request, input.Address, sizeof(request));
    if (request.Version != DG9_WINDOW_VERSION || !request.Client)
        goto done;
    Wait_Semaphore(dg_gl_mutex, 0);
    held_mutex = dg_gl_mutex;
    reply.Version = DG9_WINDOW_VERSION;
    owner.Process = params->tagProcess;
    owner.Context = (DWORD)_GetCurrentContext();
    handle.Tag = params->tagProcess;
    handle.Handle = params->hDevice;
    flags = Dg9GlDisableInterrupts();
    acquired = Dg9OwnerAcquire(&dg_gl_owners, request.Client, owner, handle);
    Dg9GlRestoreInterrupts(flags);
    if (!acquired) {
        status = DG_ESCAPE_OWNER;
        goto unlock;
    }
    client_lease.acquired(request.Client);
    if (dg_gl_faulted) {
        status = DG_ESCAPE_STOPPED;
        goto unlock;
    }
    if (!Dg9CursorTakeoverSafe() || !Dg9Primary(&width, &height, &stride, &offset, &bpp) ||
        offset || !(Dg9Caps(Dg9Generation(), FALSE) & DG_CAP_GL_PRESENT_BOUNDS)) {
        status = DG_ESCAPE_UNSUPPORTED;
        goto unlock;
    }
    if (request.Operation == DG9_WINDOW_BIND) {
        if (request.Binding || request.Flags || !request.Context || !request.Drawable ||
            !request.Width || !request.Height || request.Width > DG_GL_MAX_DIMENSION ||
            request.Height > DG_GL_MAX_DIMENSION || !dg_window_next)
            goto unlock;
        for (i = 0; i < DG9_WINDOW_SLOTS && dg_windows[i].Token; ++i) {
        }
        if (i == DG9_WINDOW_SLOTS) {
            status = DG_ESCAPE_RESOURCES;
            goto unlock;
        }
        window = &dg_windows[i];
        memset(window, 0, sizeof(*window));
        window->Token = dg_window_next++;
        window->Client = request.Client;
        window->Context = request.Context;
        window->Drawable = request.Drawable;
        window->Width = request.Width;
        window->Height = request.Height;
        window->Generation = Dg9Generation();
        reply.Binding = window->Token;
        reply.Capabilities = DG9_WINDOW_FRONT_ONLY;
        status = DG_ESCAPE_OK;
        goto unlock;
    }
    if (request.Context || request.Drawable || request.Width || request.Height ||
        request.Flags & ~DG9_WINDOW_FRONT_ONLY ||
        (request.Operation != DG9_WINDOW_PREPARE && request.Flags))
        goto unlock;
    window = Dg9WindowFind(request.Binding, request.Client);
    if (!window || window->Generation != Dg9Generation()) {
        status = DG_ESCAPE_OWNER;
        goto unlock;
    }
    reply.Binding = window->Token;
    if (request.Operation == DG9_WINDOW_FINISH || request.Operation == DG9_WINDOW_RELEASE) {
        status = Dg9WindowDiscard(window, bpp);
        if (status == DG_ESCAPE_OK && request.Operation == DG9_WINDOW_RELEASE)
            memset(window, 0, sizeof(*window));
    } else if (request.Operation == DG9_WINDOW_PREPARE) {
        if (window->Retained)
            goto unlock;
        ULONG *commands = Dg9Build();
        memset(commands, 0, 40);
        commands[0] = DG_GL_PRESENT;
        commands[1] = 40;
        commands[2] = window->Client;
        commands[3] = window->Context;
        commands[4] = window->Drawable;
        commands[5] = DG_GL_PRESENT_RETAIN | DG_GL_PRESENT_BOUNDED |
                      (request.Flags ? DG_GL_PRESENT_FRONT_ONLY : 0);
        commands[7] = window->Generation;
        commands[8] = window->Width;
        commands[9] = window->Height;
        status = Dg9WindowSubmit(40);
        if (status == DG_ESCAPE_OK && DgGlQueueCurrent(&dg_gl_queue)) {
            /* Written with the PRESENT entry's completion, before COMPLETED. */
            window->Slot = DgGlQueueLoad(&dg_gl_queue, DG_GL_QUEUE_PRESENT_SLOT);
            window->EpochLow = DgGlQueueLoad(&dg_gl_queue, DG_GL_QUEUE_PRESENT_EPOCH_LO);
            window->EpochHigh = DgGlQueueLoad(&dg_gl_queue, DG_GL_QUEUE_PRESENT_EPOCH_HI);
            window->FrameLow = DgGlQueueLoad(&dg_gl_queue, DG_GL_QUEUE_PRESENT_FRAME_LO);
            window->FrameHigh = DgGlQueueLoad(&dg_gl_queue, DG_GL_QUEUE_PRESENT_FRAME_HI);
            window->Retained = TRUE;
        } else if (status == DG_ESCAPE_OK) {
            window->Slot = Dg9GlRead(NULL, DG_GL_REG_PRESENT_SLOT);
            window->EpochLow = Dg9GlRead(NULL, DG_GL_REG_PRESENT_EPOCH_LO);
            window->EpochHigh = Dg9GlRead(NULL, DG_GL_REG_PRESENT_EPOCH_HI);
            window->FrameLow = Dg9GlRead(NULL, DG_GL_REG_PRESENT_FRAME_LO);
            window->FrameHigh = Dg9GlRead(NULL, DG_GL_REG_PRESENT_FRAME_HI);
            window->Retained = TRUE;
        }
    }
unlock:
    client_lease.reset();
    if (api->TraceText) {
        flags = Dg9GlDisableInterrupts();
        DgIdentityText("DGWIN");
        DgIdentityField("op", request.Operation);
        DgIdentityField("status", status);
        DgIdentityField("width", width);
        DgIdentityField("height", height);
        DgIdentityField("stride", stride);
        DgIdentityField("offset", offset);
        DgIdentityField("bpp", bpp);
        DgIdentityChar('\n');
        Dg9GlRestoreInterrupts(flags);
    }
    reply.Status = status;
    memcpy(output.Address, &reply, sizeof(reply));
    {
        const DWORD count = sizeof(reply);
        memcpy(returned.Address, &count, sizeof(count));
    }
    mutex_lease.reset();
    *result = 0;
done:
    return TRUE;
}

/* Source and destination rectangles are scalar values from GDI BitBlt DDI,
 * never from DeviceIoControl. EDI holds the source's validated bitmap token. */
extern "C" unsigned short DREAMGPU_CDECL DreamGpuWindowBlt(const DG9_CHANNEL_BLT *state) {
    struct DG9_WINDOW *window = NULL;
    DG9_OWNER_SLOT *owner;
    DWORD width, height, stride, offset, bpp, context, flags, client = 0;
    DWORD sx = state->Client_EBX & 65535, sy = state->Client_EBX >> 16;
    DWORD dx = state->Client_ECX & 65535, dy = state->Client_ECX >> 16;
    DWORD w = state->Client_ESI & 65535, h = state->Client_ESI >> 16;
    ULONG *r, status;
    unsigned i;
    BOOL acquired = FALSE;
    DWORD held_mutex = 0;
    semaphore_owner mutex_lease(&held_mutex);
    client_reference client_lease;
    if (!dg_gl_initialized || !dg_gl_mutex || !w || !h)
        return 0;
    /* Declining leaves the DIB engine to copy the tag bitmap, which is only
     * legal while the CPU owns the primary. As in DreamGpuPrimaryAccess, only
     * this thread's own critical section or VMM reentry declines here. */
    if (Dg9GlCritical()) {
        if (dg_desktop_active)
            Dg9DesktopFault(DG_ESCAPE_STOPPED);
        return 0;
    }
    Wait_Semaphore(dg_gl_mutex, 0);
    held_mutex = dg_gl_mutex;
    context = (DWORD)_GetCurrentContext();
    for (i = 0; i < DG9_WINDOW_SLOTS; ++i)
        if (dg_windows[i].Token == state->Client_EDI) {
            window = &dg_windows[i];
            break;
        }
    if (!window || !window->Token || !window->Retained || dg_gl_faulted ||
        window->Generation != Dg9Generation())
        goto done;
    flags = Dg9GlDisableInterrupts();
    owner = Dg9OwnerFind(&dg_gl_owners, window->Client);
    if (owner && owner->State == DG9_OWNER_ACTIVE && owner->Owner.Context == context) {
        acquired = Dg9OwnerAcquire(&dg_gl_owners, window->Client, owner->Owner, owner->Handle);
        client = window->Client;
        if (acquired)
            client_lease.acquired(client);
    }
    Dg9GlRestoreInterrupts(flags);
    if (!acquired || !Dg9CursorTakeoverSafe() ||
        !Dg9Primary(&width, &height, &stride, &offset, &bpp) || offset || sx > window->Width ||
        w > window->Width - sx || sy > window->Height || h > window->Height - sy || dx > width ||
        w > width - dx || dy > height || h > height - dy)
        goto done;
    if (!dg_desktop_active) {
        r = Dg9DesktopRecord(DG_DESKTOP_SEED, 0, 0, bpp);
        r[4] = width;
        r[5] = height;
        r[11] = stride;
        status = Dg9WindowSubmit(96);
        if (status != DG_ESCAPE_OK)
            Dg9DesktopFault(status);
        dg_desktop_width = width;
        dg_desktop_height = height;
        dg_desktop_stride = stride;
        dg_desktop_bpp = bpp;
        dg_desktop_active = TRUE;
        Dg9PrimaryGpuOwned(TRUE);
    }
    /* GPU-owned VRAM has no CPU copy in any other geometry or pixel format. */
    if (width != dg_desktop_width || height != dg_desktop_height || stride != dg_desktop_stride ||
        bpp != dg_desktop_bpp)
        Dg9DesktopFault(DG_ESCAPE_STOPPED);
    r = Dg9DesktopRecord(DG_DESKTOP_BLIT, client, window->Drawable, dg_desktop_bpp);
    r[2] = dx;
    r[3] = dy;
    r[4] = w;
    r[5] = h;
    r[6] = sx;
    r[7] = sy;
    r[10] = window->Slot;
    r[12] = window->EpochLow;
    r[13] = window->EpochHigh;
    r[14] = window->FrameLow;
    r[15] = window->FrameHigh;
    status = Dg9WindowSubmit(96);
    if (status != DG_ESCAPE_OK)
        Dg9DesktopFault(status);
    client_lease.reset();
    mutex_lease.reset();
    return 1;
done:
    client_lease.reset();
    mutex_lease.reset();
    return 0;
}
