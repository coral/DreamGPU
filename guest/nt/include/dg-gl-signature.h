/* SPDX-License-Identifier: GPL-2.0-or-later
 * Host signatures are immutable within one engine generation. Keep the
 * finite protocol vocabulary cached per adapter, including unsupported and
 * zero-argument functions. Callers serialize access with the GL mutex.
 */
#ifndef DG_GL_SIGNATURE_H
#define DG_GL_SIGNATURE_H
#include "gl-funcs.h"

typedef struct {
    ULONG Generation, Hits, Misses, Invalidations;
    ULONG Words[FEnum_zzMGLFuncEnum_max];
    unsigned char Known[(FEnum_zzMGLFuncEnum_max + 7) / 8];
} DG_GL_SIGNATURE_CACHE;

static void DgGlSignatureReset(DG_GL_SIGNATURE_CACHE *cache, ULONG generation) {
    ULONG i;
    for (i = 0; i < sizeof(cache->Known); ++i)
        cache->Known[i] = 0;
    cache->Generation = generation;
    ++cache->Invalidations;
}

static ULONG DgGlSignatureLookup(DG_GL_SIGNATURE_CACHE *cache, ULONG generation, ULONG function,
                                 ULONG (*fetch)(void *, ULONG), void *context) {
    ULONG words;
    unsigned char bit;
    if (cache->Generation != generation) {
        DgGlSignatureReset(cache, generation);
    }
    /* Future appended vocabulary remains queryable without indexing beyond
     * this driver's bounded allocation. It simply cannot be retained here. */
    if (function >= FEnum_zzMGLFuncEnum_max) {
        ++cache->Misses;
        return fetch(context, function);
    }
    bit = (unsigned char)(1U << (function & 7));
    if (cache->Known[function / 8] & bit) {
        ++cache->Hits;
        return cache->Words[function];
    }
    ++cache->Misses;
    words = fetch(context, function);
    cache->Words[function] = words;
    cache->Known[function / 8] |= bit;
    return words;
}
#endif
