use super::*;
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::Cell,
};
std::thread_local! {static LIVE:Cell<usize>=const{Cell::new(0)};}
unsafe extern "C" fn allocate(_: *mut c_void, bytes: usize) -> *mut c_void {
    let layout = Layout::from_size_align(bytes + 8, 8).unwrap();
    let raw = unsafe { alloc(layout) };
    assert!(!raw.is_null());
    unsafe {
        raw.cast::<usize>().write(bytes);
        core::ptr::write_bytes(raw.add(8), 0xa5, bytes)
    };
    LIVE.with(|v| v.set(v.get() + 1));
    unsafe { raw.add(8).cast() }
}
unsafe extern "C" fn free(_: *mut c_void, p: *mut c_void) {
    let raw = unsafe { p.cast::<u8>().sub(8) };
    let bytes = unsafe { raw.cast::<usize>().read() };
    unsafe { dealloc(raw, Layout::from_size_align(bytes + 8, 8).unwrap()) };
    LIVE.with(|v| v.set(v.get() - 1));
}
unsafe extern "C" fn forget(_: *mut c_void, _: *mut Texture) {}
fn memory(api: &DreamGpuGlApi) -> Memory {
    Memory {
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
#[test]
fn endian_staging_preserves_raw_float_bits_and_each_index_width() {
    let api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    let m = memory(&api);
    for secondary in [false, true] {
        let stride = if secondary { 80 } else { 64 };
        for (kind, size) in [
            (GL_UNSIGNED_BYTE, 1),
            (GL_UNSIGNED_SHORT, 2),
            (GL_UNSIGNED_INT, 4),
        ] {
            let mut data = Vec::new();
            let mut expected = Vec::new();
            for i in 0..stride / 4 * 2 {
                let word = match i % 3 {
                    0 => 0x7f80_0001u32,
                    1 => 0x8000_0000,
                    _ => i,
                };
                data.extend(word.to_le_bytes());
                expected.extend(word.to_be_bytes());
            }
            for i in [1u32, 0, 1] {
                match size {
                    1 => {
                        data.push(i as u8);
                        expected.push(i as u8);
                    }
                    2 => {
                        data.extend((i as u16).to_le_bytes());
                        expected.extend((i as u16).to_be_bytes());
                    }
                    _ => {
                        data.extend(i.to_le_bytes());
                        expected.extend(i.to_be_bytes());
                    }
                }
            }
            let args = [
                GL_TRIANGLES,
                3,
                kind,
                2,
                DG_GL_ARRAY_POSITION | if secondary { DG_GL_ARRAY_SECONDARY } else { 0 },
                0,
                0,
                0,
            ];
            assert!(unsafe {
                stage_arrays(
                    &m,
                    FEnum_glDrawElements,
                    &args,
                    data.as_ptr(),
                    data.len() as u32,
                    false,
                )
            }
            .is_none());
            LIVE.with(|v| assert_eq!(v.get(), 0));
            let staging = unsafe {
                stage_arrays(
                    &m,
                    FEnum_glDrawElements,
                    &args,
                    data.as_ptr(),
                    data.len() as u32,
                    true,
                )
            }
            .unwrap();
            assert_eq!(
                unsafe { core::slice::from_raw_parts(staging.p, data.len()) },
                expected
            );
            drop(staging);
            LIVE.with(|v| assert_eq!(v.get(), 0));
        }
    }
}
#[test]
fn dispatch_rejects_primitive_changes_before_native_calls_and_accepts_empty_arrays() {
    let api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    let m = memory(&api);
    let mut state: ContextState = unsafe { core::mem::zeroed() };
    let args = [0u8; 32];
    assert_eq!(
        unsafe {
            dreamgpu_gl_data(
                &m,
                &mut state,
                0,
                1,
                0xffff,
                args.as_ptr(),
                args.as_ptr(),
                0,
            )
        },
        DG_GL_ERROR_UNSUPPORTED
    );
    assert_eq!(
        unsafe {
            dreamgpu_gl_data(
                &m,
                &mut state,
                1,
                1,
                FEnum_glDrawArrays,
                args.as_ptr(),
                args.as_ptr(),
                0,
            )
        },
        DG_GL_ERROR_CONTEXT
    );
    assert_eq!(
        unsafe {
            dreamgpu_gl_data(
                &m,
                &mut state,
                0,
                1,
                FEnum_glDrawArrays,
                args.as_ptr(),
                args.as_ptr(),
                0,
            )
        },
        0
    );
}
