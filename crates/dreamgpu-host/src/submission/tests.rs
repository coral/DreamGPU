// SPDX-License-Identifier: GPL-2.0-or-later
use super::*;
use std::{
    alloc::{alloc, dealloc, Layout},
    collections::HashMap,
};
#[derive(Default)]
struct Heap {
    live: HashMap<usize, Layout>,
    fail: bool,
    freed: usize,
}
unsafe extern "C" fn allocate(o: *mut c_void, n: usize) -> *mut c_void {
    unsafe {
        let h = &mut *o.cast::<Heap>();
        if h.fail {
            return ptr::null_mut();
        }
        let layout = Layout::from_size_align(n.max(1), 8).unwrap();
        let p = alloc(layout);
        p.write_bytes(0xa5, n);
        assert!(h.live.insert(p as usize, layout).is_none());
        p.cast()
    }
}
unsafe extern "C" fn release(o: *mut c_void, p: *mut c_void) {
    unsafe {
        let h = &mut *o.cast::<Heap>();
        let layout = h.live.remove(&(p as usize)).expect("exactly one owner");
        h.freed += 1;
        dealloc(p.cast(), layout);
    }
}
fn memory(h: &mut Heap) -> Memory {
    Memory {
        opaque: (h as *mut Heap).cast(),
        allocate,
        free: release,
    }
}
unsafe fn request(m: &Memory, seq: u32) -> Request {
    Request {
        data: unsafe { allocate(m.opaque, 64).cast() },
        bytes: 64,
        sequence: seq,
        generation: 9,
        records: 2,
        trace_queued_us: 0,
        primary_width: 640,
        primary_height: 480,
    }
}
#[test]
fn submission_failure_reset_and_executing_snapshot_ownership() {
    unsafe {
        let mut h = Heap::default();
        let m = memory(&mut h);
        let mut s: Submission = core::mem::zeroed();
        let r = request(&m, 1);
        h.fail = true;
        assert_eq!(dreamgpu_submission_submit(&mut s, &m, &r, 0), 0);
        assert_eq!(h.live.len(), 1);
        h.fail = false;
        assert_eq!(dreamgpu_submission_submit(&mut s, &m, &r, 1), 0);
        assert_eq!(dreamgpu_submission_submit(&mut s, &m, &r, 0), 1);
        let r2 = request(&m, 2);
        assert_eq!(dreamgpu_submission_submit(&mut s, &m, &r2, 0), 0);
        free(m, r2.data.cast());
        let running = dreamgpu_submission_take(&mut s);
        assert_eq!((*running).data, r.data);
        assert!(s.pending.is_null());
        let r3 = request(&m, 3);
        assert_eq!(dreamgpu_submission_submit(&mut s, &m, &r3, 0), 1);
        dreamgpu_submission_reset(&mut s, &m, 10, 88, 99);
        assert!(s.pending.is_null());
        assert_eq!(h.live.len(), 2);
        assert_eq!(
            (
                s.reset,
                s.reset_generation,
                s.reset_cpu_epoch,
                s.reset_cpu_generation
            ),
            (1, 10, 88, 99)
        );
        let mut d: State = core::mem::zeroed();
        d.active = 1;
        d.exclusive = 1;
        d.coherent = 1;
        let mut f: Frame = core::mem::zeroed();
        f.generation = 9;
        let mut ticket: ResetTicket = core::mem::zeroed();
        assert_eq!(dreamgpu_submission_reset_snapshot(&s, &mut ticket), 1);
        assert_eq!(
            dreamgpu_submission_reset_done(&mut s, &m, &ticket, &mut d, &mut f),
            1
        );
        assert_eq!(
            (d.active, d.exclusive, d.coherent, f.generation, s.reset),
            (0, 0, 0, 0, 0)
        );
        let resources = Resources::default();
        dreamgpu_submission_complete(
            &mut s,
            &m,
            running,
            DG_GL_ERROR_GENERATION,
            &resources,
            &d,
            &f,
        );
        assert!(h.live.is_empty());
        let mut c: Completion = core::mem::zeroed();
        assert_eq!(dreamgpu_submission_poll(&mut s, &mut c), 1);
        assert!(c.reset);
        assert_eq!(c.generation, 10);
        assert_eq!(dreamgpu_submission_poll(&mut s, &mut c), 1);
        assert_eq!(c.error, DG_GL_ERROR_GENERATION);
        dreamgpu_submission_free(&mut s, &m);
        assert!(h.live.is_empty());
    }
}
#[test]
fn bulk_results_move_on_success_free_on_error_and_bound_ring() {
    unsafe {
        let mut h = Heap::default();
        let m = memory(&mut h);
        let mut s: Submission = core::mem::zeroed();
        let mut resources = Resources::default();
        let d: State = core::mem::zeroed();
        let f: Frame = core::mem::zeroed();
        resources.contexts[0].native = std::ptr::dangling_mut::<c_void>();
        for seq in 0..25 {
            let r = request(&m, seq);
            assert_eq!(dreamgpu_submission_submit(&mut s, &m, &r, 0), 1);
            let b = dreamgpu_submission_take(&mut s);
            assert!(dreamgpu_submission_query(b, &m, DG_GL_MAX_READBACK_BYTES + 1).is_null());
            let p = dreamgpu_submission_query(b, &m, 65536);
            assert!(!p.is_null());
            p.write_bytes(seq as u8, 65536);
            (*b).result_bytes = 65536;
            (*b).result_type = 2;
            dreamgpu_submission_complete(&mut s, &m, b, 0, &resources, &d, &f);
        }
        assert_eq!((s.len, h.live.len()), (16, 16));
        let mut c: Completion = core::mem::zeroed();
        for seq in 9..25 {
            assert_eq!(dreamgpu_submission_poll(&mut s, &mut c), 1);
            assert_eq!(c.sequence, seq);
            assert!(c.resources_live);
            assert_eq!(*c.bulk_result, seq as u8);
            assert_eq!(c.result_bytes, 65536);
            free(m, c.bulk_result.cast());
        }
        assert!(h.live.is_empty());
        let r = request(&m, 26);
        dreamgpu_submission_submit(&mut s, &m, &r, 0);
        let b = dreamgpu_submission_take(&mut s);
        h.fail = true;
        assert!(dreamgpu_submission_query(b, &m, 65536).is_null());
        h.fail = false;
        assert!(!dreamgpu_submission_query(b, &m, 65536).is_null());
        dreamgpu_submission_complete(&mut s, &m, b, DG_GL_ERROR_HOST, &resources, &d, &f);
        assert!(h.live.is_empty());
        dreamgpu_submission_poll(&mut s, &mut c);
        assert!(c.bulk_result.is_null());
        assert_eq!(c.result_bytes, 0);
        let r = request(&m, 27);
        dreamgpu_submission_submit(&mut s, &m, &r, 0);
        let b = dreamgpu_submission_take(&mut s);
        let p = dreamgpu_submission_query(b, &m, 4);
        ptr::copy_nonoverlapping([1, 2, 3, 4].as_ptr(), p, 4);
        (*b).result_bytes = 4;
        dreamgpu_submission_complete(&mut s, &m, b, 0, &resources, &d, &f);
        dreamgpu_submission_poll(&mut s, &mut c);
        assert_eq!(&c.result[..4], &[1, 2, 3, 4]);
        assert!(c.bulk_result.is_null());
        let r = request(&m, 28);
        dreamgpu_submission_submit(&mut s, &m, &r, 0);
        dreamgpu_submission_free(&mut s, &m);
        assert!(h.live.is_empty());
    }
}
#[derive(Default)]
struct Calls {
    cancel_after: u32,
    checks: u32,
    closed: u32,
    reports: Vec<u32>,
}
unsafe extern "C" fn cancelled(o: *mut c_void) -> u32 {
    unsafe {
        let c = &mut *o.cast::<Calls>();
        c.checks += 1;
        (c.checks > c.cancel_after) as u32
    }
}
unsafe extern "C" fn report(o: *mut c_void, _: *const u8, error: u32) {
    unsafe { (&mut *o.cast::<Calls>()).reports.push(error) }
}
unsafe extern "C" fn close(o: *mut c_void) {
    unsafe {
        (*o.cast::<Calls>()).closed += 1;
    }
}
unsafe extern "C" fn new_context(_: *mut c_void, _: *mut c_void) -> *mut c_void {
    ptr::null_mut()
}
unsafe extern "C" fn new_drawable(_: *mut c_void, _: u32, _: u32) -> *mut c_void {
    ptr::null_mut()
}
unsafe extern "C" fn free_resource(_: *mut c_void, _: u32) {}
unsafe extern "C" fn in_begin(_: *mut c_void, _: u32) -> u32 {
    0
}
unsafe extern "C" fn current(_: *mut c_void, _: u32, _: u32) -> u32 {
    0
}
unsafe extern "C" fn call(_: *mut c_void, _: u32, _: u32, _: *const u8) -> u32 {
    0
}
unsafe extern "C" fn data(
    _: *mut c_void,
    _: u32,
    _: u32,
    _: *const u8,
    _: *const u8,
    _: u32,
) -> u32 {
    0
}
unsafe extern "C" fn present(_: *mut c_void, _: u32, _: u32, _: u32) -> u32 {
    0
}
unsafe extern "C" fn desktop(_: *mut c_void, _: *const u8) -> u32 {
    0
}
#[test]
fn execution_stops_between_records_on_cancel_error_or_malformed_framing() {
    unsafe {
        let mut h = Heap::default();
        let m = memory(&mut h);
        let mut s: Submission = core::mem::zeroed();
        let r = request(&m, 1);
        let mut records = [0; 64];
        for off in [0, 32] {
            records[off..off + 4].copy_from_slice(&8u32.to_le_bytes());
            records[off + 4..off + 8].copy_from_slice(&32u32.to_le_bytes());
        }
        ptr::copy_nonoverlapping(records.as_ptr(), r.data, 64);
        dreamgpu_submission_submit(&mut s, &m, &r, 0);
        let b = dreamgpu_submission_take(&mut s);
        let mut c = Calls {
            cancel_after: 1,
            ..Calls::default()
        };
        let o = (&mut c as *mut Calls).cast();
        let run = Run {
            opaque: o,
            cancelled,
            report,
        };
        let p = Platform {
            opaque: o,
            context_new: new_context,
            drawable_new: new_drawable,
            context_free: free_resource,
            drawable_free: free_resource,
            close_begin: close,
            in_begin,
            make_current: current,
            call,
            data,
            words: in_begin,
            query: call,
            present,
            desktop,
        };
        let mut resources = Resources::default();
        assert_eq!(
            dreamgpu_submission_run(b, &mut resources, &p, &run, 0),
            DG_GL_ERROR_GENERATION
        );
        assert_eq!((c.closed, c.reports.len()), (1, 1));
        c.cancel_after = 100;
        c.checks = 0;
        c.closed = 0;
        c.reports.clear();
        assert_eq!(dreamgpu_submission_run(b, &mut resources, &p, &run, 0), 0);
        assert_eq!(c.closed, 2);
        c.reports.clear();
        ptr::copy_nonoverlapping(99u32.to_le_bytes().as_ptr(), r.data, 4);
        assert_eq!(
            dreamgpu_submission_run(b, &mut resources, &p, &run, 0),
            DG_GL_ERROR_UNSUPPORTED
        );
        assert_eq!(c.reports.len(), 1);
        ptr::copy_nonoverlapping(0u32.to_le_bytes().as_ptr(), r.data.add(4), 4);
        assert_eq!(
            dreamgpu_submission_run(b, &mut resources, &p, &run, 0),
            DG_GL_ERROR_BATCH
        );
        c.checks = 0;
        assert_eq!(
            dreamgpu_submission_run(b, &mut resources, &p, &run, DG_GL_ERROR_TRANSPORT),
            DG_GL_ERROR_TRANSPORT
        );
        assert_eq!(c.checks, 0);
        drop_batch(m, b);
        assert!(h.live.is_empty());
    }
}
#[test]
fn native_completion_layout_matches_shared_header() {
    if cfg!(target_pointer_width = "64") {
        assert_eq!(core::mem::size_of::<Frame>(), 32);
        assert_eq!(core::mem::size_of::<Completion>(), 576);
        assert_eq!(core::mem::offset_of!(Completion, bulk_result), 568);
        assert_eq!(core::mem::size_of::<Batch>(), 576);
    }
}

#[test]
fn superseding_reset_cannot_acknowledge_a_packet_with_stale_metadata() {
    unsafe {
        let mut h = Heap::default();
        let m = memory(&mut h);
        let mut s: Submission = core::mem::zeroed();
        let mut d: State = core::mem::zeroed();
        d.active = 1;
        d.coherent = 1;
        let mut f: Frame = core::mem::zeroed();
        f.generation = 20;
        let mut old: ResetTicket = core::mem::zeroed();
        assert_eq!(dreamgpu_submission_reset_snapshot(&s, &mut old), 0);
        dreamgpu_submission_reset(&mut s, &m, 10, 88, 99);
        assert_eq!(dreamgpu_submission_reset_snapshot(&s, &mut old), 1);
        // These transitions model the BQL reset callback running while the render
        // worker has unlocked for native packet I/O. The sent ticket stays immutable.
        for (generation, epoch, frame) in [(11, 88, 99), (10, 89, 99), (10, 88, 100)] {
            dreamgpu_submission_reset(&mut s, &m, generation, epoch, frame);
            assert_eq!(
                dreamgpu_submission_reset_done(&mut s, &m, &old, &mut d, &mut f),
                0
            );
            assert_eq!(
                (s.reset, s.len, d.active, d.coherent, f.generation),
                (1, 0, 1, 1, 20)
            );
            assert_eq!((old.generation, old.epoch, old.frame), (10, 88, 99));
        }
        let mut current: ResetTicket = core::mem::zeroed();
        dreamgpu_submission_reset_snapshot(&s, &mut current);
        assert_eq!(
            dreamgpu_submission_reset_done(&mut s, &m, &current, &mut d, &mut f),
            1
        );
        assert_eq!((s.reset, s.len, d.active, f.generation), (0, 1, 0, 0));
        assert_eq!(
            dreamgpu_submission_reset_done(&mut s, &m, &current, &mut d, &mut f),
            0
        );
        let mut c: Completion = core::mem::zeroed();
        assert_eq!(dreamgpu_submission_poll(&mut s, &mut c), 1);
        assert!(c.reset);
        assert_eq!(c.generation, current.generation);
        assert_eq!(dreamgpu_submission_poll(&mut s, &mut c), 0);
        dreamgpu_submission_free(&mut s, &m);
        assert!(h.live.is_empty());
    }
}
