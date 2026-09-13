/* SPDX-License-Identifier: GPL-2.0-or-later
 * Freestanding C++23: checked views and move-only page owners. The only OS
 * boundary is the explicit cdecl VMM service table. No runtime or allocator
 * other than the supplied page services; no callback retains caller storage.
 */
#include <stdint.h>
typedef uint32_t DWORD;
#include "dg-memory.h"
#include "../include/ownership.hpp"
static_assert(sizeof(DWORD) == 4);
#if defined(__i386__)
static_assert(sizeof(DG9_USER_LOCK) == 12 && sizeof(DG9_MEMORY_SERVICES) == 24);
#endif

extern "C" int DREAMGPU_CDECL Dg9UserSpan(DWORD address, DWORD bytes, DG9_USER_SPAN *span) {
    if (!span || address < 0x10000UL || address >= 0x80000000UL || !bytes ||
        bytes > DG9_MEMORY_MAX_BYTES || bytes > 0x80000000UL - address)
        return 0;
    const DWORD last = address + bytes - 1;
    *span = {address >> 12, (last >> 12) - (address >> 12) + 1, address & 4095, bytes};
    return span->Pages <= DG9_MEMORY_MAX_PAGES;
}
extern "C" int DREAMGPU_CDECL Dg9UserPtes(const DWORD *ptes, DWORD count, int write) {
    if (!ptes || !count || count > DG9_MEMORY_MAX_PAGES)
        return 0;
    const DWORD required = write ? 7 : 5;
    for (DWORD i = 0; i < count; ++i)
        if ((ptes[i] & required) != required)
            return 0;
    return 1;
}
namespace {
struct pin_deleter {
    const DG9_MEMORY_SERVICES *services;
    void *context;
    void operator()(DG9_USER_LOCK *lock) const noexcept {
        DreamGpuUnlockUser(services, context, lock);
    }
};
using pin_owner = dreamgpu::unique_owner<DG9_USER_LOCK, pin_deleter>;
struct page_deleter {
    const DG9_MEMORY_SERVICES *services;
    void *context;
    void operator()(unsigned char *address) const noexcept {
        services->Free(context, address);
    }
};
using page_owner = dreamgpu::unique_owner<unsigned char, page_deleter>;

/* Only pinned aliases or owned staging pages become views. User linear
 * addresses never become C++ pointers. The byte loop supports unaligned
 * input and output, including the returned DWORD; no typed user dereference.
 */
class byte_view final {
    unsigned char *data_;
    DWORD bytes_;

  public:
    byte_view(void *data, DWORD bytes) noexcept
        : data_(static_cast<unsigned char *>(data)), bytes_(bytes) {}
    bool copy_from(const byte_view &source) noexcept {
        if (!data_ || !source.data_ || bytes_ != source.bytes_)
            return false;
        for (DWORD i = 0; i < bytes_; ++i)
            data_[i] = source.data_[i];
        return true;
    }
};
} // namespace
extern "C" void DREAMGPU_CDECL DreamGpuUnlockUser(const DG9_MEMORY_SERVICES *services,
                                                  void *context, DG9_USER_LOCK *lock) {
    if (!lock)
        return;
    if (lock->Alias)
        services->UnlockPages(context, lock->Alias, lock->Pages);
    *lock = {};
}
extern "C" int DREAMGPU_CDECL DreamGpuLockUser(const DG9_MEMORY_SERVICES *services, void *context,
                                               DWORD address, DWORD bytes, int write,
                                               DG9_USER_LOCK *lock) {
    if (!lock)
        return 0;
    *lock = {};
    DG9_USER_SPAN span;
    if (!services || !services->CheckPages || !services->LockPages || !services->CopyPtes ||
        !services->UnlockPages || !Dg9UserSpan(address, bytes, &span) ||
        services->CheckPages(context, span.First, span.Pages) != span.Pages)
        return 0;
    lock->Alias = services->LockPages(context, span.First, span.Pages, &lock->Address);
    if (!lock->Alias) {
        *lock = {};
        return 0;
    }
    lock->Pages = span.Pages;
    pin_owner owner(lock, {services, context});
    DWORD ptes[DG9_MEMORY_MAX_PAGES];
    /* Check original user permissions after locking; the global kernel alias
     * is writable even when the corresponding user pages are read-only. */
    if (!lock->Address || (lock->Alias & 4095) ||
        !services->CopyPtes(context, span.First, span.Pages, ptes) ||
        !Dg9UserPtes(ptes, span.Pages, write))
        return 0;
    lock->Address = static_cast<unsigned char *>(lock->Address) + span.Offset;
    (void)owner.release(); // Transfer the pin to the flat ABI caller.
    return 1;
}
extern "C" DWORD DREAMGPU_CDECL DreamGpuCopyUser(const DG9_MEMORY_SERVICES *services, void *context,
                                                 DWORD input_address, DWORD output_address,
                                                 DWORD returned_address, DWORD bytes) {
    if (!services || !services->Allocate || !services->Free)
        return 87;
    DG9_USER_LOCK input{}, output{}, returned{};
    if (!DreamGpuLockUser(services, context, input_address, bytes, 0, &input))
        return 87;
    pin_owner input_owner(&input, {services, context});
    if (!DreamGpuLockUser(services, context, output_address, bytes, 1, &output))
        return 87;
    pin_owner output_owner(&output, {services, context});
    if (!DreamGpuLockUser(services, context, returned_address, sizeof(DWORD), 1, &returned))
        return 87;
    pin_owner returned_owner(&returned, {services, context});
    page_owner staging(
        static_cast<unsigned char *>(services->Allocate(context, (bytes + 4095) >> 12)),
        {services, context});
    if (!staging)
        return 8;
    const byte_view snapshot(staging.get(), bytes);
    byte_view target(output.Address, bytes);
    byte_view returned_bytes(returned.Address, sizeof(DWORD));
    /* All fallible operations finish before output changes; the snapshot
     * preserves overlapping input/output and a returned-count alias. */
    byte_view(staging.get(), bytes).copy_from(byte_view(input.Address, bytes));
    target.copy_from(snapshot);
    returned_bytes.copy_from(byte_view(&bytes, sizeof(bytes)));
    return 0;
}
