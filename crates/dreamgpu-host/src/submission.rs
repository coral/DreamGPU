// SPDX-License-Identifier: GPL-2.0-or-later
//! Accepted DMA batches, cancellation, and bounded completion ownership.
use crate::{
    composition::State,
    gl::{execute_cached, Lookup, Platform, Resources},
    gl_api::*,
};
use core::{ffi::c_void, ptr};
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Memory {
    pub opaque: *mut c_void,
    pub allocate: unsafe extern "C" fn(*mut c_void, usize) -> *mut c_void,
    pub free: unsafe extern "C" fn(*mut c_void, *mut c_void),
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Frame {
    pub slot: u32,
    pub client: u32,
    pub drawable: u32,
    pub epoch: u64,
    pub generation: u64,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Completion {
    pub sequence: u32,
    pub generation: u32,
    pub error: u32,
    pub resources_live: bool,
    pub reset: bool,
    pub present: Frame,
    pub result_bytes: u32,
    pub result_type: u32,
    pub result: [u8; DG_GL_MAX_RESULT_BYTES as usize],
    pub bulk_result: *mut u8,
}
/// Pending batches: one legacy SUBMIT, or up to the queued transport depth.
pub const PENDING: usize = DG_GL_QUEUE_DEPTH as usize;
/// Completions outlive their batches until the device's main loop polls them.
/// Each pending batch and reset contributes one; the queue bounds both.
pub const DONE: usize = 64;
/// Fault domains remembered between engine resets. Eviction only stops
/// skipping for an old domain whose frontend no longer submits.
pub const FAULTED: usize = 16;
#[repr(C)]
pub struct Batch {
    pub data: *mut u8,
    pub bytes: usize,
    pub sequence: u32,
    pub generation: u32,
    pub records: u32,
    pub trace_queued_us: u64,
    pub primary_width: u32,
    pub primary_height: u32,
    /// Optional fault domain; zero client or stream is none.
    pub client: u32,
    pub stream: u32,
    /// Nonzero: complete with this error without executing (no snapshot).
    pub error: u32,
    pub result_bytes: u32,
    pub result_type: u32,
    pub result: [u8; DG_GL_MAX_RESULT_BYTES as usize],
    pub bulk_result: *mut u8,
}
#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
pub struct Domain {
    pub client: u32,
    pub stream: u32,
}
impl Domain {
    fn some(self) -> bool {
        self.client != 0 && self.stream != 0
    }
}
#[repr(C)]
pub struct Submission {
    pub pending: [*mut Batch; PENDING],
    pub pending_head: u32,
    pub pending_len: u32,
    pub reset: u32,
    pub reset_generation: u32,
    pub reset_cpu_epoch: u64,
    pub reset_cpu_generation: u64,
    pub head: u32,
    pub len: u32,
    pub done: [Completion; DONE],
    pub faulted: [Domain; FAULTED],
    pub faulted_next: u32,
}
/// True when `domain` already failed in this engine generation.
pub fn faulted(list: &[Domain; FAULTED], domain: Domain) -> bool {
    domain.some() && list.contains(&domain)
}
/// Remember a failed domain once, replacing the oldest entry when full.
pub fn fault(list: &mut [Domain; FAULTED], next: &mut u32, domain: Domain) {
    if domain.some() && !list.contains(&domain) {
        list[*next as usize % FAULTED] = domain;
        *next = (*next + 1) % FAULTED as u32;
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Request {
    pub data: *mut u8,
    pub bytes: usize,
    pub sequence: u32,
    pub generation: u32,
    pub records: u32,
    pub trace_queued_us: u64,
    pub primary_width: u32,
    pub primary_height: u32,
    pub client: u32,
    pub stream: u32,
    /// Nonzero with null data: an in-order completion that never executes.
    pub error: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Run {
    pub opaque: *mut c_void,
    pub cancelled: unsafe extern "C" fn(*mut c_void) -> u32,
    pub report: unsafe extern "C" fn(*mut c_void, *const u8, u32),
}
unsafe fn free(memory: Memory, p: *mut c_void) {
    if !p.is_null() {
        unsafe { (memory.free)(memory.opaque, p) }
    }
}
unsafe fn drop_batch(memory: Memory, b: *mut Batch) {
    if !b.is_null() {
        let (data, bulk) = unsafe { ((*b).data, (*b).bulk_result) };
        unsafe {
            (*b).data = ptr::null_mut();
            (*b).bulk_result = ptr::null_mut();
            free(memory, bulk.cast());
            free(memory, data.cast());
            free(memory, b.cast());
        }
    }
}
/// # Safety
/// Engine mutex held. Request contains an exclusively owned immutable DMA snapshot,
/// or null data with a preset error; success transfers it, failure leaves it with
/// the caller. Batches execute in submission order. Allocator has C lifetime.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_submission_submit(
    s: *mut Submission,
    m: *const Memory,
    r: *const Request,
    stopping: u32,
) -> u32 {
    unsafe {
        let r = *r;
        if (*s).pending_len as usize == PENDING
            || stopping != 0
            || (r.data.is_null() != (r.error != 0))
        {
            return 0;
        }
        let m = *m;
        let b = (m.allocate)(m.opaque, core::mem::size_of::<Batch>()).cast::<Batch>();
        if b.is_null() {
            return 0;
        }
        b.write(Batch {
            data: r.data,
            bytes: r.bytes,
            sequence: r.sequence,
            generation: r.generation,
            records: r.records,
            trace_queued_us: r.trace_queued_us,
            primary_width: r.primary_width,
            primary_height: r.primary_height,
            client: r.client,
            stream: r.stream,
            error: r.error,
            result_bytes: 0,
            result_type: 0,
            result: [0; DG_GL_MAX_RESULT_BYTES as usize],
            bulk_result: ptr::null_mut(),
        });
        let i = ((*s).pending_head + (*s).pending_len) as usize % PENDING;
        (*s).pending[i] = b;
        (*s).pending_len += 1;
        1
    }
}
/// # Safety
/// Engine mutex held. Returned batch becomes exclusively render-worker owned.
/// Every earlier batch has completed, so the fault list is current: a batch of
/// a failed domain is marked skipped here and never executes.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_submission_take(s: *mut Submission) -> *mut Batch {
    unsafe {
        if (*s).pending_len == 0 {
            return ptr::null_mut();
        }
        let i = (*s).pending_head as usize;
        let b = (*s).pending[i];
        (*s).pending[i] = ptr::null_mut();
        (*s).pending_head = ((*s).pending_head + 1) % PENDING as u32;
        (*s).pending_len -= 1;
        let domain = Domain {
            client: (*b).client,
            stream: (*b).stream,
        };
        if (*b).error == 0 && faulted(&(*s).faulted, domain) {
            (*b).error = DG_GL_ERROR_SKIPPED;
        }
        b
    }
}
/// # Safety
/// Engine mutex held; pending batch has not started execution. An executing batch
/// is separately owned and observes cancellation between records.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_submission_reset(
    s: *mut Submission,
    m: *const Memory,
    generation: u32,
    epoch: u64,
    frame: u64,
) {
    unsafe {
        (*s).reset = 1;
        (*s).reset_generation = generation;
        (*s).reset_cpu_epoch = epoch;
        (*s).reset_cpu_generation = frame;
        drop_pending(s, *m);
        (*s).faulted = [Domain::default(); FAULTED];
        (*s).faulted_next = 0;
    }
}
unsafe fn drop_pending(s: *mut Submission, m: Memory) {
    unsafe {
        while (*s).pending_len != 0 {
            let i = (*s).pending_head as usize;
            let b = (*s).pending[i];
            (*s).pending[i] = ptr::null_mut();
            (*s).pending_head = ((*s).pending_head + 1) % PENDING as u32;
            (*s).pending_len -= 1;
            drop_batch(m, b);
        }
    }
}
unsafe fn push(s: *mut Submission, m: Memory, c: Completion) {
    unsafe {
        if (*s).len as usize == DONE {
            let i = (*s).head as usize;
            let old = (*s).done[i].bulk_result;
            (*s).done[i] = core::mem::zeroed();
            (*s).head = ((*s).head + 1) % DONE as u32;
            (*s).len -= 1;
            free(m, old.cast());
        }
        let i = ((*s).head + (*s).len) as usize % DONE;
        (*s).done[i] = c;
        (*s).len += 1;
    }
}
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct ResetTicket {
    pub generation: u32,
    pub epoch: u64,
    pub frame: u64,
}
/// # Safety
/// Engine mutex held. Output is caller-owned storage, independent of the engine.
/// The ticket must be copied before dropping the mutex for native reset I/O.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_submission_reset_snapshot(
    s: *const Submission,
    out: *mut ResetTicket,
) -> u32 {
    unsafe {
        if (*s).reset == 0 {
            return 0;
        }
        out.write(ResetTicket {
            generation: (*s).reset_generation,
            epoch: (*s).reset_cpu_epoch,
            frame: (*s).reset_cpu_generation,
        });
    }
    1
}
/// # Safety
/// Mutex held, no live output jobs, all contexts/drawables already closed by the
/// render worker. Ticket identifies the packet actually sent. A superseding reset
/// remains pending with no acknowledgement; the caller must process it before
/// taking new batches. Reset completions share the bounded batch completion ring.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_submission_reset_done(
    s: *mut Submission,
    m: *const Memory,
    ticket: *const ResetTicket,
    desktop: *mut State,
    last: *mut Frame,
) -> u32 {
    unsafe {
        let ticket = *ticket;
        if (*s).reset == 0
            || ticket.generation != (*s).reset_generation
            || ticket.epoch != (*s).reset_cpu_epoch
            || ticket.frame != (*s).reset_cpu_generation
        {
            return 0;
        }
        let mut c: Completion = core::mem::zeroed();
        c.generation = ticket.generation;
        c.reset = true;
        (*s).reset = 0;
        (*desktop).exclusive = 0;
        (*desktop).active = 0;
        (*desktop).coherent = 0;
        last.write(core::mem::zeroed());
        push(s, *m, c);
    }
    1
}
/// # Safety
/// Sole render worker owns batch/results. Allocation precedes native query output;
/// no Rust borrow spans native writes. A batch contains at most one validated query.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_submission_query(
    b: *mut Batch,
    m: *const Memory,
    required: u32,
) -> *mut u8 {
    unsafe {
        if required > DG_GL_MAX_BULK_RESULT_BYTES {
            return ptr::null_mut();
        }
        if required <= DG_GL_MAX_RESULT_BYTES {
            return (*b).result.as_mut_ptr();
        }
        if !(*b).bulk_result.is_null() {
            return ptr::null_mut();
        }
        let m = *m;
        let p = (m.allocate)(m.opaque, required as usize).cast::<u8>();
        (*b).bulk_result = p;
        p
    }
}
/// # Safety
/// Batch snapshot passed full framing validation and remains immutable until this
/// function returns. Callback may mutate results but must not release/reenter batch.
/// Registry callback ownership follows dreamgpu_gl_execute; no batch borrow survives
/// cancellation/native/report callbacks.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_submission_run(
    b: *mut Batch,
    resources: *mut Resources,
    platform: *const Platform,
    run: *const Run,
    initial: u32,
) -> u32 {
    unsafe {
        let (data, bytes, w, h) = (
            (*b).data,
            (*b).bytes,
            (*b).primary_width,
            (*b).primary_height,
        );
        let run = *run;
        if (*b).error != 0 {
            return (*b).error;
        }
        let mut result = initial;
        let mut offset = 0;
        let mut lookup = Lookup::default();
        while result == 0 && offset < bytes {
            if (run.cancelled)(run.opaque) != 0 {
                return DG_GL_ERROR_GENERATION;
            }
            // Recheck framing defensively without borrowing mutable batch results.
            if bytes - offset < DG_GL_HEADER_BYTES as usize {
                return DG_GL_ERROR_BATCH;
            }
            let record = data.add(offset);
            let n = u32::from_le(ptr::read_unaligned(
                record.add(DG_GL_OFF_SIZE as usize).cast::<u32>(),
            )) as usize;
            if n < DG_GL_HEADER_BYTES as usize || n > bytes - offset {
                return DG_GL_ERROR_BATCH;
            }
            result = match execute_cached(
                resources,
                &*platform,
                core::slice::from_raw_parts(record, n),
                w,
                h,
                &mut lookup,
            ) {
                Ok(()) => 0,
                Err(error) => error,
            };
            (run.report)(run.opaque, record, result);
            offset += n;
        }
        result
    }
}
/// # Safety
/// Render worker exclusively owns b and its result; engine mutex held for ring
/// insertion. Resources live under the render worker; callbacks free only allocations.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_submission_complete(
    s: *mut Submission,
    m: *const Memory,
    b: *mut Batch,
    error: u32,
    resources: *const Resources,
    desktop: *const State,
    last: *const Frame,
) {
    unsafe {
        let m = *m;
        // A batch finishing while a reset is pending belongs to the discarded
        // generation (usually cancelled by it); the reset clears the list.
        if error != 0 && (*s).reset == 0 {
            let domain = Domain {
                client: (*b).client,
                stream: (*b).stream,
            };
            fault(&mut (*s).faulted, &mut (*s).faulted_next, domain);
        }
        let mut c: Completion = core::mem::zeroed();
        c.sequence = (*b).sequence;
        c.generation = (*b).generation;
        c.error = error;
        c.present = *last;
        c.resources_live = (*desktop).active != 0 || (*desktop).exclusive != 0;
        for i in 0..DG_GL_MAX_CONTEXTS as usize {
            c.resources_live |= !(*resources).contexts[i].native.is_null();
        }
        for i in 0..DG_GL_MAX_DRAWABLES as usize {
            c.resources_live |= !(*resources).drawables[i].native.is_null();
        }
        if error == 0 {
            c.result_bytes = (*b).result_bytes;
            c.result_type = (*b).result_type;
            if !(*b).bulk_result.is_null() {
                c.bulk_result = (*b).bulk_result;
                (*b).bulk_result = ptr::null_mut();
            } else {
                assert!(c.result_bytes <= DG_GL_MAX_RESULT_BYTES);
                ptr::copy_nonoverlapping(
                    (*b).result.as_ptr(),
                    c.result.as_mut_ptr(),
                    c.result_bytes as usize,
                );
            }
        }
        drop_batch(m, b);
        push(s, m, c);
    }
}
/// # Safety
/// Engine mutex held. Success transfers the result's bulk allocation to the caller.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_submission_poll(s: *mut Submission, out: *mut Completion) -> u32 {
    unsafe {
        if (*s).len == 0 {
            return 0;
        }
        let i = (*s).head as usize;
        out.write((*s).done[i]);
        (*s).done[i] = core::mem::zeroed();
        (*s).head = ((*s).head + 1) % DONE as u32;
        (*s).len -= 1;
        1
    }
}
/// # Safety
/// All worker threads joined; no callbacks/readers remain. Clears owned slots before
/// allocation callbacks and drops pending snapshot and every unconsumed result once.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_submission_free(s: *mut Submission, m: *const Memory) {
    unsafe {
        let m = *m;
        drop_pending(s, m);
        let mut c: Completion = core::mem::zeroed();
        while dreamgpu_submission_poll(s, &mut c) != 0 {
            free(m, c.bulk_result.cast());
        }
    }
}
#[cfg(test)]
mod tests;
