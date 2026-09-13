/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native function signatures are immutable within a device generation.
 * OPEN forces refresh too, because a saved guest RAM image may meet a newer
 * host with the same saved generation. Caller holds the GL channel mutex.
 */
#ifndef DG9_FUNCTION_CACHE_H
#define DG9_FUNCTION_CACHE_H
#define DG9_FUNCTION_CACHE_SLOTS 4096
/* Keep arbitrary native ULONG results, including the unsupported sentinel. */
typedef struct {
    ULONG Generation, Initialized;
    ULONG Words[DG9_FUNCTION_CACHE_SLOTS];
    BYTE Valid[DG9_FUNCTION_CACHE_SLOTS];
} DG9_FUNCTION_CACHE;
static void Dg9FunctionCacheRefresh(DG9_FUNCTION_CACHE *cache, ULONG generation, int force) {
    ULONG i;
    if (!cache->Initialized || cache->Generation != generation || force) {
        for (i = 0; i < DG9_FUNCTION_CACHE_SLOTS; ++i)
            cache->Valid[i] = 0;
        cache->Generation = generation;
        cache->Initialized = 1;
    }
}
static ULONG Dg9FunctionCacheWords(DG9_FUNCTION_CACHE *cache, ULONG function,
                                   ULONG (*query)(void *, ULONG), void *context) {
    ULONG words;
    if (function >= DG9_FUNCTION_CACHE_SLOTS || !cache->Initialized)
        return query(context, function);
    if (cache->Valid[function])
        return cache->Words[function];
    words = query(context, function);
    cache->Words[function] = words;
    cache->Valid[function] = 1;
    return words;
}
#endif
