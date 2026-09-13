/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t DWORD;
#include "../../../guest/win9x/dg-memory.h"
int main(void) {
    DG9_USER_SPAN span;
    DWORD ptes[DG9_MEMORY_MAX_PAGES];
    unsigned i;
    assert(Dg9UserSpan(0x10000, 1, &span) && span.Pages == 1 && !span.Offset);
    assert(Dg9UserSpan(0x10fff, DG9_MEMORY_MAX_BYTES, &span) && span.Pages == 18);
    assert(Dg9UserSpan(0x10fff, 65536UL + 48UL, &span) && span.Pages == 18);
    assert(Dg9UserSpan(0x7fffffff, 1, &span));
    assert(!Dg9UserSpan(0, 4, &span));
    assert(!Dg9UserSpan(0xffff, 1, &span));
    assert(!Dg9UserSpan(0x10000, 0, &span));
    assert(!Dg9UserSpan(0x10000, DG9_MEMORY_MAX_BYTES + 1, &span));
    assert(!Dg9UserSpan(0x7fffffff, 2, &span));
    assert(!Dg9UserSpan(0xc0000000, 4, &span));
    assert(!Dg9UserSpan(0xfffffffe, 4, &span));
    for (i = 0; i < DG9_MEMORY_MAX_PAGES; ++i)
        ptes[i] = 0x1007 + (i << 12);
    assert(Dg9UserPtes(ptes, DG9_MEMORY_MAX_PAGES, 1));
    for (i = 0; i < DG9_MEMORY_MAX_PAGES; ++i) {
        DWORD old = ptes[i];
        ptes[i] &= ~2u;
        assert(Dg9UserPtes(ptes, DG9_MEMORY_MAX_PAGES, 0));
        assert(!Dg9UserPtes(ptes, DG9_MEMORY_MAX_PAGES, 1));
        ptes[i] = old & ~4u;
        assert(!Dg9UserPtes(ptes, DG9_MEMORY_MAX_PAGES, 0));
        ptes[i] = old & ~1u;
        assert(!Dg9UserPtes(ptes, DG9_MEMORY_MAX_PAGES, 0));
        ptes[i] = old;
    }
    assert(!Dg9UserPtes(ptes, 0, 0));
    assert(!Dg9UserPtes(ptes, DG9_MEMORY_MAX_PAGES + 1, 0));
    puts("Win9x checked-span and per-page permissions pass");
    return 0;
}
