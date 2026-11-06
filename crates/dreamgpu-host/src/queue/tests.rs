// SPDX-License-Identifier: GPL-2.0-or-later
//! Device queue against modelled guest RAM and the real engine submission FIFO.
use super::*;
use crate::{
    composition::State,
    device::Memory,
    gl::Resources,
    submission::{self as sub, Frame, Submission},
};
use core::ptr;
use std::{
    alloc::{alloc, dealloc, Layout},
    collections::HashMap,
    vec::Vec,
};

const PAGE: u64 = 0x1000;
const BUFFERS: u64 = 0x10000;
const RESULT: u64 = 0x8000;
const RAM: usize = 0x100000;

struct Host {
    ram: Vec<u8>,
    deny_ram: bool,
    snapshots: HashMap<usize, Layout>,
    engine: Submission,
    resources: Resources,
    engine_present: bool,
    /// Execution error per guest sequence; absent means success.
    errors: HashMap<u32, u32>,
    executed: Vec<u32>,
    results: HashMap<u32, Vec<u8>>,
    generation: u32,
    writes: usize,
    stops: Vec<(u32, u32)>,
}
impl Host {
    fn new() -> Box<Self> {
        Box::new(Self {
            ram: vec![0; RAM],
            deny_ram: false,
            snapshots: HashMap::new(),
            engine: unsafe { core::mem::zeroed() },
            resources: Resources::default(),
            engine_present: true,
            errors: HashMap::new(),
            executed: Vec::new(),
            results: HashMap::new(),
            generation: 3,
            writes: 0,
            stops: Vec::new(),
        })
    }
    fn word(&self, address: u64) -> u32 {
        let a = address as usize;
        u32::from_le_bytes(self.ram[a..a + 4].try_into().unwrap())
    }
    fn set(&mut self, address: u64, value: u32) {
        let a = address as usize;
        self.ram[a..a + 4].copy_from_slice(&value.to_le_bytes());
    }
}
fn host(o: *mut c_void) -> &'static mut Host {
    unsafe { &mut *o.cast::<Host>() }
}
unsafe extern "C" fn allocate(o: *mut c_void, bytes: u32) -> *mut u8 {
    let layout = Layout::from_size_align(bytes.max(1) as usize, 8).unwrap();
    let p = unsafe { alloc(layout) };
    host(o).snapshots.insert(p as usize, layout);
    p
}
unsafe extern "C" fn release(o: *mut c_void, p: *mut u8) {
    let layout = host(o).snapshots.remove(&(p as usize)).expect("one owner");
    unsafe { dealloc(p, layout) };
}
unsafe extern "C" fn sub_allocate(o: *mut c_void, bytes: usize) -> *mut c_void {
    unsafe { allocate(o, bytes as u32).cast() }
}
unsafe extern "C" fn sub_release(o: *mut c_void, p: *mut c_void) {
    unsafe { release(o, p.cast()) }
}
unsafe extern "C" fn read(o: *mut c_void, address: u64, data: *mut u8, bytes: u32) -> u32 {
    let h = host(o);
    let (a, n) = (address as usize, bytes as usize);
    if a + n > h.ram.len() {
        return 0;
    }
    unsafe { ptr::copy_nonoverlapping(h.ram.as_ptr().add(a), data, n) };
    1
}
unsafe extern "C" fn write_ram(o: *mut c_void, address: u64, data: *const u8, bytes: u32) -> u32 {
    let h = host(o);
    let (a, n) = (address as usize, bytes as usize);
    if a + n > h.ram.len() {
        return 0;
    }
    h.writes += 1;
    unsafe { ptr::copy_nonoverlapping(data, h.ram.as_mut_ptr().add(a), n) };
    1
}
unsafe extern "C" fn ram(o: *mut c_void, address: u64, bytes: u32, _: u32) -> u32 {
    let h = host(o);
    (!h.deny_ram && address as usize + bytes as usize <= h.ram.len()) as u32
}
unsafe extern "C" fn validate(
    _: *mut c_void,
    data: *const u8,
    bytes: u32,
    records: *mut u32,
) -> u32 {
    unsafe { records.write(1) };
    let word = |i: usize| unsafe { ptr::read_unaligned(data.cast::<u32>().add(i)) };
    if bytes < 32 || word(1) != bytes {
        return DG_GL_ERROR_BATCH;
    }
    // Opcode 99 models any record the host validator rejects.
    if word(0) == 99 {
        DG_GL_ERROR_UNSUPPORTED
    } else {
        0
    }
}
unsafe extern "C" fn begin(_: *mut c_void, _: u32) {}
unsafe extern "C" fn record(_: *mut c_void, _: *const u8) {}
fn memory(o: *mut c_void) -> sub::Memory {
    sub::Memory {
        opaque: o,
        allocate: sub_allocate,
        free: sub_release,
    }
}
unsafe extern "C" fn enqueue(
    o: *mut c_void,
    data: *mut u8,
    bytes: u32,
    records: u32,
    info: *const Enqueue,
) -> u32 {
    let h = host(o);
    if !h.engine_present {
        return DG_GL_ERROR_TRANSPORT;
    }
    let info = unsafe { *info };
    let r = sub::Request {
        data,
        bytes: bytes as usize,
        sequence: info.sequence,
        generation: h.generation,
        records,
        trace_queued_us: 0,
        primary_width: 0,
        primary_height: 0,
        client: info.client,
        stream: info.stream,
        error: 0,
    };
    let m = memory(o);
    if unsafe { sub::dreamgpu_submission_submit(&mut h.engine, &m, &r, 0) } != 0 {
        0
    } else {
        DG_GL_ERROR_LIMIT
    }
}
unsafe extern "C" fn marker(o: *mut c_void, info: *const Enqueue) -> u32 {
    let h = host(o);
    if !h.engine_present {
        return 1;
    }
    let info = unsafe { *info };
    let r = sub::Request {
        data: ptr::null_mut(),
        bytes: 0,
        sequence: info.sequence,
        generation: h.generation,
        records: 0,
        trace_queued_us: 0,
        primary_width: 0,
        primary_height: 0,
        client: info.client,
        stream: info.stream,
        error: info.error,
    };
    let m = memory(o);
    (unsafe { sub::dreamgpu_submission_submit(&mut h.engine, &m, &r, 0) } == 0) as u32
}
unsafe extern "C" fn barrier(_: *mut c_void) {}
unsafe extern "C" fn stop(o: *mut c_void, sequence: u32) {
    let h = host(o);
    // Record what the guest could see when the device stopped it.
    let completed = h.word(PAGE + u64::from(DG_GL_QUEUE_COMPLETED));
    h.stops.push((sequence, completed));
}

struct Device {
    host: Box<Host>,
    queue: Box<Queue>,
    produced: u32,
}
impl Device {
    fn new() -> Self {
        let mut d = Self {
            host: Host::new(),
            queue: Box::new(unsafe { core::mem::zeroed() }),
            produced: 0,
        };
        assert_eq!(d.enable(PAGE, 3), 1);
        d
    }
    fn callbacks(&mut self) -> QueueCallbacks {
        let o = (&mut *self.host as *mut Host).cast();
        QueueCallbacks {
            gl: GlCallbacks {
                memory: Memory {
                    opaque: o,
                    allocate,
                    free: release,
                    read,
                    ram,
                },
                validate,
                diagnostics_begin: begin,
                diagnostic_record: record,
                enqueue,
            },
            write: write_ram,
            barrier,
            marker,
            fault: stop,
        }
    }
    fn enable(&mut self, address: u64, generation: u32) -> u32 {
        let cb = self.callbacks();
        let current = self.host.generation;
        self.produced = 0;
        if address + 4096 <= RAM as u64 {
            self.host.set(address + u64::from(DG_GL_QUEUE_PRODUCED), 0);
            self.host.set(address + u64::from(DG_GL_QUEUE_WAIT), 0);
        }
        unsafe { dreamgpu_queue_enable(&mut *self.queue, &cb, address, generation, current, 0) }
    }
    fn entry(&self, index: u32) -> u64 {
        PAGE + u64::from(DG_GL_QUEUE_RING) + u64::from(index % 16) * 64
    }
    /// Publish one batch whose single word is `opcode` (sequence `seq`).
    fn publish(&mut self, seq: u32, opcode: u32, bytes: u32, domain: (u32, u32), result: u32) {
        let index = self.produced;
        let buffer = BUFFERS + u64::from(index % 8) * 0x8000;
        // One record spanning the batch, as the real validator requires.
        self.host.set(buffer, opcode);
        self.host.set(buffer + 4, bytes);
        let e = self.entry(index);
        let generation = self.host.generation;
        for (offset, value) in [
            (DG_GL_ENTRY_ADDR_LO, buffer as u32),
            (DG_GL_ENTRY_ADDR_HI, 0),
            (DG_GL_ENTRY_BYTES, bytes),
            (DG_GL_ENTRY_SEQUENCE, seq),
            (DG_GL_ENTRY_GENERATION, generation),
            (DG_GL_ENTRY_CLIENT, domain.0),
            (DG_GL_ENTRY_STREAM, domain.1),
            (
                DG_GL_ENTRY_RESULT_ADDR_LO,
                if result != 0 { RESULT as u32 } else { 0 },
            ),
            (DG_GL_ENTRY_RESULT_ADDR_HI, 0),
            (DG_GL_ENTRY_RESULT_CAPACITY, result),
            (DG_GL_ENTRY_RESERVED0, 0),
            (DG_GL_ENTRY_RESERVED1, 0),
            (DG_GL_ENTRY_STATUS, 0xdead),
        ] {
            self.host.set(e + u64::from(offset), value);
        }
        self.produced = self.produced.wrapping_add(1);
        let produced = self.produced;
        self.host
            .set(PAGE + u64::from(DG_GL_QUEUE_PRODUCED), produced);
    }
    fn doorbell(&mut self) -> u32 {
        let cb = self.callbacks();
        let generation = self.host.generation;
        unsafe { dreamgpu_queue_consume(&mut *self.queue, &cb, generation, 32, 0) }
    }
    /// Run the engine for one batch and deliver its completion. Returns the
    /// guest sequence and outcome, or None when the engine is idle.
    fn step(&mut self) -> Option<(u32, QueueOutcome)> {
        let h = &mut *self.host;
        let b = unsafe { sub::dreamgpu_submission_take(&mut h.engine) };
        if b.is_null() {
            return None;
        }
        let (seq, preset) = unsafe { ((*b).sequence, (*b).error) };
        let error = if preset != 0 {
            preset
        } else {
            h.executed.push(seq);
            if let Some(result) = h.results.get(&seq) {
                unsafe {
                    ptr::copy_nonoverlapping(
                        result.as_ptr(),
                        (*b).result.as_mut_ptr(),
                        result.len(),
                    );
                    (*b).result_bytes = result.len() as u32;
                    (*b).result_type = DG_GL_RESULT_INT;
                }
            }
            h.errors.get(&seq).copied().unwrap_or(0)
        };
        let o = (h as *mut Host).cast();
        let m = memory(o);
        let d: State = unsafe { core::mem::zeroed() };
        let f: Frame = Frame {
            slot: 2,
            client: 1,
            drawable: 1,
            epoch: 0x1_0000_0005,
            generation: seq as u64,
        };
        unsafe {
            sub::dreamgpu_submission_complete(&mut h.engine, &m, b, error, &h.resources, &d, &f)
        };
        let mut c: Completion = unsafe { core::mem::zeroed() };
        assert_eq!(
            unsafe { sub::dreamgpu_submission_poll(&mut h.engine, &mut c) },
            1
        );
        assert_eq!(c.sequence, seq);
        let cb = self.callbacks();
        let outcome = unsafe { dreamgpu_queue_complete(&mut *self.queue, &cb, &c) };
        Some((seq, outcome))
    }
    fn drain(&mut self) -> Vec<u32> {
        let mut done = Vec::new();
        loop {
            while let Some((seq, _)) = self.step() {
                done.push(seq);
            }
            let consumed = self.queue.consumed;
            self.doorbell();
            if self.queue.consumed == consumed && self.queue.len == 0 {
                return done;
            }
        }
    }
    fn page(&self, offset: u32) -> u32 {
        self.host.word(PAGE + u64::from(offset))
    }
    fn status(&self, index: u32) -> (u32, u32) {
        let e = self.entry(index);
        (
            self.host.word(e + u64::from(DG_GL_ENTRY_STATUS)),
            self.host.word(e + u64::from(DG_GL_ENTRY_ERROR)),
        )
    }
}
impl Drop for Device {
    fn drop(&mut self) {
        let o = (&mut *self.host as *mut Host).cast();
        let m = memory(o);
        unsafe {
            let mut c: Completion = core::mem::zeroed();
            while sub::dreamgpu_submission_poll(&mut self.host.engine, &mut c) != 0 {}
            sub::dreamgpu_submission_free(&mut self.host.engine, &m);
        }
        assert!(self.host.snapshots.is_empty(), "snapshot leaked");
    }
}
const OK: u32 = DG_STATUS_DONE;
const FAILED: u32 = DG_STATUS_DONE | DG_STATUS_ERROR;

#[test]
fn enable_requires_current_generation_aligned_ram_and_idle_legacy() {
    let mut d = Device::new();
    assert_eq!(d.page(DG_GL_QUEUE_STATE), DG_GL_QUEUE_ACTIVE);
    assert_eq!(d.page(DG_GL_QUEUE_GENERATION), 3);
    let cb = d.callbacks();
    let q = &mut *d.queue;
    unsafe {
        assert_eq!(dreamgpu_queue_enable(q, &cb, PAGE, 2, 3, 0), 0);
        assert_eq!(dreamgpu_queue_enable(q, &cb, PAGE + 4, 3, 3, 0), 0);
        assert_eq!(dreamgpu_queue_enable(q, &cb, PAGE, 3, 3, 1), 0);
        assert_eq!(dreamgpu_queue_enable(q, &cb, RAM as u64, 3, 3, 0), 0);
        assert_eq!(dreamgpu_queue_enable(q, &cb, !4095u64, 3, 3, 0), 0);
    }
    d.host.deny_ram = true;
    assert_eq!(d.enable(PAGE, 3), 0);
    d.host.deny_ram = false;
    assert_eq!(d.enable(PAGE, 3), 1);
    // Disabling an idle queue clears STATE; a busy one refuses.
    d.publish(1, 6, 32, (0, 0), 0);
    d.doorbell();
    let cb = d.callbacks();
    assert_eq!(unsafe { dreamgpu_queue_disable(&mut *d.queue, &cb) }, 0);
    assert_eq!(d.drain(), vec![1]);
    assert_eq!(unsafe { dreamgpu_queue_disable(&mut *d.queue, &cb) }, 1);
    assert_eq!(d.page(DG_GL_QUEUE_STATE), 0);
    assert_eq!(d.doorbell(), 0);
}

#[test]
fn pipelined_batches_complete_in_order_across_ring_reuse_and_depth() {
    let mut d = Device::new();
    let mut next = 1;
    let mut completed = Vec::new();
    // 70 batches: the ring wraps four times and the device never holds more
    // than sixteen unfinished entries, however far ahead the guest publishes.
    for round in 0..7 {
        for _ in 0..10 {
            d.publish(next, 6, 32 + round * 4, (0, 0), 0);
            next += 1;
        }
        d.doorbell();
        assert!(d.queue.len as usize <= DEPTH);
        assert_eq!(d.page(DG_GL_QUEUE_CONSUMED), d.queue.consumed);
        // Consumed command RAM is reusable before execution: the host copied it.
        assert_eq!(d.queue.consumed, d.produced);
        completed.extend(d.drain());
        assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), d.produced);
    }
    assert_eq!(completed, (1..next).collect::<Vec<_>>());
    assert_eq!(d.host.executed, completed);
    for i in 60..70 {
        assert_eq!(d.status(i), (OK, 0));
    }
    // PRESENT identity of the last completion is published with it.
    assert_eq!(d.page(DG_GL_QUEUE_PRESENT_SLOT), 2);
    assert_eq!(d.page(DG_GL_QUEUE_PRESENT_EPOCH_LO), 5);
    assert_eq!(d.page(DG_GL_QUEUE_PRESENT_EPOCH_HI), 1);
    assert_eq!(d.page(DG_GL_QUEUE_PRESENT_FRAME_LO), 70);
    // A full ring stops consumption at the depth until completions free it.
    for _ in 0..16 {
        d.publish(next, 6, 32, (0, 0), 0);
        next += 1;
    }
    d.doorbell();
    assert_eq!(d.queue.len, 16);
    assert_eq!(d.step().map(|s| s.0), Some(71));
    assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), 71);
    d.publish(next, 6, 32, (0, 0), 0);
    d.doorbell();
    assert_eq!(d.queue.len, 16);
    assert_eq!(d.drain().len(), 16);
    assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), 87);
}

#[test]
fn counters_wrap_as_signed_differences() {
    let mut d = Device::new();
    let start = 0xffff_fff8u32;
    d.queue.consumed = start;
    d.queue.completed = start;
    d.produced = start;
    // Arm a completion wait across the wrap: it holds only once reached.
    let target = start.wrapping_add(20);
    d.host
        .set(PAGE + u64::from(DG_GL_QUEUE_WAIT_COMPLETED), target);
    d.host.set(
        PAGE + u64::from(DG_GL_QUEUE_WAIT),
        DG_GL_QUEUE_WAIT_FOR_COMPLETED,
    );
    let mut raised = Vec::new();
    for seq in 0..24 {
        if d.produced.wrapping_sub(d.queue.completed) == 16 {
            let (done, outcome) = d.step().unwrap();
            if outcome.flags & OUTCOME_IRQ != 0 {
                raised.push(done);
            }
        }
        d.publish(seq, 6, 32, (0, 0), 0);
        d.doorbell();
        assert!(d.queue.len as usize <= DEPTH);
    }
    while let Some((done, outcome)) = d.step() {
        if outcome.flags & OUTCOME_IRQ != 0 {
            raised.push(done);
        }
    }
    assert_eq!(raised, (19..24).collect::<Vec<_>>());
    assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), start.wrapping_add(24));
    assert_eq!(d.status(start.wrapping_add(23)), (OK, 0));
}

#[test]
fn waits_raise_only_when_armed_and_satisfied() {
    let mut d = Device::new();
    d.publish(1, 6, 32, (0, 0), 0);
    assert_eq!(d.doorbell(), 0, "unarmed");
    assert_eq!(d.step().unwrap().1.flags, 0, "unarmed completion");
    d.host.set(PAGE + u64::from(DG_GL_QUEUE_WAIT_CONSUMED), 2);
    d.host.set(
        PAGE + u64::from(DG_GL_QUEUE_WAIT),
        DG_GL_QUEUE_WAIT_FOR_CONSUMED,
    );
    d.publish(2, 6, 32, (0, 0), 0);
    assert_eq!(d.doorbell(), OUTCOME_IRQ);
    d.host.set(PAGE + u64::from(DG_GL_QUEUE_WAIT_COMPLETED), 2);
    d.host.set(
        PAGE + u64::from(DG_GL_QUEUE_WAIT),
        DG_GL_QUEUE_WAIT_FOR_COMPLETED,
    );
    assert_eq!(d.doorbell(), 0, "completion not yet reached");
    assert_eq!(d.step().unwrap().1.flags, OUTCOME_IRQ);
    assert_eq!(d.queue.interrupts, 2);
}

#[test]
fn faults_skip_later_batches_of_their_domain_only() {
    let mut d = Device::new();
    d.host.errors.insert(2, DG_GL_ERROR_HOST);
    // Domain A: 1 ok, 2 fails while 3 and 5 are already queued behind it.
    // Domain B (4) and the unscoped batch (6) still execute.
    d.publish(1, 6, 32, (7, 1), 0);
    d.publish(2, 6, 32, (7, 1), 0);
    d.publish(3, 6, 32, (7, 1), 0);
    d.publish(4, 6, 32, (7, 2), 0);
    d.publish(5, 6, 32, (7, 1), 0);
    d.publish(6, 6, 32, (0, 0), 0);
    d.doorbell();
    assert_eq!(d.drain(), vec![1, 2, 3, 4, 5, 6]);
    assert_eq!(d.host.executed, vec![1, 2, 4, 6]);
    assert_eq!(d.status(1), (FAILED, DG_GL_ERROR_HOST));
    assert_eq!(d.status(2), (FAILED, DG_GL_ERROR_SKIPPED));
    assert_eq!(d.status(3), (OK, 0));
    assert_eq!(d.status(4), (FAILED, DG_GL_ERROR_SKIPPED));
    // Consumed after the device saw the fault: skipped without a snapshot.
    d.publish(7, 6, 32, (7, 1), 0);
    d.publish(8, 6, 32, (7, 2), 0);
    d.doorbell();
    assert_eq!(d.drain(), vec![8]);
    assert_eq!(d.status(6), (FAILED, DG_GL_ERROR_SKIPPED));
    assert_eq!(d.status(7), (OK, 0));
    assert_eq!(d.host.executed, vec![1, 2, 4, 6, 8]);
}

#[test]
fn validation_failures_complete_in_order_and_fault_their_domain() {
    let mut d = Device::new();
    // Rejected with nothing ahead: completes at consumption.
    d.publish(1, 99, 32, (5, 5), 0);
    d.doorbell();
    assert_eq!(
        (d.status(0), d.page(DG_GL_QUEUE_COMPLETED)),
        ((FAILED, DG_GL_ERROR_UNSUPPORTED), 1)
    );
    // Rejected behind an executing batch: its completion waits its turn and
    // the engine skips the same domain's batches queued after it.
    d.publish(2, 6, 32, (6, 6), 0);
    d.publish(3, 99, 32, (6, 6), 0);
    d.publish(4, 6, 32, (6, 6), 0);
    d.publish(5, 6, 32, (8, 8), 0);
    d.doorbell();
    assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), 1);
    assert_eq!(d.drain(), vec![2, 3, 4, 5]);
    assert_eq!(d.host.executed, vec![2, 5]);
    // The device remembers the first domain too: consumed later, skipped.
    d.publish(50, 6, 32, (5, 5), 0);
    d.doorbell();
    assert_eq!(d.status(5), (FAILED, DG_GL_ERROR_SKIPPED));
    assert_eq!(d.status(2), (FAILED, DG_GL_ERROR_UNSUPPORTED));
    assert_eq!(d.status(3), (FAILED, DG_GL_ERROR_SKIPPED));
    assert_eq!(d.status(4), (OK, 0));
    // Reserved words, stale generation and malformed sizes never reach the engine.
    d.publish(6, 6, 32, (0, 0), 0);
    let e = d.entry(6);
    d.host.set(e + u64::from(DG_GL_ENTRY_RESERVED1), 1);
    d.publish(7, 6, 32, (0, 0), 0);
    d.host
        .set(d.entry(7) + u64::from(DG_GL_ENTRY_GENERATION), 2);
    d.publish(8, 6, 30, (0, 0), 0);
    d.doorbell();
    assert_eq!(d.status(6), (FAILED, DG_GL_ERROR_BATCH));
    assert_eq!(d.status(7), (FAILED, DG_GL_ERROR_GENERATION));
    assert_eq!(d.status(8), (FAILED, DG_GL_ERROR_BATCH));
    assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), 9);
    // Without any engine a rejection behind queued work still waits its turn.
    d.publish(9, 6, 32, (0, 0), 0);
    d.doorbell();
    d.host.engine_present = false;
    d.publish(10, 99, 32, (0, 0), 0);
    d.doorbell();
    assert_eq!(d.queue.len, 2);
    assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), 9);
    assert_eq!(d.step().map(|s| s.0), Some(9));
    assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), 11);
    assert_eq!(d.status(10), (FAILED, DG_GL_ERROR_UNSUPPORTED));
}

#[test]
fn query_results_are_dmaed_with_their_entry() {
    let mut d = Device::new();
    d.host.results.insert(2, vec![1, 2, 3, 4]);
    d.host.results.insert(3, vec![9; 16]);
    let query = |d: &mut Device, seq: u32| {
        let buffer = BUFFERS + u64::from(d.produced % 8) * 0x8000;
        d.publish(seq, DG_GL_QUERY, DG_GL_QUERY_BYTES, (0, 0), 8);
        d.host.set(buffer + 32, FEnum_glGetError);
    };
    d.publish(1, 6, 32, (0, 0), 0);
    query(&mut d, 2);
    // A result larger than the entry's capacity: DMA error, nothing copied.
    query(&mut d, 3);
    d.doorbell();
    d.drain();
    let e = d.entry(1);
    assert_eq!(d.status(1), (OK, 0));
    assert_eq!(d.host.word(e + u64::from(DG_GL_ENTRY_RESULT_BYTES)), 4);
    assert_eq!(
        d.host.word(e + u64::from(DG_GL_ENTRY_RESULT_TYPE)),
        DG_GL_RESULT_INT
    );
    assert_eq!(
        &d.host.ram[RESULT as usize..RESULT as usize + 8],
        &[1, 2, 3, 4, 0, 0, 0, 0]
    );
    assert_eq!(d.status(2), (FAILED, DG_GL_ERROR_DMA));
    assert_eq!(
        d.host
            .word(d.entry(2) + u64::from(DG_GL_ENTRY_RESULT_BYTES)),
        0
    );
}

#[test]
fn reset_discards_unfinished_entries_and_requires_reenable() {
    let mut d = Device::new();
    for seq in 1..=6 {
        d.publish(seq, 6, 32, (1, 1), 0);
    }
    d.host.errors.insert(1, DG_GL_ERROR_HOST);
    d.doorbell();
    assert_eq!(d.step().map(|s| s.0), Some(1));
    // Engine reset drops pending snapshots; the device forgets its entries.
    let o = (&mut *d.host as *mut Host).cast();
    let m = memory(o);
    unsafe { sub::dreamgpu_submission_reset(&mut d.host.engine, &m, 4, 0, 0) };
    let cb = d.callbacks();
    unsafe { dreamgpu_queue_reset(&mut *d.queue, &cb) };
    assert_eq!(d.page(DG_GL_QUEUE_STATE), 0);
    assert_eq!((d.queue.len, d.queue.pending_bytes), (0, 0));
    assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), 1);
    // A late doorbell for the old page does nothing; a stale completion has
    // no entry to finish.
    assert_eq!(d.doorbell(), 0);
    let c: Completion = unsafe { core::mem::zeroed() };
    assert_eq!(
        unsafe { dreamgpu_queue_complete(&mut *d.queue, &cb, &c) }.flags,
        0
    );
    // Re-enabled for the new generation, counters restart and the old fault
    // domain executes again.
    d.host.generation = 4;
    assert_eq!(d.enable(PAGE, 3), 0);
    assert_eq!(d.enable(PAGE, 4), 1);
    assert_eq!(
        (d.page(DG_GL_QUEUE_CONSUMED), d.page(DG_GL_QUEUE_COMPLETED)),
        (0, 0)
    );
    d.publish(10, 6, 32, (1, 1), 0);
    d.doorbell();
    assert_eq!(d.drain(), vec![10]);
    assert_eq!(d.host.executed, vec![1, 10]);
}

#[test]
fn overrun_stops_consumption_but_accepted_entries_finish() {
    let mut d = Device::new();
    d.publish(1, 6, 32, (0, 0), 0);
    d.doorbell();
    d.host.set(PAGE + u64::from(DG_GL_QUEUE_PRODUCED), 40);
    assert_eq!(d.doorbell(), 0);
    assert_eq!(d.page(DG_GL_QUEUE_STATE), 0);
    assert_eq!(d.step().map(|s| s.0), Some(1));
    assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), 1);
    assert_eq!(d.queue.consumed, 1);
}

#[test]
fn snapshot_budget_bounds_host_memory() {
    let mut d = Device::new();
    let big = 12 << 20;
    // Guest RAM is small in this model; command bytes beyond it fail DMA,
    // so only the bytes accounting is exercised through rejected snapshots.
    d.queue.pending_bytes = 0;
    d.publish(1, 6, 32, (0, 0), 0);
    d.doorbell();
    d.queue.pending_bytes = PENDING_BYTES - 16;
    d.publish(2, 6, big, (0, 0), 0);
    d.doorbell();
    assert_eq!(d.queue.consumed, 1, "over budget waits for a completion");
    d.queue.pending_bytes = 32;
    assert_eq!(d.step().map(|s| s.0), Some(1));
    d.doorbell();
    assert_eq!(d.queue.consumed, 2);
    d.drain();
    assert_eq!(d.status(1), (FAILED, DG_GL_ERROR_DMA));
}

#[test]
fn coherence_sensitive_failure_requests_a_fault_stop() {
    let mut d = Device::new();
    // A DESKTOP READBACK record marks its batch sensitive before validation.
    let buffer = BUFFERS;
    d.publish(5, 6, 96, (0, 0), 0);
    let mut words = [0u32; 24];
    words[0] = DG_GL_DESKTOP;
    words[1] = 96;
    words[8] = DG_DESKTOP_READBACK;
    for (i, w) in words.iter().enumerate() {
        d.host.set(buffer + i as u64 * 4, *w);
    }
    d.host.errors.insert(5, DG_GL_ERROR_DESKTOP);
    d.doorbell();
    let (_, outcome) = d.step().unwrap();
    assert_eq!(outcome.flags & OUTCOME_FAULT, OUTCOME_FAULT);
    assert_eq!(outcome.fault_sequence, 5);
    // Stopped before its completion became visible.
    assert_eq!(d.host.stops, vec![(5, 0)]);
    assert_eq!(d.page(DG_GL_QUEUE_COMPLETED), 1);
}

#[test]
fn native_queue_layout_matches_shared_header() {
    if cfg!(target_pointer_width = "64") {
        assert_eq!(core::mem::size_of::<QueueEntry>(), 48);
        assert_eq!(core::mem::size_of::<Queue>(), 976);
        assert_eq!(core::mem::offset_of!(Queue, kicks), 944);
        assert_eq!(core::mem::size_of::<QueueOutcome>(), 8);
        assert_eq!(core::mem::size_of::<Enqueue>(), 16);
    }
}
