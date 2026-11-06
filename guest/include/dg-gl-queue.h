/* SPDX-License-Identifier: GPL-2.0-or-later
 * Guest side of the queued GL transport (gl.h, DG_GL_REG_QUEUE_*), shared by
 * the NT miniport and the Win9x VxD. Freestanding C: ULONG is the driver's
 * unsigned 32-bit type. Every call holds the driver's GL mutex.
 *
 * Command buffers are driver-owned and never mapped to applications. A batch
 * is copied from the caller and validated in its buffer, then published; the
 * host copies it again at its doorbell. Buffer i % Buffers is reused only once
 * the device reports entry i - Buffers consumed, and an entry slot only once
 * its completion has been reaped, so no published bytes change underneath it.
 */
#ifndef DG_GL_QUEUE_H
#define DG_GL_QUEUE_H

#define DG_GL_QUEUE_MAX_BUFFERS 8
#define DG_GL_QUEUE_FAULTS 16
#define DG_GL_QUEUE_LOST (-1)

typedef struct {
    void *Memory;
    ULONG Physical; /* below 4 GiB */
} DG_GL_QUEUE_BUFFER;

/* A failed batch whose request already returned: reported once to Token. */
typedef struct {
    ULONG Token, Stream, Error, Sequence;
} DG_GL_QUEUE_FAULT;

typedef struct {
    ULONG Token, Stream, Sequence, Async;
} DG_GL_QUEUE_OWNER;

typedef struct {
    void *Context;
    ULONG (*Read)(void *, ULONG);
    void (*Write)(void *, ULONG, ULONG);
    /* Full fence: orders the arm/publish stores before following loads. */
    void (*Barrier)(void *);
    /* Prepare before arming (nonzero on success); Wait sleeps until the
     * completion interrupt (nonzero) or the driver's watchdog (zero); Finish
     * releases what Prepare acquired. */
    int (*Prepare)(void *);
    int (*Wait)(void *);
    void (*Finish)(void *);
} DG_GL_QUEUE_OPS;

typedef struct {
    volatile ULONG *Page;
    ULONG PagePhysical;
    DG_GL_QUEUE_BUFFER Buffers[DG_GL_QUEUE_MAX_BUFFERS];
    ULONG BufferCount, BufferBytes;
    ULONG Active, Generation, Produced, Reaped;
    DG_GL_QUEUE_OWNER Owners[DG_GL_QUEUE_DEPTH];
    DG_GL_QUEUE_FAULT Faults[DG_GL_QUEUE_FAULTS];
    /* Diagnostics only. */
    ULONG Doorbells, Sleeps, Lost;
} DG_GL_QUEUE;

/* Outcome of one waited entry. */
typedef struct {
    ULONG Status, Error, ResultBytes, ResultType;
} DG_GL_QUEUE_RESULT;

static inline ULONG DgGlQueueLoad(const DG_GL_QUEUE *q, ULONG offset) {
    return q->Page[offset / 4];
}
static inline void DgGlQueueStore(DG_GL_QUEUE *q, ULONG offset, ULONG value) {
    q->Page[offset / 4] = value;
}
static inline ULONG DgGlQueueEntry(ULONG index, ULONG field) {
    return DG_GL_QUEUE_RING + (index % DG_GL_QUEUE_DEPTH) * DG_GL_QUEUE_ENTRY_BYTES + field;
}
/* Signed wrapping comparison of free-running counters: a reached b. */
static inline int DgGlQueueReached(ULONG a, ULONG b) {
    return (long)(int)(a - b) >= 0;
}

static inline int DgGlQueueSupported(const DG_GL_QUEUE_OPS *ops) {
    return ops->Read(ops->Context, DG_GL_REG_QUEUE_VERSION) == DG_GL_QUEUE_VERSION;
}

/* The page still names this queue: no engine reset since it was enabled. The
 * device writes STATE zero synchronously with its reset, so while this holds
 * GENERATION is the engine generation without an MMIO read. */
static inline int DgGlQueueCurrent(const DG_GL_QUEUE *q) {
    return q->Active && DgGlQueueLoad(q, DG_GL_QUEUE_STATE) == DG_GL_QUEUE_ACTIVE &&
           DgGlQueueLoad(q, DG_GL_QUEUE_GENERATION) == q->Generation;
}

static inline void DgGlQueueAddFault(DG_GL_QUEUE *q, ULONG token, ULONG stream, ULONG error,
                                     ULONG sequence) {
    ULONG i, free_slot = DG_GL_QUEUE_FAULTS;
    if (!token || !stream)
        return;
    for (i = 0; i < DG_GL_QUEUE_FAULTS; ++i) {
        if (q->Faults[i].Token == token && q->Faults[i].Stream == stream)
            return; /* the first failure of a stream is the one reported */
        if (!q->Faults[i].Token && free_slot == DG_GL_QUEUE_FAULTS)
            free_slot = i;
    }
    /* A full table drops this report. The stream's later entries complete
     * SKIPPED, which its own next request reports instead. */
    if (free_slot == DG_GL_QUEUE_FAULTS)
        return;
    q->Faults[free_slot].Token = token;
    q->Faults[free_slot].Stream = stream;
    q->Faults[free_slot].Error = error;
    q->Faults[free_slot].Sequence = sequence;
}

/* Take the oldest unreported failure of token (stream zero: any stream). */
static inline int DgGlQueueTakeFault(DG_GL_QUEUE *q, ULONG token, ULONG stream,
                                     DG_GL_QUEUE_FAULT *fault) {
    ULONG i;
    if (!token)
        return 0;
    for (i = 0; i < DG_GL_QUEUE_FAULTS; ++i)
        if (q->Faults[i].Token == token && (!stream || q->Faults[i].Stream == stream)) {
            *fault = q->Faults[i];
            q->Faults[i].Token = q->Faults[i].Stream = 0;
            return 1;
        }
    return 0;
}

static inline void DgGlQueueDropClient(DG_GL_QUEUE *q, ULONG token) {
    ULONG i;
    for (i = 0; i < DG_GL_QUEUE_FAULTS; ++i)
        if (q->Faults[i].Token == token)
            q->Faults[i].Token = q->Faults[i].Stream = 0;
}

/* The engine reset: unreaped entries will never complete. Their asynchronous
 * owners learn a generation failure; synchronous waiters see LOST. */
static inline void DgGlQueueLose(DG_GL_QUEUE *q) {
    ULONG i;
    for (i = q->Reaped; i != q->Produced; ++i) {
        const DG_GL_QUEUE_OWNER *o = &q->Owners[i % DG_GL_QUEUE_DEPTH];
        if (o->Async)
            DgGlQueueAddFault(q, o->Token, o->Stream, DG_GL_ERROR_GENERATION, o->Sequence);
    }
    q->Reaped = q->Produced;
    q->Active = 0;
    ++q->Lost;
}

/* Record outcomes the device has written since the last reap. */
static inline void DgGlQueueReap(DG_GL_QUEUE *q) {
    ULONG completed;
    if (!DgGlQueueCurrent(q)) {
        if (q->Active)
            DgGlQueueLose(q);
        return;
    }
    completed = DgGlQueueLoad(q, DG_GL_QUEUE_COMPLETED);
    /* Never trust a counter beyond what was published. */
    if (completed - q->Reaped > q->Produced - q->Reaped)
        completed = q->Produced;
    while (q->Reaped != completed) {
        const DG_GL_QUEUE_OWNER *o = &q->Owners[q->Reaped % DG_GL_QUEUE_DEPTH];
        ULONG status = DgGlQueueLoad(q, DgGlQueueEntry(q->Reaped, DG_GL_ENTRY_STATUS));
        if (o->Async && (status & DG_STATUS_ERROR))
            DgGlQueueAddFault(q, o->Token, o->Stream,
                              DgGlQueueLoad(q, DgGlQueueEntry(q->Reaped, DG_GL_ENTRY_ERROR)),
                              o->Sequence);
        ++q->Reaped;
    }
}

/* Enable the allocated page and buffers for the current engine generation.
 * Any earlier queue state must already be lost or drained. */
static inline int DgGlQueueEnable(DG_GL_QUEUE *q, const DG_GL_QUEUE_OPS *ops, ULONG generation) {
    ULONG i;
    if (!q->Page || !q->BufferCount || !DgGlQueueSupported(ops))
        return 0;
    if (q->Active)
        DgGlQueueLose(q);
    DgGlQueueStore(q, DG_GL_QUEUE_PRODUCED, 0);
    DgGlQueueStore(q, DG_GL_QUEUE_WAIT, 0);
    DgGlQueueStore(q, DG_GL_QUEUE_WAIT_COMPLETED, 0);
    DgGlQueueStore(q, DG_GL_QUEUE_WAIT_CONSUMED, 0);
    for (i = 0; i < DG_GL_QUEUE_DEPTH; ++i) {
        q->Owners[i].Token = q->Owners[i].Stream = q->Owners[i].Async = 0;
        q->Owners[i].Sequence = 0;
    }
    ops->Barrier(ops->Context);
    ops->Write(ops->Context, DG_GL_REG_QUEUE_ADDR_LO, q->PagePhysical);
    ops->Write(ops->Context, DG_GL_REG_QUEUE_ADDR_HI, 0);
    ops->Write(ops->Context, DG_GL_REG_QUEUE_CONTROL, generation);
    if (ops->Read(ops->Context, DG_GL_REG_QUEUE_CONTROL) != 1)
        return 0;
    q->Generation = generation;
    q->Produced = q->Reaped = 0;
    q->Active = 1;
    if (!DgGlQueueCurrent(q)) {
        q->Active = 0;
        return 0;
    }
    return 1;
}

static inline int DgGlQueueHolds(const DG_GL_QUEUE *q, ULONG which, ULONG target) {
    return DgGlQueueReached(DgGlQueueLoad(q, which == DG_GL_QUEUE_WAIT_FOR_COMPLETED
                                                 ? DG_GL_QUEUE_COMPLETED
                                                 : DG_GL_QUEUE_CONSUMED),
                            target);
}

/* Sleep until a device counter reaches target: 1 reached, 0 watchdog,
 * DG_GL_QUEUE_LOST after an engine reset. The interrupt is armed only here. */
static inline int DgGlQueueWaitFor(DG_GL_QUEUE *q, const DG_GL_QUEUE_OPS *ops, ULONG which,
                                   ULONG target) {
    const ULONG offset = which == DG_GL_QUEUE_WAIT_FOR_COMPLETED ? DG_GL_QUEUE_WAIT_COMPLETED
                                                                 : DG_GL_QUEUE_WAIT_CONSUMED;
    int woke;
    for (;;) {
        if (!DgGlQueueCurrent(q))
            return DG_GL_QUEUE_LOST;
        if (DgGlQueueHolds(q, which, target))
            return 1;
        if (!ops->Prepare(ops->Context))
            return 0;
        DgGlQueueStore(q, offset, target);
        DgGlQueueStore(q, DG_GL_QUEUE_WAIT, which);
        /* The device writes its counter, fences, then reads WAIT. Arming,
         * fencing, then rechecking means one of the two sees the other. */
        ops->Barrier(ops->Context);
        if (DgGlQueueHolds(q, which, target) || !DgGlQueueCurrent(q)) {
            DgGlQueueStore(q, DG_GL_QUEUE_WAIT, 0);
            ops->Finish(ops->Context);
            continue;
        }
        ++q->Sleeps;
        woke = ops->Wait(ops->Context);
        DgGlQueueStore(q, DG_GL_QUEUE_WAIT, 0);
        ops->Finish(ops->Context);
        if (!woke) {
            if (!DgGlQueueCurrent(q))
                return DG_GL_QUEUE_LOST;
            return DgGlQueueHolds(q, which, target);
        }
    }
}

/* Command buffer for the next entry, waiting for a free buffer and entry
 * slot. Null with *result 0 (watchdog) or DG_GL_QUEUE_LOST. */
static inline void *DgGlQueueBuffer(DG_GL_QUEUE *q, const DG_GL_QUEUE_OPS *ops, int *result) {
    int waited;
    DgGlQueueReap(q);
    *result = DG_GL_QUEUE_LOST;
    if (!q->Active)
        return 0;
    if (q->Produced - q->Reaped >= DG_GL_QUEUE_DEPTH) {
        waited = DgGlQueueWaitFor(q, ops, DG_GL_QUEUE_WAIT_FOR_COMPLETED,
                                  q->Produced - DG_GL_QUEUE_DEPTH + 1);
        DgGlQueueReap(q);
        if (waited != 1) {
            *result = waited;
            return 0;
        }
    }
    if (!DgGlQueueHolds(q, DG_GL_QUEUE_WAIT_FOR_CONSUMED, q->Produced - q->BufferCount + 1)) {
        waited = DgGlQueueWaitFor(q, ops, DG_GL_QUEUE_WAIT_FOR_CONSUMED,
                                  q->Produced - q->BufferCount + 1);
        if (waited != 1) {
            DgGlQueueReap(q);
            *result = waited;
            return 0;
        }
    }
    *result = 1;
    return q->Buffers[q->Produced % q->BufferCount].Memory;
}

/* Publish the batch built in DgGlQueueBuffer's buffer and ring the doorbell.
 * Returns the entry index. Token/Stream name the fault domain (zero: none). */
static inline ULONG DgGlQueuePublish(DG_GL_QUEUE *q, const DG_GL_QUEUE_OPS *ops, ULONG bytes,
                                     ULONG sequence, ULONG generation, ULONG token, ULONG stream,
                                     ULONG result_physical, ULONG result_capacity, int async) {
    const ULONG index = q->Produced;
    DG_GL_QUEUE_OWNER *o = &q->Owners[index % DG_GL_QUEUE_DEPTH];
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_ADDR_LO),
                   q->Buffers[index % q->BufferCount].Physical);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_ADDR_HI), 0);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_BYTES), bytes);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_SEQUENCE), sequence);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_GENERATION), generation);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_CLIENT), stream ? token : 0);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_STREAM), token ? stream : 0);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_RESULT_ADDR_LO), result_physical);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_RESULT_ADDR_HI), 0);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_RESULT_CAPACITY), result_capacity);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_RESERVED0), 0);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_RESERVED1), 0);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_STATUS), 0);
    DgGlQueueStore(q, DgGlQueueEntry(index, DG_GL_ENTRY_ERROR), 0);
    o->Token = token;
    o->Stream = stream;
    o->Sequence = sequence;
    o->Async = async != 0;
    /* Command bytes and entry words precede PRODUCED; PRODUCED precedes the
     * doorbell, whose MMIO store the device orders after RAM stores. */
    ops->Barrier(ops->Context);
    q->Produced = index + 1;
    DgGlQueueStore(q, DG_GL_QUEUE_PRODUCED, q->Produced);
    ops->Write(ops->Context, DG_GL_REG_QUEUE_DOORBELL, q->Produced);
    ++q->Doorbells;
    return index;
}

/* Wait for one published entry: 1 with its outcome, 0 watchdog or LOST. */
static inline int DgGlQueueWaitEntry(DG_GL_QUEUE *q, const DG_GL_QUEUE_OPS *ops, ULONG index,
                                     DG_GL_QUEUE_RESULT *result) {
    int waited = DgGlQueueWaitFor(q, ops, DG_GL_QUEUE_WAIT_FOR_COMPLETED, index + 1);
    DgGlQueueReap(q);
    if (waited != 1)
        return waited;
    result->Status = DgGlQueueLoad(q, DgGlQueueEntry(index, DG_GL_ENTRY_STATUS));
    result->Error = result->Status & DG_STATUS_ERROR
                        ? DgGlQueueLoad(q, DgGlQueueEntry(index, DG_GL_ENTRY_ERROR))
                        : 0;
    result->ResultBytes = DgGlQueueLoad(q, DgGlQueueEntry(index, DG_GL_ENTRY_RESULT_BYTES));
    result->ResultType = DgGlQueueLoad(q, DgGlQueueEntry(index, DG_GL_ENTRY_RESULT_TYPE));
    /* A completed entry is always DONE; anything else is a broken device. */
    if (!(result->Status & DG_STATUS_DONE)) {
        result->Status = DG_STATUS_DONE | DG_STATUS_ERROR;
        result->Error = DG_GL_ERROR_TRANSPORT;
    }
    return 1;
}
#endif
