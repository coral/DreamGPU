/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t DWORD;
#include "../../../guest/win9x/dg-owner.h"

int main(void) {
    DG9_OWNER_TABLE table;
    DG9_OWNER_KEY parent = {11, 101}, child = {22, 202}, reused = {11, 303};
    DG9_HANDLE_KEY original = {11, 1001}, other = {11, 1002};
    DWORD first, second, replacement, token;
    unsigned int i;
    Dg9OwnerInitialize(&table);
    first = Dg9OwnerClaim(&table, parent, original);
    second = Dg9OwnerClaim(&table, child, original);
    assert(first && second && first != second);
    /* Handle inheritance does not transfer an existing client token's owner.
     * A child can have its own separately registered client on that handle. */
    assert(!Dg9OwnerAcquire(&table, first, child, original));
    assert(!Dg9OwnerAcquire(&table, first, parent, other));
    assert(Dg9OwnerAcquire(&table, first, parent, original));
    assert(Dg9OwnerAcquire(&table, second, child, original));
    /* Forced owner exit blocks new requests but preserves a pending driver's
     * reference until actual completion. It does not retire another process. */
    assert(Dg9OwnerRetireToken(&table, first));
    assert(!Dg9OwnerRetireToken(&table, first));
    assert(!Dg9OwnerRetireToken(&table, 0));
    Dg9OwnerRetireProcess(&table, parent.Process);
    assert(!Dg9OwnerAcquire(&table, first, parent, original));
    assert(!Dg9OwnerTakeRetired(&table));
    assert(!Dg9OwnerFinishRetired(&table, first));
    assert(Dg9OwnerRelease(&table, first));
    assert(Dg9OwnerTakeRetired(&table) == first);
    assert(!Dg9OwnerTakeRetired(&table));
    assert(!Dg9OwnerRelease(&table, first));
    assert(Dg9OwnerFinishRetired(&table, first));
    assert(!Dg9OwnerFinishRetired(&table, first));
    replacement = Dg9OwnerClaim(&table, reused, original);
    assert(replacement && replacement != first);
    assert(!Dg9OwnerAcquire(&table, first, reused, original));
    assert(!Dg9OwnerAcquire(&table, replacement, parent, original));
    /* The final OS handle close retires all clients attached to that object;
     * a job still in flight must drain before host cleanup can be claimed. */
    Dg9OwnerRetireHandle(&table, original);
    assert(!Dg9OwnerAcquire(&table, second, child, original));
    assert(Dg9OwnerTakeRetired(&table) == replacement);
    assert(Dg9OwnerFinishRetired(&table, replacement));
    assert(!Dg9OwnerTakeRetired(&table));
    assert(Dg9OwnerRelease(&table, second));
    assert(Dg9OwnerTakeRetired(&table) == second);
    assert(Dg9OwnerFinishRetired(&table, second));
    for (i = 0; i < DG9_OWNER_SLOTS; ++i)
        assert(Dg9OwnerClaim(&table, parent, original));
    assert(!Dg9OwnerClaim(&table, parent, original));
    Dg9OwnerRetireProcess(&table, parent.Process);
    for (i = 0; i < DG9_OWNER_SLOTS; ++i) {
        token = Dg9OwnerTakeRetired(&table);
        assert(token);
        assert(Dg9OwnerFinishRetired(&table, token));
    }
    table.NextToken = 0xffffffffUL;
    token = Dg9OwnerClaim(&table, parent, original);
    assert(token == 0xffffffffUL && !table.NextToken);
    assert(!Dg9OwnerClaim(&table, parent, original));
    assert(!Dg9OwnerFind(&table, 0));
    puts("Win9x ownership/rundown state tests pass; real OS identity evidence is recorded "
         "separately");
    return 0;
}
