// SPDX-License-Identifier: GPL-2.0-or-later
//! Device side of the queued GL transport (gl.h, DG_GL_REG_QUEUE_*).
//!
//! The guest publishes entries in one RAM page and rings a doorbell. Each entry
//! is copied once into host memory, then its command bytes are snapshotted and
//! validated by the same transaction as a legacy SUBMIT. Completions are written
//! back in entry order. Callers hold QEMU's global lock; no guest pointer is kept.
use crate::{
    device::{gl_submit, Enqueue, GlCallbacks, GlRequest, GlResult},
    gl_api::*,
    submission::{fault, faulted, Completion, Domain, FAULTED},
};
use core::ffi::c_void;

pub const DEPTH: usize = DG_GL_QUEUE_DEPTH as usize;
/// Host snapshot bytes held for unfinished entries. One entry is always
/// admitted, so a single maximal legacy-sized batch still makes progress.
pub const PENDING_BYTES: u64 = 32 << 20;
pub const OUTCOME_IRQ: u32 = 1;
pub const OUTCOME_FAULT: u32 = 2;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct QueueCallbacks {
    pub gl: GlCallbacks,
    /// DMA write of host bytes into guest RAM; nonzero on success.
    pub write: unsafe extern "C" fn(*mut c_void, u64, *const u8, u32) -> u32,
    /// Full fence between device page writes and the following page reads.
    pub barrier: unsafe extern "C" fn(*mut c_void),
    /// Hand the engine an in-order completion that never executes. Zero when
    /// the engine accepted it; nonzero when no engine can take it.
    pub marker: unsafe extern "C" fn(*mut c_void, *const Enqueue) -> u32,
    /// A coherence-sensitive batch (desktop READBACK/RETURN) failed: stop the
    /// guest before its completion becomes visible, as the registers do.
    pub fault: unsafe extern "C" fn(*mut c_void, u32),
}

/// Device record of one consumed, unfinished entry.
#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct QueueEntry {
    pub result_address: u64,
    pub sequence: u32,
    pub slot: u32,
    pub client: u32,
    pub stream: u32,
    pub bytes: u32,
    pub sensitive: u32,
    pub result_capacity: u32,
    /// Error decided before execution (validation, skipped domain).
    pub error: u32,
    /// The engine owns this entry's completion.
    pub engine: u32,
    pub reserved: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Queue {
    pub address: u64,
    pub active: u32,
    pub generation: u32,
    pub consumed: u32,
    pub completed: u32,
    pub head: u32,
    pub len: u32,
    pub pending_bytes: u64,
    pub entries: [QueueEntry; DEPTH],
    pub faulted: [Domain; FAULTED],
    pub faulted_next: u32,
    pub reserved: u32,
    /// Diagnostics only: doorbells handled, entries consumed, entries
    /// completed and interrupts requested since realize.
    pub kicks: u64,
    pub taken: u64,
    pub finished: u64,
    pub interrupts: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct QueueOutcome {
    pub flags: u32,
    /// Sequence of a failed coherence-sensitive batch with OUTCOME_FAULT.
    pub fault_sequence: u32,
}

unsafe fn read(cb: &QueueCallbacks, address: u64, out: &mut [u8]) -> bool {
    let m = cb.gl.memory;
    unsafe { (m.read)(m.opaque, address, out.as_mut_ptr(), out.len() as u32) != 0 }
}
unsafe fn write(cb: &QueueCallbacks, address: u64, data: &[u8]) -> bool {
    unsafe {
        (cb.write)(
            cb.gl.memory.opaque,
            address,
            data.as_ptr(),
            data.len() as u32,
        ) != 0
    }
}
unsafe fn write_words(cb: &QueueCallbacks, address: u64, words: &[u32]) -> bool {
    let mut bytes = [0u8; 64];
    for (i, w) in words.iter().enumerate() {
        bytes[i * 4..i * 4 + 4].copy_from_slice(&w.to_le_bytes());
    }
    unsafe { write(cb, address, &bytes[..words.len() * 4]) }
}
fn word(data: &[u8], offset: u32) -> u32 {
    u32::from_le_bytes(
        data[offset as usize..offset as usize + 4]
            .try_into()
            .unwrap(),
    )
}
fn entry_address(q: &Queue, index: u32) -> u64 {
    q.address
        + u64::from(DG_GL_QUEUE_RING)
        + u64::from(index % DG_GL_QUEUE_DEPTH) * u64::from(DG_GL_QUEUE_ENTRY_BYTES)
}

impl Queue {
    fn domain(e: &QueueEntry) -> Domain {
        Domain {
            client: e.client,
            stream: e.stream,
        }
    }
    unsafe fn finish(&mut self, cb: &QueueCallbacks, e: &QueueEntry, c: Option<&Completion>) {
        let mut error = c.map_or(e.error, |c| c.error);
        let (mut bytes, mut kind) = (0, 0);
        if let Some(c) = c.filter(|c| c.error == 0 && c.result_bytes != 0) {
            let data = if c.bulk_result.is_null() {
                c.result.as_ptr()
            } else {
                c.bulk_result.cast_const()
            };
            // A contiguous range or a page list (DG_GL_RESULT_PAGES), rechecked.
            let ok = unsafe {
                crate::device::result_write(
                    cb.gl.memory,
                    cb.write,
                    e.result_address,
                    e.result_capacity,
                    data,
                    c.result_bytes,
                )
            };
            if ok {
                (bytes, kind) = (c.result_bytes, c.result_type);
            } else {
                error = DG_GL_ERROR_DMA;
            }
        }
        let status = DG_STATUS_DONE | if error != 0 { DG_STATUS_ERROR } else { 0 };
        unsafe {
            write_words(
                cb,
                entry_address(self, e.slot) + u64::from(DG_GL_ENTRY_STATUS),
                &[status, error, bytes, kind],
            );
            if let Some(c) = c {
                let p = c.present;
                write_words(
                    cb,
                    self.address + u64::from(DG_GL_QUEUE_PRESENT_SLOT),
                    &[
                        p.slot,
                        p.epoch as u32,
                        (p.epoch >> 32) as u32,
                        p.generation as u32,
                        (p.generation >> 32) as u32,
                    ],
                );
            }
        }
        if error != 0 {
            fault(&mut self.faulted, &mut self.faulted_next, Self::domain(e));
        }
        self.completed = self.completed.wrapping_add(1);
        self.finished += 1;
        unsafe {
            (cb.barrier)(cb.gl.memory.opaque);
            write_words(
                cb,
                self.address + u64::from(DG_GL_QUEUE_COMPLETED),
                &[self.completed],
            );
        }
    }
    fn push(&mut self, e: QueueEntry) {
        let i = (self.head + self.len) as usize % DEPTH;
        self.entries[i] = e;
        self.len += 1;
        self.pending_bytes += u64::from(e.bytes);
    }
    fn pop(&mut self) -> QueueEntry {
        let e = self.entries[self.head as usize];
        self.entries[self.head as usize] = QueueEntry::default();
        self.head = (self.head + 1) % DEPTH as u32;
        self.len -= 1;
        self.pending_bytes -= u64::from(e.bytes);
        e
    }
    /// Entries no engine could take complete once everything before them has.
    unsafe fn drain_unowned(&mut self, cb: &QueueCallbacks) {
        while self.len != 0 && self.entries[self.head as usize].engine == 0 {
            let e = self.pop();
            unsafe { self.finish(cb, &e, None) };
        }
    }
    unsafe fn wait_satisfied(&mut self, cb: &QueueCallbacks) -> bool {
        if self.active == 0 {
            return false;
        }
        unsafe { (cb.barrier)(cb.gl.memory.opaque) };
        let mut wait = [0u8; 12];
        if !unsafe { read(cb, self.address + u64::from(DG_GL_QUEUE_WAIT), &mut wait) } {
            return false;
        }
        let (flags, completed, consumed) = (word(&wait, 0), word(&wait, 4), word(&wait, 8));
        let raise = (flags & DG_GL_QUEUE_WAIT_FOR_COMPLETED != 0
            && self.completed.wrapping_sub(completed) as i32 >= 0)
            || (flags & DG_GL_QUEUE_WAIT_FOR_CONSUMED != 0
                && self.consumed.wrapping_sub(consumed) as i32 >= 0);
        if raise {
            self.interrupts += 1;
        }
        raise
    }
    unsafe fn stop(&mut self, cb: &QueueCallbacks) {
        if self.active != 0 {
            unsafe { write_words(cb, self.address + u64::from(DG_GL_QUEUE_STATE), &[0]) };
        }
        self.active = 0;
    }
}

/// # Safety
/// `q` and `cb` are valid; called under the device lock.
/// Enable the queue page at `address` for `generation`. Nonzero on success.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_queue_enable(
    q: *mut Queue,
    cb: *const QueueCallbacks,
    address: u64,
    generation: u32,
    current_generation: u32,
    legacy_busy: u32,
) -> u32 {
    let (q, cb) = unsafe { (&mut *q, &*cb) };
    let m = cb.gl.memory;
    if generation != current_generation
        || legacy_busy != 0
        || q.len != 0
        || !address.is_multiple_of(u64::from(DG_GL_QUEUE_PAGE_BYTES))
        || address
            .checked_add(u64::from(DG_GL_QUEUE_PAGE_BYTES))
            .is_none()
        || unsafe { (m.ram)(m.opaque, address, DG_GL_QUEUE_PAGE_BYTES, 1) } == 0
    {
        return 0;
    }
    unsafe { q.stop(cb) };
    q.address = address;
    q.generation = generation;
    q.consumed = 0;
    q.completed = 0;
    q.head = 0;
    q.pending_bytes = 0;
    if !unsafe { write(cb, address, &[0u8; 64]) }
        || !unsafe {
            write_words(
                cb,
                address + u64::from(DG_GL_QUEUE_GENERATION),
                &[generation],
            )
        }
    {
        return 0;
    }
    unsafe { (cb.barrier)(m.opaque) };
    if !unsafe { write_words(cb, address, &[DG_GL_QUEUE_ACTIVE]) } {
        return 0;
    }
    q.active = 1;
    1
}

/// # Safety
/// `q` and `cb` are valid; called under the device lock.
/// Disable an idle queue (no unfinished entries). Nonzero on success.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_queue_disable(q: *mut Queue, cb: *const QueueCallbacks) -> u32 {
    let (q, cb) = unsafe { (&mut *q, &*cb) };
    if q.len != 0 {
        return 0;
    }
    unsafe { q.stop(cb) };
    1
}

/// # Safety
/// `q` and `cb` are valid; called under the device lock after the engine
/// discarded every pending batch. Unfinished entries are forgotten.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_queue_reset(q: *mut Queue, cb: *const QueueCallbacks) {
    let (q, cb) = unsafe { (&mut *q, &*cb) };
    unsafe { q.stop(cb) };
    q.head = 0;
    q.len = 0;
    q.pending_bytes = 0;
    q.entries = [QueueEntry::default(); DEPTH];
    q.faulted = [Domain::default(); FAULTED];
    q.faulted_next = 0;
}

/// # Safety
/// `q` and `cb` are valid; called under the device lock.
/// Consume published entries while capacity allows. Returns OUTCOME_IRQ when
/// an armed wait now holds.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_queue_consume(
    q: *mut Queue,
    cb: *const QueueCallbacks,
    current_generation: u32,
    bpp: u32,
    busy_2d: u32,
) -> u32 {
    let (q, cb) = unsafe { (&mut *q, &*cb) };
    if q.active == 0 {
        return 0;
    }
    q.kicks += 1;
    let mut produced = [0u8; 4];
    if !unsafe {
        read(
            cb,
            q.address + u64::from(DG_GL_QUEUE_PRODUCED),
            &mut produced,
        )
    } {
        return 0;
    }
    let produced = word(&produced, 0);
    if produced.wrapping_sub(q.consumed) > DG_GL_QUEUE_DEPTH
        || produced.wrapping_sub(q.completed) > DG_GL_QUEUE_DEPTH
    {
        // A driver overwrote entries it did not own. Stop consuming; already
        // accepted entries still complete. Re-enable after a drain or reset.
        unsafe { q.stop(cb) };
        return 0;
    }
    let before = q.consumed;
    while q.consumed != produced && (q.len as usize) < DEPTH {
        let slot = q.consumed % DG_GL_QUEUE_DEPTH;
        let mut raw = [0u8; DG_GL_QUEUE_ENTRY_BYTES as usize];
        if !unsafe { read(cb, entry_address(q, slot), &mut raw[..48]) } {
            unsafe { q.stop(cb) };
            break;
        }
        let bytes = word(&raw, DG_GL_ENTRY_BYTES);
        if q.len != 0 && q.pending_bytes + u64::from(bytes) > PENDING_BYTES {
            break;
        }
        q.consumed = q.consumed.wrapping_add(1);
        q.taken += 1;
        let request = GlRequest {
            address: u64::from(word(&raw, DG_GL_ENTRY_ADDR_LO))
                | u64::from(word(&raw, DG_GL_ENTRY_ADDR_HI)) << 32,
            result_address: u64::from(word(&raw, DG_GL_ENTRY_RESULT_ADDR_LO))
                | u64::from(word(&raw, DG_GL_ENTRY_RESULT_ADDR_HI)) << 32,
            bytes,
            generation: word(&raw, DG_GL_ENTRY_GENERATION),
            current_generation,
            bpp,
            busy_2d,
            result_capacity: word(&raw, DG_GL_ENTRY_RESULT_CAPACITY),
            sequence: word(&raw, DG_GL_ENTRY_SEQUENCE),
            client: word(&raw, DG_GL_ENTRY_CLIENT),
            stream: word(&raw, DG_GL_ENTRY_STREAM),
        };
        let mut e = QueueEntry {
            sequence: request.sequence,
            slot,
            client: request.client,
            stream: request.stream,
            bytes,
            ..QueueEntry::default()
        };
        let mut out = GlResult::default();
        let mut query = false;
        e.error =
            if word(&raw, DG_GL_ENTRY_RESERVED0) != 0 || word(&raw, DG_GL_ENTRY_RESERVED1) != 0 {
                DG_GL_ERROR_BATCH
            } else if faulted(&q.faulted, Queue::domain(&e)) {
                DG_GL_ERROR_SKIPPED
            } else {
                unsafe { gl_submit(cb.gl, request, &mut out, &mut query) }
            };
        e.sensitive = out.sensitive;
        if e.error == 0 {
            e.result_capacity = out.result_capacity;
            e.result_address = request.result_address;
            e.engine = 1;
            q.push(e);
            continue;
        }
        // Not executed. Its completion still follows every earlier entry, and
        // the engine must learn the failed domain in that same order.
        e.bytes = 0;
        if query {
            // A rejected QUERY (an enum the host does not answer is
            // DG_GL_ERROR_UNSUPPORTED) changed nothing, and its synchronous
            // submitter receives this error itself: the frontend reports a GL
            // error and keeps the context. Its stream continues, so neither
            // this queue nor the engine records a failure of its domain.
            // Batches that fail while executing still fail theirs.
            e.client = 0;
            e.stream = 0;
        }
        if q.len == 0 {
            unsafe { q.finish(cb, &e, None) };
            continue;
        }
        let info = Enqueue {
            sequence: e.sequence,
            client: e.client,
            stream: e.stream,
            error: e.error,
        };
        e.engine = (unsafe { (cb.marker)(cb.gl.memory.opaque, &info) } == 0) as u32;
        q.push(e);
    }
    if q.consumed != before {
        unsafe {
            write_words(
                cb,
                q.address + u64::from(DG_GL_QUEUE_CONSUMED),
                &[q.consumed],
            )
        };
    }
    if unsafe { q.wait_satisfied(cb) } {
        OUTCOME_IRQ
    } else {
        0
    }
}

/// # Safety
/// `q`, `cb` and `c` are valid; called under the device lock with an engine
/// completion of the current generation while `q.len` is nonzero.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_queue_complete(
    q: *mut Queue,
    cb: *const QueueCallbacks,
    c: *const Completion,
) -> QueueOutcome {
    let (q, cb, c) = unsafe { (&mut *q, &*cb, &*c) };
    let mut outcome = QueueOutcome::default();
    unsafe { q.drain_unowned(cb) };
    if q.len == 0 {
        return outcome;
    }
    let e = q.pop();
    if c.error != 0 && e.sensitive != 0 {
        unsafe { (cb.fault)(cb.gl.memory.opaque, e.sequence) };
        outcome.flags |= OUTCOME_FAULT;
        outcome.fault_sequence = e.sequence;
    }
    unsafe { q.finish(cb, &e, Some(c)) };
    unsafe { q.drain_unowned(cb) };
    if unsafe { q.wait_satisfied(cb) } {
        outcome.flags |= OUTCOME_IRQ;
    }
    outcome
}

#[cfg(test)]
mod tests;
