// SPDX-License-Identifier: GPL-2.0-or-later
//! Exact outer command framing. Payload snapshots are bounded before slicing;
//! function/data validators run only after the corresponding payload fits.
use crate::{gl_api::*, gl_validation as gl};
use core::ffi::c_void;
pub type Rejection = unsafe extern "C" fn(*mut c_void, *const u8, u32, u32);
fn word(r: &[u8], offset: u32) -> u32 {
    u32::from_le_bytes(r[offset as usize..offset as usize + 4].try_into().unwrap())
}
fn rejected(r: &[u8], e: u32, reject: Option<Rejection>, opaque: *mut c_void) -> u32 {
    if let Some(f) = reject {
        unsafe { f(opaque, r.as_ptr(), r.len() as u32, e) }
    }
    e
}
#[expect(
    clippy::too_many_arguments,
    reason = "Validates an immutable wire batch against independent device limits and a rejection callback."
)]
unsafe fn validate(
    data: &[u8],
    generation: u32,
    width: u32,
    height: u32,
    vram: u32,
    reject: Option<Rejection>,
    opaque: *mut c_void,
    records: *mut u32,
) -> u32 {
    let mut offset = 0;
    let mut count = 0;
    let mut desktop_count = 0;
    let mut work = 0;
    while offset < data.len() {
        if data.len() - offset < (DG_GL_HEADER_BYTES as usize) {
            return DG_GL_ERROR_BATCH;
        }
        count += 1;
        if count > DG_GL_MAX_RECORDS {
            return DG_GL_ERROR_BATCH;
        }
        let tail = &data[offset..];
        let op = word(tail, DG_GL_OFF_OP);
        let size = word(tail, DG_GL_OFF_SIZE);
        let flags = word(tail, DG_GL_OFF_FLAGS);
        let mut expected = DG_GL_HEADER_BYTES;
        if size < DG_GL_HEADER_BYTES
            || size as usize > tail.len()
            || size & 3 != 0
            || word(tail, DG_GL_OFF_CLIENT) == 0
            || word(tail, DG_GL_OFF_RESERVED) != 0
        {
            return DG_GL_ERROR_BATCH;
        }
        if flags != 0
            && (op != DG_GL_PRESENT
                || flags
                    & !(DG_GL_PRESENT_EXCLUSIVE
                        | DG_GL_PRESENT_RETAIN
                        | DG_GL_PRESENT_FRONT_ONLY
                        | DG_GL_PRESENT_NO_EXPORT
                        | DG_GL_PRESENT_BOUNDED)
                    != 0
                || (flags & DG_GL_PRESENT_NO_EXPORT != 0
                    && (flags & !DG_GL_PRESENT_BOUNDED) != DG_GL_PRESENT_NO_EXPORT)
                || (flags & DG_GL_PRESENT_EXCLUSIVE != 0 && flags & DG_GL_PRESENT_RETAIN != 0))
        {
            return DG_GL_ERROR_BATCH;
        }
        if word(tail, DG_GL_OFF_GENERATION) != generation {
            return DG_GL_ERROR_GENERATION;
        }
        let r = &tail[..size as usize];
        match op {
            DG_GL_CREATE_CONTEXT => expected += 4,
            DG_GL_CREATE_DRAWABLE => expected += 8,
            DG_GL_CALL => {
                if size < expected + 4 {
                    return DG_GL_ERROR_BATCH;
                }
                let words = gl::dreamgpu_gl_function_words(word(r, expected));
                if words == u32::MAX || words & DG_GL_FUNCTION_KIND_MASK != 0 {
                    return rejected(r, DG_GL_ERROR_UNSUPPORTED, reject, opaque);
                }
                expected += 4 + words * 4;
            }
            DG_GL_DATA_CALL => {
                if size < DG_GL_DATA_ARGS {
                    return DG_GL_ERROR_BATCH;
                }
                let function = word(r, DG_GL_DATA_FUNCTION);
                let metadata = gl::dreamgpu_gl_function_words(function);
                let bytes = word(r, DG_GL_DATA_BYTES);
                if metadata == u32::MAX
                    || metadata & DG_GL_FUNCTION_KIND_MASK != DG_GL_FUNCTION_INLINE_DATA
                {
                    return rejected(r, DG_GL_ERROR_UNSUPPORTED, reject, opaque);
                }
                let words = metadata & !DG_GL_FUNCTION_INLINE_DATA;
                if bytes > DG_GL_MAX_BYTES {
                    return DG_GL_ERROR_BATCH;
                }
                let padded = (bytes + 3) & !3;
                if size != DG_GL_DATA_ARGS + words * 4 + padded {
                    return DG_GL_ERROR_BATCH;
                }
                let at = (DG_GL_DATA_ARGS + words * 4) as usize;
                let e = unsafe {
                    gl::dreamgpu_gl_data_validate(
                        function,
                        r.as_ptr().add(DG_GL_DATA_ARGS as usize),
                        r.as_ptr().add(at),
                        bytes,
                    )
                };
                if e != 0 {
                    return rejected(r, e, reject, opaque);
                }
                if r[at + bytes as usize..at + padded as usize]
                    .iter()
                    .any(|x| *x != 0)
                {
                    return DG_GL_ERROR_BATCH;
                }
                expected = size;
            }
            DG_GL_QUERY => {
                if size != DG_GL_QUERY_BYTES || data.len() != DG_GL_QUERY_BYTES as usize {
                    return DG_GL_ERROR_BATCH;
                }
                let e = unsafe { gl::dreamgpu_gl_query_validate(word(r, 32), r.as_ptr().add(36)) };
                if e != 0 {
                    return rejected(r, e, reject, opaque);
                }
                expected = DG_GL_QUERY_BYTES;
            }
            DG_GL_DESKTOP => {
                expected += DG_DESKTOP_BYTES;
                desktop_count += 1;
                if size != expected || desktop_count > DG_DESKTOP_MAX_RECORDS {
                    return DG_GL_ERROR_BATCH;
                }
                let e = unsafe {
                    crate::desktop::dreamgpu_desktop_validate(
                        r.as_ptr().add(DG_GL_HEADER_BYTES as usize),
                        width,
                        height,
                        vram,
                        &mut work,
                    )
                };
                if e != 0 {
                    return rejected(r, e, reject, opaque);
                }
            }
            DG_GL_DESTROY_CONTEXT
            | DG_GL_DESTROY_DRAWABLE
            | DG_GL_MAKE_CURRENT
            | DG_GL_CLOSE_CLIENT => {}
            DG_GL_PRESENT => {
                if flags & DG_GL_PRESENT_BOUNDED != 0 {
                    expected += 8;
                    if size != expected {
                        return DG_GL_ERROR_BATCH;
                    }
                    if !dimensions(r) {
                        return DG_GL_ERROR_DRAWABLE;
                    }
                }
            }
            _ => return DG_GL_ERROR_UNSUPPORTED,
        }
        if size != expected {
            return DG_GL_ERROR_BATCH;
        }
        if op == DG_GL_CALL {
            let e = unsafe { gl::dreamgpu_gl_call_validate(word(r, 32), r.as_ptr().add(36)) };
            if e != 0 {
                return rejected(r, e, reject, opaque);
            }
        }
        if op == DG_GL_CREATE_DRAWABLE && !dimensions(r) {
            return DG_GL_ERROR_DRAWABLE;
        }
        if flags & DG_GL_PRESENT_EXCLUSIVE != 0 && (width == 0 || height == 0) {
            return DG_GL_ERROR_DRAWABLE;
        }
        offset += size as usize;
    }
    unsafe { *records = count };
    0
}
fn dimensions(r: &[u8]) -> bool {
    let w = word(r, 32);
    let h = word(r, 36);
    w != 0 && h != 0 && w <= DG_GL_MAX_DIMENSION && h <= DG_GL_MAX_DIMENSION
}
/// # Safety
/// Data is a fully initialized immutable host DMA snapshot of `bytes` bytes.
/// Rejection callback may read only its bounded record and retains no pointer;
/// records output is disjoint. No pointer into guest-shared RAM is accepted.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_batch_validate(
    data: *const u8,
    bytes: usize,
    generation: u32,
    width: u32,
    height: u32,
    vram: u32,
    records: *mut u32,
    reject: Option<Rejection>,
    opaque: *mut c_void,
) -> u32 {
    if records.is_null() {
        return DG_GL_ERROR_BATCH;
    }
    unsafe { *records = 0 };
    if data.is_null() || bytes == 0 || bytes > DG_GL_MAX_BYTES as usize || bytes & 3 != 0 {
        return DG_GL_ERROR_BATCH;
    }
    unsafe {
        validate(
            core::slice::from_raw_parts(data, bytes),
            generation,
            width,
            height,
            vram,
            reject,
            opaque,
            records,
        )
    }
}
#[cfg(test)]
mod tests;
