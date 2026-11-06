/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the production guest queue (dg-gl-queue.h) against a modelled
 * device: negotiation with old and new hosts, pipelined publication with
 * buffer and entry reuse only after the device released them, counter
 * wraparound, interrupts only for armed waits, per-stream fault reports,
 * watchdog expiry and an engine reset with entries in flight.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;
#include "gpu.h"
#include "gl.h"
#include "dg-gl-queue.h"

#define BUFFER_BASE 0x100000u
#define BUFFER_STRIDE 0x1000u

typedef struct {
    ULONG Page[1024];
    unsigned char Buffers[DG_GL_QUEUE_MAX_BUFFERS][64];
    ULONG Version, Active, Generation, Address;
    ULONG Consumed, Completed;
    /* Behaviour: entries consumed per doorbell / per wait, completed per wait. */
    ULONG ConsumeOnDoorbell, ConsumeOnWait, CompleteOnWait;
    ULONG FailSequence, FailError, FailLow, FailHigh;
    ULONG Reads, Writes, Doorbells, Interrupts, Prepares, Finishes, Waits;
    int Irq, ResetOnWait;
    /* Buffers published but not yet consumed: the driver must not touch them. */
    int Owned[DG_GL_QUEUE_MAX_BUFFERS];
} Device;

static Device *Current;
static ULONG Word(Device *d, ULONG offset) {
    return d->Page[offset / 4];
}
static void Put(Device *d, ULONG offset, ULONG value) {
    d->Page[offset / 4] = value;
}
static void CheckWait(Device *d) {
    ULONG flags = Word(d, DG_GL_QUEUE_WAIT);
    if (((flags & DG_GL_QUEUE_WAIT_FOR_COMPLETED) &&
         (int32_t)(d->Completed - Word(d, DG_GL_QUEUE_WAIT_COMPLETED)) >= 0) ||
        ((flags & DG_GL_QUEUE_WAIT_FOR_CONSUMED) &&
         (int32_t)(d->Consumed - Word(d, DG_GL_QUEUE_WAIT_CONSUMED)) >= 0)) {
        d->Irq = 1;
        ++d->Interrupts;
    }
}
static void Consume(Device *d, ULONG limit) {
    ULONG produced = Word(d, DG_GL_QUEUE_PRODUCED);
    while (d->Active && limit-- && d->Consumed != produced) {
        ULONG entry = DgGlQueueEntry(d->Consumed, 0);
        ULONG address = Word(d, entry + DG_GL_ENTRY_ADDR_LO);
        ULONG buffer = (address - BUFFER_BASE) / BUFFER_STRIDE;
        assert(buffer < DG_GL_QUEUE_MAX_BUFFERS && d->Owned[buffer]);
        assert(Word(d, entry + DG_GL_ENTRY_GENERATION) == d->Generation);
        assert(!Word(d, entry + DG_GL_ENTRY_RESERVED0) && !Word(d, entry + DG_GL_ENTRY_RESERVED1));
        /* The host copies the command bytes now: their buffer is free again. */
        assert(d->Buffers[buffer][0] == (unsigned char)Word(d, entry + DG_GL_ENTRY_SEQUENCE));
        d->Owned[buffer] = 0;
        ++d->Consumed;
    }
    Put(d, DG_GL_QUEUE_CONSUMED, d->Consumed);
    CheckWait(d);
}
static void Complete(Device *d, ULONG limit) {
    while (d->Active && limit-- && d->Completed != d->Consumed) {
        ULONG entry = DgGlQueueEntry(d->Completed, 0);
        ULONG sequence = Word(d, entry + DG_GL_ENTRY_SEQUENCE);
        int failed =
            sequence == d->FailSequence || (sequence >= d->FailLow && sequence <= d->FailHigh);
        Put(d, entry + DG_GL_ENTRY_STATUS, DG_STATUS_DONE | (failed ? DG_STATUS_ERROR : 0));
        Put(d, entry + DG_GL_ENTRY_ERROR, failed ? d->FailError : 0);
        Put(d, entry + DG_GL_ENTRY_RESULT_BYTES, 4);
        Put(d, entry + DG_GL_ENTRY_RESULT_TYPE, DG_GL_RESULT_INT);
        ++d->Completed;
        Put(d, DG_GL_QUEUE_COMPLETED, d->Completed);
    }
    CheckWait(d);
}
static void Reset(Device *d) {
    if (d->Active)
        Put(d, DG_GL_QUEUE_STATE, 0);
    d->Active = 0;
    ++d->Generation;
    memset(d->Owned, 0, sizeof(d->Owned));
}
static ULONG Read(void *context, ULONG reg) {
    Device *d = context;
    ++d->Reads;
    if (reg == DG_GL_REG_QUEUE_VERSION)
        return d->Version;
    if (reg == DG_GL_REG_QUEUE_CONTROL)
        return d->Active;
    assert(!"unexpected register read");
    return 0;
}
static void Write(void *context, ULONG reg, ULONG value) {
    Device *d = context;
    ++d->Writes;
    switch (reg) {
        case DG_GL_REG_QUEUE_ADDR_LO:
            d->Address = value;
            break;
        case DG_GL_REG_QUEUE_ADDR_HI:
            assert(!value);
            break;
        case DG_GL_REG_QUEUE_CONTROL:
            if (!d->Version || value != d->Generation || d->Address != 0x5000)
                break;
            memset(d->Page, 0, 64);
            d->Consumed = d->Completed = 0;
            Put(d, DG_GL_QUEUE_GENERATION, value);
            Put(d, DG_GL_QUEUE_STATE, DG_GL_QUEUE_ACTIVE);
            d->Active = 1;
            break;
        case DG_GL_REG_QUEUE_DOORBELL:
            ++d->Doorbells;
            assert(value == Word(d, DG_GL_QUEUE_PRODUCED));
            if (d->Active) {
                ULONG index = Word(d, DG_GL_QUEUE_PRODUCED) - 1;
                ULONG address = Word(d, DgGlQueueEntry(index, DG_GL_ENTRY_ADDR_LO));
                d->Owned[(address - BUFFER_BASE) / BUFFER_STRIDE] = 1;
            }
            Consume(d, d->ConsumeOnDoorbell);
            break;
        default:
            assert(!"unexpected register write");
    }
}
static void Barrier(void *context) {
    (void)context;
}
static int Prepare(void *context) {
    Device *d = context;
    ++d->Prepares;
    d->Irq = 0;
    return 1;
}
static int Wait(void *context) {
    Device *d = context;
    ++d->Waits;
    assert(Word(d, DG_GL_QUEUE_WAIT)); /* never sleeps unarmed */
    if (d->ResetOnWait) {
        d->ResetOnWait = 0;
        Reset(d);
        return 0;
    }
    /* The device keeps working while the driver sleeps; it interrupts only
     * once the armed condition holds. No progress at all is the watchdog. */
    for (;;) {
        ULONG consumed = d->Consumed, completed = d->Completed;
        Consume(d, d->ConsumeOnWait);
        Complete(d, d->CompleteOnWait);
        if (d->Irq || (consumed == d->Consumed && completed == d->Completed))
            return d->Irq;
    }
}
static void Finish(void *context) {
    ++((Device *)context)->Finishes;
}

static DG_GL_QUEUE_OPS Ops(Device *d) {
    DG_GL_QUEUE_OPS ops = {d, Read, Write, Barrier, Prepare, Wait, Finish};
    return ops;
}
static void Attach(DG_GL_QUEUE *q, Device *d, ULONG buffers) {
    ULONG i;
    memset(q, 0, sizeof(*q));
    q->Page = d->Page;
    q->PagePhysical = 0x5000;
    q->BufferCount = buffers;
    q->BufferBytes = 64;
    for (i = 0; i < buffers; ++i) {
        q->Buffers[i].Memory = d->Buffers[i];
        q->Buffers[i].Physical = BUFFER_BASE + i * BUFFER_STRIDE;
    }
}
/* Build and publish one batch whose first byte names its sequence. */
static ULONG Submit(DG_GL_QUEUE *q, const DG_GL_QUEUE_OPS *ops, ULONG sequence, ULONG token,
                    ULONG stream, int async) {
    int result;
    unsigned char *buffer = DgGlQueueBuffer(q, ops, &result);
    assert(result == 1 && buffer);
    memset(buffer, 0, 64);
    buffer[0] = (unsigned char)sequence;
    return DgGlQueuePublish(q, ops, 32, sequence, q->Generation, token, stream, 0, 0, async);
}

static void OldHost(void) {
    Device d = {0};
    DG_GL_QUEUE q;
    DG_GL_QUEUE_OPS ops = Ops(&d);
    int result;
    Attach(&q, &d, 4);
    assert(!DgGlQueueSupported(&ops));
    assert(!DgGlQueueEnable(&q, &ops, 0));
    assert(!q.Active && !DgGlQueueCurrent(&q));
    assert(!DgGlQueueBuffer(&q, &ops, &result) && result == DG_GL_QUEUE_LOST);
    /* No queue register was written: an older device sees nothing new. */
    assert(d.Writes == 0);
}

static void Pipelined(ULONG start) {
    Device d = {0};
    DG_GL_QUEUE q;
    DG_GL_QUEUE_OPS ops = Ops(&d);
    DG_GL_QUEUE_RESULT r;
    ULONG i, index = 0, writes, reads;
    d.Version = 1;
    d.Generation = 9;
    /* The device copies lazily and executes only while the driver waits. */
    d.ConsumeOnDoorbell = 0;
    d.ConsumeOnWait = 3;
    d.CompleteOnWait = 2;
    Attach(&q, &d, 4);
    assert(DgGlQueueEnable(&q, &ops, 9));
    assert(DgGlQueueCurrent(&q) && q.Generation == 9);
    /* White-box: start both sides just below the 32-bit wrap. */
    q.Produced = q.Reaped = d.Consumed = d.Completed = start;
    Put(&d, DG_GL_QUEUE_CONSUMED, start);
    Put(&d, DG_GL_QUEUE_COMPLETED, start);
    Put(&d, DG_GL_QUEUE_PRODUCED, start);
    writes = d.Writes;
    reads = d.Reads;
    for (i = 1; i <= 100; ++i) {
        index = Submit(&q, &ops, i, 3, 1, 1);
        assert(q.Produced - q.Reaped <= DG_GL_QUEUE_DEPTH);
        assert(q.Produced - d.Consumed <= q.BufferCount);
    }
    /* Exactly one MMIO write (the doorbell) and no read per batch. */
    assert(d.Writes - writes == 100 && d.Reads == reads);
    assert(d.Doorbells == 100 && q.Doorbells == 100);
    assert(DgGlQueueWaitEntry(&q, &ops, index, &r) == 1);
    assert(r.Status == DG_STATUS_DONE && !r.Error && r.ResultBytes == 4);
    assert(q.Reaped == q.Produced && q.Produced == start + 100);
    /* Every sleep was armed, prepared and finished exactly once. */
    assert(d.Prepares == d.Finishes && d.Waits <= d.Prepares && d.Waits > 0);
    assert(!Word(&d, DG_GL_QUEUE_WAIT));
    assert(!DgGlQueueTakeFault(&q, 3, 0, &(DG_GL_QUEUE_FAULT){0}));
}

static void NoSleepWhenReady(void) {
    Device d = {0};
    DG_GL_QUEUE q;
    DG_GL_QUEUE_OPS ops = Ops(&d);
    DG_GL_QUEUE_RESULT r;
    ULONG i, index;
    d.Version = 1;
    d.ConsumeOnDoorbell = 16;
    Attach(&q, &d, 8);
    assert(DgGlQueueEnable(&q, &ops, 0));
    for (i = 0; i < 40; ++i) {
        index = Submit(&q, &ops, i, 0, 0, 0);
        Complete(&d, 16);
    }
    assert(DgGlQueueWaitEntry(&q, &ops, index, &r) == 1);
    /* A device that keeps up never makes the driver arm, sleep or interrupt. */
    assert(d.Prepares == 0 && d.Interrupts == 0);
    (void)r;
}

static void Faults(void) {
    Device d = {0};
    DG_GL_QUEUE q;
    DG_GL_QUEUE_OPS ops = Ops(&d);
    DG_GL_QUEUE_RESULT r;
    DG_GL_QUEUE_FAULT fault;
    ULONG i, index;
    d.Version = 1;
    d.ConsumeOnDoorbell = 16;
    d.CompleteOnWait = 16;
    Attach(&q, &d, 8);
    assert(DgGlQueueEnable(&q, &ops, 0));
    d.FailSequence = 2;
    d.FailError = DG_GL_ERROR_HOST;
    Submit(&q, &ops, 1, 7, 1, 1);
    Submit(&q, &ops, 2, 7, 1, 1); /* fails after its request returned */
    index = Submit(&q, &ops, 3, 7, 2, 0);
    assert(DgGlQueueWaitEntry(&q, &ops, index, &r) == 1 && r.Status == DG_STATUS_DONE);
    assert(!DgGlQueueTakeFault(&q, 8, 0, &fault));
    assert(!DgGlQueueTakeFault(&q, 7, 2, &fault));
    assert(DgGlQueueTakeFault(&q, 7, 0, &fault));
    assert(fault.Token == 7 && fault.Stream == 1 && fault.Error == DG_GL_ERROR_HOST &&
           fault.Sequence == 2);
    assert(!DgGlQueueTakeFault(&q, 7, 0, &fault)); /* reported once */
    /* A synchronous request's failure belongs to its caller only. */
    d.FailSequence = 4;
    index = Submit(&q, &ops, 4, 7, 1, 0);
    assert(DgGlQueueWaitEntry(&q, &ops, index, &r) == 1);
    assert(r.Status == (DG_STATUS_DONE | DG_STATUS_ERROR) && r.Error == DG_GL_ERROR_HOST);
    assert(!DgGlQueueTakeFault(&q, 7, 0, &fault));
    /* Only the first failure of a stream is kept; closing drops a client. */
    d.FailError = DG_GL_ERROR_SKIPPED;
    d.FailLow = 10;
    d.FailHigh = 10 + DG_GL_QUEUE_FAULTS + 3;
    for (i = 0; i < DG_GL_QUEUE_FAULTS + 4; ++i) {
        index = Submit(&q, &ops, 10 + i, 20 + i % 2, 1 + i, 1);
        Submit(&q, &ops, 200, 20, 1, 1);
    }
    index = Submit(&q, &ops, 99, 0, 0, 0);
    assert(DgGlQueueWaitEntry(&q, &ops, index, &r) == 1);
    for (i = 0; i < DG_GL_QUEUE_FAULTS; ++i)
        assert(q.Faults[i].Token == 20 || q.Faults[i].Token == 21);
    DgGlQueueDropClient(&q, 20);
    assert(!DgGlQueueTakeFault(&q, 20, 0, &fault));
    assert(DgGlQueueTakeFault(&q, 21, 0, &fault) && fault.Error == DG_GL_ERROR_SKIPPED);
}

static void WatchdogAndReset(void) {
    Device d = {0};
    DG_GL_QUEUE q;
    DG_GL_QUEUE_OPS ops = Ops(&d);
    DG_GL_QUEUE_RESULT r;
    DG_GL_QUEUE_FAULT fault;
    ULONG index;
    int result;
    d.Version = 1;
    d.Generation = 4;
    d.ConsumeOnDoorbell = 16;
    Attach(&q, &d, 8);
    assert(DgGlQueueEnable(&q, &ops, 4));
    /* A device that never completes: the watchdog returns, still current. */
    index = Submit(&q, &ops, 1, 5, 1, 0);
    assert(DgGlQueueWaitEntry(&q, &ops, index, &r) == 0);
    assert(DgGlQueueCurrent(&q) && !Word(&d, DG_GL_QUEUE_WAIT));
    /* Engine reset with entries in flight: waiters see LOST, asynchronous
     * owners a generation fault, and nothing completes for the old page. */
    Submit(&q, &ops, 2, 5, 2, 1);
    Submit(&q, &ops, 3, 5, 3, 1);
    index = Submit(&q, &ops, 4, 5, 2, 0);
    d.ResetOnWait = 1;
    assert(DgGlQueueWaitEntry(&q, &ops, index, &r) == DG_GL_QUEUE_LOST);
    assert(!q.Active && q.Lost == 1);
    assert(DgGlQueueTakeFault(&q, 5, 2, &fault) && fault.Error == DG_GL_ERROR_GENERATION &&
           fault.Sequence == 2);
    assert(DgGlQueueTakeFault(&q, 5, 3, &fault) && fault.Sequence == 3);
    assert(!DgGlQueueTakeFault(&q, 5, 0, &fault));
    assert(!DgGlQueueBuffer(&q, &ops, &result) && result == DG_GL_QUEUE_LOST);
    /* The old generation cannot re-enable; the new one restarts the counters. */
    assert(!DgGlQueueEnable(&q, &ops, 4));
    assert(DgGlQueueEnable(&q, &ops, 5));
    assert(q.Produced == 0 && q.Reaped == 0);
    d.CompleteOnWait = 1;
    index = Submit(&q, &ops, 5, 5, 1, 0);
    assert(index == 0 && DgGlQueueWaitEntry(&q, &ops, index, &r) == 1);
    /* A device that disappears behind a still-ACTIVE page cannot advance
     * beyond what was published. */
    Put(&d, DG_GL_QUEUE_COMPLETED, 1000);
    DgGlQueueReap(&q);
    assert(q.Reaped == q.Produced);
}

int main(void) {
    Current = NULL;
    OldHost();
    Pipelined(0);
    Pipelined(0xfffffff0u);
    NoSleepWhenReady();
    Faults();
    WatchdogAndReset();
    puts("gl-queue: ok");
    return 0;
}
