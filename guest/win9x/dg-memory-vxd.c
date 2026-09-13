/* SPDX-License-Identifier: GPL-2.0-or-later
 * Watcom VMM service/entrypoint adapters only. C++ owns checked page spans,
 * permissions, immutable copy staging and cleanup across every failure path.
 * Included after the DDB's required first locked-data address.
 */
#include "dg-memory.h"
static DWORD __declspec(naked) __cdecl Dg9CheckPages(DWORD first, DWORD count, DWORD flags) {
    VMMJmp(_PageCheckLinRange);
}
static DWORD __cdecl Dg9MemoryCheck(void *context, DWORD first, DWORD pages) {
    (void)context;
    return Dg9CheckPages(first, pages, 0);
}
static DWORD __cdecl Dg9MemoryPin(void *context, DWORD first, DWORD pages, void **address) {
    DWORD alias;
    (void)context;
    alias = _LinPageLock(first, pages, PAGEMAPGLOBAL);
    *address = (void *)alias;
    return alias;
}
static int __cdecl Dg9MemoryPtes(void *context, DWORD first, DWORD pages, DWORD *ptes) {
    (void)context;
    return _CopyPageTable(first, pages, ptes, 0) != 0;
}
static void __cdecl Dg9MemoryUnpin(void *context, DWORD alias, DWORD pages) {
    (void)context;
    _LinPageUnLock(alias >> 12, pages, PAGEMAPGLOBAL);
}
static void *__cdecl Dg9MemoryAllocate(void *context, DWORD pages) {
    (void)context;
    return (void *)_PageAllocate(pages, PG_SYS, 0, 0, 0, 0, NULL, PAGEFIXED | PAGEZEROINIT);
}
static void __cdecl Dg9MemoryFree(void *context, void *address) {
    (void)context;
    _PageFree(address, 0);
}
static const DG9_MEMORY_SERVICES dg_memory_services = {
    Dg9MemoryCheck, Dg9MemoryPin, Dg9MemoryPtes, Dg9MemoryUnpin, Dg9MemoryAllocate, Dg9MemoryFree};
static BOOL Dg9MemoryControl(struct DIOCParams *params, DWORD *result) {
    if ((params->dwIoControlCode & DG9_MEMORY_MASK) != DG9_MEMORY_BASE)
        return FALSE;
    *result = 87;
    if (params->dwIoControlCode != DG9_MEMORY_COPY || params->lpOverlapped ||
        params->cbInBuffer != params->cbOutBuffer || Get_Crit_Section_Status(NULL, NULL))
        return TRUE;
    *result = DreamGpuCopyUser(&dg_memory_services, NULL, params->lpInBuffer, params->lpOutBuffer,
                               params->lpcbBytesReturned, params->cbInBuffer);
    return TRUE;
}
