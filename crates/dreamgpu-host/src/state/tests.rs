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
    active: u32,
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
unsafe extern "C" fn active_texture(texture: u32) {
    N.with(|n| n.borrow_mut().active = texture)
}
unsafe extern "C" fn native_units(pname: u32, out: *mut i32) {
    assert_eq!(pname, GL_MAX_TEXTURE_UNITS);
    unsafe { *out = 4 };
}
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
    // Namespace, then the 2D, 1D and cube map defaults.
    for fail in 1..=4 {
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
    assert_eq!(count, 3);
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
    assert_eq!(count, 3);
    a.fail = 0;
    assert_eq!(
        unsafe { dreamgpu_context_state_init(&m, &mut second, shared) },
        0
    );
    assert_eq!(unsafe { (*shared).refs }, 2);
    assert_eq!(count, 6);
    unsafe { dreamgpu_context_state_release(&m, &mut first) };
    assert_eq!(count, 3);
    assert_eq!(unsafe { (*shared).refs }, 1);
    unsafe { dreamgpu_context_state_release(&m, &mut second) };
    assert_eq!(count, 0);
    assert_eq!(bytes, 0);
    assert!(a.live.is_empty());
    count = DG_GL_MAX_TEXTURES - 2;
    a.calls = 0;
    assert_eq!(
        unsafe { dreamgpu_context_state_init(&m, &mut first, null_mut()) },
        DG_GL_ERROR_LIMIT
    );
    assert_eq!(a.calls, 0);
    assert_eq!(count, DG_GL_MAX_TEXTURES - 2);
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
            s.default_texture_cube,
            [
                core::ptr::addr_of_mut!(s.bound_texture),
                core::ptr::addr_of_mut!(s.bound_texture_1d),
            ]
            .as_ptr(),
            2,
        )
    };
    assert_eq!(unsafe { ((*old).refs, (*old).deleted) }, (1, 1));
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_2D, 7]),
        0
    );
    let new = s.bound_texture;
    assert_ne!(old, new);
    assert_eq!(count, 5);
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
        // The restored 2D texture, unit 0's 1D default, the 2D default bound
        // on units 1-7 and the cube map default of every unit are each fenced
        // once.
        assert_eq!(n.fences, 4);
    });
    unsafe { dreamgpu_context_state_release(&m, &mut s) };
    assert_eq!(count, 0);
    assert_eq!(a.forgotten, 5);
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
    // Owner plus one binding per texture unit; the failed push took none.
    assert_eq!(unsafe { (*s.bound_texture).refs }, 1 + UNITS as u32);
    N.with(|n| n.borrow_mut().fail_push = false);
    for _ in 0..16 {
        assert_eq!(
            command(&m, &mut s, FEnum_glPushAttrib, &[GL_TEXTURE_BIT]),
            0
        );
    }
    assert_eq!(s.attrib_depth, 16);
    // Each TEXTURE_BIT entry retains the 2D default bound on all eight units.
    assert_eq!(
        unsafe { (*s.bound_texture).refs },
        1 + UNITS as u32 + 16 * UNITS as u32
    );
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
            + 8
            + 16 * pointer
            + 16 * core::mem::size_of::<UnitAttrib>()
            + 2 * pointer
            + 8 * pointer
            + 16 * 8 * pointer
            + 2 * pointer
            + 8
            + pointer
    );
    // Appended ARB_fragment_program state: void *, int32_t, uint32_t in C.
    assert_eq!(
        core::mem::offset_of!(ContextState, program),
        core::mem::offset_of!(ContextState, proxy_rejected_cube) + pointer
    );
    assert_eq!(
        core::mem::offset_of!(ContextState, program_error_kind),
        core::mem::offset_of!(ContextState, program) + pointer + 4
    );
    // Then the ARB_vertex_program binding: void * in C.
    assert_eq!(
        core::mem::offset_of!(ContextState, vertex_program),
        core::mem::offset_of!(ContextState, program_error_kind) + 4
    );
    // Appended cube map state mirrors the C tail field for field.
    assert_eq!(
        core::mem::offset_of!(ContextState, default_texture_cube),
        core::mem::offset_of!(ContextState, unit_attrib) + 16 * core::mem::size_of::<UnitAttrib>()
    );
    assert_eq!(
        core::mem::offset_of!(ContextState, proxy_rejected_cube),
        core::mem::offset_of!(ContextState, default_texture_cube) + (2 + 8 + 16 * 8) * pointer
    );
    // Appended multitexture state mirrors DreamGpuUnitAttrib/units in C.
    assert_eq!(core::mem::offset_of!(UnitAttrib, units), pointer);
    assert_eq!(core::mem::size_of::<UnitAttrib>(), pointer + 16 * pointer);
    assert_eq!(
        core::mem::offset_of!(ContextState, units),
        core::mem::offset_of!(ContextState, active_unit) + 8
    );
}

#[test]
fn texture_units_move_binding_ownership_and_texture_bit_restores_every_unit() {
    let mut api = api();
    api.dg_glActiveTexture = Some(active_texture);
    api.dg_glGetIntegerv = Some(native_units);
    let mut a = Allocator::default();
    let mut count = 0;
    let mut bytes = 0;
    let m = memory(&api, &mut a, &mut count, &mut bytes);
    let mut s = ContextState::EMPTY;
    assert_eq!(
        unsafe { dreamgpu_context_state_init(&m, &mut s, null_mut()) },
        0
    );
    let default = s.default_texture;
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_2D, 7]),
        0
    );
    let seven = s.bound_texture;
    assert_ne!(seven, default);
    // Switching moves ownership: unit 0 keeps 7 while unit 2 starts at default.
    assert_eq!(
        command(&m, &mut s, FEnum_glActiveTexture, &[GL_TEXTURE0 + 2]),
        0
    );
    assert_eq!(
        (s.active_unit, N.with(|n| n.borrow().active)),
        (2, GL_TEXTURE0 + 2)
    );
    assert_eq!((s.bound_texture, s.units[0][0]), (default, seven));
    assert!(s.units[2][0].is_null());
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_2D, 8]),
        0
    );
    let eight = s.bound_texture;
    // The native limit (4) bounds units; the rejected switch changes nothing.
    assert_eq!(
        command(&m, &mut s, FEnum_glActiveTexture, &[GL_TEXTURE0 + 4]),
        0
    );
    assert_ne!(s.guest_errors & 1, 0);
    assert_eq!((s.active_unit, s.unit_limit), (2, 4));
    s.guest_errors = 0;
    let all = s.unit_bindings();
    assert_eq!((all[0][0], all[1][0], all[2][0]), (seven, default, eight));
    assert_eq!(unsafe { ((*seven).refs, (*eight).refs) }, (2, 2));
    // TEXTURE_BIT saves every unit and the active unit.
    assert_eq!(
        command(&m, &mut s, FEnum_glPushAttrib, &[GL_TEXTURE_BIT]),
        0
    );
    assert_eq!(unsafe { ((*seven).refs, (*eight).refs) }, (3, 3));
    assert_eq!(
        command(&m, &mut s, FEnum_glActiveTexture, &[GL_TEXTURE0]),
        0
    );
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_2D, 0]),
        0
    );
    assert_eq!(
        (s.active_unit, s.bound_texture, s.units[2][0]),
        (0, default, eight)
    );
    assert_eq!(command(&m, &mut s, FEnum_glPopAttrib, &[]), 0);
    assert_eq!(
        (s.active_unit, s.bound_texture, s.units[0][0]),
        (2, eight, seven)
    );
    assert!(s.units[2][0].is_null());
    assert_eq!(unsafe { ((*seven).refs, (*eight).refs) }, (2, 2));
    // Deleting a texture bound on an inactive unit reverts only that unit.
    let slots = unsafe { binding_slots(&mut s) };
    unsafe {
        crate::texture::names::dreamgpu_texture_delete(
            &m,
            s.textures,
            7u32.to_le_bytes().as_ptr(),
            1,
            s.default_texture,
            s.default_texture_1d,
            s.default_texture_cube,
            slots.as_ptr(),
            slots.len() as u32,
        )
    };
    assert_eq!((s.units[0][0], s.bound_texture), (default, eight));
    unsafe { dreamgpu_context_state_release(&m, &mut s) };
    assert_eq!(count, 0);
    assert!(a.live.is_empty());
}

std::thread_local! {static CUBE_ENABLED: core::cell::Cell<(u32, u32)> = const { core::cell::Cell::new((0, 0)) };}
/// Enable queries of the draw admission: only TEXTURE_CUBE_MAP is enabled, on
/// the native unit recorded first; the second word counts the queries.
unsafe extern "C" fn cube_enabled(cap: u32) -> u8 {
    CUBE_ENABLED.with(|c| {
        let (unit, queries) = c.get();
        c.set((unit, queries + 1));
        u8::from(cap == GL_TEXTURE_CUBE_MAP && N.with(|n| n.borrow().active) == unit)
    })
}

#[test]
fn cube_map_bindings_follow_units_texture_bit_deletion_and_draw_admission() {
    let mut api = api();
    api.dg_glActiveTexture = Some(active_texture);
    api.dg_glGetIntegerv = Some(native_units);
    let mut a = Allocator::default();
    let mut count = 0;
    let mut bytes = 0;
    let m = memory(&api, &mut a, &mut count, &mut bytes);
    let mut s = ContextState::EMPTY;
    N.with(|n| *n.borrow_mut() = Native::default());
    assert_eq!(
        unsafe { dreamgpu_context_state_init(&m, &mut s, null_mut()) },
        0
    );
    // The default cube map carries its face table and is bound on every unit.
    let default = s.default_texture_cube;
    assert_eq!(s.bound_texture_cube, default);
    assert!(s.units_cube[1..].iter().all(|&t| t == default) && s.units_cube[0].is_null());
    assert_eq!(
        unsafe {
            (
                (*default).target,
                (*default).refs,
                (*default).faces.is_null(),
            )
        },
        (GL_TEXTURE_CUBE_MAP, 1 + UNITS as u32, false)
    );
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_CUBE_MAP, 9]),
        0
    );
    let nine = s.bound_texture_cube;
    assert_eq!(
        unsafe { ((*nine).target, (*nine).guest_name, (*nine).faces.is_null()) },
        (GL_TEXTURE_CUBE_MAP, 9, false)
    );
    // The first bind fixed the target: binding 9 as 2D is INVALID_OPERATION
    // and changes no binding; a fresh name may still become a 2D texture.
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_2D, 9]),
        0
    );
    assert_eq!(
        s.guest_errors,
        1 << (GL_INVALID_OPERATION - GL_INVALID_ENUM)
    );
    assert_eq!(
        (s.bound_texture, s.bound_texture_cube),
        (s.default_texture, nine)
    );
    s.guest_errors = 0;
    // Unit 1 starts at the default cube map; unit 0 keeps 9.
    assert_eq!(
        command(&m, &mut s, FEnum_glActiveTexture, &[GL_TEXTURE0 + 1]),
        0
    );
    assert_eq!((s.bound_texture_cube, s.units_cube[0]), (default, nine));
    assert!(s.units_cube[1].is_null());
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_CUBE_MAP, 10]),
        0
    );
    let ten = s.bound_texture_cube;
    let all = s.unit_bindings();
    assert_eq!((all[0][2], all[1][2], all[2][2]), (nine, ten, default));
    // TEXTURE_BIT saves every unit's cube map and the active unit.
    assert_eq!(
        command(&m, &mut s, FEnum_glPushAttrib, &[GL_TEXTURE_BIT]),
        0
    );
    assert_eq!(unsafe { ((*nine).refs, (*ten).refs) }, (3, 3));
    assert_eq!(
        command(&m, &mut s, FEnum_glActiveTexture, &[GL_TEXTURE0]),
        0
    );
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_CUBE_MAP, 0]),
        0
    );
    assert_eq!((s.bound_texture_cube, s.units_cube[1]), (default, ten));
    assert_eq!(command(&m, &mut s, FEnum_glPopAttrib, &[]), 0);
    assert_eq!(
        (s.active_unit, s.bound_texture_cube, s.units_cube[0]),
        (1, ten, nine)
    );
    assert!(s.units_cube[1].is_null());
    assert_eq!(unsafe { ((*nine).refs, (*ten).refs) }, (2, 2));
    // The owner plus the six units still at the default.
    assert_eq!(unsafe { (*default).refs }, UNITS as u32 - 1);

    // Draw admission: undefined levels on any face of an enabled cube map,
    // on any unit, refuse the draw; a disabled one is never sampled.
    unsafe {
        let mut f = crate::texture::image_levels(nine, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z);
        f.undefined_levels = 1 << 2;
        crate::texture::set_image_levels(nine, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, &f);
        assert_eq!((*nine).undefined_levels, 1 << 2);
    }
    let mut admission = api;
    admission.dg_glIsEnabled = Some(cube_enabled);
    CUBE_ENABLED.with(|c| c.set((GL_TEXTURE0 + 2, 0)));
    assert_eq!(unsafe { prepare_textures(&admission, &s, 1) }, Ok(()));
    CUBE_ENABLED.with(|c| c.set((GL_TEXTURE0, 0)));
    assert_eq!(
        unsafe { prepare_textures(&admission, &s, 1) },
        Err(DG_GL_ERROR_TEXTURE)
    );
    // Only the poisoned texture's unit was queried, and the active unit restored.
    assert_eq!(CUBE_ENABLED.with(|c| c.get().1), 1);
    assert_eq!(N.with(|n| n.borrow().active), GL_TEXTURE0 + 1);

    // Face charges are released with the texture; deleting reverts every
    // unit's cube binding (active or not) to the default cube map.
    unsafe {
        let mut f = crate::texture::image_levels(nine, GL_TEXTURE_CUBE_MAP_POSITIVE_Y);
        f.levels[0] = 64;
        crate::texture::set_image_levels(nine, GL_TEXTURE_CUBE_MAP_POSITIVE_Y, &f);
        let mut f = crate::texture::image_levels(ten, GL_TEXTURE_CUBE_MAP_NEGATIVE_X);
        f.levels[1] = 16;
        crate::texture::set_image_levels(ten, GL_TEXTURE_CUBE_MAP_NEGATIVE_X, &f);
        assert_eq!(((*nine).levels[0], (*ten).levels[1]), (64, 16));
    }
    bytes = 80;
    let slots = unsafe { binding_slots(&mut s) };
    let names: Vec<u8> = [9u32, 10].iter().flat_map(|v| v.to_le_bytes()).collect();
    unsafe {
        crate::texture::names::dreamgpu_texture_delete(
            &m,
            s.textures,
            names.as_ptr(),
            2,
            s.default_texture,
            s.default_texture_1d,
            s.default_texture_cube,
            slots.as_ptr(),
            slots.len() as u32,
        )
    };
    assert_eq!((s.units_cube[0], s.bound_texture_cube), (default, default));
    assert_eq!((bytes, count), (0, 3));
    assert_eq!(unsafe { (*default).refs }, 1 + UNITS as u32);
    // The freed name may now become a 2D texture.
    assert_eq!(
        command(&m, &mut s, FEnum_glBindTexture, &[GL_TEXTURE_2D, 9]),
        0
    );
    assert_eq!(s.guest_errors, 0);
    unsafe { dreamgpu_context_state_release(&m, &mut s) };
    assert_eq!(count, 0);
    assert!(a.live.is_empty());
}
