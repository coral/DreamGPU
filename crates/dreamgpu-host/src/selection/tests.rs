// SPDX-License-Identifier: GPL-2.0-or-later
use super::*;
use std::sync::Mutex;
static LOCK: Mutex<()> = Mutex::new(());
static mut MODE: u32 = GL_RENDER;
static mut ERROR: u32 = 0;
static mut RESULT: i32 = 0;
static mut ALLOC_FAIL: bool = false;
static mut FREES: u32 = 0;
static mut CALLS: u32 = 0;
unsafe extern "C" fn err() -> u32 {
    unsafe {
        let e = ERROR;
        ERROR = 0;
        e
    }
}
unsafe extern "C" fn render(mode: u32) -> i32 {
    unsafe {
        CALLS += 1;
        if !matches!(mode, GL_RENDER | GL_SELECT | GL_FEEDBACK) {
            ERROR = GL_INVALID_ENUM;
            return 0;
        }
        MODE = mode;
        RESULT
    }
}
unsafe extern "C" fn select(_: i32, _: *mut u32) {}
unsafe extern "C" fn feedback(_: i32, _: u32, _: *mut f32) {}
unsafe extern "C" fn alloc(_: *mut core::ffi::c_void, n: usize) -> *mut core::ffi::c_void {
    unsafe {
        if ALLOC_FAIL {
            return null_mut();
        }
        let layout = std::alloc::Layout::from_size_align(n + 8, 8).unwrap();
        let p = std::alloc::alloc(layout);
        if p.is_null() {
            return null_mut();
        }
        p.cast::<usize>().write(n);
        p.add(8).cast()
    }
}
unsafe extern "C" fn dealloc(_: *mut core::ffi::c_void, p: *mut core::ffi::c_void) {
    unsafe {
        FREES += 1;
        let b = p.cast::<u8>().sub(8);
        let n = b.cast::<usize>().read();
        std::alloc::dealloc(b, std::alloc::Layout::from_size_align(n + 8, 8).unwrap());
    }
}
unsafe extern "C" fn forget(_: *mut core::ffi::c_void, _: *mut crate::texture::Texture) {}
fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGetError = Some(err);
    a.dg_glRenderMode = Some(render);
    a.dg_glSelectBuffer = Some(select);
    a.dg_glFeedbackBuffer = Some(feedback);
    a
}
fn memory(a: &DreamGpuGlApi, credit: &mut u64) -> Memory {
    Memory {
        api: a,
        bytes: core::ptr::null_mut(),
        image_bytes: credit,
        count: core::ptr::null_mut(),
        opaque: null_mut(),
        allocate: alloc,
        free: dealloc,
        forget_read: forget,
    }
}
fn run(m: &Memory, s: &mut State, f: u32, a: [u32; 3]) -> Result<Vec<u32>, u32> {
    let n = shape(f, a);
    assert!(n > 0);
    let mut out = vec![0xabu8; (n * 4) as usize];
    let mut e = 0;
    unsafe {
        query(m, s, &mut e, f, a, out.as_mut_ptr())?;
    }
    Ok(out
        .as_chunks::<4>()
        .0
        .iter()
        .map(|v| u32::from_le_bytes(*v))
        .collect())
}
fn reset() {
    unsafe {
        MODE = GL_RENDER;
        ERROR = 0;
        RESULT = 0;
        ALLOC_FAIL = false;
        FREES = 0;
        CALLS = 0;
    }
}
#[test]
fn retained_buffers_replacement_admission_and_cleanup() {
    let _g = LOCK.lock().unwrap();
    reset();
    let a = api();
    let mut credit = 0;
    let m = memory(&a, &mut credit);
    let mut s = State::EMPTY;
    assert_eq!(
        run(&m, &mut s, FEnum_glRenderMode, [GL_SELECT, 0, 0]).unwrap(),
        [GL_INVALID_OPERATION, 0, 0]
    );
    assert_eq!(unsafe { CALLS }, 0);
    assert_eq!(
        run(&m, &mut s, FEnum_glSelectBuffer, [8, 0, 0]).unwrap(),
        [0]
    );
    assert_eq!(credit, 32);
    let p = s.selection;
    unsafe {
        ALLOC_FAIL = true;
    }
    assert_eq!(
        run(&m, &mut s, FEnum_glSelectBuffer, [16, 0, 0]).unwrap(),
        [GL_OUT_OF_MEMORY]
    );
    assert_eq!(s.selection, p);
    assert_eq!(credit, 32);
    unsafe {
        ALLOC_FAIL = false;
        *m.image_bytes = u64::from(crate::pixel_image::LIMIT);
    }
    assert_eq!(
        run(&m, &mut s, FEnum_glFeedbackBuffer, [8, GL_2D, 0]).unwrap(),
        [GL_OUT_OF_MEMORY]
    );
    assert!(s.feedback.is_null());
    unsafe {
        *m.image_bytes = 32;
    }
    assert_eq!(
        run(&m, &mut s, FEnum_glSelectBuffer, [4, 0, 0]).unwrap(),
        [0]
    );
    assert_eq!(credit, 16);
    unsafe {
        assert_eq!({ FREES }, 1);
    }
    run(&m, &mut s, FEnum_glRenderMode, [GL_SELECT, 0, 0]).unwrap();
    assert_eq!(
        run(&m, &mut s, FEnum_glSelectBuffer, [4, 0, 0]).unwrap(),
        [GL_INVALID_OPERATION]
    );
    unsafe {
        release(&m, &mut s);
        release(&m, &mut s);
        assert_eq!({ FREES }, 2);
        assert_eq!({ MODE }, GL_RENDER);
    }
    assert_eq!(credit, 0);
}
#[test]
fn native_transition_once_bounded_hit_prefix_and_no_replay() {
    let _g = LOCK.lock().unwrap();
    reset();
    let a = api();
    let mut credit = 0;
    let m = memory(&a, &mut credit);
    let mut s = State::EMPTY;
    run(&m, &mut s, FEnum_glSelectBuffer, [16, 0, 0]).unwrap();
    run(&m, &mut s, FEnum_glRenderMode, [GL_SELECT, 0, 0]).unwrap();
    unsafe {
        core::ptr::copy_nonoverlapping([2, 10, 20, 31, 32, 0, 40, 50].as_ptr(), s.selection, 8);
        RESULT = 2;
    }
    assert_eq!(
        run(&m, &mut s, FEnum_glRenderMode, [GL_RENDER, 0, 0]).unwrap(),
        [0, 2, 8]
    );
    let calls = unsafe { CALLS };
    assert_eq!(
        run(&m, &mut s, FEnum_glRenderMode, [0, 0, 5]).unwrap(),
        [2, 10, 20, 31, 32]
    );
    assert_eq!(
        run(&m, &mut s, FEnum_glRenderMode, [0, 5, 3]).unwrap(),
        [0, 40, 50]
    );
    assert_eq!(
        run(&m, &mut s, FEnum_glRenderMode, [0, 7, 2]),
        Err(DG_GL_ERROR_BATCH)
    );
    assert_eq!(unsafe { CALLS }, calls);
    unsafe {
        RESULT = 0;
    }
    assert_eq!(
        run(&m, &mut s, FEnum_glRenderMode, [0xffff, 0, 0]).unwrap(),
        [GL_INVALID_ENUM, 0, 0]
    );
    assert_eq!(s.mode, GL_RENDER);
    unsafe {
        release(&m, &mut s);
    }
    assert_eq!(credit, 0);
}
#[test]
fn feedback_overflow_prefix_and_malformed_native_hit_rejected() {
    let _g = LOCK.lock().unwrap();
    reset();
    let a = api();
    let mut credit = 0;
    let m = memory(&a, &mut credit);
    let mut s = State::EMPTY;
    run(&m, &mut s, FEnum_glFeedbackBuffer, [4, GL_3D, 0]).unwrap();
    run(&m, &mut s, FEnum_glRenderMode, [GL_FEEDBACK, 0, 0]).unwrap();
    unsafe {
        RESULT = -1;
        for i in 0..4 {
            *s.feedback.add(i) = (i as f32 + 0.5).to_bits();
        }
    }
    assert_eq!(
        run(&m, &mut s, FEnum_glRenderMode, [GL_RENDER, 0, 0]).unwrap(),
        [0, u32::MAX, 4]
    );
    assert_eq!(
        run(&m, &mut s, FEnum_glRenderMode, [0, 0, 4]).unwrap(),
        [0.5f32, 1.5, 2.5, 3.5].map(f32::to_bits)
    );
    run(&m, &mut s, FEnum_glSelectBuffer, [4, 0, 0]).unwrap();
    unsafe {
        RESULT = 0;
    }
    run(&m, &mut s, FEnum_glRenderMode, [GL_SELECT, 0, 0]).unwrap();
    unsafe {
        *s.selection = u32::MAX;
        RESULT = 1;
    }
    assert_eq!(
        run(&m, &mut s, FEnum_glRenderMode, [GL_RENDER, 0, 0]),
        Err(DG_GL_ERROR_HOST)
    );
    unsafe {
        release(&m, &mut s);
    }
    assert_eq!(credit, 0);
}
#[test]
fn malformed_query_sizes_are_rejected_before_native_or_allocation() {
    assert_eq!(shape(FEnum_glSelectBuffer, [u32::MAX, 0, 0]), 0);
    assert_eq!(shape(FEnum_glSelectBuffer, [1, 1, 0]), 0);
    assert_eq!(shape(FEnum_glRenderMode, [0, u32::MAX, 1]), 0);
    assert_eq!(
        shape(FEnum_glRenderMode, [0, DG_GL_MAX_CAPTURE_VALUES, 1]),
        0
    );
    assert_eq!(shape(FEnum_glRenderMode, [0, 0, 0]), 0);
    assert_eq!(shape(FEnum_glRenderMode, [GL_RENDER, 1, 0]), 0);
}
