// SPDX-License-Identifier: GPL-2.0-or-later
//! Actual stream state machine, with spies only at allocation and native GL.
use super::*;
use crate::texture::Texture;
use core::ffi::c_void;
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
    collections::BTreeMap,
};

#[derive(Default)]
struct Allocator {
    live: BTreeMap<usize, Layout>,
    fail: bool,
    calls: usize,
    frees: usize,
}
unsafe extern "C" fn allocate(p: *mut c_void, bytes: usize) -> *mut c_void {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    a.calls += 1;
    if a.fail {
        return null_mut();
    }
    let layout = Layout::from_size_align(bytes, 8).unwrap();
    let p = unsafe { alloc(layout) };
    assert!(!p.is_null());
    a.live.insert(p as usize, layout);
    p.cast()
}
unsafe extern "C" fn free(p: *mut c_void, object: *mut c_void) {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    let layout = a
        .live
        .remove(&(object as usize))
        .expect("free must consume a live allocation exactly once");
    a.frees += 1;
    unsafe { dealloc(object.cast(), layout) };
}
unsafe extern "C" fn forget(_: *mut c_void, _: *mut Texture) {}
#[derive(Default)]
struct Native {
    kind: u32,
    unpack: [i32; 6],
    calls: usize,
    bytes: Vec<u8>,
    bitmap: [u32; 4],
    fail_draw: bool,
    error: u32,
}
thread_local! { static N: RefCell<Native> = RefCell::new(Native::default()); }
unsafe extern "C" fn geti(name: u32, out: *mut i32) {
    N.with(|n| unsafe {
        out.write(n.borrow().unpack[UNPACK.iter().position(|v| *v == name).unwrap()])
    });
}
unsafe extern "C" fn seti(name: u32, value: i32) {
    N.with(|n| n.borrow_mut().unpack[UNPACK.iter().position(|v| *v == name).unwrap()] = value);
}
unsafe extern "C" fn get_error() -> u32 {
    N.with(|n| core::mem::take(&mut n.borrow_mut().error))
}
unsafe fn record(bytes: usize, data: *const u8) {
    N.with(|n| {
        let mut n = n.borrow_mut();
        assert_eq!(
            n.unpack,
            [1, 0, 0, 0, i32::from(cfg!(target_endian = "big")), 0]
        );
        n.calls += 1;
        n.bytes = if bytes == 0 {
            vec![]
        } else {
            unsafe { core::slice::from_raw_parts(data, bytes) }.to_vec()
        };
        if n.fail_draw {
            n.error = GL_INVALID_OPERATION;
        }
    });
}
unsafe extern "C" fn draw(w: i32, h: i32, format: u32, kind: u32, data: *const c_void) {
    N.with(|n| n.borrow_mut().kind = kind);
    let bytes = byte_count(FEnum_glDrawPixels, w as u32, h as u32, format, kind).unwrap();
    unsafe { record(bytes as usize, data.cast()) };
}
unsafe extern "C" fn bitmap(w: i32, h: i32, xo: f32, yo: f32, xm: f32, ym: f32, data: *const u8) {
    N.with(|n| n.borrow_mut().bitmap = [xo.to_bits(), yo.to_bits(), xm.to_bits(), ym.to_bits()]);
    unsafe { record((w as usize).div_ceil(8) * h as usize, data) };
}
fn api() -> DreamGpuGlApi {
    N.with(|n| {
        *n.borrow_mut() = Native {
            unpack: [8, 23, 4, 7, 1, 1],
            ..Native::default()
        }
    });
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGetIntegerv = Some(geti);
    a.dg_glPixelStorei = Some(seti);
    a.dg_glGetError = Some(get_error);
    a.dg_glDrawPixels = Some(draw);
    a.dg_glBitmap = Some(bitmap);
    a
}
fn memory(api: &DreamGpuGlApi, alloc: &mut Allocator, budget: &mut u64) -> Memory {
    Memory {
        api,
        bytes: null_mut(),
        image_bytes: budget,
        count: null_mut(),
        opaque: (alloc as *mut Allocator).cast(),
        allocate,
        free,
        forget_read: forget,
    }
}
fn args(total: u32, offset: u32, flags: u32, id: u32) -> [u32; 8] {
    [total, 1, GL_RED, GL_UNSIGNED_BYTE, total, offset, flags, id]
}
unsafe fn send(m: &Memory, s: &mut State, a: [u32; 8], data: &[u8]) -> u32 {
    let mut errors = 0;
    unsafe { stream(m, s, &mut errors, FEnum_glDrawPixels, &a, data) }
}
#[test]
fn rejects_malformed_before_allocation_or_native_call() {
    let api = api();
    let mut alloc = Allocator::default();
    let mut budget = 0;
    let m = memory(&api, &mut alloc, &mut budget);
    let mut cases = Vec::new();
    for (index, value) in [
        (0, u32::MAX),
        (2, u32::MAX),
        (3, u32::MAX),
        (4, 5),
        (5, 1),
        (6, 8),
        (6, FIRST | ABORT),
        (7, 0),
    ] {
        let mut a = args(4, 0, FIRST | LAST, 1);
        a[index] = value;
        cases.push(a);
    }
    cases.push(args(5, 0, FIRST | LAST, 1)); // incomplete LAST
    cases.push(args(3, 0, FIRST | LAST, 1)); // oversized payload
    for a in cases {
        let mut s = State::EMPTY;
        assert_ne!(unsafe { send(&m, &mut s, a, &[1, 2, 3, 4]) }, 0);
        assert_eq!(s.active, 0);
    }
    assert_eq!(alloc.calls, 0);
    assert_eq!(budget, 0);
    N.with(|n| assert_eq!(n.borrow().calls, 0));
    assert_eq!(
        byte_count(FEnum_glDrawPixels, 4096, 4096, GL_RGBA, GL_UNSIGNED_BYTE),
        Some(LIMIT)
    );
    assert_eq!(
        byte_count(FEnum_glDrawPixels, 4097, 4096, GL_RGBA, GL_UNSIGNED_BYTE),
        None
    );
    assert_eq!(
        validate(
            FEnum_glBitmap,
            &[8, 1, GL_COLOR_INDEX, GL_BITMAP, 1, 0, FIRST, 1],
            &[0; 16]
        ),
        DG_GL_ERROR_BATCH
    );
}
#[test]
fn matching_corrupt_continuations_abort_once_without_committing() {
    for which in 0..7 {
        let api = api();
        let mut alloc = Allocator::default();
        let mut budget = 0;
        let m = memory(&api, &mut alloc, &mut budget);
        let mut s = State::EMPTY;
        assert_eq!(
            unsafe { send(&m, &mut s, args(4, 0, FIRST, 1), &[1, 2]) },
            0
        );
        let mut a = args(4, 2, LAST, 1);
        let mut bytes: &[u8] = &[3, 4];
        match which {
            0 => a[5] = 1,
            1 => a[5] = 3,
            2 => a[2] = GL_BLUE,
            3 => a[4] = 5,
            4 => a[6] = 8,
            5 => bytes = &[3],
            6 => bytes = &[],
            _ => unreachable!(),
        }
        assert_ne!(unsafe { send(&m, &mut s, a, bytes) }, 0);
        assert_eq!(s.active, 0);
        assert_eq!(budget, 0);
        assert_eq!(alloc.frees, 1);
        unsafe { release(&m, &mut s) };
        assert_eq!(alloc.frees, 1);
        assert_ne!(
            unsafe { send(&m, &mut s, args(4, 0, FIRST | LAST, 1), &[1, 2, 3, 4]) },
            0
        );
        N.with(|n| assert_eq!(n.borrow().calls, 0));
    }
}
#[test]
fn unrelated_id_and_duplicate_first_preserve_owner_then_single_commit_consumes_id() {
    let api = api();
    let mut alloc = Allocator::default();
    let mut budget = 0;
    let m = memory(&api, &mut alloc, &mut budget);
    let mut s = State::EMPTY;
    assert_eq!(
        unsafe { send(&m, &mut s, args(4, 0, FIRST, 7), &[1, 2]) },
        0
    );
    let owned = s.pixels;
    for a in [
        args(4, 2, LAST, 8),
        args(4, 0, FIRST, 8),
        args(4, 0, FIRST, 7),
        args(4, 2, ABORT, 8),
    ] {
        assert_ne!(unsafe { send(&m, &mut s, a, &[3, 4]) }, 0);
        assert_eq!(s.pixels, owned);
        assert_eq!(s.received, 2);
        assert_eq!(budget, 4);
    }
    assert_eq!(unsafe { send(&m, &mut s, args(4, 2, LAST, 7), &[3, 4]) }, 0);
    assert_eq!(s.active, 0);
    assert_eq!(budget, 0);
    assert_eq!(alloc.frees, 1);
    N.with(|n| {
        let n = n.borrow();
        assert_eq!(n.calls, 1);
        assert_eq!(n.bytes, [1, 2, 3, 4]);
        assert_eq!(n.unpack, [8, 23, 4, 7, 1, 1]);
    });
    assert_ne!(unsafe { send(&m, &mut s, args(4, 2, LAST, 7), &[3, 4]) }, 0);
    assert_ne!(
        unsafe { send(&m, &mut s, args(4, 0, FIRST | LAST, 7), &[1, 2, 3, 4]) },
        0
    );
    N.with(|n| assert_eq!(n.borrow().calls, 1));
}
#[test]
fn global_budget_allocation_failure_interleave_and_teardown_are_exact() {
    let api = api();
    let mut alloc = Allocator::default();
    let mut budget = u64::from(LIMIT) - 4;
    let m = memory(&api, &mut alloc, &mut budget);
    let mut a = State::EMPTY;
    let mut b = State::EMPTY;
    assert_eq!(unsafe { send(&m, &mut a, args(4, 0, FIRST, 1), &[1]) }, 0);
    assert_eq!(budget, u64::from(LIMIT));
    assert_eq!(
        unsafe { send(&m, &mut b, args(1, 0, FIRST | LAST, 1), &[2]) },
        DG_GL_ERROR_LIMIT
    );
    assert_eq!(alloc.calls, 1);
    assert_eq!(b.last_id, 0);
    assert_eq!(unsafe { interleave(&m, &mut a) }, DG_GL_ERROR_CONTEXT);
    assert_eq!(unsafe { interleave(&m, &mut a) }, 0);
    assert_eq!(budget, u64::from(LIMIT) - 4);
    alloc.fail = true;
    assert_eq!(
        unsafe { send(&m, &mut b, args(1, 0, FIRST | LAST, 1), &[2]) },
        DG_GL_ERROR_LIMIT
    );
    assert_eq!(b.last_id, 0);
    assert_eq!(budget, u64::from(LIMIT) - 4);
    alloc.fail = false;
    assert_eq!(unsafe { send(&m, &mut b, args(4, 0, FIRST, 1), &[2]) }, 0);
    assert_eq!(unsafe { send(&m, &mut b, args(4, 1, ABORT, 1), &[]) }, 0);
    unsafe {
        release(&m, &mut a);
        release(&m, &mut b)
    };
    assert!(alloc.live.is_empty());
    assert_eq!(alloc.frees, 2);
    assert_eq!(budget, u64::from(LIMIT) - 4);
}
#[test]
fn bitmap_float_metadata_zero_area_and_native_error_preserve_transaction() {
    let api = api();
    let mut alloc = Allocator::default();
    let mut budget = 0;
    let m = memory(&api, &mut alloc, &mut budget);
    let mut s = State::EMPTY;
    let mut errors = 0;
    let values = [
        (-0f32).to_bits(),
        1.25f32.to_bits(),
        (-0.125f32).to_bits(),
        3.5f32.to_bits(),
    ];
    let prefix: Vec<u8> = values.iter().flat_map(|v| v.to_le_bytes()).collect();
    assert_eq!(
        unsafe {
            stream(
                &m,
                &mut s,
                &mut errors,
                FEnum_glBitmap,
                &[0, 100, GL_COLOR_INDEX, GL_BITMAP, 0, 0, FIRST | LAST, 1],
                &prefix,
            )
        },
        0
    );
    assert_eq!(alloc.calls, 0);
    assert_eq!(budget, 0);
    N.with(|n| {
        let mut n = n.borrow_mut();
        assert_eq!(n.bitmap, values);
        assert_eq!(n.calls, 1);
        assert_eq!(n.unpack, [8, 23, 4, 7, 1, 1]);
        n.fail_draw = true;
    });
    assert_eq!(
        unsafe {
            stream(
                &m,
                &mut s,
                &mut errors,
                FEnum_glDrawPixels,
                &args(4, 0, FIRST | LAST, 2),
                &[1, 2, 3, 4],
            )
        },
        DG_GL_ERROR_HOST
    );
    assert_ne!(errors, 0);
    assert_eq!(s.active, 0);
    assert_eq!(s.last_id, 2);
    assert_eq!(budget, 0);
    assert!(alloc.live.is_empty());
    N.with(|n| {
        let n = n.borrow();
        assert_eq!(n.calls, 2);
        assert_eq!(n.unpack, [8, 23, 4, 7, 1, 1]);
    });
    assert_ne!(
        unsafe { send(&m, &mut s, args(4, 0, FIRST | LAST, 2), &[1, 2, 3, 4]) },
        0
    );
    N.with(|n| assert_eq!(n.borrow().calls, 2));
}

#[test]
fn two_live_contexts_share_exact_budget_without_cross_context_release() {
    let api = api();
    let mut allocator = Allocator::default();
    let mut budget = 0;
    let m = memory(&api, &mut allocator, &mut budget);
    let mut a = State::EMPTY;
    let mut b = State::EMPTY;
    let mut c = State::EMPTY;
    let half = LIMIT / 2;
    assert_eq!(
        unsafe { send(&m, &mut a, args(half, 0, FIRST, 1), &[1]) },
        0
    );
    assert_eq!(
        unsafe { send(&m, &mut b, args(half, 0, FIRST, 1), &[2]) },
        0
    );
    assert_eq!(budget, u64::from(LIMIT));
    assert_eq!(allocator.live.len(), 2);
    assert_eq!(
        unsafe { send(&m, &mut c, args(1, 0, FIRST | LAST, 1), &[3]) },
        DG_GL_ERROR_LIMIT
    );
    unsafe { release(&m, &mut a) };
    assert_eq!(budget, u64::from(half));
    assert_eq!(b.active, 1);
    assert_eq!(b.received, 1);
    assert_eq!(unsafe { *b.pixels }, 2);
    assert_eq!(
        unsafe { send(&m, &mut c, args(1, 0, FIRST | LAST, 1), &[3]) },
        0
    );
    assert_eq!(budget, u64::from(half));
    unsafe {
        release(&m, &mut b);
        release(&m, &mut c)
    };
    assert_eq!(budget, 0);
    assert!(allocator.live.is_empty());
    assert_eq!(allocator.frees, 3);
}

#[test]
fn missing_native_entry_point_releases_commit_without_mutating_unpack() {
    for missing in 0..4 {
        let mut api = api();
        match missing {
            0 => api.dg_glDrawPixels = None,
            1 => api.dg_glGetIntegerv = None,
            2 => api.dg_glPixelStorei = None,
            3 => api.dg_glGetError = None,
            _ => unreachable!(),
        }
        let mut allocator = Allocator::default();
        let mut budget = 0;
        let m = memory(&api, &mut allocator, &mut budget);
        let mut s = State::EMPTY;
        assert_eq!(
            unsafe { send(&m, &mut s, args(1, 0, FIRST | LAST, 1), &[1]) },
            DG_GL_ERROR_UNSUPPORTED
        );
        assert_eq!(budget, 0);
        assert_eq!(s.active, 0);
        assert_eq!(s.last_id, 1);
        assert!(allocator.live.is_empty());
        N.with(|n| {
            let n = n.borrow();
            assert_eq!(n.calls, 0);
            assert_eq!(n.unpack, [8, 23, 4, 7, 1, 1]);
        });
    }
}

#[test]
fn context_release_drains_pending_image_before_repeated_process_cleanup() {
    let api = api();
    let mut allocator = Allocator::default();
    let mut budget = 0;
    let m = memory(&api, &mut allocator, &mut budget);
    // Empty resource handles isolate the actual context teardown path.
    let mut context: crate::state::ContextState = unsafe { core::mem::zeroed() };
    assert_eq!(
        unsafe { send(&m, &mut context.image, args(4, 0, FIRST, 9), &[1]) },
        0
    );
    assert_eq!(budget, 4);
    unsafe { crate::state::dreamgpu_context_state_release(&m, &mut context) };
    assert_eq!(context.image.active, 0);
    assert_eq!(budget, 0);
    assert_eq!(allocator.frees, 1);
    unsafe { crate::state::dreamgpu_context_state_release(&m, &mut context) };
    assert_eq!(allocator.frees, 1);
    assert!(allocator.live.is_empty());
    N.with(|n| assert_eq!(n.borrow().calls, 0));
}

#[test]
fn bitmap_indices_expand_backwards_with_odd_rows_and_keep_wire_offsets() {
    for format in [GL_COLOR_INDEX, GL_STENCIL_INDEX] {
        for (width, packed, expected) in [
            (
                9,
                vec![0xa5, 0xff, 0x5a, 0x7f],
                vec![1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 0],
            ),
            (
                17,
                vec![0xa5, 0x3c, 0xff, 0x5a, 0xc3, 0x7f],
                vec![
                    1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1,
                    1, 0, 0, 0, 0, 1, 1, 0,
                ],
            ),
        ] {
            let api = api();
            let mut allocator = Allocator::default();
            let mut budget = 0;
            let m = memory(&api, &mut allocator, &mut budget);
            let mut s = State::EMPTY;
            let total = packed.len() as u32;
            let mut a = [width, 2, format, GL_BITMAP, total, 0, FIRST, 1];
            assert_eq!(unsafe { send(&m, &mut s, a, &packed[..1]) }, 0);
            assert_eq!(s.total, total);
            assert_eq!(s.received, 1);
            assert_eq!(budget, u64::from(width * 2));
            assert_eq!(allocator.live[&(s.pixels as usize)].size(), expected.len());
            N.with(|n| assert_eq!(n.borrow().calls, 0));
            a[5] = 1;
            a[6] = LAST;
            assert_eq!(unsafe { send(&m, &mut s, a, &packed[1..]) }, 0);
            assert_eq!(budget, 0);
            assert_eq!(allocator.frees, 1);
            N.with(|n| {
                let n = n.borrow();
                assert_eq!(n.kind, GL_UNSIGNED_BYTE);
                assert_eq!(n.calls, 1);
                assert_eq!(n.bytes, expected);
                assert_eq!(n.unpack, [8, 23, 4, 7, 1, 1]);
            });
        }
    }
}

#[test]
fn bitmap_expanded_budget_rejects_before_allocation_and_aborts_exact_credit() {
    let api = api();
    let mut allocator = Allocator::default();
    let mut budget = u64::from(LIMIT) - 17;
    let m = memory(&api, &mut allocator, &mut budget);
    let mut s = State::EMPTY;
    // Four wire bytes fit the remaining budget, eighteen expanded bytes do not.
    let mut a = [9, 2, GL_STENCIL_INDEX, GL_BITMAP, 4, 0, FIRST, 1];
    assert_eq!(unsafe { send(&m, &mut s, a, &[0x80]) }, DG_GL_ERROR_LIMIT);
    assert_eq!(allocator.calls, 0);
    assert_eq!(s.last_id, 0);
    assert_eq!(budget, u64::from(LIMIT) - 17);
    // Per-image expansion cap applies even when the packed wire fits comfortably.
    let huge = [
        8193,
        8192,
        GL_STENCIL_INDEX,
        GL_BITMAP,
        1025 * 8192,
        0,
        FIRST,
        1,
    ];
    assert_eq!(unsafe { send(&m, &mut s, huge, &[0]) }, DG_GL_ERROR_BATCH);
    assert_eq!(allocator.calls, 0);
    budget = 0;
    assert_eq!(unsafe { send(&m, &mut s, a, &[0x80]) }, 0);
    assert_eq!(budget, 18);
    a[5] = 1;
    a[6] = ABORT;
    assert_eq!(unsafe { send(&m, &mut s, a, &[]) }, 0);
    assert_eq!(budget, 0);
    assert_eq!(allocator.frees, 1);
    unsafe { release(&m, &mut s) };
    assert_eq!(allocator.frees, 1);
    N.with(|n| assert_eq!(n.borrow().calls, 0));
}

#[test]
fn zero_width_bitmap_indices_do_not_iterate_extreme_height_or_allocate() {
    let api = api();
    let mut allocator = Allocator::default();
    let mut budget = 0;
    let m = memory(&api, &mut allocator, &mut budget);
    let mut s = State::EMPTY;
    let start = std::time::Instant::now();
    let a = [
        0,
        i32::MAX as u32,
        GL_STENCIL_INDEX,
        GL_BITMAP,
        0,
        0,
        FIRST | LAST,
        1,
    ];
    assert_eq!(unsafe { send(&m, &mut s, a, &[]) }, 0);
    assert!(start.elapsed() < std::time::Duration::from_secs(1));
    assert_eq!(allocator.calls, 0);
    assert_eq!(budget, 0);
    N.with(|n| {
        let n = n.borrow();
        assert_eq!(n.calls, 1);
        assert_eq!(n.kind, GL_UNSIGNED_BYTE);
        assert!(n.bytes.is_empty());
    });
}
