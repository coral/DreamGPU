/* SPDX-License-Identifier: GPL-2.0-or-later
 * Existing MMIO limits are host capabilities, never compile-time promises.
 * Cache on the adapter; OPEN also invalidates saved RAM from an older host.
 * ULONG is an unsigned 32-bit type. Kept freestanding for boundary tests.
 */
#ifndef DG_GL_LIMITS_H
#define DG_GL_LIMITS_H
typedef struct {
    ULONG Generation, MaxBytes, MaxRecords, Initialized;
} DG_GL_LIMITS;

/* local: DG_ESCAPE_MAX_BYTES, or DG_ESCAPE_QUEUE_MAX_BYTES while the queued
 * transport supplies packet buffers of that size. The transport changes only
 * at OPEN (forced) or with the engine generation, except that a lost queue
 * must never leave a larger cached limit than its fallback buffer holds. */
static inline int DgGlLimitsRefreshLocal(DG_GL_LIMITS *limits, ULONG generation, int force,
                                         ULONG local, ULONG (*read)(void *, ULONG), void *context) {
    ULONG bytes, records;
    if (local > DG_ESCAPE_QUEUE_MAX_BYTES)
        local = DG_ESCAPE_QUEUE_MAX_BYTES;
    if (!limits->Initialized || limits->Generation != generation || force ||
        limits->MaxBytes > local) {
        bytes = read(context, DG_GL_REG_MAX_BYTES);
        records = read(context, DG_GL_REG_MAX_RECORDS);
        limits->Generation = generation;
        limits->Initialized = 1;
        limits->MaxBytes = bytes < local ? bytes : local;
        limits->MaxBytes &= ~3UL;
        limits->MaxRecords = records < DG_GL_MAX_RECORDS ? records : DG_GL_MAX_RECORDS;
        if (limits->MaxRecords > limits->MaxBytes / DG_GL_HEADER_BYTES)
            limits->MaxRecords = limits->MaxBytes / DG_GL_HEADER_BYTES;
    }
    return limits->MaxBytes >= DG_GL_QUERY_BYTES && limits->MaxRecords != 0;
}

static inline int DgGlLimitsRefresh(DG_GL_LIMITS *limits, ULONG generation, int force,
                                    ULONG (*read)(void *, ULONG), void *context) {
    return DgGlLimitsRefreshLocal(limits, generation, force, DG_ESCAPE_MAX_BYTES, read, context);
}
#endif
