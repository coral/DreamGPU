// SPDX-License-Identifier: GPL-2.0-or-later
//! Immediate fixed-function vectors decoded from immutable little-endian bytes.
#![allow(non_upper_case_globals)] // Preserve generated GL function identifiers.
use crate::gl_api::*;

/// # Safety
/// API is valid for the current native context; args contains 8 host-endian
/// words and data contains bytes immutable bytes. GL consumes vector arguments
/// during the call. No pointers into the snapshot are installed in GL state.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_vector(
    api: *const DreamGpuGlApi,
    function: u32,
    args: *const u32,
    data: *const u8,
    bytes: u32,
) -> u32 {
    if api.is_null() || args.is_null() || data.is_null() {
        return DG_GL_ERROR_BATCH;
    }
    let api = unsafe { &*api };
    let args = unsafe { &*(args.cast::<[u32; 8]>()) };
    let expected =
        unsafe { crate::gl_validation::dreamgpu_gl_vector_bytes(function, args.as_ptr()) };
    let wide = function == FEnum_glTexGendv || function == FEnum_glClipPlane;
    let count = expected / if wide { 8 } else { 4 };
    if expected == 0 || expected != bytes || count > 4 {
        return DG_GL_ERROR_BATCH;
    }
    let data = unsafe { core::slice::from_raw_parts(data, bytes as usize) };
    let mut floats = [0f32; 4];
    let mut doubles = [0f64; 4];
    let mut integers = [0i32; 4];
    for i in 0..count as usize {
        let word = u32::from_le_bytes(data[i * 4..i * 4 + 4].try_into().unwrap());
        integers[i] = word as i32;
        if wide {
            doubles[i] = f64::from_bits(u64::from_le_bytes(
                data[i * 8..i * 8 + 8].try_into().unwrap(),
            ));
        } else {
            floats[i] = f32::from_bits(word);
        }
    }
    macro_rules! call { ($field:ident($($arg:expr),*)) => {
        match api.$field { Some(call) => unsafe { call($($arg),*) }, None => return DG_GL_ERROR_UNSUPPORTED }
    }; }
    match function {
        FEnum_glTexParameterfv => call!(dg_glTexParameterfv(args[0], args[1], floats.as_ptr())),
        FEnum_glTexParameteriv => call!(dg_glTexParameteriv(args[0], args[1], integers.as_ptr())),
        FEnum_glTexEnvfv => call!(dg_glTexEnvfv(args[0], args[1], floats.as_ptr())),
        FEnum_glTexEnviv => call!(dg_glTexEnviv(args[0], args[1], integers.as_ptr())),
        FEnum_glLightfv => call!(dg_glLightfv(args[0], args[1], floats.as_ptr())),
        FEnum_glMaterialfv => call!(dg_glMaterialfv(args[0], args[1], floats.as_ptr())),
        FEnum_glFogfv => call!(dg_glFogfv(args[0], floats.as_ptr())),
        FEnum_glLightModelfv => call!(dg_glLightModelfv(args[0], floats.as_ptr())),
        FEnum_glTexGenfv => call!(dg_glTexGenfv(args[0], args[1], floats.as_ptr())),
        FEnum_glTexGendv => call!(dg_glTexGendv(args[0], args[1], doubles.as_ptr())),
        FEnum_glClipPlane => call!(dg_glClipPlane(args[0], doubles.as_ptr())),
        _ => return DG_GL_ERROR_UNSUPPORTED,
    }
    0
}

#[cfg(test)]
mod tests;
