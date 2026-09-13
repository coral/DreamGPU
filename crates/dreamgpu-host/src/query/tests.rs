use super::*;
use std::{cell::RefCell, collections::VecDeque};
struct Native {
    pack: [i32; 5],
    calls: u32,
    errors: VecDeque<u32>,
    read_error: u32,
}
std::thread_local! { static NATIVE: RefCell<Native> = const { RefCell::new(Native { pack: [8,31,3,5,1], calls: 0, errors: VecDeque::new(), read_error: 0 }) }; }
unsafe extern "C" fn get_integer(name: u32, out: *mut i32) {
    NATIVE.with(|n| {
        let mut n = n.borrow_mut();
        n.calls += 1;
        let i = PACK_NAMES.iter().position(|p| *p == name).unwrap();
        unsafe { *out = n.pack[i] };
    });
}
unsafe extern "C" fn pixel_store(name: u32, value: i32) {
    NATIVE.with(|n| {
        let mut n = n.borrow_mut();
        n.calls += 1;
        let i = PACK_NAMES.iter().position(|p| *p == name).unwrap();
        n.pack[i] = value;
    });
}
unsafe extern "C" fn get_error() -> u32 {
    NATIVE.with(|n| n.borrow_mut().errors.pop_front().unwrap_or(0))
}
unsafe extern "C" fn get_float(name: u32, out: *mut f32) {
    assert_eq!(name, GL_CURRENT_SECONDARY_COLOR);
    for (i, bits) in [0x80000000u32, 1, 0x7fc00042, 0x3f800000]
        .into_iter()
        .enumerate()
    {
        unsafe { *out.add(i) = f32::from_bits(bits) };
    }
}
unsafe extern "C" fn read_pixel(
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    format: u32,
    ty: u32,
    out: *mut c_void,
) {
    assert_eq!(
        (x, y, w, h, format, ty),
        (1, 2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE)
    );
    NATIVE.with(|n| {
        let mut n = n.borrow_mut();
        assert_eq!(n.pack, [1, 0, 0, 0, 0]);
        n.calls += 1;
        let error = n.read_error;
        if error != 0 {
            n.errors.push_back(error);
        }
    });
    unsafe {
        core::ptr::copy_nonoverlapping([10u8, 20, 30, 255, 40, 50, 60, 255].as_ptr(), out.cast(), 8)
    };
}
fn state() -> QueryState {
    QueryState {
        in_begin: 0,
        has_drawable: 1,
        width: 8,
        height: 8,
        draw_buffer: GL_BACK,
        read_buffer: GL_FRONT,
        binding_1d: 19,
        binding_2d: 0xf1234567,
        attrib_depth: 3,
        textures: core::ptr::null_mut(),
    }
}
fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGetIntegerv = Some(get_integer);
    a.dg_glPixelStorei = Some(pixel_store);
    a.dg_glReadPixels = Some(read_pixel);
    a.dg_glGetError = Some(get_error);
    a.dg_glGetFloatv = Some(get_float);
    a
}
fn run(
    a: &DreamGpuGlApi,
    s: &QueryState,
    errors: &mut u32,
    fnc: u32,
    args: [u32; 3],
    out: &mut [u8],
) -> (u32, u32, u32) {
    let args: Vec<u8> = args.into_iter().flat_map(u32::to_le_bytes).collect();
    let mut bytes = 99;
    let mut kind = 99;
    let error = unsafe {
        dreamgpu_gl_query(
            a,
            s,
            errors,
            fnc,
            args.as_ptr(),
            out.as_mut_ptr(),
            out.len() as u32,
            None,
            core::ptr::null_mut(),
            &mut bytes,
            &mut kind,
        )
    };
    (error, bytes, kind)
}
#[test]
fn typed_logical_state_uses_guest_names_and_exact_little_endian_bits() {
    let a = api();
    let s = state();
    let mut errors = 0;
    let mut out = [0xcc; 34];
    assert_eq!(
        run(
            &a,
            &s,
            &mut errors,
            FEnum_glGetDoublev,
            [GL_TEXTURE_BINDING_2D, 0, 0],
            &mut out[1..9]
        ),
        (0, 8, DG_GL_RESULT_DOUBLE)
    );
    assert_eq!(
        f64::from_le_bytes(out[1..9].try_into().unwrap()),
        0xf1234567u32 as f64
    );
    assert_eq!(out[0], 0xcc);
    assert_eq!(out[9], 0xcc);
    assert_eq!(
        run(
            &a,
            &s,
            &mut errors,
            FEnum_glGetFloatv,
            [GL_CURRENT_SECONDARY_COLOR, 0, 0],
            &mut out[1..17]
        ),
        (0, 16, DG_GL_RESULT_FLOAT)
    );
    for (i, bits) in [0x80000000u32, 1, 0x7fc00042, 0].into_iter().enumerate() {
        assert_eq!(&out[1 + i * 4..5 + i * 4], &bits.to_le_bytes());
    }
    assert_eq!(
        run(
            &a,
            &s,
            &mut errors,
            FEnum_glGetString,
            [GL_VENDOR, 0, 0],
            &mut out[1..10]
        ),
        (0, 9, DG_GL_RESULT_STRING)
    );
    assert_eq!(&out[1..10], b"DreamGPU\0");
    assert_eq!(
        run(
            &a,
            &s,
            &mut errors,
            FEnum_glGetBooleanv,
            [GL_DOUBLEBUFFER, 0, 0],
            &mut out[1..2]
        ),
        (0, 1, DG_GL_RESULT_BOOL)
    );
    assert_eq!(out[1], 1);
}
#[test]
fn readback_restores_pack_state_and_preserves_each_guest_error() {
    let a = api();
    let s = state();
    let mut errors = 0;
    let mut out = [0xee; 10];
    NATIVE.with(|n| {
        let mut n = n.borrow_mut();
        n.errors.push_back(GL_INVALID_ENUM);
        n.read_error = GL_INVALID_OPERATION;
    });
    assert_eq!(
        run(
            &a,
            &s,
            &mut errors,
            FEnum_glReadPixels,
            [1, 2, (1 << 16) | 2],
            &mut out[1..9]
        ),
        (DG_GL_ERROR_HOST, 0, DG_GL_RESULT_INT)
    );
    assert_eq!(out[0], 0xee);
    assert_eq!(out[9], 0xee);
    assert_eq!(errors, 5);
    NATIVE.with(|n| {
        assert_eq!(n.borrow().pack, [8, 31, 3, 5, 1]);
        n.borrow_mut().read_error = 0;
    });
    for expected in [GL_INVALID_ENUM, GL_INVALID_OPERATION, 0] {
        assert_eq!(
            run(
                &a,
                &s,
                &mut errors,
                FEnum_glGetError,
                [0, 0, 0],
                &mut out[..4]
            ),
            (0, 4, DG_GL_RESULT_INT)
        );
        assert_eq!(u32::from_le_bytes(out[..4].try_into().unwrap()), expected);
    }
    assert_eq!(
        run(
            &a,
            &s,
            &mut errors,
            FEnum_glReadPixels,
            [1, 2, (1 << 16) | 2],
            &mut out[1..9]
        )
        .0,
        0
    );
    assert_eq!(&out[1..9], &[10, 20, 30, 255, 40, 50, 60, 255]);
    NATIVE.with(|n| assert_eq!(n.borrow().pack, [8, 31, 3, 5, 1]));
}
#[test]
fn invalid_capacity_drawable_context_and_missing_api_do_not_mutate_gl() {
    let mut a = api();
    let mut s = state();
    let mut errors = 0;
    let mut out = [0xaa; 8];
    assert_eq!(
        run(
            &a,
            &s,
            &mut errors,
            FEnum_glReadPixels,
            [1, 2, (1 << 16) | 2],
            &mut out[..7]
        )
        .0,
        DG_GL_ERROR_LIMIT
    );
    assert_eq!(
        run(
            &a,
            &s,
            &mut errors,
            FEnum_glReadPixels,
            [7, 2, (1 << 16) | 2],
            &mut out
        )
        .0,
        DG_GL_ERROR_DRAWABLE
    );
    s.in_begin = 1;
    assert_eq!(
        run(
            &a,
            &s,
            &mut errors,
            FEnum_glReadPixels,
            [1, 2, (1 << 16) | 2],
            &mut out
        )
        .0,
        DG_GL_ERROR_CONTEXT
    );
    s.in_begin = 0;
    a.dg_glPixelStorei = None;
    assert_eq!(
        run(
            &a,
            &s,
            &mut errors,
            FEnum_glReadPixels,
            [1, 2, (1 << 16) | 2],
            &mut out
        )
        .0,
        DG_GL_ERROR_UNSUPPORTED
    );
    assert_eq!(out, [0xaa; 8]);
    NATIVE.with(|n| assert_eq!(n.borrow().calls, 0));
}

unsafe extern "C" fn texture_tile(
    opaque: *mut c_void,
    target: u32,
    level: u32,
    first: u32,
    capacity: u32,
    out: *mut u8,
) -> u32 {
    assert_eq!(
        (target, level, first, capacity),
        (GL_TEXTURE_2D, 2, 16384, 65536)
    );
    // Like the C cache callback, update its own error storage during execution.
    // Query does not borrow that context storage across the callback.
    unsafe {
        *(opaque as *mut u32) = 4;
    }
    for i in 0..capacity as usize {
        unsafe {
            *out.add(i) = (i % 251) as u8;
        }
    }
    0
}
#[test]
fn large_texture_callback_is_bounded_and_owns_its_disjoint_outputs() {
    let a = api();
    let s = state();
    let mut errors = 0;
    let mut out = vec![0xa5; 65538];
    let args: Vec<u8> = [
        GL_TEXTURE_2D,
        2 | (16384 << DG_GL_TEXTURE_READ_COUNT_SHIFT),
        16384,
    ]
    .into_iter()
    .flat_map(u32::to_le_bytes)
    .collect();
    let mut bytes = 0;
    let mut kind = 0;
    assert_eq!(
        unsafe {
            dreamgpu_gl_query(
                &a,
                &s,
                &mut errors,
                FEnum_glGetTexImage,
                args.as_ptr(),
                out.as_mut_ptr().add(1),
                65536,
                Some(texture_tile),
                (&mut errors as *mut u32).cast(),
                &mut bytes,
                &mut kind,
            )
        },
        0
    );
    assert_eq!((bytes, kind, errors), (65536, DG_GL_RESULT_INT, 4));
    assert_eq!((out[0], out[65537]), (0xa5, 0xa5));
    assert!(out[1..65537]
        .iter()
        .enumerate()
        .all(|(i, b)| *b == (i % 251) as u8));
    assert_eq!(
        core::mem::offset_of!(QueryState, textures),
        if core::mem::size_of::<usize>() == 8 {
            40
        } else {
            36
        }
    );
    assert_eq!(
        core::mem::size_of::<QueryState>(),
        if core::mem::size_of::<usize>() == 8 {
            48
        } else {
            40
        }
    );
}
