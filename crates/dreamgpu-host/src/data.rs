// SPDX-License-Identifier: GPL-2.0-or-later
//! Validated inline-data dispatch and native-endian array staging.
#![allow(non_upper_case_globals)]
use crate::{
    gl_api::*,
    gl_validation as validation, query, resource,
    state::ContextState,
    texture::{
        dreamgpu_texture_upload, dreamgpu_texture_wait, dreamgpu_texture_written,
        names::{dreamgpu_texture_delete, Memory},
        Texture,
    },
};
use core::{ffi::c_void, ptr::addr_of_mut};
unsafe fn bound(state: *mut ContextState, target: u32) -> *mut Texture {
    unsafe {
        if target == GL_TEXTURE_1D {
            (*state).bound_texture_1d
        } else {
            (*state).bound_texture
        }
    }
}
unsafe extern "C" fn zero(opaque: *mut c_void, t: *mut Texture, level: u32, w: u32, h: u32) -> u32 {
    unsafe { resource::dreamgpu_texture_zero(opaque.cast::<Memory>(), t, level, w, h) }
}
unsafe fn stage_arrays<'a>(
    m: &'a Memory,
    fnc: u32,
    a: &[u32; 8],
    data: *const u8,
    bytes: u32,
    big: bool,
) -> Option<resource::OwnedBytes<'a>> {
    if !big {
        return None;
    }
    let storage = unsafe { resource::OwnedBytes::copied(m, data, bytes as usize) }?;
    let elements = fnc == FEnum_glDrawElements;
    let vertices = a[if elements { 3 } else { 2 }] as usize;
    let attributes = a[if elements { 4 } else { 3 }];
    let stride = if attributes & (DG_GL_ARRAY_INDEX | DG_GL_ARRAY_EDGE) != 0 {
        DG_GL_VERTEX_EXTENDED_BYTES
    } else if attributes & DG_GL_ARRAY_SECONDARY != 0 {
        DG_GL_VERTEX_SECONDARY_BYTES
    } else {
        DG_GL_VERTEX_BYTES
    } as usize;
    for vertex in 0..vertices {
        let base = vertex * stride;
        for offset in (base..base + stride.min(DG_GL_VERTEX_SECONDARY_BYTES as usize)).step_by(4) {
            let word = u32::from_le(unsafe { data.add(offset).cast::<u32>().read_unaligned() });
            unsafe {
                core::ptr::copy_nonoverlapping(
                    word.to_be_bytes().as_ptr(),
                    storage.p.add(offset),
                    4,
                )
            };
        }
        if stride == DG_GL_VERTEX_EXTENDED_BYTES as usize {
            let offset = base + DG_GL_VERTEX_INDEX as usize;
            let word = u64::from_le(unsafe { data.add(offset).cast::<u64>().read_unaligned() });
            unsafe {
                core::ptr::copy_nonoverlapping(
                    word.to_be_bytes().as_ptr(),
                    storage.p.add(offset),
                    8,
                )
            };
        }
    }
    if elements {
        let size = validation::dreamgpu_gl_index_bytes(a[2]) as usize;
        for i in 0..a[1] as usize {
            let offset = vertices * stride + i * size;
            if size == 2 {
                let v = u16::from_le(unsafe { data.add(offset).cast::<u16>().read_unaligned() });
                unsafe {
                    core::ptr::copy_nonoverlapping(
                        v.to_be_bytes().as_ptr(),
                        storage.p.add(offset),
                        2,
                    )
                };
            } else if size == 4 {
                let v = u32::from_le(unsafe { data.add(offset).cast::<u32>().read_unaligned() });
                unsafe {
                    core::ptr::copy_nonoverlapping(
                        v.to_be_bytes().as_ptr(),
                        storage.p.add(offset),
                        4,
                    )
                };
            }
        }
    }
    Some(storage)
}
/// # Safety
/// State/accounting belong to current render worker. Args/data are the immutable
/// snapshot already accepted by data_validate; memory callbacks cannot reenter.
/// The only staged copy is required endian conversion on big-endian hosts.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_data(
    memory: *const Memory,
    state: *mut ContextState,
    in_begin: u32,
    serial: u64,
    function: u32,
    args: *const u8,
    data: *const u8,
    bytes: u32,
) -> u32 {
    if memory.is_null() || state.is_null() || args.is_null() || (bytes != 0 && data.is_null()) {
        return DG_GL_ERROR_CONTEXT;
    }
    let metadata = validation::dreamgpu_gl_function_words(function);
    let words = metadata & !DG_GL_FUNCTION_INLINE_DATA;
    if metadata & DG_GL_FUNCTION_INLINE_DATA == 0 || words > 8 {
        return DG_GL_ERROR_UNSUPPORTED;
    }
    if in_begin != 0 && function != FEnum_glMaterialfv {
        return DG_GL_ERROR_CONTEXT;
    }
    let mut a = [0; 8];
    for (i, w) in a.iter_mut().enumerate().take(words as usize) {
        *w = u32::from_le(unsafe { args.add(i * 4).cast::<u32>().read_unaligned() });
    }
    let m = unsafe { &*memory };
    let api = unsafe { &*m.api };
    let texture = unsafe { bound(state, a[0]) };
    let errors = unsafe { addr_of_mut!((*state).guest_errors) };
    if crate::pixel_image::image_function(function) {
        let data = if bytes == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, bytes as usize) }
        };
        return unsafe {
            crate::pixel_image::stream(m, addr_of_mut!((*state).image), errors, function, &a, data)
        };
    }
    let pending = unsafe { crate::pixel_image::interleave(m, addr_of_mut!((*state).image)) };
    if pending != 0 {
        return pending;
    }
    if crate::evaluator::map_function(function) {
        let data = unsafe { core::slice::from_raw_parts(data, bytes as usize) };
        return unsafe { crate::evaluator::set(api, errors, function, &a, data) }
            .map_or_else(|e| e, |()| 0);
    }
    if function == FEnum_glPolygonStipple {
        return unsafe { crate::stipple::set(api, errors, data, bytes) }.map_or_else(|e| e, |()| 0);
    }
    if function == FEnum_glPrioritizeTextures {
        let data = if bytes == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, bytes as usize) }
        };
        return unsafe {
            crate::texture::control::prioritize(api, (*state).textures, errors, data)
        }
        .map_or_else(|e| e, |()| 0);
    }
    if crate::pixels::map_input(function).is_some() {
        let data = if bytes == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, bytes as usize) }
        };
        return unsafe { crate::pixels::set_map(api, errors, function, a[0], a[1], data) }
            .map_or_else(|e| e, |()| 0);
    }
    if validation::dreamgpu_gl_vector_function(function) != 0 {
        let parameter = matches!(function, FEnum_glTexParameterfv | FEnum_glTexParameteriv);
        if parameter {
            unsafe {
                dreamgpu_texture_wait(m.api, texture, serial);
                if let Err(e) = query::remember(api, errors) {
                    return e;
                }
            }
        }
        let e =
            unsafe { crate::vector::dreamgpu_gl_vector(m.api, function, a.as_ptr(), data, bytes) };
        if e != 0 {
            return e;
        }
        if parameter {
            let e = unsafe { api.dg_glGetError.expect("complete API")() };
            if e != 0 {
                unsafe { query::store(errors, e) };
                return DG_GL_ERROR_TEXTURE;
            }
            unsafe { dreamgpu_texture_written(m.api, texture, serial) };
        }
        return 0;
    }
    if matches!(function, FEnum_glDrawArrays | FEnum_glDrawElements) {
        let elements = function == FEnum_glDrawElements;
        if a[if elements { 3 } else { 2 }] == 0 || (elements && a[1] == 0) {
            return 0;
        }
        let two = unsafe { (*state).bound_texture };
        let one = unsafe { (*state).bound_texture_1d };
        for (t, cap) in [(two, GL_TEXTURE_2D), (one, GL_TEXTURE_1D)] {
            if unsafe {
                (*t).undefined_levels != 0 && api.dg_glIsEnabled.expect("complete API")(cap) != 0
            } {
                return DG_GL_ERROR_TEXTURE;
            }
        }
        let staged =
            unsafe { stage_arrays(m, function, &a, data, bytes, cfg!(target_endian = "big")) };
        if cfg!(target_endian = "big") && staged.is_none() {
            return DG_GL_ERROR_HOST;
        }
        let data = staged.as_ref().map_or(data, |s| s.p.cast_const());
        unsafe {
            dreamgpu_texture_wait(m.api, two, serial);
            dreamgpu_texture_wait(m.api, one, serial);
            if let Err(e) = query::remember(api, errors) {
                return e;
            }
        }
        let mut gl_error = 0;
        let e = unsafe {
            crate::arrays::dreamgpu_gl_arrays(
                m.api,
                function,
                a.as_ptr(),
                data,
                bytes,
                &mut gl_error,
            )
        };
        unsafe { query::store(errors, gl_error) };
        return e;
    }
    if function == FEnum_glDeleteTextures {
        unsafe {
            dreamgpu_texture_delete(
                memory,
                (*state).textures,
                data,
                a[0],
                (*state).default_texture,
                (*state).default_texture_1d,
                addr_of_mut!((*state).bound_texture),
                addr_of_mut!((*state).bound_texture_1d),
            )
        };
        return 0;
    }
    unsafe {
        dreamgpu_texture_wait(m.api, texture, serial);
        if let Err(e) = query::remember(api, errors) {
            return e;
        }
    }
    let mut gl_error = 0;
    let e = unsafe {
        dreamgpu_texture_upload(
            m.api,
            texture,
            m.bytes,
            serial,
            function,
            a.as_ptr(),
            data,
            bytes,
            Some(zero),
            memory.cast_mut().cast(),
            &mut gl_error,
        )
    };
    unsafe { query::store(errors, gl_error) };
    e
}
#[cfg(test)]
mod tests;
