/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;
#include "gl.h"
#include "dg-escape.h"
#include "dg-gl-signature.h"
#include "dg-gl-validate.h"

typedef struct {
    DG_GL_SIGNATURE_CACHE Cache;
    ULONG Generation, Calls, ClearWords;
} Adapter;

static ULONG Fetch(void *context, ULONG function) {
    Adapter *adapter = context;
    ++adapter->Calls;
    if (function == FEnum_glClear)
        return adapter->ClearWords;
    if (function == FEnum_glEnd)
        return 0;
    if (function == FEnum_zzMGLFuncEnum_max - 1)
        return 3;
    return (ULONG)-1;
}

static ULONG Lookup(void *context, ULONG function) {
    Adapter *adapter = context;
    return DgGlSignatureLookup(&adapter->Cache, adapter->Generation, function, Fetch, adapter);
}

int main(void) {
    Adapter a = {.Generation = 1, .ClearWords = 1};
    Adapter b = {.Generation = 1, .ClearWords = 2};
    ULONG batch[DG_GL_MAX_RECORDS * 10], old, i;
    DG_GL_LIMITS limits = {1, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    assert(sizeof(a.Cache) < 16 * 1024);
    /* A whole maximum-record scalar batch performs one host signature
     * lookup, while the real validator still checks each immutable record. */
    memset(batch, 0, sizeof(batch));
    for (i = 0; i < DG_GL_MAX_RECORDS; ++i) {
        ULONG *record = batch + i * 10;
        record[0] = DG_GL_CALL;
        record[1] = 40;
        record[3] = record[4] = 1;
        record[8] = FEnum_glClear;
        record[9] = 0x4000;
    }
    assert(DgValidateUserGl(batch, sizeof(batch), 7, a.Generation, &limits, Lookup, &a));
    assert(a.Calls == 1 && a.Cache.Hits == DG_GL_MAX_RECORDS - 1 && a.Cache.Misses == 1);
    assert(batch[2] == 7 && batch[7] == a.Generation);
    /* Valid zero and unsupported UINT_MAX each need their own known bit. */
    assert(Lookup(&a, FEnum_glEnd) == 0 && Lookup(&a, FEnum_glEnd) == 0);
    assert(Lookup(&a, FEnum_glAccum) == (ULONG)-1);
    assert(Lookup(&a, FEnum_glAccum) == (ULONG)-1);
    assert(Lookup(&a, FEnum_zzMGLFuncEnum_max - 1) == 3);
    assert(Lookup(&a, FEnum_zzMGLFuncEnum_max - 1) == 3);
    assert(a.Calls == 4);
    /* Out-of-vocabulary values never index or alias the retained table. */
    old = a.Calls;
    assert(Lookup(&a, FEnum_zzMGLFuncEnum_max) == (ULONG)-1);
    assert(Lookup(&a, (ULONG)-1) == (ULONG)-1);
    assert(Lookup(&a, (ULONG)-1) == (ULONG)-1);
    assert(a.Calls == old + 3);
    /* Another adapter cannot inherit signatures from this guest. */
    assert(Lookup(&b, FEnum_glClear) == 2 && b.Calls == 1);
    assert(Lookup(&a, FEnum_glClear) == 1);
    /* A generation transition invalidates supported AND negative entries.
     * A formerly valid scalar now advertised as data must fail validation. */
    a.Generation = limits.Generation = 2;
    a.ClearWords = DG_GL_FUNCTION_INLINE_DATA | 1;
    old = a.Calls;
    assert(!DgValidateUserGl(batch, 40, 7, a.Generation, &limits, Lookup, &a));
    assert(a.Calls == old + 1 && a.Cache.Invalidations == 2);
    assert(Lookup(&a, FEnum_glAccum) == (ULONG)-1 && a.Calls == old + 2);
    assert(Lookup(&a, FEnum_glEnd) == 0 && a.Calls == old + 3);
    /* Equality, rather than ordering, also handles generation wrap. */
    a.Generation = (ULONG)-1;
    a.ClearWords = 1;
    assert(Lookup(&a, FEnum_glClear) == 1);
    a.Generation = 1;
    a.ClearWords = 4;
    assert(Lookup(&a, FEnum_glClear) == 4 && a.Cache.Invalidations == 4);
    /* A fresh client invalidates a restored cache even when the saved
     * device generation is identical to the new host's current one. */
    old = a.Calls;
    a.ClearWords = 1;
    DgGlSignatureReset(&a.Cache, a.Generation);
    assert(Lookup(&a, FEnum_glClear) == 1 && a.Calls == old + 1);
    assert(Lookup(&a, FEnum_glAccum) == (ULONG)-1 && a.Calls == old + 2);
    assert(a.Cache.Invalidations == 5);
    puts("GL signatures: maximum records/one query, zero/unknown caching, bounded indexes, adapter "
         "isolation and generation invalidation passed");
    return 0;
}
