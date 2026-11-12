use super::*;
use std::cell::RefCell;

/// Native light state of the mock context, and the calls it saw.
#[derive(Clone, Default)]
struct Native {
    lighting: bool,
    program: bool,
    enabled: [bool; LIGHTS],
    w: [f32; LIGHTS],
    cutoff: [f32; LIGHTS],
    queries: Vec<u32>,
    sets: Vec<(u32, f32)>,
    /// Native cutoffs at each native draw (DrawArrays, Begin) or End.
    draws: Vec<(&'static str, [f32; LIGHTS])>,
}
thread_local! { static NATIVE: RefCell<Native> = RefCell::new(Native::default()); }
fn light(name: u32) -> usize {
    (name - GL_LIGHT0) as usize
}
unsafe extern "C" fn is_enabled(cap: u32) -> u8 {
    NATIVE.with(|n| {
        let mut n = n.borrow_mut();
        n.queries.push(cap);
        u8::from(match cap {
            GL_LIGHTING => n.lighting,
            GL_VERTEX_PROGRAM_ARB => n.program,
            _ => n.enabled[light(cap)],
        })
    })
}
unsafe extern "C" fn get_light(name: u32, pname: u32, out: *mut f32) {
    NATIVE.with(|n| {
        let n = n.borrow();
        let i = light(name);
        unsafe {
            match pname {
                GL_POSITION => {
                    for (c, v) in [0.25, -0.5, 1., n.w[i]].iter().enumerate() {
                        *out.add(c) = *v;
                    }
                }
                GL_SPOT_CUTOFF => *out = n.cutoff[i],
                _ => panic!("unexpected light query {pname:#x}"),
            }
        }
    })
}
unsafe extern "C" fn set_light(name: u32, pname: u32, value: f32) {
    assert_eq!(pname, GL_SPOT_CUTOFF);
    NATIVE.with(|n| {
        let mut n = n.borrow_mut();
        n.cutoff[light(name)] = value;
        n.sets.push((name, value));
    })
}
fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glIsEnabled = Some(is_enabled);
    a.dg_glGetLightfv = Some(get_light);
    a.dg_glLightf = Some(set_light);
    a
}
fn saw(what: &'static str) {
    NATIVE.with(|n| {
        let mut n = n.borrow_mut();
        let cutoffs = n.cutoff;
        n.draws.push((what, cutoffs));
    })
}
unsafe extern "C" fn no_error() -> u32 {
    0
}
unsafe extern "C" fn mask(_: u32) {}
unsafe extern "C" fn nothing() {}
unsafe extern "C" fn pointer(_: i32, _: u32, _: i32, _: *const core::ffi::c_void) {}
unsafe extern "C" fn normal_pointer(_: u32, _: i32, _: *const core::ffi::c_void) {}
unsafe extern "C" fn vector(_: *const f32) {}
unsafe extern "C" fn floats(_: u32, _: *mut f32) {}
unsafe extern "C" fn draw_arrays(_: u32, _: i32, _: i32) {
    saw("DrawArrays");
}
unsafe extern "C" fn draw_elements(_: u32, _: i32, _: u32, _: *const core::ffi::c_void) {
    saw("DrawElements");
}
unsafe extern "C" fn unit(_: u32) {}
unsafe extern "C" fn bind_buffer(_: u32, _: u32) {}
unsafe extern "C" fn begin(_: u32) {
    saw("Begin");
}
unsafe extern "C" fn end() {
    saw("End");
}
/// The light entries plus what array draws and Begin/End need.
fn draw_api() -> DreamGpuGlApi {
    let mut a = api();
    a.dg_glGetError = Some(no_error);
    a.dg_glGetFloatv = Some(floats);
    a.dg_glPushClientAttrib = Some(mask);
    a.dg_glPopClientAttrib = Some(nothing);
    a.dg_glEnableClientState = Some(mask);
    a.dg_glDisableClientState = Some(mask);
    a.dg_glVertexPointer = Some(pointer);
    a.dg_glColorPointer = Some(pointer);
    a.dg_glNormalPointer = Some(normal_pointer);
    a.dg_glTexCoordPointer = Some(pointer);
    a.dg_glSecondaryColorPointer = Some(pointer);
    a.dg_glDrawArrays = Some(draw_arrays);
    a.dg_glDrawElements = Some(draw_elements);
    a.dg_glColor4fv = Some(vector);
    a.dg_glNormal3fv = Some(vector);
    a.dg_glTexCoord4fv = Some(vector);
    a.dg_glSecondaryColor3fv = Some(vector);
    a.dg_glBegin = Some(begin);
    a.dg_glEnd = Some(end);
    a.dg_glClientActiveTexture = Some(unit);
    a.dg_glBindBuffer = Some(bind_buffer);
    a
}
unsafe extern "C" fn allocate(_: *mut core::ffi::c_void, _: usize) -> *mut core::ffi::c_void {
    panic!("no allocation expected")
}
unsafe extern "C" fn free(_: *mut core::ffi::c_void, _: *mut core::ffi::c_void) {}
unsafe extern "C" fn forget(_: *mut core::ffi::c_void, _: *mut crate::texture::Texture) {}
fn memory(api: &DreamGpuGlApi) -> crate::texture::names::Memory {
    crate::texture::names::Memory {
        api,
        bytes: core::ptr::null_mut(),
        image_bytes: core::ptr::null_mut(),
        count: core::ptr::null_mut(),
        opaque: core::ptr::null_mut(),
        allocate,
        free,
        forget_read: forget,
    }
}
/// Hitman 2's state: lighting on, LIGHT0..3 directional with stale cones.
fn stale_cones() -> [f32; LIGHTS] {
    let mut cutoff = [180.; LIGHTS];
    cutoff[..4].copy_from_slice(&[30., 12., 60., 88.]);
    let mut enabled = [false; LIGHTS];
    enabled[..4].fill(true);
    reset(Native {
        lighting: true,
        enabled,
        cutoff,
        ..Native::default()
    });
    cutoff
}
fn reset(native: Native) {
    NATIVE.with(|n| *n.borrow_mut() = native);
}
fn native() -> Native {
    NATIVE.with(|n| n.borrow().clone())
}

#[test]
fn unlit_draws_ask_one_question_and_change_nothing() {
    let mut n = Native {
        enabled: [true; LIGHTS],
        cutoff: [30.; LIGHTS],
        ..Native::default()
    };
    reset(n.clone());
    let held = unsafe { hold(&api()) };
    assert_eq!(held, Held::EMPTY);
    n.queries = vec![GL_LIGHTING];
    let seen = native();
    assert_eq!(seen.queries, n.queries);
    assert!(seen.sets.is_empty());
    unsafe { release(&api(), &held) };
    assert!(native().sets.is_empty());
}

// Hitman 2: four directional lights whose cones its spot-light path left
// behind (any cutoff but 180), next to lights the draw must not touch.
#[test]
fn enabled_directional_cones_open_for_the_draw_and_close_after() {
    let mut cutoff = [180.; LIGHTS];
    let mut enabled = [false; LIGHTS];
    let mut w = [0.; LIGHTS];
    // LIGHT0..3: directional, enabled, stale cones of several sizes.
    for (i, c) in [30., 0., 90., 45.5].into_iter().enumerate() {
        enabled[i] = true;
        cutoff[i] = c;
    }
    // LIGHT4: a real spot light (positional) keeps its cone.
    enabled[4] = true;
    cutoff[4] = 20.;
    w[4] = 1.;
    // LIGHT5: directional without a cone. LIGHT6: a disabled stale cone.
    enabled[5] = true;
    cutoff[6] = 10.;
    reset(Native {
        lighting: true,
        enabled,
        w,
        cutoff,
        ..Native::default()
    });
    let api = api();
    let held = unsafe { hold(&api) };
    assert_eq!(held.lights, 0b1111);
    assert_eq!(&held.cutoffs[..4], &[30., 0., 90., 45.5]);
    let during = native();
    assert_eq!(&during.cutoff[..4], &[180.; 4]);
    assert_eq!(during.cutoff[4..], cutoff[4..]);
    assert_eq!(
        during.sets,
        (0..4).map(|i| (GL_LIGHT0 + i, 180.)).collect::<Vec<_>>()
    );
    // The program question is asked once, at the first cone.
    assert_eq!(
        during
            .queries
            .iter()
            .filter(|&&q| q == GL_VERTEX_PROGRAM_ARB)
            .count(),
        1
    );
    unsafe { release(&api, &held) };
    let after = native();
    assert_eq!(after.cutoff, cutoff);
    assert_eq!(after.sets.len(), 8);
    // Nothing is held twice: a second release has nothing left to give back.
    unsafe { release(&api, &Held::EMPTY) };
    assert_eq!(native().sets.len(), 8);
}

#[test]
fn vertex_programs_and_positional_lights_keep_native_cones() {
    let mut enabled = [false; LIGHTS];
    enabled[0] = true;
    enabled[1] = true;
    let mut cutoff = [180.; LIGHTS];
    cutoff[0] = 15.;
    cutoff[1] = 25.;
    let mut w = [0.; LIGHTS];
    w[0] = 1.;
    // A vertex program may read state.light[1].spot.direction.w.
    reset(Native {
        lighting: true,
        program: true,
        enabled,
        w,
        cutoff,
        ..Native::default()
    });
    let held = unsafe { hold(&api()) };
    assert_eq!(held, Held::EMPTY);
    assert!(native().sets.is_empty());
    assert_eq!(native().cutoff, cutoff);
    // Without the program only the directional light opens.
    reset(Native {
        lighting: true,
        enabled,
        w,
        cutoff,
        ..Native::default()
    });
    let held = unsafe { hold(&api()) };
    assert_eq!(held.lights, 0b10);
    assert_eq!(native().cutoff[0], 15.);
}

#[test]
fn a_context_without_light_entries_draws_unchanged() {
    let mut partial = api();
    partial.dg_glLightf = None;
    reset(Native {
        lighting: true,
        enabled: [true; LIGHTS],
        cutoff: [30.; LIGHTS],
        ..Native::default()
    });
    assert_eq!(unsafe { hold(&partial) }, Held::EMPTY);
    assert!(native().queries.is_empty());
}

// Hitman 2 draws its characters as client-array draws: the cones are open
// while the native draw runs and the guest's cutoffs are back afterwards.
#[test]
fn array_draws_run_with_open_cones_and_restore_them() {
    let api = draw_api();
    let m = memory(&api);
    let mut state: crate::state::ContextState = unsafe { core::mem::zeroed() };
    let cutoff = stale_cones();
    // DrawArrays: three position-only legacy vertices.
    let args: Vec<u8> = [GL_TRIANGLES, 0, 3, DG_GL_ARRAY_POSITION, 0, 0, 0, 0]
        .iter()
        .flat_map(|w| w.to_le_bytes())
        .collect();
    let data = vec![0u8; 3 * DG_GL_VERTEX_BYTES as usize];
    let e = unsafe {
        crate::data::dreamgpu_gl_data(
            &m,
            &mut state,
            0,
            1,
            FEnum_glDrawArrays,
            args.as_ptr(),
            data.as_ptr(),
            data.len() as u32,
        )
    };
    assert_eq!(e, 0);
    let seen = native();
    let mut open = cutoff;
    open[..4].fill(180.);
    assert_eq!(seen.draws, vec![("DrawArrays", open)]);
    assert_eq!(seen.cutoff, cutoff);
    assert_eq!(state.guest_errors, 0);
}

// Immediate mode holds the cones from the Begin hook (before the native
// Begin) to the End hook (after the native End): no light call falls inside.
#[test]
fn begin_end_hold_cones_in_the_context_until_end() {
    let api = draw_api();
    let m = memory(&api);
    let mut state: crate::state::ContextState = unsafe { core::mem::zeroed() };
    let cutoff = stale_cones();
    let resource = |state: &mut crate::state::ContextState, function: u32, args: &[u32]| {
        let bytes: Vec<u8> = args.iter().flat_map(|w| w.to_le_bytes()).collect();
        unsafe {
            crate::state::dreamgpu_context_resource(
                &m,
                state,
                1,
                function,
                bytes.as_ptr(),
                bytes.len() as u32,
                None,
                core::ptr::null_mut(),
            )
        }
    };
    assert_eq!(resource(&mut state, FEnum_glBegin, &[GL_TRIANGLES]), 0);
    assert_eq!(state.spot.lights, 0b1111);
    assert_eq!(&state.spot.cutoffs[..4], &cutoff[..4]);
    unsafe { begin(GL_TRIANGLES) };
    let sets = native().sets.len();
    unsafe { end() };
    assert_eq!(resource(&mut state, FEnum_glEnd, &[]), 0);
    let seen = native();
    let mut open = cutoff;
    open[..4].fill(180.);
    assert_eq!(seen.draws, vec![("Begin", open), ("End", open)]);
    // Only the End hook gave the cutoffs back, after the native End.
    assert_eq!(sets, 4);
    assert_eq!(seen.sets.len(), 8);
    assert_eq!(seen.cutoff, cutoff);
    assert_eq!(state.spot, Held::EMPTY);
}

// The character draws themselves: client-array DrawRangeElements (the buffer
// draw record) from a shared vertex section, base 2650 like 47's first one.
#[test]
fn ranged_client_draws_run_with_open_cones_and_restore_them() {
    let api = draw_api();
    let m = memory(&api);
    let mut state: crate::state::ContextState = unsafe { core::mem::zeroed() };
    let cutoff = stale_cones();
    let base = 2650u32;
    let mut data = vec![0u8; 128];
    data[..4].copy_from_slice(&(GL_FLOAT | (3 << 16)).to_le_bytes());
    data.extend(core::iter::repeat_n(0u8, 3 * 12));
    data.extend(
        [base, base + 1, base + 2]
            .iter()
            .flat_map(|i| (*i as u16).to_le_bytes()),
    );
    data.resize(data.len().next_multiple_of(4), 0);
    let a = [
        GL_TRIANGLES,
        base,
        3,
        DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION,
        0,
        0,
        3,
        GL_UNSIGNED_SHORT,
        0,
        0,
    ];
    let args: Vec<u8> = a.iter().flat_map(|w| w.to_le_bytes()).collect();
    let e = unsafe {
        crate::data::dreamgpu_gl_data(
            &m,
            &mut state,
            0,
            1,
            FEnum_glDrawRangeElements,
            args.as_ptr(),
            data.as_ptr(),
            data.len() as u32,
        )
    };
    assert_eq!(e, 0);
    let seen = native();
    let mut open = cutoff;
    open[..4].fill(180.);
    assert_eq!(seen.draws, vec![("DrawElements", open)]);
    assert_eq!(seen.cutoff, cutoff);
    assert_eq!(state.guest_errors, 0);
}
