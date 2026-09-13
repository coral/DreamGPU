use super::*;
use std::cell::RefCell;
std::thread_local! {static WORDS:RefCell<Vec<u64>>=const { RefCell::new(Vec::new()) };}
unsafe extern "C" fn floats(a: u32, b: u32, p: *const f32) {
    assert_eq!((a, b), (GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR));
    WORDS.with(|w| {
        *w.borrow_mut() = (0..4)
            .map(|i| unsafe { (*p.add(i)).to_bits() as u64 })
            .collect()
    });
}
unsafe extern "C" fn doubles(a: u32, p: *const f64) {
    assert_eq!(a, GL_CLIP_PLANE0);
    WORDS.with(|w| *w.borrow_mut() = (0..4).map(|i| unsafe { (*p.add(i)).to_bits() }).collect());
}
unsafe extern "C" fn ints(a: u32, b: u32, p: *const i32) {
    assert_eq!((a, b), (GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR));
    WORDS.with(|w| *w.borrow_mut() = (0..4).map(|i| unsafe { *p.add(i) as u32 as u64 }).collect());
}
#[test]
fn vector_bits_and_widths_are_exact_and_rejected_lengths_never_dispatch() {
    let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    api.dg_glTexParameterfv = Some(floats);
    api.dg_glClipPlane = Some(doubles);
    api.dg_glTexEnviv = Some(ints);
    let mut args = [GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, 0, 0, 0, 0, 0, 0];
    let values = [0x80000000u32, 1, 0x7f800041, 0x3f000000];
    let data: Vec<u8> = values.into_iter().flat_map(u32::to_le_bytes).collect();
    assert_eq!(
        unsafe {
            dreamgpu_gl_vector(
                &api,
                FEnum_glTexParameterfv,
                args.as_ptr(),
                data.as_ptr(),
                16,
            )
        },
        0
    );
    WORDS.with(|w| assert_eq!(*w.borrow(), values.map(u64::from)));
    assert_eq!(
        unsafe {
            dreamgpu_gl_vector(
                &api,
                FEnum_glTexParameterfv,
                args.as_ptr(),
                data.as_ptr(),
                15,
            )
        },
        DG_GL_ERROR_BATCH
    );
    args[0] = GL_TEXTURE_ENV;
    args[1] = GL_TEXTURE_ENV_COLOR;
    assert_eq!(
        unsafe { dreamgpu_gl_vector(&api, FEnum_glTexEnviv, args.as_ptr(), data.as_ptr(), 16) },
        0
    );
    WORDS.with(|w| assert_eq!(*w.borrow(), values.map(u64::from)));
    args[0] = GL_CLIP_PLANE0;
    args[1] = 0;
    let values = [
        0x8000000000000000u64,
        1,
        0x7ff0000000000041,
        0x3fe0000000000000,
    ];
    let data: Vec<u8> = values.into_iter().flat_map(u64::to_le_bytes).collect();
    assert_eq!(
        unsafe { dreamgpu_gl_vector(&api, FEnum_glClipPlane, args.as_ptr(), data.as_ptr(), 32) },
        0
    );
    WORDS.with(|w| assert_eq!(*w.borrow(), values));
    api.dg_glClipPlane = None;
    assert_eq!(
        unsafe { dreamgpu_gl_vector(&api, FEnum_glClipPlane, args.as_ptr(), data.as_ptr(), 32) },
        DG_GL_ERROR_UNSUPPORTED
    );
}
