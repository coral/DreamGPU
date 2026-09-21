use super::*;
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
    collections::{BTreeMap, VecDeque},
};
#[derive(Default)]
struct Allocator {
    live: BTreeMap<usize, Layout>,
    calls: usize,
    fail: usize,
    forgotten: usize,
}
unsafe extern "C" fn allocate(p: *mut c_void, bytes: usize) -> *mut c_void {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    a.calls += 1;
    if a.calls == a.fail {
        return null_mut();
    }
    let layout = Layout::from_size_align(bytes, 8).unwrap();
    let p = unsafe { alloc(layout) };
    // Deliberately not zero initialized; production initialization must own writes.
    assert!(!p.is_null());
    unsafe { core::ptr::write_bytes(p, 0xaa, bytes) };
    a.live.insert(p as usize, layout);
    p.cast()
}
unsafe extern "C" fn free(p: *mut c_void, object: *mut c_void) {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    let l = a
        .live
        .remove(&(object as usize))
        .expect("foreign/double free");
    unsafe { dealloc(object.cast(), l) }
}
unsafe extern "C" fn forget(p: *mut c_void, _: *mut Texture) {
    unsafe { &mut *p.cast::<Allocator>() }.forgotten += 1;
}
#[derive(Default)]
struct Native {
    errors: VecDeque<u32>,
    fail_push: bool,
    fail_pop: bool,
    sum: bool,
    next: u32,
    draw: Vec<u32>,
    read: u32,
    fences: u32,
    deleted_fences: u32,
}
std::thread_local! {static N:RefCell<Native>=RefCell::new(Native::default());}
unsafe extern "C" fn get_error() -> u32 {
    N.with(|n| n.borrow_mut().errors.pop_front().unwrap_or(0))
}
unsafe extern "C" fn gen(_: i32, out: *mut u32) {
    N.with(|n| {
        let mut n = n.borrow_mut();
        n.next += 1;
        unsafe { *out = n.next }
    })
}
unsafe extern "C" fn bind(_: u32, _: u32) {}
unsafe extern "C" fn delete(_: i32, _: *const u32) {}
unsafe extern "C" fn push(_: u32) {
    N.with(|n| {
        let mut n = n.borrow_mut();
        if n.fail_push {
            n.errors.push_back(GL_INVALID_OPERATION);
        }
    })
}
unsafe extern "C" fn pop() {
    N.with(|n| {
        let mut n = n.borrow_mut();
        if n.fail_pop {
            n.errors.push_back(GL_INVALID_OPERATION);
        }
    })
}
unsafe extern "C" fn enabled(cap: u32) -> u8 {
    assert_eq!(cap, GL_COLOR_SUM);
    N.with(|n| u8::from(n.borrow().sum))
}
unsafe extern "C" fn enable(cap: u32) {
    assert_eq!(cap, GL_COLOR_SUM);
    N.with(|n| n.borrow_mut().sum = true)
}
unsafe extern "C" fn disable(cap: u32) {
    assert_eq!(cap, GL_COLOR_SUM);
    N.with(|n| n.borrow_mut().sum = false)
}
unsafe extern "C" fn draw(value: u32) {
    N.with(|n| n.borrow_mut().draw = vec![value])
}
unsafe extern "C" fn draws(count: i32, p: *const u32) {
    assert_eq!(count, 2);
    N.with(|n| n.borrow_mut().draw = unsafe { core::slice::from_raw_parts(p, 2) }.to_vec())
}
unsafe extern "C" fn read(value: u32) {
    N.with(|n| n.borrow_mut().read = value)
}
unsafe extern "C" fn fence(condition: u32, flags: u32) -> *mut __GLsync {
    assert_eq!((condition, flags), (GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
    N.with(|n| {
        let mut n = n.borrow_mut();
        n.fences += 1;
        (n.fences as usize + 1) as *mut __GLsync
    })
}
unsafe extern "C" fn delete_sync(_: *mut __GLsync) {
    N.with(|n| n.borrow_mut().deleted_fences += 1)
}
unsafe extern "C" fn wait(_: *mut __GLsync, _: u32, _: u64) {}
fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGetError = Some(get_error);
    a.dg_glGenTextures = Some(gen);
    a.dg_glBindTexture = Some(bind);
    a.dg_glDeleteTextures = Some(delete);
    a.dg_glPushAttrib = Some(push);
    a.dg_glPopAttrib = Some(pop);
    a.dg_glIsEnabled = Some(enabled);
    a.dg_glEnable = Some(enable);
    a.dg_glDisable = Some(disable);
    a.dg_glDrawBuffer = Some(draw);
    a.dg_glDrawBuffers = Some(draws);
    a.dg_glReadBuffer = Some(read);
    a.dg_glFenceSync = Some(fence);
    a.dg_glDeleteSync = Some(delete_sync);
    a.dg_glWaitSync = Some(wait);
    a
}
fn memory(api: &DreamGpuGlApi, a: &mut Allocator, count: &mut u32, bytes: &mut u64) -> Memory {
    Memory {
        api,
        count,
        bytes,
        image_bytes: core::ptr::null_mut(),
        opaque: (a as *mut Allocator).cast(),
        allocate,
        free,
        forget_read: forget,
    }
}
fn command(m: &Memory, s: &mut ContextState, fnc: u32, args: &[u32]) -> u32 {
    let data: Vec<u8> = args.iter().flat_map(|v| v.to_le_bytes()).collect();
    unsafe {
        dreamgpu_context_resource(
            m,
            s,
            1,
            fnc,
            data.as_ptr(),
            data.len() as u32,
            None,
            null_mut(),
        )
    }
}
#[test]
fn transactional_initialization_rolls_back_each_allocation_and_shared_reference() {
    let api = api();
    let mut a = Allocator::default();
    let mut count = 0;
    let mut bytes = 0;
    let m = memory(&api, &mut a, &mut count, &mut bytes);
    for fail in 1..=3 {
        a.calls = 0;
        a.fail = fail;
        let mut s = ContextState::EMPTY;
        assert_eq!(
            unsafe { dreamgpu_context_state_init(&m, &mut s, null_mut()) },
            DG_GL_ERROR_HOST
        );
        assert!(s.textures.is_null());
        assert!(a.live.is_empty());
        assert_eq!(count, 0);
        unsafe { dreamgpu_context_state_release(&m, &mut s) };
        assert!(a.live.is_empty());
    }
    a.fail = 0;
    let mut first = ContextState::EMPTY;
    assert_eq!(
        unsafe { dreamgpu_context_state_init(&m, &mut first, null_mut()) },
        0
    );
    assert_eq!(count, 2);
    let shared = first.textures;
    assert_eq!(unsafe { (*shared).refs }, 1);
    a.calls = 0;
    a.fail = 2;
    let mut second = ContextState::EMPTY;
    assert_eq!(
        unsafe { dreamgpu_context_state_init(&m, &mut second, shared) },
        DG_GL_ERROR_HOST
    );
    assert_eq!(unsafe { (*shared).refs }, 1);
    assert_eq!(count, 2);
    a.fail = 0;
    assert_eq!(
        unsafe { dreamgpu_context_state_init(&m, &mut second, shared) },
        0
    );
    assert_eq!(unsafe { (*shared).refs }, 2);
    assert_eq!(count, 4);
    unsafe { dreamgpu_context_state_release(&m, &mut first) };
    assert_eq!(count, 2);
    assert_eq!(unsafe { (*shared).refs }, 1);
    unsafe { dreamgpu_context_state_release(&m, &mut second) };
    assert_eq!(count, 0);
    assert_eq!(bytes, 0);
    assert!(a.live.is_empty());
    count = DG_GL_MAX_TEXTURES - 1;
    a.calls = 0;
    assert_eq!(
        unsafe { dreamgpu_context_state_init(&m, &mut first, null_mut()) },
        DG_GL_ERROR_LIMIT
    );
    assert_eq!(a.calls, 0);
    assert_eq!(count, DG_GL_MAX_TEXTURES - 1);
}
#[test]
fn attrib_restores_deleted_binding_and_logical_buffers_only_after_native_success() {
    let api = api();
    let mut a = Allocator::default();
    let mut count = 0;
    let mut bytes = 0;
    let m = memory(&api, &mut a, &mut count, &mut bytes);
    let mut s = ContextState::EMPTY;
    assert_eq!(
        unsafe { dreamgpu_context_state_init(&m, &mut s, null_mut()) },
        0
    );
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_2D, 7]),
        0
    );
    let old = s.bound_texture;
    N.with(|n| n.borrow_mut().sum = true);
    let mask = GL_TEXTURE_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_PIXEL_MODE_BIT;
    assert_eq!(command(&m, &mut s, FEnum_glPushAttrib, &[mask]), 0);
    assert_eq!(unsafe { (*old).refs }, 3);
    assert_eq!(s.attrib_depth, 1);
    unsafe {
        crate::texture::names::dreamgpu_texture_delete(
            &m,
            s.textures,
            7u32.to_le_bytes().as_ptr(),
            1,
            s.default_texture,
            s.default_texture_1d,
            &mut s.bound_texture,
            &mut s.bound_texture_1d,
        )
    };
    assert_eq!(unsafe { ((*old).refs, (*old).deleted) }, (1, 1));
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_2D, 7]),
        0
    );
    let new = s.bound_texture;
    assert_ne!(old, new);
    assert_eq!(count, 4);
    assert_eq!(
        command(&m, &mut s, FEnum_glDrawBuffer, &[GL_FRONT_AND_BACK]),
        0
    );
    assert_eq!(command(&m, &mut s, FEnum_glReadBuffer, &[GL_FRONT]), 0);
    N.with(|n| {
        let mut n = n.borrow_mut();
        n.sum = false;
        n.fail_pop = true;
        assert_eq!(n.draw, [GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1]);
        assert_eq!(n.read, GL_COLOR_ATTACHMENT1);
    });
    assert_eq!(command(&m, &mut s, FEnum_glPopAttrib, &[]), 0);
    assert_eq!(s.attrib_depth, 1);
    assert_eq!(s.bound_texture, new);
    assert_eq!(s.guest_errors, 4);
    N.with(|n| n.borrow_mut().fail_pop = false);
    assert_eq!(command(&m, &mut s, FEnum_glPopAttrib, &[]), 0);
    assert_eq!(s.attrib_depth, 0);
    assert_eq!(s.bound_texture, old);
    assert_eq!(s.draw_buffer, GL_BACK);
    assert_eq!(s.read_buffer, GL_BACK);
    N.with(|n| {
        let n = n.borrow();
        assert!(n.sum);
        assert_eq!(n.draw, [GL_COLOR_ATTACHMENT0]);
        assert_eq!(n.read, GL_COLOR_ATTACHMENT0);
        assert_eq!(n.fences, 2);
    });
    unsafe { dreamgpu_context_state_release(&m, &mut s) };
    assert_eq!(count, 0);
    assert_eq!(a.forgotten, 4);
    assert!(a.live.is_empty());
}
#[test]
fn attribute_overflow_underflow_and_failed_push_retain_owned_state() {
    let api = api();
    let mut a = Allocator::default();
    let mut count = 0;
    let mut bytes = 0;
    let m = memory(&api, &mut a, &mut count, &mut bytes);
    let mut s = ContextState::EMPTY;
    assert_eq!(
        unsafe { dreamgpu_context_state_init(&m, &mut s, null_mut()) },
        0
    );
    assert_eq!(command(&m, &mut s, FEnum_glPopAttrib, &[]), 0);
    assert_eq!(s.guest_errors, 1 << (GL_STACK_UNDERFLOW - GL_INVALID_ENUM));
    N.with(|n| n.borrow_mut().fail_push = true);
    assert_eq!(
        command(&m, &mut s, FEnum_glPushAttrib, &[GL_TEXTURE_BIT]),
        0
    );
    assert_eq!(s.attrib_depth, 0);
    assert_eq!(unsafe { (*s.bound_texture).refs }, 2);
    N.with(|n| n.borrow_mut().fail_push = false);
    for _ in 0..16 {
        assert_eq!(
            command(&m, &mut s, FEnum_glPushAttrib, &[GL_TEXTURE_BIT]),
            0
        );
    }
    assert_eq!(s.attrib_depth, 16);
    assert_eq!(unsafe { (*s.bound_texture).refs }, 18);
    assert_eq!(
        command(&m, &mut s, FEnum_glPushAttrib, &[GL_TEXTURE_BIT]),
        0
    );
    assert_eq!(s.attrib_depth, 16);
    assert_ne!(
        s.guest_errors & (1 << (GL_STACK_OVERFLOW - GL_INVALID_ENUM)),
        0
    );
    unsafe { dreamgpu_context_state_release(&m, &mut s) };
    assert_eq!(count, 0);
    assert!(a.live.is_empty());
    assert_eq!(
        core::mem::size_of::<Attrib>(),
        if core::mem::size_of::<usize>() == 8 {
            32
        } else {
            24
        }
    );
}

#[test]
fn context_state_c_abi_layout() {
    let pointer = core::mem::size_of::<usize>();
    assert_eq!(
        core::mem::offset_of!(ContextState, guest_errors),
        5 * pointer
    );
    assert_eq!(
        core::mem::offset_of!(ContextState, attrib),
        5 * pointer + 16
    );
    assert_eq!(core::mem::offset_of!(Attrib, texture), 16);
    assert_eq!(core::mem::size_of::<Attrib>(), 16 + 2 * pointer);
    assert_eq!(
        core::mem::size_of::<ContextState>(),
        5 * pointer
            + 16
            + 16 * (16 + 2 * pointer)
            + core::mem::size_of::<crate::pixel_image::State>()
            + core::mem::size_of::<crate::selection::State>()
            + 2 * pointer
            + 8
            + pointer
    );
}
