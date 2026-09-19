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
#include "dg-function-cache.h"
#include "packet32.h"
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
#define Dg9GlCritical() api->Critical()
#define Dg9GlDisableInterrupts() api->DisableInterrupts()
#define Dg9GlRestoreInterrupts(flags) api->RestoreInterrupts(flags)
#define _GetCurrentContext() api->CurrentContext()
#define Create_Semaphore(count) api->CreateSemaphore(count)
#define Destroy_Semaphore(value) api->DestroySemaphore(value)
#define Wait_Semaphore(value, flags) api->WaitSemaphore(value)
#define Signal_Semaphore(value) api->SignalSemaphore(value)
#define Dg9CursorTakeoverSafe() api->CursorSafe()
#define Dg9Primary(w, h, s, o) api->Primary(w, h, s, o)
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
static void Dg9WindowRetire(DWORD client);
static DG9_OWNER_TABLE dg_gl_owners;
static BOOL dg_gl_initialized, dg_gl_faulted;
static DWORD dg_gl_mutex, dg_gl_physical, dg_gl_result_physical;
static ULONG *dg_gl_commands;
static BYTE *dg_gl_result;
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
static ULONG Dg9GlSubmit(ULONG bytes, DG_ESCAPE_REPLY *reply) {
    if (dg_gl_faulted)
        return DG_ESCAPE_STOPPED;
    const auto status = api->Submit(dg_gl_physical, bytes, reply);
    if (status == DG_ESCAPE_TIMEOUT)
        dg_gl_faulted = TRUE;
    return status;
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
        memset(dg_gl_commands, 0, DG_GL_HEADER_BYTES);
        dg_gl_commands[0] = DG_GL_CLOSE_CLIENT;
        dg_gl_commands[1] = DG_GL_HEADER_BYTES;
        dg_gl_commands[2] = token;
        dg_gl_commands[7] = reply->Generation;
        /* A retired client's cleanup completion is not this caller's result
         * identity, including when the caller just completed a typed query. */
        if (Dg9GlSubmit(DG_GL_HEADER_BYTES, &cleanup) != DG_ESCAPE_OK)
            break;
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
    Dg9OwnerInitialize(&dg_gl_owners);
    dg_gl_initialized = TRUE;
    return TRUE;
}

static ULONG Dg9GlResultLimit(ULONG capabilities) {
    return capabilities & DG_CAP_GL_BULK_READBACK ? DG_ESCAPE_MAX_RESULT_BYTES
                                                  : DG_ESCAPE_LEGACY_RESULT_BYTES;
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
    DWORD flags;
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
            reply.Generation = Dg9GlRead(NULL, DG_REG_GENERATION);
            Dg9GlDrain(&reply);
            Signal_Semaphore(dg_gl_mutex);
        }
        return FALSE; /* preserve the mini-VDD's ordinary handle accounting */
    }
    if (params->dwIoControlCode != DG_ESCAPE)
        return FALSE;
    *result = 87;
    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    memset(&returned, 0, sizeof(returned));
    memset(&reply, 0, sizeof(reply));
    if (!DgVxdRegisters() || Dg9GlCritical() || params->lpOverlapped ||
        params->cbInBuffer < sizeof(request) || params->cbOutBuffer < sizeof(reply) ||
        params->cbOutBuffer > sizeof(reply) + DG_ESCAPE_MAX_RESULT_BYTES || !params->tagProcess ||
        !params->hDevice)
        return TRUE;
    if (!Dg9LockUser(params->lpInBuffer, params->cbInBuffer, FALSE, &input) ||
        !Dg9LockUser(params->lpOutBuffer, params->cbOutBuffer, TRUE, &output) ||
        !Dg9LockUser(params->lpcbBytesReturned, sizeof(DWORD), TRUE, &returned))
        goto done;
    memcpy(&request, input.Address, sizeof(request));
    if (request.Version != DG_ESCAPE_VERSION || request.Reserved || request.Reserved2 ||
        request.Bytes > DG_ESCAPE_MAX_BYTES ||
        request.Bytes != params->cbInBuffer - sizeof(request) ||
        request.ResultCapacity > DG_ESCAPE_MAX_RESULT_BYTES ||
        request.ResultCapacity > params->cbOutBuffer - sizeof(reply) ||
        (request.Operation != DG_ESCAPE_SUBMIT && (request.Bytes || request.ResultCapacity)))
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
    reply.Generation = Dg9GlRead(NULL, DG_REG_GENERATION);
    reply.MaxResultBytes = Dg9GlResultLimit(Dg9GlRead(NULL, DG_REG_CAPS));
    if (Dg9GlRead(NULL, DG_GL_REG_VERSION) != DG_GL_VERSION ||
        !(Dg9GlRead(NULL, DG_REG_CAPS) & DG_CAP_GL_TRANSPORT) ||
        !DgGlLimitsRefresh(&dg_gl_limits, reply.Generation, request.Operation == DG_ESCAPE_OPEN,
                           Dg9GlRead, NULL)) {
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
        reply.FunctionWords = Dg9GlRead(NULL, DG_REG_CAPS);
        status = DG_ESCAPE_OK;
    } else if (request.Operation == DG_ESCAPE_QUERY) {
        reply.FunctionWords = Dg9GlWords(NULL, request.Function);
        status = DG_ESCAPE_OK;
    } else if (request.Operation == DG_ESCAPE_SUBMIT && !request.Function) {
        memcpy(dg_gl_commands, (BYTE *)input.Address + sizeof(request), request.Bytes);
        if (!DreamGpuValidatePacket(dg_gl_commands, request.Bytes, token, reply.Generation,
                                    &dg_gl_limits, Dg9GlWords, NULL))
            goto unlock;
        query = dg_gl_commands[0] == DG_GL_QUERY;
        if (query != (request.ResultCapacity != 0))
            goto unlock;
        if (query) {
            memset(dg_gl_result, 0, request.ResultCapacity);
            Dg9GlWrite(DG_GL_REG_RESULT_ADDR_LO, dg_gl_result_physical);
            Dg9GlWrite(DG_GL_REG_RESULT_ADDR_HI, 0);
            Dg9GlWrite(DG_GL_REG_RESULT_CAPACITY, request.ResultCapacity);
        }
        status = Dg9GlSubmit(request.Bytes, &reply);
        if (query && status == DG_ESCAPE_OK) {
            reply.ResultBytes = Dg9GlRead(NULL, DG_GL_REG_RESULT_BYTES);
            reply.ResultType = Dg9GlRead(NULL, DG_GL_REG_RESULT_TYPE);
            if (!DreamGpuValidateResult(reply.ResultType, reply.ResultBytes, request.ResultCapacity,
                                        dg_gl_result)) {
                reply.ResultBytes = reply.ResultType = 0;
                status = DG_ESCAPE_HOST;
            }
        }
    }
unlock:
    client_lease.reset();
    if (dg_gl_initialized)
        Dg9GlDrain(&reply);
    reply.Status = status;
    memcpy(output.Address, &reply, sizeof(reply));
    if (reply.ResultBytes)
        memcpy((BYTE *)output.Address + sizeof(reply), dg_gl_result, reply.ResultBytes);
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
static DWORD dg_desktop_width, dg_desktop_height, dg_desktop_stride;

static void Dg9DesktopFault(ULONG status) {
    dg_gl_faulted = TRUE;
    api->Fatal(status);
    __builtin_unreachable();
}
extern "C" void DREAMGPU_CDECL DreamGpuPrimaryFault(void) {
    Dg9DesktopFault(DG_ESCAPE_STOPPED);
}
static ULONG *Dg9DesktopRecord(ULONG op, ULONG client, ULONG drawable) {
    memset(dg_gl_commands, 0, 96);
    dg_gl_commands[0] = DG_GL_DESKTOP;
    dg_gl_commands[1] = 96;
    dg_gl_commands[2] = client ? client : 0xffffffffUL;
    dg_gl_commands[4] = drawable;
    dg_gl_commands[7] = Dg9GlRead(NULL, DG_REG_GENERATION);
    dg_gl_commands[8] = op;
    return dg_gl_commands + 8;
}
static ULONG Dg9WindowSubmit(ULONG bytes) {
    DG_ESCAPE_REPLY reply;
    ULONG status;
    memset(&reply, 0, sizeof(reply));
    reply.Generation = Dg9GlRead(NULL, DG_REG_GENERATION);
    status = Dg9GlSubmit(bytes, &reply);
    if (api->TraceText) {
        if (status != DG_ESCAPE_OK) {
            DgIdentityText("DGWINDOWERROR");
            DgIdentityField("opcode", dg_gl_commands[0]);
            DgIdentityField("operation", dg_gl_commands[8]);
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
    r = Dg9DesktopRecord(DG_DESKTOP_READBACK, 0, 0);
    r[4] = dg_desktop_width;
    r[5] = dg_desktop_height;
    r[11] = dg_desktop_stride;
    status = Dg9WindowSubmit(96);
    if (status != DG_ESCAPE_OK)
        Dg9DesktopFault(status);
    r = Dg9DesktopRecord(DG_DESKTOP_RETURN, 0, 0);
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
static ULONG Dg9WindowDiscard(struct DG9_WINDOW *window) {
    ULONG *r, status;
    if (!window->Retained)
        return DG_ESCAPE_OK;
    r = Dg9DesktopRecord(DG_DESKTOP_DISCARD, window->Client, window->Drawable);
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
    unsigned i;
    for (i = 0; i < DG9_WINDOW_SLOTS; ++i) {
        if (dg_windows[i].Client != client)
            continue;
        if (Dg9WindowDiscard(&dg_windows[i]) != DG_ESCAPE_OK)
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
    DWORD flags, width = 0, height = 0, stride = 0, offset = 0;
    unsigned i;
    BOOL acquired = FALSE, critical;
    ULONG status = DG_ESCAPE_INVALID;
    if (params->dwIoControlCode != DG9_WINDOW_IOCTL)
        return FALSE;
    *result = 87;
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
    if (!dg_gl_initialized || !dg_gl_mutex || !DgVxdRegisters() || critical ||
        params->lpOverlapped || params->cbInBuffer != sizeof(request) ||
        params->cbOutBuffer != sizeof(reply))
        return TRUE;
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
    if (!Dg9CursorTakeoverSafe() || !Dg9Primary(&width, &height, &stride, &offset) || offset ||
        !(Dg9GlRead(NULL, DG_REG_CAPS) & DG_CAP_GL_PRESENT_BOUNDS)) {
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
        window->Generation = Dg9GlRead(NULL, DG_REG_GENERATION);
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
    if (!window || window->Generation != Dg9GlRead(NULL, DG_REG_GENERATION)) {
        status = DG_ESCAPE_OWNER;
        goto unlock;
    }
    reply.Binding = window->Token;
    if (request.Operation == DG9_WINDOW_FINISH || request.Operation == DG9_WINDOW_RELEASE) {
        status = Dg9WindowDiscard(window);
        if (status == DG_ESCAPE_OK && request.Operation == DG9_WINDOW_RELEASE)
            memset(window, 0, sizeof(*window));
    } else if (request.Operation == DG9_WINDOW_PREPARE) {
        if (window->Retained)
            goto unlock;
        memset(dg_gl_commands, 0, 40);
        dg_gl_commands[0] = DG_GL_PRESENT;
        dg_gl_commands[1] = 40;
        dg_gl_commands[2] = window->Client;
        dg_gl_commands[3] = window->Context;
        dg_gl_commands[4] = window->Drawable;
        dg_gl_commands[5] = DG_GL_PRESENT_RETAIN | DG_GL_PRESENT_BOUNDED |
                            (request.Flags ? DG_GL_PRESENT_FRONT_ONLY : 0);
        dg_gl_commands[7] = window->Generation;
        dg_gl_commands[8] = window->Width;
        dg_gl_commands[9] = window->Height;
        status = Dg9WindowSubmit(40);
        if (status == DG_ESCAPE_OK) {
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
    DWORD width, height, stride, offset, context, flags, client = 0;
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
        window->Generation != Dg9GlRead(NULL, DG_REG_GENERATION))
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
    if (!acquired || !Dg9CursorTakeoverSafe() || !Dg9Primary(&width, &height, &stride, &offset) ||
        offset || sx > window->Width || w > window->Width - sx || sy > window->Height ||
        h > window->Height - sy || dx > width || w > width - dx || dy > height || h > height - dy)
        goto done;
    if (!dg_desktop_active) {
        r = Dg9DesktopRecord(DG_DESKTOP_SEED, 0, 0);
        r[4] = width;
        r[5] = height;
        r[11] = stride;
        status = Dg9WindowSubmit(96);
        if (status != DG_ESCAPE_OK)
            Dg9DesktopFault(status);
        dg_desktop_width = width;
        dg_desktop_height = height;
        dg_desktop_stride = stride;
        dg_desktop_active = TRUE;
        Dg9PrimaryGpuOwned(TRUE);
    }
    if (width != dg_desktop_width || height != dg_desktop_height || stride != dg_desktop_stride)
        Dg9DesktopFault(DG_ESCAPE_STOPPED);
    r = Dg9DesktopRecord(DG_DESKTOP_BLIT, client, window->Drawable);
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
