/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed-capacity ownership core. The Watcom adapter serializes these calls.
 * No allocation, exceptions, OS callbacks or runtime support.
 */
#include <stdint.h>
typedef uint32_t DWORD;
#include "dg-owner.h"
static_assert(sizeof(DG9_OWNER_SLOT) == 28);

extern "C" void DREAMGPU_CDECL Dg9OwnerInitialize(DG9_OWNER_TABLE *table) {
    table->NextToken = 1;
    for (auto &slot : table->Slots) {
        slot.Owner.Process = slot.Owner.Context = 0;
        slot.Handle.Tag = slot.Handle.Handle = 0;
        slot.Token = slot.References = slot.State = 0;
    }
}
extern "C" DG9_OWNER_SLOT *DREAMGPU_CDECL Dg9OwnerFind(DG9_OWNER_TABLE *table, DWORD token) {
    if (!token)
        return 0;
    for (auto &slot : table->Slots)
        if (slot.Token == token && slot.State != DG9_OWNER_FREE)
            return &slot;
    return 0;
}
extern "C" DWORD DREAMGPU_CDECL DreamGpuOwnerClaim(DG9_OWNER_TABLE *table,
                                                   const DG9_OWNER_KEY *owner_key,
                                                   const DG9_HANDLE_KEY *handle_key) {
    const auto &owner = *owner_key;
    const auto &handle = *handle_key;
    if (!table->NextToken || !owner.Process || !owner.Context || !handle.Tag || !handle.Handle)
        return 0;
    for (auto &entry : table->Slots) {
        auto *slot = &entry;
        if (slot->State != DG9_OWNER_FREE)
            continue;
        slot->Owner = owner;
        slot->Handle = handle;
        slot->Token = table->NextToken++;
        slot->References = 0;
        slot->State = DG9_OWNER_ACTIVE;
        return slot->Token;
    }
    return 0;
}
extern "C" int DREAMGPU_CDECL DreamGpuOwnerAcquire(DG9_OWNER_TABLE *table, DWORD token,
                                                   const DG9_OWNER_KEY *owner_key,
                                                   const DG9_HANDLE_KEY *handle_key) {
    const auto &owner = *owner_key;
    const auto &handle = *handle_key;
    DG9_OWNER_SLOT *slot = Dg9OwnerFind(table, token);
    if (!slot || slot->State != DG9_OWNER_ACTIVE || slot->References == 0xffffffffUL ||
        slot->Owner.Process != owner.Process || slot->Owner.Context != owner.Context ||
        slot->Handle.Tag != handle.Tag || slot->Handle.Handle != handle.Handle)
        return 0;
    ++slot->References;
    return 1;
}
extern "C" int DREAMGPU_CDECL Dg9OwnerRelease(DG9_OWNER_TABLE *table, DWORD token) {
    DG9_OWNER_SLOT *slot = Dg9OwnerFind(table, token);
    if (!slot || !slot->References || slot->State == DG9_OWNER_DRAINING)
        return 0;
    --slot->References;
    return 1;
}
extern "C" int DREAMGPU_CDECL Dg9OwnerRetireToken(DG9_OWNER_TABLE *table, DWORD token) {
    DG9_OWNER_SLOT *slot = Dg9OwnerFind(table, token);
    if (!slot || slot->State != DG9_OWNER_ACTIVE)
        return 0;
    slot->State = DG9_OWNER_RETIRING;
    return 1;
}
extern "C" void DREAMGPU_CDECL DreamGpuOwnerRetireHandle(DG9_OWNER_TABLE *table,
                                                         const DG9_HANDLE_KEY *handle_key) {
    const auto &handle = *handle_key;
    for (auto &entry : table->Slots) {
        auto *slot = &entry;
        if (slot->State == DG9_OWNER_ACTIVE && slot->Handle.Tag == handle.Tag &&
            slot->Handle.Handle == handle.Handle)
            slot->State = DG9_OWNER_RETIRING;
    }
}
extern "C" void DREAMGPU_CDECL Dg9OwnerRetireProcess(DG9_OWNER_TABLE *table, DWORD process) {
    for (auto &entry : table->Slots) {
        auto *slot = &entry;
        if (slot->State == DG9_OWNER_ACTIVE && slot->Owner.Process == process)
            slot->State = DG9_OWNER_RETIRING;
    }
}
extern "C" DWORD DREAMGPU_CDECL Dg9OwnerTakeRetired(DG9_OWNER_TABLE *table) {
    for (auto &entry : table->Slots) {
        auto *slot = &entry;
        if (slot->State == DG9_OWNER_RETIRING && !slot->References) {
            slot->State = DG9_OWNER_DRAINING;
            return slot->Token;
        }
    }
    return 0;
}
extern "C" int DREAMGPU_CDECL Dg9OwnerFinishRetired(DG9_OWNER_TABLE *table, DWORD token) {
    DG9_OWNER_SLOT *slot = Dg9OwnerFind(table, token);
    if (!slot || slot->State != DG9_OWNER_DRAINING || slot->References)
        return 0;
    slot->Owner.Process = slot->Owner.Context = 0;
    slot->Handle.Tag = slot->Handle.Handle = 0;
    slot->Token = slot->State = 0;
    return 1;
}
