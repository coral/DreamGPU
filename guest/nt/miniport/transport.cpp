/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded NT5 command submission. Waiting threads sleep; the ISR queues a DPC
 * to signal completion because KeSetEvent is not legal at device IRQL.
 * All imported kernel routines exist on Windows 2000. The newer declaration
 * version works around a MinGW WDM header's unconditional power-structure use.
 */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include "ownership.hpp"
extern "C" {
/* MinGW's WDM header supplies these intrinsics itself. Suppress duplicate
 * user-mode inline definitions with incompatible C++ GNU inline attributes. */
#define __INTRINSIC_DEFINED_InterlockedBitTestAndSet
#define __INTRINSIC_DEFINED_InterlockedBitTestAndReset
#include <ntddk.h>
#include "dg-ioctl.h"
#include "transport.h"
#include "dg-gl-limits.h"
#include "dg-gl-registers.h"
#include "dg-gl-validate.h"
#include "dg-gl-result.h"
#include "dg-gl-signature.h"
#include "dg-kernel.h"

} /* extern C */
struct PoolDeleter {
    void operator()(void *value) const noexcept {
        ExFreePool(value);
    }
};
extern "C" {

/* VideoPortAllocatePool/FreePool only appeared in XP. Keep the NT5.0 build
 * on kernel pool APIs that are present in the actual Windows 2000 loader. */
PVOID DgAllocatePaged(ULONG bytes, ULONG tag) {
    return ExAllocatePoolWithTag(PagedPool, bytes, tag);
}

VOID DgFreePool(PVOID memory) {
    ExFreePool(memory);
}

static WCHAR RegistryBuffer[512];
static UNICODE_STRING RegistryPath;
static ULONG IrqConfiguration[5];
static volatile ULONG *DebugPage;

/* Diagnostic counters are sampled through volatile guest memory. Their
 * existing caller serialization is unchanged; these are not atomic counters. */
static void IncrementDiagnostic(volatile ULONG *value) {
    *value = *value + 1;
}
static void DecrementDiagnostic(volatile ULONG *value) {
    *value = *value - 1;
}

VOID DgDiagnosticsIsrEntry(PVOID device, PVOID transport, PVOID handler) {
    /* Count all invocations, including shared IRQs belonging to another
     * device. This distinguishes callback registration from IRQ recognition. */
    if (DebugPage) {
        IncrementDiagnostic(&DebugPage[13]);
        DebugPage[14] = (ULONG_PTR)device;
        DebugPage[15] = (ULONG_PTR)transport;
        DebugPage[16] = (ULONG_PTR)handler;
    }
}

VOID DgDiagnosticsIrq(ULONG level, ULONG vector, ULONG mode, ULONG assigned_level,
                      ULONG assigned_vector) {
    IrqConfiguration[0] = level;
    IrqConfiguration[1] = vector;
    IrqConfiguration[2] = mode;
    IrqConfiguration[3] = assigned_level;
    IrqConfiguration[4] = assigned_vector;
}

VOID DgRecordStatus(PCWSTR name, ULONG value) {
    OBJECT_ATTRIBUTES attributes;
    UNICODE_STRING value_name;
    HANDLE key;
    if (!RegistryPath.Length || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    InitializeObjectAttributes(&attributes, &RegistryPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes)))
        return;
    RtlInitUnicodeString(&value_name, name);
    ZwSetValueKey(key, &value_name, 0, REG_DWORD, &value, sizeof(value));
    ZwClose(key);
}

VOID DgDiagnosticsInitialize(PVOID registry_path) {
    PUNICODE_STRING path = (PUNICODE_STRING)registry_path;
    if (!path || path->Length >= sizeof(RegistryBuffer))
        return;
    memcpy(RegistryBuffer, path->Buffer, path->Length);
    RegistryBuffer[path->Length / sizeof(WCHAR)] = 0;
    RtlInitUnicodeString(&RegistryPath, RegistryBuffer);
    DgRecordStatus(L"DreamGPUEntryStatus", 0xffffffff);
    DgRecordStatus(L"DreamGPUInitStage", 1);
}

typedef struct {
    PEPROCESS Process;
    HANDLE ProcessId;
    ULONG Token;
} DG_CLIENT;

typedef struct {
    volatile ULONG *Registers;
    PVOID Commands;
    PHYSICAL_ADDRESS Address;
    volatile ULONG *Diagnostics;
    KEVENT Completion;
    KDPC Dpc;
    ULONG Sequence;
    ULONG BatchCount;
    ULONG NativeSubmitFlags;
    PVOID CursorPixels;
    PHYSICAL_ADDRESS CursorAddress;
    ULONG CursorSequence;
    BOOLEAN CursorSupported;
    volatile ULONG InterruptCount;
    volatile ULONG DpcCount;
    ULONG TimeoutCount;
    KMUTEX TimingMutex;
    KEVENT TimingCompletion;
    KDPC TimingDpc;
    ULONG TimingSequence;
    BOOLEAN TimingSupported;
    KMUTEX GlMutex;
    KEVENT GlCompletion;
    KEVENT DesktopStopped;
    KDPC GlDpc;
    PVOID GlCommands;
    PHYSICAL_ADDRESS GlAddress;
    PVOID GlResult;
    PHYSICAL_ADDRESS GlResultAddress;
    DG_GL_SIGNATURE_CACHE *GlSignatures;
    ULONG GlSignatureGeneration;
    DG_GL_LIMITS GlLimits;
    DG_GL_REGISTERS GlRegisters;
    ULONG GlSequence, NextClient;
    ULONG GlPresentCaps;
    volatile ULONG DesktopActive;
    ULONG DesktopWidth, DesktopHeight, DesktopStride;
    BOOLEAN GlFaulted, GlNotifyRegistered;
    volatile LONG DpcReferences;
    KEVENT DpcsIdle;
    DG_CLIENT Clients[DG_ESCAPE_MAX_CLIENTS];
} DG_TRANSPORT;

static DG_TRANSPORT *AdapterTransport;
static PDRIVER_UNLOAD PortUnload;
static PDRIVER_DISPATCH PortDeviceControl;
static KSPIN_LOCK CallbackLock;
static KEVENT CallbacksIdle;
static ULONG CallbackCount;
static BOOLEAN DriverReady;
static VOID NTAPI ProcessNotify(HANDLE parent, HANDLE process, BOOLEAN create);
static VOID NTAPI DriverUnload(PDRIVER_OBJECT driver);

static ULONG ReadReg(DG_TRANSPORT *t, ULONG offset) {
    ULONG value;
    /* An MMIO access's width is part of the device ABI. In GCC 16 a masked
     * volatile ULONG load can become testb, which is invalid on this BAR and
     * silently makes a pending IRQ appear absent. Force the bus transaction. */
    __asm__ __volatile__("movl %1, %0"
                         : "=r"(value)
                         : "m"(t->Registers[offset / sizeof(ULONG)])
                         : "memory");
    return value;
}

static VOID WriteReg(DG_TRANSPORT *t, ULONG offset, ULONG value) {
    __asm__ __volatile__("movl %1, %0"
                         : "=m"(t->Registers[offset / sizeof(ULONG)])
                         : "r"(value)
                         : "memory");
}

static VOID NTAPI Complete(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)context;
    (void)dpc;
    (void)arg1;
    (void)arg2;
    t->DpcCount = t->DpcCount + 1;
    t->Diagnostics[3] = t->DpcCount;
    KeSetEvent(&t->Completion, IO_NO_INCREMENT, FALSE);
    if (!InterlockedDecrement(&t->DpcReferences))
        KeSetEvent(&t->DpcsIdle, IO_NO_INCREMENT, FALSE);
}

static VOID NTAPI CompleteGl(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)context;
    (void)dpc;
    (void)arg1;
    (void)arg2;
    KeSetEvent(&t->GlCompletion, IO_NO_INCREMENT, FALSE);
    if (!InterlockedDecrement(&t->DpcReferences))
        KeSetEvent(&t->DpcsIdle, IO_NO_INCREMENT, FALSE);
}

static VOID NTAPI CompleteTiming(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)context;
    (void)dpc;
    (void)arg1;
    (void)arg2;
    KeSetEvent(&t->TimingCompletion, IO_NO_INCREMENT, FALSE);
    if (!InterlockedDecrement(&t->DpcReferences))
        KeSetEvent(&t->DpcsIdle, IO_NO_INCREMENT, FALSE);
}

BOOLEAN DgTransportTimingSupported(PVOID transport) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    return t && t->TimingSupported;
}

BOOLEAN DgTransportSetRate(PVOID transport, ULONG rate) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    if (!DgTimingRateValid(rate))
        return FALSE;
    if (!t || !t->TimingSupported)
        return rate == DG_TIMING_DEFAULT_HZ;
    /* MMIO commits cancel any pending timing wait, without taking its mutex. */
    WriteReg(t, DG_TIMING_REG_RATE, rate);
    return ReadReg(t, DG_TIMING_REG_RATE) == rate;
}

ULONG DgTransportTiming(PVOID transport, PVOID input, ULONG input_bytes, PVOID output,
                        ULONG output_bytes) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    DG_TIMING_REQUEST request;
    DG_TIMING_REPLY reply = {};
    LARGE_INTEGER timeout;
    ULONG serial, phase, i;
    NTSTATUS waited;
    if (!input || !output || input_bytes != sizeof(request) || output_bytes != sizeof(reply) ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    RtlCopyMemory(&request, input, sizeof(request));
    if (request.Version != DG_TIMING_VERSION || request.Operation > DG_TIMING_WAIT_END ||
        request.Reserved0 || request.Reserved1)
        return 0;
    reply.Version = DG_TIMING_VERSION;
    reply.Status = DG_TIMING_REPLY_UNSUPPORTED;
    if (!t || !t->TimingSupported)
        goto done;
    timeout.QuadPart = -2500000; /* 250 ms watchdog, not a refresh poll. */
    waited = KeWaitForSingleObject(&t->TimingMutex, Executive, KernelMode, FALSE, &timeout);
    if (waited != STATUS_SUCCESS) {
        reply.Status = DG_TIMING_REPLY_TIMEOUT;
        goto done;
    }
    reply.Status = DG_TIMING_REPLY_OK;
    if (request.Operation) {
        ULONG sequence = ++t->TimingSequence;
        if (!sequence)
            sequence = ++t->TimingSequence;
        KeResetEvent(&t->TimingCompletion);
        WriteReg(t, DG_REG_IRQ_STATUS, DG_IRQ_DISPLAY_TIMING);
        WriteReg(t, DG_TIMING_REG_SEQUENCE, sequence);
        WriteReg(t, DG_TIMING_REG_COMMAND, request.Operation);
        waited =
            KeWaitForSingleObject(&t->TimingCompletion, Executive, KernelMode, FALSE, &timeout);
        if (waited != STATUS_SUCCESS || ReadReg(t, DG_TIMING_REG_COMPLETED) != sequence ||
            ReadReg(t, DG_TIMING_REG_STATUS) != DG_TIMING_DONE) {
            reply.Status =
                waited == STATUS_TIMEOUT ? DG_TIMING_REPLY_TIMEOUT : DG_TIMING_REPLY_CANCELLED;
            WriteReg(t, DG_TIMING_REG_COMMAND, DG_TIMING_CANCEL);
        }
    }
    for (i = 0; i < 3; ++i) {
        serial = ReadReg(t, DG_TIMING_REG_SNAPSHOT);
        reply.ScanLine = ReadReg(t, DG_TIMING_REG_SCANLINE);
        reply.Height = ReadReg(t, DG_TIMING_REG_HEIGHT);
        phase = ReadReg(t, DG_TIMING_REG_PHASE);
        reply.RateHz = phase >> 16;
        reply.InVBlank = phase & 1;
        reply.UntilBeginNs = ReadReg(t, DG_TIMING_REG_BEGIN_NS);
        reply.UntilEndNs = ReadReg(t, DG_TIMING_REG_END_NS);
        if (serial == ReadReg(t, DG_TIMING_REG_SERIAL))
            break;
    }
    if (i == 3)
        reply.Status = DG_TIMING_REPLY_CANCELLED;
    KeReleaseMutex(&t->TimingMutex, FALSE);
done:
    RtlCopyMemory(output, &reply, sizeof(reply));
    return sizeof(reply);
}

static ULONG InterruptMask(DG_TRANSPORT *t) {
    return DG_IRQ_COMPLETION | (t->GlCommands ? DG_IRQ_GL_COMPLETION : 0) |
           (t->TimingSupported ? DG_IRQ_DISPLAY_TIMING : 0);
}

PVOID DgTransportCreate(PVOID registers) {
    DG_TRANSPORT *t;
    PHYSICAL_ADDRESS highest;
    ULONG caps;
    if (AdapterTransport)
        return NULL; /* one adapter in the canonical profile */
    t = (DG_TRANSPORT *)ExAllocatePoolWithTag(NonPagedPool, sizeof(*t), 0x47524a51);
    dreamgpu::unique_owner<DG_TRANSPORT, PoolDeleter> pending(t);
    if (!t)
        return NULL;
    RtlZeroMemory(t, sizeof(*t));
    t->Registers = (volatile ULONG *)registers;
    caps = ReadReg(t, DG_REG_CAPS);
    t->TimingSupported = (caps & DG_CAP_DISPLAY_TIMING) &&
                         ReadReg(t, DG_TIMING_REG_VERSION) == DG_TIMING_VERSION &&
                         ReadReg(t, DG_TIMING_REG_RATES) == DG_TIMING_RATES;
    t->GlPresentCaps = caps & (DG_CAP_GL_FRONT_BUFFERS | DG_CAP_GL_PRESENT_BOUNDS);
    t->NativeSubmitFlags =
        DG_SUBMIT_START | ((caps & DG_CAP_INLINE_NO_IRQ) ? DG_SUBMIT_INLINE_NO_IRQ : 0);
    t->CursorSupported = (caps & DG_CAP_CURSOR) &&
                         ReadReg(t, DG_CURSOR_REG_VERSION) == DG_CURSOR_ABI_VERSION &&
                         ReadReg(t, DG_CURSOR_REG_MAX_DIMENSION) >= DG_CURSOR_MAX_DIMENSION;
    highest.QuadPart = 0xffffffffULL;
    t->Commands = MmAllocateContiguousMemory(PAGE_SIZE, highest);
    if (!t->Commands) {
        return NULL;
    }
    t->Address = MmGetPhysicalAddress(t->Commands);
    /* Readable with QMP xp at BATCH_ADDR + 0xc00 even if the guest desktop
     * cannot repaint. The command ABI uses at most 2560 bytes of this page.
     * Words: magic, version, IRQ count, DPC count, level, vector, mode,
     * submit sequence, timeout count, wait status, device status, assigned
     * level, assigned vector. Words 13-16 identify ISR entry/callback context
     * for a kernel debugger; these are guest kernel addresses, never host
     * pointers or application data. */
    t->Diagnostics = (volatile ULONG *)((PUCHAR)t->Commands + 0xc00);
    RtlZeroMemory(t->Commands, PAGE_SIZE);
    DebugPage = t->Diagnostics;
    t->Diagnostics[41] = t->CursorSupported;
    t->Diagnostics[0] = 0x4744494a; /* JIDG */
    t->Diagnostics[1] = 1;
    t->Diagnostics[4] = IrqConfiguration[0];
    t->Diagnostics[5] = IrqConfiguration[1];
    t->Diagnostics[6] = IrqConfiguration[2];
    t->Diagnostics[11] = IrqConfiguration[3];
    t->Diagnostics[12] = IrqConfiguration[4];
    KeInitializeEvent(&t->Completion, NotificationEvent, FALSE);
    KeInitializeDpc(&t->Dpc, Complete, t);
    KeInitializeMutex(&t->TimingMutex, 0);
    KeInitializeEvent(&t->TimingCompletion, NotificationEvent, FALSE);
    KeInitializeDpc(&t->TimingDpc, CompleteTiming, t);
    KeInitializeMutex(&t->GlMutex, 0);
    KeInitializeEvent(&t->GlCompletion, NotificationEvent, FALSE);
    KeInitializeEvent(&t->DesktopStopped, NotificationEvent, FALSE);
    KeInitializeDpc(&t->GlDpc, CompleteGl, t);
    KeInitializeEvent(&t->DpcsIdle, NotificationEvent, FALSE);
    WriteReg(t, DG_REG_RESET, 1);
    WriteReg(t, DG_REG_BATCH_ADDR_LO, t->Address.LowPart);
    WriteReg(t, DG_REG_BATCH_ADDR_HI, t->Address.HighPart);
    WriteReg(t, DG_REG_IRQ_STATUS, DG_IRQ_COMPLETION);
    WriteReg(t, DG_REG_IRQ_ENABLE, InterruptMask(t));
    AdapterTransport = t;
    return pending.release();
}

VOID DgTransportReset(PVOID transport) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    if (!t)
        return;
    DgGlRegistersInvalidate(&t->GlRegisters);
    WriteReg(t, DG_REG_RESET, 1);
    WriteReg(t, DG_REG_BATCH_ADDR_LO, t->Address.LowPart);
    WriteReg(t, DG_REG_BATCH_ADDR_HI, t->Address.HighPart);
    t->BatchCount = 0;
    t->DesktopActive = 0;
    WriteReg(t, DG_REG_IRQ_STATUS, InterruptMask(t));
    WriteReg(t, DG_REG_IRQ_ENABLE, InterruptMask(t));
}

VOID DgTransportDestroy(PVOID transport) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    if (!t)
        return;
    DgTransportReset(t);
    WriteReg(t, DG_REG_IRQ_ENABLE, 0);
    KeRemoveQueueDpc(&t->Dpc);
    if (DebugPage == t->Diagnostics)
        DebugPage = NULL;
    if (AdapterTransport == t)
        AdapterTransport = NULL;
    /* This is only called before device initialization succeeds, so no
     * completed submissions or running completion DPC can still own t. */
    if (t->CursorPixels)
        MmFreeContiguousMemory(t->CursorPixels);
    MmFreeContiguousMemory(t->Commands);
    ExFreePool(t);
}

BOOLEAN DgTransportInterrupt(PVOID transport) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    ULONG pending;
    if (!t)
        return FALSE;
    pending = ReadReg(t, DG_REG_IRQ_STATUS) & InterruptMask(t);
    if (!pending)
        return FALSE;
    t->InterruptCount = t->InterruptCount + 1;
    t->Diagnostics[2] = t->InterruptCount;
    WriteReg(t, DG_REG_IRQ_STATUS, pending);
    if (pending & DG_IRQ_COMPLETION) {
        InterlockedIncrement(&t->DpcReferences);
        if (!KeInsertQueueDpc(&t->Dpc, NULL, NULL))
            InterlockedDecrement(&t->DpcReferences);
    }
    if (pending & DG_IRQ_DISPLAY_TIMING) {
        InterlockedIncrement(&t->DpcReferences);
        if (!KeInsertQueueDpc(&t->TimingDpc, NULL, NULL))
            InterlockedDecrement(&t->DpcReferences);
    }
    if (pending & DG_IRQ_GL_COMPLETION) {
        InterlockedIncrement(&t->DpcReferences);
        if (!KeInsertQueueDpc(&t->GlDpc, NULL, NULL))
            InterlockedDecrement(&t->DpcReferences);
    }
    return TRUE;
}

BOOLEAN DgTransportSubmit(PVOID transport, const PVOID commands, ULONG count) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    LARGE_INTEGER timeout;
    NTSTATUS status;
    ULONG sequence;
    if (!t || !count || count > DG_MAX_COMMANDS || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;
    if (ReadReg(t, DG_REG_STATUS) & DG_STATUS_BUSY)
        return FALSE;
    sequence = ++t->Sequence;
    if (!sequence)
        sequence = ++t->Sequence;
    t->Diagnostics[7] = sequence;
    RtlCopyMemory(t->Commands, commands, count * DG_COMMAND_BYTES);
    KeResetEvent(&t->Completion);
    WriteReg(t, DG_REG_IRQ_STATUS, DG_IRQ_COMPLETION);
    if (t->BatchCount != count) {
        WriteReg(t, DG_REG_BATCH_COUNT, count);
        t->BatchCount = count;
    }
    WriteReg(t, DG_REG_SUBMIT_SEQUENCE, sequence);
    /* x86 is cache-coherent; the compiler barrier orders batch stores before
     * the uncached MMIO doorbell. Host snapshots the batch at this write. */
    __asm__ __volatile__("" ::: "memory");
    /* Synchronous inline work has already published DONE at MMIO return.
     * Suppress its redundant IRQ when supported; continuations retain the
     * normal event/IRQ path and therefore never need an idle polling loop. */
    WriteReg(t, DG_REG_SUBMIT, t->NativeSubmitFlags);
    timeout.QuadPart = -20000000LL; /* two seconds, never a polling interval */
    for (;;) {
        ULONG device_status = ReadReg(t, DG_REG_STATUS);
        if (!(device_status & DG_STATUS_BUSY) && ReadReg(t, DG_REG_COMPLETED_SEQUENCE) == sequence)
            return (device_status & DG_STATUS_ERROR) == 0;
        status = KeWaitForSingleObject(&t->Completion, Executive, KernelMode, FALSE, &timeout);
        if (status != STATUS_SUCCESS) {
            ULONG device_status = ReadReg(t, DG_REG_STATUS);
            ++t->TimeoutCount;
            t->Diagnostics[8] = t->TimeoutCount;
            t->Diagnostics[9] = status;
            t->Diagnostics[10] = device_status;
            DgRecordStatus(L"DreamGPUTimeoutCount", t->TimeoutCount);
            DgRecordStatus(L"DreamGPUInterruptCount", t->InterruptCount);
            DgRecordStatus(L"DreamGPUDpcCount", t->DpcCount);
            DgRecordStatus(L"DreamGPUWaitStatus", status);
            DgRecordStatus(L"DreamGPULastDeviceStatus", device_status);
            /* An IRQ or DPC may have been lost after the device finished.
             * A completed copy must succeed, never be replayed or reset. */
            if (!(device_status & DG_STATUS_BUSY) &&
                ReadReg(t, DG_REG_COMPLETED_SEQUENCE) == sequence) {
                WriteReg(t, DG_REG_IRQ_STATUS, DG_IRQ_COMPLETION);
                WriteReg(t, DG_REG_IRQ_ENABLE, InterruptMask(t));
                return (device_status & DG_STATUS_ERROR) == 0;
            }
            /* Global RESET also discards native GL/canvas state. Once the
             * GL channel exists, recovery must be an explicit repaint/reset. */
            if (!t->GlCommands)
                DgTransportReset(t);
            return FALSE;
        }
        /* A late DPC from the previous command may wake this submission.
         * Reset before checking again so it cannot become a busy loop. */
        KeResetEvent(&t->Completion);
    }
}

BOOLEAN DgTransportEnter(PVOID transport) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    return t && KeGetCurrentIrql() == PASSIVE_LEVEL &&
           KeWaitForSingleObject(&t->GlMutex, Executive, KernelMode, FALSE, NULL) == STATUS_SUCCESS;
}

VOID DgTransportLeave(PVOID transport) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    KeReleaseMutex(&t->GlMutex, FALSE);
}

static ULONG DesktopSubmit(DG_TRANSPORT *, const DG_COMMAND *, ULONG);
static ULONG NTAPI KernelOwner(PVOID, ULONG);
static ULONG NTAPI KernelPresent(PVOID, const DG_KERNEL_PRESENT_REQUEST *);
static ULONG NTAPI KernelCohere(PVOID, ULONG);
static ULONG NTAPI KernelCursor(PVOID, const DG_KERNEL_CURSOR_REQUEST *);

static ULONG NTAPI DgKernelSubmit(PVOID transport, const DG_COMMAND *commands, ULONG count) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    BOOLEAN result;
    if (!DgTransportEnter(t))
        return FALSE;
    /* This shares the miniport's mode/GL/process-exit serialization. The
     * PDEV's open video handle pins the miniport until GDI releases it. */
    result = t->DesktopActive ? DesktopSubmit(t, commands, count)
                              : DgTransportSubmit(t, (PVOID)commands, count);
    DgTransportLeave(t);
    return result;
}

BOOLEAN DgTransportKernel(PVOID transport, PVOID input, ULONG input_bytes, PVOID output,
                          ULONG output_bytes) {
    DG_KERNEL_INTERFACE channel;
    if (!transport || !DriverReady || !PortDeviceControl || !input || !output ||
        input_bytes != sizeof(ULONG) || *(ULONG *)input != DG_KERNEL_VERSION ||
        output_bytes < sizeof(channel))
        return FALSE;
    channel.Version = DG_KERNEL_VERSION;
    channel.Size = sizeof(channel);
    channel.Submit = DgKernelSubmit;
    channel.Context = transport;
    channel.Owner = KernelOwner;
    channel.Present = KernelPresent;
    channel.Cohere = KernelCohere;
    channel.DesktopActive = &((DG_TRANSPORT *)transport)->DesktopActive;
    channel.Cursor = KernelCursor;
    channel.WindowCapabilities =
        ReadReg((DG_TRANSPORT *)transport, DG_REG_CAPS) & DG_CAP_GL_FRONT_BUFFERS
            ? DG_WINDOW_CAP_FRONT_ONLY
            : 0;
    RtlCopyMemory(output, &channel, sizeof(channel));
    return TRUE;
}

static NTSTATUS NTAPI DeviceControl(PDEVICE_OBJECT device, PIRP irp) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_VIDEO_DG_TIMING) {
        DG_TRANSPORT *t;
        ULONG bytes = 0;
        KIRQL irql;
        KeAcquireSpinLock(&CallbackLock, &irql);
        t = DriverReady ? AdapterTransport : NULL;
        if (t && !CallbackCount++)
            KeResetEvent(&CallbacksIdle);
        KeReleaseSpinLock(&CallbackLock, irql);
        if (t) {
            bytes = DgTransportTiming(t, irp->AssociatedIrp.SystemBuffer,
                                      stack->Parameters.DeviceIoControl.InputBufferLength,
                                      irp->AssociatedIrp.SystemBuffer,
                                      stack->Parameters.DeviceIoControl.OutputBufferLength);
            KeAcquireSpinLock(&CallbackLock, &irql);
            if (!--CallbackCount)
                KeSetEvent(&CallbacksIdle, IO_NO_INCREMENT, FALSE);
            KeReleaseSpinLock(&CallbackLock, irql);
        }
        /* Bypass VideoPort's mode/GL serialization while this thread sleeps. */
        irp->IoStatus.Status = bytes ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
        irp->IoStatus.Information = bytes;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return bytes ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    }
    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_VIDEO_DG_KERNEL) {
        DG_TRANSPORT *t = AdapterTransport;
        if (t) {
            t->Diagnostics[26] = irp->RequestorMode;
            t->Diagnostics[27] = ExGetPreviousMode();
        }
        if (irp->RequestorMode != KernelMode) {
            if (t)
                IncrementDiagnostic(&t->Diagnostics[28]);
            irp->IoStatus.Status = STATUS_ACCESS_DENIED;
            irp->IoStatus.Information = 0;
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return STATUS_ACCESS_DENIED;
        }
    }
    return PortDeviceControl(device, irp);
}

/* All GL functions below run at PASSIVE_LEVEL under GlMutex. The separately
 * serialized native 2D stream keeps its existing event and DMA page. */
static ULONG FetchFunctionWords(void *context, ULONG function) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)context;
    WriteReg(t, DG_GL_REG_QUERY_FUNCTION, function);
    return ReadReg(t, DG_GL_REG_FUNCTION_WORDS);
}

static ULONG FunctionWords(void *context, ULONG function) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)context;
    return DgGlSignatureLookup(t->GlSignatures, t->GlSignatureGeneration, function,
                               FetchFunctionWords, t);
}

static ULONG ReadGlRegister(void *context, ULONG reg) {
    return ReadReg((DG_TRANSPORT *)context, reg);
}

static VOID WriteGlRegister(void *context, ULONG reg, ULONG value) {
    WriteReg((DG_TRANSPORT *)context, reg, value);
}

static ULONG SubmitGl(DG_TRANSPORT *t, ULONG bytes, DG_ESCAPE_REPLY *reply) {
    ULONG sequence, device_status, generation, error;
    LARGE_INTEGER timeout;
    NTSTATUS status;
    if (t->GlFaulted)
        return DG_ESCAPE_STOPPED;
    if (ReadReg(t, DG_GL_REG_STATUS) & DG_STATUS_BUSY)
        return DG_ESCAPE_STOPPED;
    /* User validation or the kernel record builder already captured/stamped
     * this generation under GlMutex. Re-reading it could mismatch the packet
     * after an external reset; the native generation gate remains final. */
    generation = ((ULONG *)t->GlCommands)[7];
    if (!DgGlRegistersRefresh(&t->GlRegisters, generation, 0, ReadGlRegister, t) ||
        !DgGlLimitsRefresh(&t->GlLimits, generation, 0, ReadGlRegister, t) ||
        bytes > t->GlLimits.MaxBytes)
        return DG_ESCAPE_UNSUPPORTED;
    sequence = ++t->GlSequence;
    if (!sequence)
        sequence = ++t->GlSequence;
    t->Diagnostics[23] = sequence;
    KeResetEvent(&t->GlCompletion);
    DgGlProgramCommands(&t->GlRegisters, t->GlAddress.LowPart, t->GlAddress.HighPart,
                        WriteGlRegister, t);
    WriteReg(t, DG_GL_REG_BYTES, bytes);
    WriteReg(t, DG_GL_REG_SEQUENCE, sequence);
    WriteReg(t, DG_GL_REG_SUBMIT, 1);
    timeout.QuadPart = -20000000LL;
    for (;;) {
        device_status = ReadReg(t, DG_GL_REG_STATUS);
        if (!(device_status & DG_STATUS_BUSY) && ReadReg(t, DG_GL_REG_COMPLETED) == sequence)
            break;
        status = KeWaitForSingleObject(&t->GlCompletion, Executive, KernelMode, FALSE, &timeout);
        if (status != STATUS_SUCCESS) {
            device_status = ReadReg(t, DG_GL_REG_STATUS);
            if (!(device_status & DG_STATUS_BUSY) && ReadReg(t, DG_GL_REG_COMPLETED) == sequence)
                break;
            /* Never destroy another process's context or a newer GPU desktop
             * by resetting the entire device on one client's failed wait. */
            t->GlFaulted = TRUE;
            IncrementDiagnostic(&t->Diagnostics[24]);
            return DG_ESCAPE_TIMEOUT;
        }
        KeResetEvent(&t->GlCompletion);
    }
    error = DgGlCompletionError(device_status, ReadGlRegister, t);
    if (reply) {
        reply->DeviceError = error;
        reply->CompletedSequence = sequence;
    }
    t->Diagnostics[25] = error;
    return device_status & DG_STATUS_ERROR ? DG_ESCAPE_HOST : DG_ESCAPE_OK;
}

/* The display calls these only while GDI owns the primary/window lock. This
 * mutex also excludes user GL, process exit, and miniport mode changes. */
static BOOLEAN OwnsClient(DG_TRANSPORT *t, ULONG token) {
    ULONG i;
    for (i = 0; i < DG_ESCAPE_MAX_CLIENTS; ++i)
        if (t->Clients[i].Token == token && t->Clients[i].Process == PsGetCurrentProcess())
            return TRUE;
    return FALSE;
}

static ULONG NTAPI KernelOwner(PVOID context, ULONG token) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)context;
    ULONG result;
    if (!DgTransportEnter(t))
        return FALSE;
    result = OwnsClient(t, token) && !t->GlFaulted ? (ULONG)(ULONG_PTR)PsGetCurrentProcess() : 0;
    DgTransportLeave(t);
    return result;
}

static ULONG *DesktopRecord(DG_TRANSPORT *t, ULONG index, ULONG op, ULONG client, ULONG drawable) {
    ULONG *words = (ULONG *)t->GlCommands + index * 24;
    RtlZeroMemory(words, 96);
    words[0] = DG_GL_DESKTOP;
    words[1] = 96;
    words[2] = client ? client : 0xffffffffUL;
    words[4] = drawable;
    words[7] = ReadReg(t, DG_REG_GENERATION);
    words[8] = op;
    return words + 8;
}

static ULONG DesktopFault(DG_TRANSPORT *t, ULONG status) {
    /* Keep authority mixed: a failed readback must never authorize stale
     * VRAM. The native fault doorbell additionally stops the guest. */
    t->GlFaulted = TRUE;
    t->Diagnostics[29] = status;
    DgRecordStatus(L"DreamGPUDesktopFault", status);
    WriteReg(t, DG_GL_REG_FAULT_STOP,
             status == DG_ESCAPE_TIMEOUT   ? DG_GL_FAULT_DRIVER_TIMEOUT
             : status == DG_ESCAPE_INVALID ? DG_GL_FAULT_DRIVER_VALIDATE
                                           : DG_GL_FAULT_DRIVER_INTERNAL);
    /* vm_stop may finish the current translated block. Never return through
     * the void GDI sync DDI: sleep without a timeout until the required reset
     * destroys this guest kernel. No host or guest CPU is busy-polled. */
    KeWaitForSingleObject(&t->DesktopStopped, Executive, KernelMode, FALSE, NULL);
    return FALSE;
}

/* The cursor plane has its own synchronous doorbell. It never initializes
 * GL, dirties the primary, raises an IRQ, or waits for a host rendering loop.
 * GDI does not opt into ASYNCMOVE, so these kernel DDIs run at PASSIVE_LEVEL. */
static ULONG NTAPI KernelCursor(PVOID transport, const DG_KERNEL_CURSOR_REQUEST *r) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    ULONG bytes = 0, result = FALSE, status;
    PHYSICAL_ADDRESS highest;
    if (!t || !r || !t->CursorSupported ||
        (r->Operation != DG_CURSOR_SHAPE && r->Operation != DG_CURSOR_MOVE) ||
        (r->Flags & ~DG_CURSOR_FLAGS_MASK) || !DgTransportEnter(t))
        return FALSE;
    if (r->Operation == DG_CURSOR_SHAPE) {
        if (!r->Pixels || !r->Width || !r->Height || r->Width > DG_CURSOR_MAX_DIMENSION ||
            r->Height > DG_CURSOR_MAX_DIMENSION || r->HotX >= r->Width || r->HotY >= r->Height ||
            (r->Format != DG_CURSOR_ARGB_PREMULTIPLIED && r->Format != DG_CURSOR_AND_XOR))
            goto done;
        bytes = r->Width * r->Height * DG_CURSOR_PIXEL_BYTES;
        if (!t->CursorPixels) {
            highest.QuadPart = 0xffffffffULL;
            t->CursorPixels = MmAllocateContiguousMemory(DG_CURSOR_MAX_BYTES, highest);
            if (!t->CursorPixels)
                goto done; /* recoverable shape fallback */
            t->CursorAddress = MmGetPhysicalAddress(t->CursorPixels);
        }
        RtlCopyMemory(t->CursorPixels, r->Pixels, bytes);
        WriteReg(t, DG_CURSOR_REG_ADDR_LO, t->CursorAddress.LowPart);
        WriteReg(t, DG_CURSOR_REG_ADDR_HI, t->CursorAddress.HighPart);
        WriteReg(t, DG_CURSOR_REG_BYTES, bytes);
        WriteReg(t, DG_CURSOR_REG_WIDTH, r->Width);
        WriteReg(t, DG_CURSOR_REG_HEIGHT, r->Height);
        WriteReg(t, DG_CURSOR_REG_HOT_X, r->HotX);
        WriteReg(t, DG_CURSOR_REG_HOT_Y, r->HotY);
        WriteReg(t, DG_CURSOR_REG_FORMAT, r->Format);
    }
    WriteReg(t, DG_CURSOR_REG_X, (ULONG)r->X);
    WriteReg(t, DG_CURSOR_REG_Y, (ULONG)r->Y);
    WriteReg(t, DG_CURSOR_REG_FLAGS, r->Flags);
    if (!++t->CursorSequence)
        ++t->CursorSequence;
    WriteReg(t, DG_CURSOR_REG_SEQUENCE, t->CursorSequence);
    KeMemoryBarrier();
    WriteReg(t, DG_CURSOR_REG_SUBMIT, r->Operation);
    status = ReadReg(t, DG_CURSOR_REG_STATUS);
    if (ReadReg(t, DG_CURSOR_REG_COMPLETED) != t->CursorSequence || !(status & DG_STATUS_DONE) ||
        (status & DG_STATUS_ERROR)) {
        IncrementDiagnostic(&t->Diagnostics[40]);
        /* An accepted hardware pointer may not silently fail its void move
         * DDI. This is a native ABI violation, not an unsupported shape. */
        DesktopFault(t, DG_ESCAPE_STOPPED);
        goto done;
    }
    IncrementDiagnostic(&t->Diagnostics[r->Operation == DG_CURSOR_SHAPE ? 38 : 39]);
    result = TRUE;
done:
    DgTransportLeave(t);
    return result;
}

static ULONG CohereLocked(DG_TRANSPORT *t) {
    ULONG *r, status;
    if (!t->DesktopActive)
        return TRUE;
    if (t->GlFaulted)
        return DesktopFault(t, DG_ESCAPE_STOPPED);
    r = DesktopRecord(t, 0, DG_DESKTOP_READBACK, 0, 0);
    r[4] = t->DesktopWidth;
    r[5] = t->DesktopHeight;
    r[11] = t->DesktopStride;
    status = SubmitGl(t, 96, NULL);
    if (status != DG_ESCAPE_OK)
        return DesktopFault(t, status);
    r = DesktopRecord(t, 0, DG_DESKTOP_RETURN, 0, 0);
    r[4] = t->DesktopWidth;
    r[5] = t->DesktopHeight;
    r[11] = t->DesktopStride;
    status = SubmitGl(t, 96, NULL);
    if (status != DG_ESCAPE_OK)
        return DesktopFault(t, status);
    t->DesktopActive = 0;
    IncrementDiagnostic(&t->Diagnostics[31]);
    return TRUE;
}

static ULONG NTAPI KernelCohere(PVOID context, ULONG reason) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)context;
    ULONG result;
    if (!DgTransportEnter(t))
        return FALSE;
    if (t->DesktopActive && reason <= DG_COHERE_MODE)
        IncrementDiagnostic(&t->Diagnostics[33 + reason]);
    result = CohereLocked(t);
    DgTransportLeave(t);
    return result;
}

static BOOLEAN PrimaryRect(ULONG x, ULONG y, ULONG w, ULONG h, ULONG width, ULONG height) {
    return w && h && x <= width && y <= height && w <= width - x && h <= height - y;
}

static ULONG DesktopSubmit(DG_TRANSPORT *t, const DG_COMMAND *commands, ULONG count) {
    ULONG i, *r, x, y, sx, sy;
    ULONGLONG work = 0;
    if (t->GlFaulted || !count || count > DG_MAX_COMMANDS)
        return FALSE;
    /* Validate the entire batch before publishing any operation. */
    for (i = 0; i < count; ++i) {
        const DG_COMMAND *c = &commands[i];
        if (c->Bpp != 4 || c->Reserved || c->DestinationStride != t->DesktopStride ||
            (c->Destination & 3))
            return DesktopFault(t, DG_ESCAPE_INVALID);
        x = c->Destination % t->DesktopStride / 4;
        y = c->Destination / t->DesktopStride;
        if (!PrimaryRect(x, y, c->Width, c->Height, t->DesktopWidth, t->DesktopHeight))
            return DesktopFault(t, DG_ESCAPE_INVALID);
        if (c->Opcode == DG_CMD_COPY) {
            sx = c->Source % t->DesktopStride / 4;
            sy = c->Source / t->DesktopStride;
            if (c->SourceStride != t->DesktopStride || (c->Source & 3) ||
                !PrimaryRect(sx, sy, c->Width, c->Height, t->DesktopWidth, t->DesktopHeight))
                return DesktopFault(t, DG_ESCAPE_INVALID);
        } else if (c->Opcode != DG_CMD_FILL)
            return DesktopFault(t, DG_ESCAPE_INVALID);
        work += (ULONGLONG)c->Width * c->Height * 4;
        if (work > DG_DESKTOP_MAX_BYTES)
            return DesktopFault(t, DG_ESCAPE_RESOURCES);
    }
    for (i = 0; i < count; ++i) {
        const DG_COMMAND *c = &commands[i];
        r = DesktopRecord(t, i, c->Opcode == DG_CMD_FILL ? DG_DESKTOP_FILL : DG_DESKTOP_COPY, 0, 0);
        r[2] = c->Destination % t->DesktopStride / 4;
        r[3] = c->Destination / t->DesktopStride;
        r[4] = c->Width;
        r[5] = c->Height;
        r[6] = c->Opcode == DG_CMD_FILL ? c->Color : c->Source % t->DesktopStride / 4;
        r[7] = c->Opcode == DG_CMD_FILL ? 0 : c->Source / t->DesktopStride;
    }
    i = SubmitGl(t, count * 96, NULL);
    return i == DG_ESCAPE_OK ? TRUE : DesktopFault(t, i);
}

static ULONG SubmitWindowPresent(DG_TRANSPORT *t, const DG_KERNEL_PRESENT_REQUEST *request,
                                 ULONG flags) {
    ULONG *r = (ULONG *)t->GlCommands;
    RtlZeroMemory(r, 40);
    r[0] = DG_GL_PRESENT;
    r[1] = 40;
    r[2] = request->Client;
    r[3] = request->Context;
    r[4] = request->Drawable;
    r[5] = flags | DG_GL_PRESENT_BOUNDED;
    r[7] = ReadReg(t, DG_REG_GENERATION);
    /* This immutable WNDOBJ snapshot is rechecked against the actual native
     * drawable before exchange/export, closing the user resize/DDI gap. */
    r[8] = request->WindowWidth;
    r[9] = request->WindowHeight;
    return SubmitGl(t, 40, NULL);
}

static ULONG NTAPI KernelPresent(PVOID context, const DG_KERNEL_PRESENT_REQUEST *request) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)context;
    ULONG i, count = 0, status = DG_ESCAPE_INVALID, *r;
    ULONG slot, epoch_lo, epoch_hi, frame_lo, frame_hi;
    ULONGLONG work = 0;
    if (!request || !DgTransportEnter(t))
        return status;
    if (!OwnsClient(t, request->Client)) {
        status = DG_ESCAPE_OWNER;
        goto done;
    }
    if (t->GlFaulted) {
        status = DG_ESCAPE_STOPPED;
        goto done;
    }
    if (request->Flags & ~DG_WINDOW_PRESENT_FLAGS_MASK)
        goto done;
    if (!(t->GlPresentCaps & DG_CAP_GL_PRESENT_BOUNDS)) {
        status = DG_ESCAPE_UNSUPPORTED;
        goto done;
    }
    if ((request->Flags & DG_WINDOW_PRESENT_FRONT_ONLY) &&
        !(t->GlPresentCaps & DG_CAP_GL_FRONT_BUFFERS)) {
        status = DG_ESCAPE_UNSUPPORTED;
        goto done;
    }
    if (!request->Context || !request->Drawable || request->Count > DG_WINDOW_MAX_CLIPS ||
        !request->Width || !request->Height || request->Width > DG_GL_MAX_DIMENSION ||
        request->Height > DG_GL_MAX_DIMENSION || request->Stride < request->Width * 4 ||
        (request->Stride & 3) || !request->WindowWidth || !request->WindowHeight ||
        request->WindowWidth > DG_GL_MAX_DIMENSION || request->WindowHeight > DG_GL_MAX_DIMENSION ||
        (ULONGLONG)request->Stride * request->Height > DG_DESKTOP_MAX_BYTES)
        goto done;
    for (i = 0; i < request->Count; ++i) {
        const DG_WINDOW_RECT *clip = &request->Clips[i];
        if (clip->Left < 0 || clip->Top < 0 || clip->Left >= clip->Right ||
            clip->Top >= clip->Bottom || clip->Right > (LONG)request->Width ||
            clip->Bottom > (LONG)request->Height || clip->Left < request->WindowX ||
            clip->Top < request->WindowY ||
            (LONGLONG)clip->Right > (LONGLONG)request->WindowX + request->WindowWidth ||
            (LONGLONG)clip->Bottom > (LONGLONG)request->WindowY + request->WindowHeight)
            goto done;
        work += (ULONGLONG)(clip->Right - clip->Left) * (clip->Bottom - clip->Top) * 4;
        if (work > DG_DESKTOP_MAX_BYTES)
            goto done;
    }
    if (!request->Count) {
        status = DG_ESCAPE_OK;
        if (!(request->Flags & DG_WINDOW_PRESENT_FRONT_ONLY) &&
            (t->GlPresentCaps & DG_CAP_GL_FRONT_BUFFERS)) {
            /* An occluded SwapBuffers still changes logical FRONT/BACK.
             * No export slot or desktop pixels are needed for this step. */
            status = SubmitWindowPresent(t, request, DG_GL_PRESENT_NO_EXPORT);
        }
        goto done;
    }
    if (t->DesktopActive &&
        (t->DesktopWidth != request->Width || t->DesktopHeight != request->Height ||
         t->DesktopStride != request->Stride))
        goto done;
    /* Export first: an invalid/deleted user context cannot enter mixed mode.
     * The export stays retained until the last visible clip consumes it. */
    status = SubmitWindowPresent(
        t, request,
        DG_GL_PRESENT_RETAIN |
            ((request->Flags & DG_WINDOW_PRESENT_FRONT_ONLY) ? DG_GL_PRESENT_FRONT_ONLY : 0));
    if (status != DG_ESCAPE_OK)
        goto done;
    slot = ReadReg(t, DG_GL_REG_PRESENT_SLOT);
    epoch_lo = ReadReg(t, DG_GL_REG_PRESENT_EPOCH_LO);
    epoch_hi = ReadReg(t, DG_GL_REG_PRESENT_EPOCH_HI);
    frame_lo = ReadReg(t, DG_GL_REG_PRESENT_FRAME_LO);
    frame_hi = ReadReg(t, DG_GL_REG_PRESENT_FRAME_HI);
    if (!t->DesktopActive) {
        r = DesktopRecord(t, 0, DG_DESKTOP_SEED, 0, 0);
        r[4] = request->Width;
        r[5] = request->Height;
        r[11] = request->Stride;
        status = SubmitGl(t, 96, NULL);
        if (status != DG_ESCAPE_OK) {
            DesktopFault(t, status);
            goto done;
        }
        t->DesktopWidth = request->Width;
        t->DesktopHeight = request->Height;
        t->DesktopStride = request->Stride;
        t->DesktopActive = 1;
        IncrementDiagnostic(&t->Diagnostics[30]);
    }
    for (i = 0; i < request->Count; ++i) {
        const DG_WINDOW_RECT *clip = &request->Clips[i];
        r = DesktopRecord(t, count++, DG_DESKTOP_BLIT, request->Client, request->Drawable);
        r[1] = i + 1 == request->Count ? DG_DESKTOP_BLIT_FINAL : 0;
        r[2] = clip->Left;
        r[3] = clip->Top;
        r[4] = clip->Right - clip->Left;
        r[5] = clip->Bottom - clip->Top;
        r[6] = clip->Left - request->WindowX;
        r[7] = clip->Top - request->WindowY;
        r[10] = slot;
        r[12] = epoch_lo;
        r[13] = epoch_hi;
        r[14] = frame_lo;
        r[15] = frame_hi;
        if (count == DG_DESKTOP_MAX_RECORDS || i + 1 == request->Count) {
            status = SubmitGl(t, count * 96, NULL);
            if (status != DG_ESCAPE_OK) {
                DesktopFault(t, status);
                goto done;
            }
            count = 0;
        }
    }
    IncrementDiagnostic(&t->Diagnostics[32]);
done:
    DgTransportLeave(t);
    return status;
}

static VOID CloseClient(DG_TRANSPORT *t, DG_CLIENT *client) {
    ULONG *words = (ULONG *)t->GlCommands;
    if (!client->Process)
        return;
    RtlZeroMemory(words, DG_GL_HEADER_BYTES);
    words[0] = DG_GL_CLOSE_CLIENT;
    words[1] = DG_GL_HEADER_BYTES;
    words[2] = client->Token;
    words[7] = ReadReg(t, DG_REG_GENERATION);
    SubmitGl(t, DG_GL_HEADER_BYTES, NULL);
    /* Tokens are never reused, even if a fault prevented host retirement.
     * The host resource cap bounds any retained objects until device reset. */
    ObDereferenceObject(client->Process);
    RtlZeroMemory(client, sizeof(*client));
    DecrementDiagnostic(&t->Diagnostics[20]);
}

static VOID NTAPI ProcessNotify(HANDLE parent, HANDLE process, BOOLEAN create) {
    DG_TRANSPORT *t;
    KIRQL irql;
    ULONG i;
    (void)parent;
    if (create)
        return;
    KeAcquireSpinLock(&CallbackLock, &irql);
    t = AdapterTransport;
    if (t) {
        if (!CallbackCount++)
            KeResetEvent(&CallbacksIdle);
    }
    KeReleaseSpinLock(&CallbackLock, irql);
    if (!t)
        return;
    KeWaitForSingleObject(&t->GlMutex, Executive, KernelMode, FALSE, NULL);
    for (i = 0; i < DG_ESCAPE_MAX_CLIENTS; ++i)
        if (t->Clients[i].Process && t->Clients[i].ProcessId == process) {
            IncrementDiagnostic(&t->Diagnostics[21]);
            CloseClient(t, &t->Clients[i]);
        }
    KeReleaseMutex(&t->GlMutex, FALSE);
    KeAcquireSpinLock(&CallbackLock, &irql);
    if (!--CallbackCount)
        KeSetEvent(&CallbacksIdle, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&CallbackLock, irql);
}

VOID DgDriverInitialize(PVOID driver_object) {
    PDRIVER_OBJECT driver = (PDRIVER_OBJECT)driver_object;
    KeInitializeSpinLock(&CallbackLock);
    KeInitializeEvent(&CallbacksIdle, NotificationEvent, TRUE);
    /* Preserve VideoPort's teardown, which disconnects the shared interrupt
     * before the adapter's DPC context and DMA storage may be freed. */
    PortUnload = driver->DriverUnload;
    PortDeviceControl = driver->MajorFunction[IRP_MJ_DEVICE_CONTROL];
    if (PortUnload && PortDeviceControl) {
        driver->DriverUnload = DriverUnload;
        driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;
        DriverReady = TRUE;
    }
}

static VOID NTAPI DriverUnload(PDRIVER_OBJECT driver) {
    DG_TRANSPORT *t;
    KIRQL irql;
    ULONG i;
    DriverReady = FALSE;
    KeAcquireSpinLock(&CallbackLock, &irql);
    t = AdapterTransport;
    AdapterTransport = NULL;
    KeReleaseSpinLock(&CallbackLock, irql);
    if (t && t->GlNotifyRegistered)
        PsSetCreateProcessNotifyRoutine(ProcessNotify, TRUE);
    if (t)
        KeWaitForSingleObject(&CallbacksIdle, Executive, KernelMode, FALSE, NULL);
    if (t && t->GlNotifyRegistered) {
        KeWaitForSingleObject(&t->GlMutex, Executive, KernelMode, FALSE, NULL);
        for (i = 0; i < DG_ESCAPE_MAX_CLIENTS; ++i)
            CloseClient(t, &t->Clients[i]);
        KeReleaseMutex(&t->GlMutex, FALSE);
    }
    if (t) {
        ULONG generation = ReadReg(t, DG_REG_GENERATION);
        /* QUERY is the only GL operation which can DMA into driver-owned
         * RAM after initial submission. Fence even a timed-out query before
         * releasing that page: engine reset changes generation and clears
         * its pending result destination synchronously under QEMU's BQL. */
        WriteReg(t, DG_REG_RESET, 1);
        if (t->GlResult && ReadReg(t, DG_REG_GENERATION) == generation) {
            /* A latched coherence fault deliberately refuses engine reset.
             * Never pretend it cancelled a pending DMA or free its target. */
            DesktopFault(t, DG_ESCAPE_STOPPED);
            return;
        }
        WriteReg(t, DG_TIMING_REG_COMMAND, DG_TIMING_CANCEL);
        KeSetEvent(&t->TimingCompletion, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(&t->TimingMutex, Executive, KernelMode, FALSE, NULL);
        KeReleaseMutex(&t->TimingMutex, FALSE);
        WriteReg(t, DG_REG_IRQ_ENABLE, 0);
    }
    PortUnload(driver);
    /* The port has disconnected the ISR. Drain queued/running DPC ownership
     * before releasing the event and DMA storage, including on SMP guests. */
    if (t) {
        if (KeRemoveQueueDpc(&t->Dpc))
            InterlockedDecrement(&t->DpcReferences);
        if (KeRemoveQueueDpc(&t->TimingDpc))
            InterlockedDecrement(&t->DpcReferences);
        if (KeRemoveQueueDpc(&t->GlDpc))
            InterlockedDecrement(&t->DpcReferences);
        while (InterlockedCompareExchange(&t->DpcReferences, 0, 0)) {
            KeResetEvent(&t->DpcsIdle);
            if (InterlockedCompareExchange(&t->DpcReferences, 0, 0))
                KeWaitForSingleObject(&t->DpcsIdle, Executive, KernelMode, FALSE, NULL);
        }
        if (t->GlCommands)
            MmFreeContiguousMemory(t->GlCommands);
        if (t->GlResult)
            MmFreeContiguousMemory(t->GlResult);
        if (t->GlSignatures)
            ExFreePool(t->GlSignatures);
        if (t->CursorPixels)
            MmFreeContiguousMemory(t->CursorPixels);
        MmFreeContiguousMemory(t->Commands);
        if (DebugPage == t->Diagnostics)
            DebugPage = NULL;
        ExFreePool(t);
    }
}

ULONG DgTransportGl(PVOID transport, PVOID input, ULONG input_bytes, PVOID output,
                    ULONG output_bytes) {
    DG_TRANSPORT *t = (DG_TRANSPORT *)transport;
    DG_ESCAPE_REQUEST request;
    DG_ESCAPE_REPLY reply;
    DG_CLIENT *client = NULL;
    ULONG i, caps, result = DG_ESCAPE_INVALID;
    PHYSICAL_ADDRESS highest;
    if (!t || !input || !output || input_bytes < sizeof(request) || output_bytes < sizeof(reply) ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;
    /* METHOD_BUFFERED input/output may alias. Save the request, then reply
     * only after the complete input batch has been copied and consumed. */
    RtlCopyMemory(&request, input, sizeof(request));
    t->Diagnostics[17] = DriverReady;
    t->Diagnostics[18] = t->GlNotifyRegistered;
    t->Diagnostics[19] = (ULONG_PTR)PsGetCurrentProcessId();
    RtlZeroMemory(&reply, sizeof(reply));
    reply.Version = DG_ESCAPE_VERSION;
    reply.MaxResultBytes = DG_ESCAPE_LEGACY_RESULT_BYTES;
    reply.FunctionWords = (ULONG)-1;
    if (request.Version != DG_ESCAPE_VERSION || request.Reserved || request.Reserved2 ||
        request.Bytes > DG_ESCAPE_MAX_BYTES || request.Bytes != input_bytes - sizeof(request) ||
        request.ResultCapacity > DG_ESCAPE_MAX_RESULT_BYTES ||
        request.ResultCapacity > output_bytes - sizeof(reply) ||
        (request.Operation != DG_ESCAPE_SUBMIT && (request.Bytes || request.ResultCapacity)))
        goto done;
    if (!DriverReady) {
        result = DG_ESCAPE_UNSUPPORTED;
        goto done;
    }
    KeWaitForSingleObject(&t->GlMutex, Executive, KernelMode, FALSE, NULL);
    reply.Generation = ReadReg(t, DG_REG_GENERATION);
    t->GlSignatureGeneration = reply.Generation;
    if (t->GlFaulted) {
        result = DG_ESCAPE_STOPPED;
        goto unlock;
    }
    if (!DgGlRegistersRefresh(&t->GlRegisters, reply.Generation,
                              request.Operation == DG_ESCAPE_OPEN, ReadGlRegister, t) ||
        !DgGlLimitsRefresh(&t->GlLimits, reply.Generation, request.Operation == DG_ESCAPE_OPEN,
                           ReadGlRegister, t)) {
        result = DG_ESCAPE_UNSUPPORTED;
        goto unlock;
    }
    reply.MaxResultBytes = (t->GlRegisters.Caps & DG_CAP_GL_BULK_READBACK)
                               ? DG_ESCAPE_MAX_RESULT_BYTES
                               : DG_ESCAPE_LEGACY_RESULT_BYTES;
    if (request.ResultCapacity > reply.MaxResultBytes)
        goto unlock;
    reply.MaxBytes = t->GlLimits.MaxBytes;
    reply.MaxRecords = t->GlLimits.MaxRecords;
    if (request.Operation == DG_ESCAPE_OPEN) {
        if (request.Client || request.Function)
            goto unlock;
        caps = t->GlRegisters.Caps;
        t->TimingSupported = (caps & DG_CAP_DISPLAY_TIMING) &&
                             ReadReg(t, DG_TIMING_REG_VERSION) == DG_TIMING_VERSION &&
                             ReadReg(t, DG_TIMING_REG_RATES) == DG_TIMING_RATES;
        t->GlPresentCaps = caps & (DG_CAP_GL_FRONT_BUFFERS | DG_CAP_GL_PRESENT_BOUNDS);
        if (!t->GlSignatures) {
            t->GlSignatures = (DG_GL_SIGNATURE_CACHE *)ExAllocatePoolWithTag(
                PagedPool, sizeof(*t->GlSignatures), 'cGRJ');
            if (!t->GlSignatures) {
                result = DG_ESCAPE_RESOURCES;
                goto unlock;
            }
            RtlZeroMemory(t->GlSignatures, sizeof(*t->GlSignatures));
        }
        if (!t->GlCommands) {
            highest.QuadPart = 0xffffffffULL;
            t->GlCommands = MmAllocateContiguousMemory(DG_ESCAPE_MAX_BYTES, highest);
            if (!t->GlCommands) {
                result = DG_ESCAPE_RESOURCES;
                goto unlock;
            }
            t->GlAddress = MmGetPhysicalAddress(t->GlCommands);
            if (!NT_SUCCESS(PsSetCreateProcessNotifyRoutine(ProcessNotify, FALSE))) {
                MmFreeContiguousMemory(t->GlCommands);
                t->GlCommands = NULL;
                result = DG_ESCAPE_RESOURCES;
                goto unlock;
            }
            t->GlNotifyRegistered = TRUE;
            WriteReg(t, DG_REG_IRQ_ENABLE, InterruptMask(t));
        }
        for (i = 0; i < DG_ESCAPE_MAX_CLIENTS; ++i)
            if (!t->Clients[i].Process) {
                client = &t->Clients[i];
                break;
            }
        if (!client || t->NextClient == (ULONG)-1) {
            result = DG_ESCAPE_RESOURCES;
            goto unlock;
        }
        /* A restored desktop can reconnect to a newer native vocabulary
         * while retaining its old guest RAM/generation. Every new client
         * rechecks both supported and negative signatures before reuse. */
        DgGlSignatureReset(t->GlSignatures, reply.Generation);
        client->Process = PsGetCurrentProcess();
        ObReferenceObject(client->Process);
        client->ProcessId = PsGetCurrentProcessId();
        client->Token = ++t->NextClient;
        IncrementDiagnostic(&t->Diagnostics[20]);
        reply.Client = client->Token;
        result = DG_ESCAPE_OK;
        goto unlock;
    }
    for (i = 0; i < DG_ESCAPE_MAX_CLIENTS; ++i)
        if (t->Clients[i].Process == PsGetCurrentProcess() &&
            t->Clients[i].Token == request.Client) {
            client = &t->Clients[i];
            break;
        }
    if (!client) {
        result = DG_ESCAPE_OWNER;
        goto unlock;
    }
    reply.Client = client->Token;
    switch (request.Operation) {
        case DG_ESCAPE_CLOSE:
            IncrementDiagnostic(&t->Diagnostics[22]);
            CloseClient(t, client);
            result = DG_ESCAPE_OK;
            break;
        case DG_ESCAPE_QUERY:
            reply.FunctionWords = FunctionWords(t, request.Function);
            result = DG_ESCAPE_OK;
            break;
        case DG_ESCAPE_SUBMIT: {
            BOOLEAN query;
            ULONG bytes, type;
            if (request.Function)
                break;
            RtlCopyMemory(t->GlCommands, (PUCHAR)input + sizeof(request), request.Bytes);
            if (!DgValidateUserGl((ULONG *)t->GlCommands, request.Bytes, client->Token,
                                  reply.Generation, &t->GlLimits, FunctionWords, t))
                break;
            query = *(ULONG *)t->GlCommands == DG_GL_QUERY;
            if (query != (request.ResultCapacity != 0))
                break;
            if (query) {
                if (!t->GlResult) {
                    highest.QuadPart = 0xffffffffULL;
                    t->GlResult = MmAllocateContiguousMemory(DG_ESCAPE_MAX_RESULT_BYTES, highest);
                    if (!t->GlResult) {
                        result = DG_ESCAPE_RESOURCES;
                        break;
                    }
                    t->GlResultAddress = MmGetPhysicalAddress(t->GlResult);
                }
                RtlZeroMemory(t->GlResult, request.ResultCapacity);
                DgGlProgramResult(&t->GlRegisters, t->GlResultAddress.LowPart,
                                  t->GlResultAddress.HighPart, request.ResultCapacity,
                                  WriteGlRegister, t);
            }
            result = SubmitGl(t, request.Bytes, &reply);
            if (!query || result != DG_ESCAPE_OK)
                break;
            KeMemoryBarrier();
            bytes = ReadReg(t, DG_GL_REG_RESULT_BYTES);
            type = ReadReg(t, DG_GL_REG_RESULT_TYPE);
            if (!DgValidateGlResult(type, bytes, request.ResultCapacity,
                                    (const unsigned char *)t->GlResult)) {
                result = DG_ESCAPE_HOST;
                break;
            }
            /* Still under GlMutex: no later query may replace the completed DMA
             * result until its immutable bytes have been copied to this caller. */
            RtlCopyMemory((PUCHAR)output + sizeof(reply), t->GlResult, bytes);
            reply.ResultBytes = bytes;
            reply.ResultType = type;
            break;
        }
    }
unlock:
    if (t->GlSignatures) {
        t->Diagnostics[42] = t->GlSignatures->Hits;
        t->Diagnostics[43] = t->GlSignatures->Misses;
        t->Diagnostics[44] = t->GlSignatures->Invalidations;
    }
    KeReleaseMutex(&t->GlMutex, FALSE);
done:
    reply.Status = result;
    RtlCopyMemory(output, &reply, sizeof(reply));
    return sizeof(reply) + reply.ResultBytes;
}

} /* extern C */
