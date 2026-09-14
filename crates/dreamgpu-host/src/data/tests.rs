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
#[test]
fn extended_array_endian_staging_preserves_double_and_byte_edge() {
    let api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    let m = memory(&api);
    let mut source = [0u8; 96];
    source[80..88].copy_from_slice(&0x7ff0_0001_dead_beefu64.to_le_bytes());
    source[88] = 1;
    let args = [
        GL_TRIANGLES,
        0,
        1,
        DG_GL_ARRAY_POSITION | DG_GL_ARRAY_INDEX | DG_GL_ARRAY_EDGE,
        0,
        0,
        0,
        0,
    ];
    let storage =
        unsafe { stage_arrays(&m, FEnum_glDrawArrays, &args, source.as_ptr(), 96, true) }.unwrap();
    let bytes = unsafe { core::slice::from_raw_parts(storage.p, 96) };
    assert_eq!(&bytes[80..88], &0x7ff0_0001_dead_beefu64.to_be_bytes());
    assert_eq!(&bytes[88..], &source[88..]);
    drop(storage);
    LIVE.with(|v| assert_eq!(v.get(), 0));
}

#[test]
fn compact_array_endian_staging_preserves_descriptors_and_byte_attributes() {
    let api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    let m = memory(&api);
    let mut source = crate::arrays::raw::tests::packet();
    source[32..40].copy_from_slice(&0x7ff0_0001_dead_beefu64.to_le_bytes());
    source[92..94].copy_from_slice(&0x807fu16.to_le_bytes());
    source[112..116].copy_from_slice(&0x7f00_1234u32.to_le_bytes());
    let args = [GL_TRIANGLES, 0, 3, DG_GL_ARRAY_RAW | 127, 0, 0, 0, 0];
    assert!(
        unsafe { stage_arrays(&m, FEnum_glDrawArrays, &args, source.as_ptr(), 180, false) }
            .is_none()
    );
    let storage =
        unsafe { stage_arrays(&m, FEnum_glDrawArrays, &args, source.as_ptr(), 180, true) }.unwrap();
    let bytes = unsafe { core::slice::from_raw_parts(storage.p, 180) };
    assert_eq!(&bytes[..32], &source[..32]);
    assert_eq!(&bytes[32..40], &0x7ff0_0001_dead_beefu64.to_be_bytes());
    assert_eq!(&bytes[92..94], &0x807fu16.to_be_bytes());
    assert_eq!(&bytes[112..116], &0x7f00_1234u32.to_be_bytes());
    assert_eq!(&bytes[176..], &source[176..]);
    drop(storage);
    LIVE.with(|v| assert_eq!(v.get(), 0));
}
