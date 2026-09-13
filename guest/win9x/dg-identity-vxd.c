/* SPDX-License-Identifier: GPL-2.0-or-later
 * Included after the mini-VDD's required first DDB in identity diagnostic
 * builds only. Observe the actual VWIN32 process/handle fields without copying
 * user buffers, exposing driver pointers, issuing GPU commands or sleeping.
 * Records go to the explicitly attached QEMU debug console, not a user reply.
 */
#include "dg-identity.h"
static DWORD dg_identity_sequence;
static DWORD dg_access_marker;

static void DgIdentityChar(char value) {
    _asm {
        mov al, [value]
        out 0e9h, al
    }
}
static void DgIdentityText(const char *text) {
    while (*text)
        DgIdentityChar(*text++);
}
static void DgIdentityHex(DWORD value) {
    unsigned int digit;
    for (digit = 0; digit < 8; ++digit) {
        DWORD nibble = value >> 28;
        DgIdentityChar((char)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10));
        value <<= 4;
    }
}
static void DgIdentityField(const char *name, DWORD value) {
    DgIdentityChar(' ');
    DgIdentityText(name);
    DgIdentityChar('=');
    DgIdentityHex(value);
}

static DWORD DgIdentityDisableInterrupts(void) {
    DWORD flags = 0;
    _asm {
        pushfd
        pop eax
        mov [flags], eax
        cli
    }
    return flags;
}
static void DgIdentityRestoreInterrupts(DWORD flags) {
    _asm {
        mov eax, [flags]
        push eax
        popfd
    }
}

static void DgIdentityTrace(DWORD event, DWORD vm, struct DIOCParams *params, DWORD destroyed) {
    DWORD critical_vm = 0, claims = 0, critical, context, flags;
    critical = Get_Crit_Section_Status(&critical_vm, &claims);
    context = (DWORD)_GetCurrentContext();
    /* One bounded record cannot interleave with another trace callback. There
     * is no UART-ready polling, allocator or blocking service in this region. */
    /* Every inline-assembly block preserves ESP: Watcom addresses C locals
     * relative to ESP, so a live pushfd across C calls would shift arguments. */
    flags = DgIdentityDisableInterrupts();
    DgIdentityText("DGID");
    DgIdentityField("seq", ++dg_identity_sequence);
    DgIdentityField("event", event);
    DgIdentityField("vm", vm);
    DgIdentityField("context", context);
    DgIdentityField("crit", critical);
    DgIdentityField("claims", claims);
    DgIdentityField("destroy", destroyed);
    if (params) {
        DgIdentityField("code", params->dwIoControlCode);
        DgIdentityField("tag", params->tagProcess);
        DgIdentityField("handle", params->hDevice);
        DgIdentityField("param_vm", params->VMHandle);
        DgIdentityField("in", params->cbInBuffer);
        DgIdentityField("out", params->cbOutBuffer);
    }
    DgIdentityChar('\n');
    DgIdentityRestoreInterrupts(flags);
}

/* Diagnostic-only scalar call from the display DDI. No user pointer or DMA. */
void DgAccessTrace(DWORD event, DWORD source, DWORD destination, DWORD extent) {
    DWORD flags, critical, context;
    if (!dg_access_marker)
        return;
    critical = Get_Crit_Section_Status(NULL, NULL);
    context = (DWORD)_GetCurrentContext();
    flags = DgIdentityDisableInterrupts();
    DgIdentityText("DGACC");
    DgIdentityField("marker", dg_access_marker);
    DgIdentityField("event", event);
    DgIdentityField("crit", critical);
    DgIdentityField("context", context);
    DgIdentityField("source", source);
    DgIdentityField("destination", destination);
    DgIdentityField("extent", extent);
    DgIdentityChar('\n');
    DgIdentityRestoreInterrupts(flags);
}

static BOOL DgIdentityControl(DWORD vm, struct DIOCParams *params, DWORD *result) {
    DWORD code = params->dwIoControlCode;
    if (code == DIOC_OPEN || code == DIOC_CLOSEHANDLE)
        DgIdentityTrace(code == DIOC_OPEN ? 1 : 3, vm, params, 0);
    if ((code & DG_IDENTITY_MASK) != DG_IDENTITY_BASE)
        return FALSE;
    DgIdentityTrace(2, vm, params, 0);
    if ((code & 0xff00) == 0x6000 && !params->cbInBuffer && !params->cbOutBuffer &&
        !params->lpInBuffer && !params->lpOutBuffer && !params->lpOverlapped)
        dg_access_marker = code & 0xff;
    /* No address or contents from the caller are dereferenced. */
    *result = params->cbInBuffer || params->cbOutBuffer || params->lpInBuffer ||
                      params->lpOutBuffer || params->lpOverlapped
                  ? 87
                  : 0;
    return TRUE;
}

static void DgIdentityDestroy(DWORD token) {
    DgIdentityTrace(4, 0, NULL, token);
}
