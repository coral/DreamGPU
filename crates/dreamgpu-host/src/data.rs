// SPDX-License-Identifier: GPL-2.0-or-later
//! Validated inline-data dispatch and native-endian array staging.
#![allow(non_upper_case_globals)]
use crate::{
    gl_api::*,
    gl_validation as validation, query, resource,
    state::{bound, ContextState},
    texture::{
        dreamgpu_texture_upload, dreamgpu_texture_wait, dreamgpu_texture_written,
        names::{dreamgpu_texture_delete, Memory},
        Texture,
    },
};
use core::{ffi::c_void, ptr::addr_of_mut};
unsafe extern "C" fn zero(
    opaque: *mut c_void,
    t: *mut Texture,
    target: u32,
    level: u32,
    w: u32,
    h: u32,
) -> u32 {
    unsafe { resource::dreamgpu_texture_zero(opaque.cast::<Memory>(), t, target, level, w, h) }
}
/// Rejected-level mask of a proxy target: 1D, 2D, cube map or 3D.
unsafe fn proxy_mask(state: *mut ContextState, target: u32) -> *mut u32 {
    unsafe {
        match target {
            GL_PROXY_TEXTURE_1D => addr_of_mut!((*state).proxy_rejected[0]),
            GL_PROXY_TEXTURE_CUBE_MAP => addr_of_mut!((*state).proxy_rejected_cube),
            GL_PROXY_TEXTURE_3D => addr_of_mut!((*state).proxy_rejected_3d),
            _ => addr_of_mut!((*state).proxy_rejected[1]),
        }
    }
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
    if attributes & DG_GL_ARRAY_RAW != 0 {
        let input = unsafe { core::slice::from_raw_parts(data, bytes as usize) };
        let mut layout = [crate::arrays::raw::Attribute::default(); 32];
        if fnc == FEnum_glVertexAttribPointerARB {
            layout = crate::arrays::raw::layout_attribs(attributes, a[4], vertices as u32, input)?;
        } else if fnc == FEnum_glDrawArraysEXT {
            layout[..16].copy_from_slice(&crate::arrays::raw::layout_extended(
                attributes,
                vertices as u32,
                input,
            )?);
        } else {
            layout[..7].copy_from_slice(&crate::arrays::raw::layout(
                attributes,
                vertices as u32,
                input,
            )?);
        }
        for a in layout.iter().filter(|a| a.unit > 1) {
            let length = vertices * a.size as usize * a.unit;
            let output =
                unsafe { core::slice::from_raw_parts_mut(storage.p.add(a.offset), length) };
            for component in output.chunks_exact_mut(a.unit) {
                component.reverse();
            }
        }
        return Some(storage);
    }
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
/// The buffer draw (ARB_vertex_buffer_object) and the BGRA buffer draw
/// (ARB_vertex_array_bgra). Every state-dependent failure is the guest's GL
/// error; buffer contents are guest (little-endian) bytes, so big-endian
/// hosts, which do not advertise the capability, refuse it too.
unsafe fn buffer_draw(
    m: &Memory,
    state: *mut ContextState,
    serial: u64,
    function: u32,
    a: &[u32; 10],
    data: &[u8],
) -> u32 {
    let api = unsafe { &*m.api };
    let errors = unsafe { addr_of_mut!((*state).guest_errors) };
    let Some(layout) = crate::arrays::raw::layout_buffer_draw(function, a, data, false) else {
        return DG_GL_ERROR_BATCH;
    };
    if a[2] == 0 {
        return 0;
    }
    if cfg!(target_endian = "big") {
        unsafe { query::store(errors, GL_INVALID_OPERATION) };
        return 0;
    }
    let resolved = match unsafe { crate::buffer::resolve(state, a, &layout, data) } {
        Ok(resolved) => resolved,
        Err(code) => {
            unsafe { query::store(errors, code) };
            return 0;
        }
    };
    if let Err(e) = unsafe { crate::state::prepare_textures(api, state, serial) } {
        return e;
    }
    if let Err(e) = unsafe { query::remember(api, errors) } {
        return e;
    }
    if let Err(e) = unsafe { crate::texture::border::prepare(m, state) } {
        return e;
    }
    let mut gl_error = 0;
    let e = match unsafe { crate::arrays::buffer_draw(api, a, data, &layout, &resolved) } {
        Ok(code) => {
            gl_error = code;
            if code == 0 {
                0
            } else {
                DG_GL_ERROR_HOST
            }
        }
        Err(e) => e,
    };
    unsafe {
        crate::texture::border::finish(m, state);
        query::store(errors, gl_error)
    };
    // A native error of a validated draw is the guest's GL error.
    if e == DG_GL_ERROR_HOST && gl_error != 0 {
        0
    } else {
        e
    }
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
    if metadata & DG_GL_FUNCTION_INLINE_DATA == 0 || words > 10 {
        return DG_GL_ERROR_UNSUPPORTED;
    }
    if in_begin != 0 && function != FEnum_glMaterialfv {
        return DG_GL_ERROR_CONTEXT;
    }
    // Only the 3D image records (EXT_texture3D) carry more than eight words.
    let mut wide = [0; 10];
    for (i, w) in wide.iter_mut().enumerate().take(words as usize) {
        *w = u32::from_le(unsafe { args.add(i * 4).cast::<u32>().read_unaligned() });
    }
    let a: [u32; 8] = wide[..8].try_into().unwrap();
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
                // An invalid value is the guest's GL error, never a record
                // fault that would fail the batch and the context.
                unsafe { query::store(errors, e) };
                return 0;
            }
            unsafe { dreamgpu_texture_written(m.api, texture, serial) };
        }
        return 0;
    }
    if matches!(
        function,
        FEnum_glDrawArrays
            | FEnum_glDrawArraysEXT
            | FEnum_glDrawElements
            | FEnum_glVertexAttribPointerARB
    ) {
        let elements = function == FEnum_glDrawElements;
        if a[if elements { 3 } else { 2 }] == 0 || (elements && a[1] == 0) {
            return 0;
        }
        // Every unit's enabled target and producer fence, not only unit 0.
        if let Err(e) = unsafe { crate::state::prepare_textures(api, state, serial) } {
            return e;
        }
        let staged =
            unsafe { stage_arrays(m, function, &a, data, bytes, cfg!(target_endian = "big")) };
        if cfg!(target_endian = "big") && staged.is_none() {
            return DG_GL_ERROR_HOST;
        }
        let data = staged.as_ref().map_or(data, |s| s.p.cast_const());
        unsafe {
            if let Err(e) = query::remember(api, errors) {
                return e;
            }
        }
        if let Err(e) = unsafe { crate::texture::border::prepare(m, state) } {
            return e;
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
        unsafe {
            crate::texture::border::finish(m, state);
            query::store(errors, gl_error)
        };
        // A native error of a validated draw depends on GL state (an enabled
        // program that failed to load, a provider sampling rule) and is the
        // guest's GL error, as for immediate mode; it never fails the batch.
        return if e == DG_GL_ERROR_HOST && gl_error != 0 {
            0
        } else {
            e
        };
    }
    if matches!(
        function,
        FEnum_glProgramStringARB | FEnum_glDeleteProgramsARB
    ) {
        let data = if bytes == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, bytes as usize) }
        };
        let result = if function == FEnum_glProgramStringARB {
            unsafe { crate::program::string(m, state, &a, data) }
        } else {
            unsafe { crate::program::delete(m, state, data) }
        };
        return result.map_or_else(|e| e, |()| 0);
    }
    if matches!(
        function,
        FEnum_glBufferDataARB | FEnum_glBufferSubDataARB | FEnum_glDeleteBuffersARB
    ) {
        let data = if bytes == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, bytes as usize) }
        };
        let result = match function {
            FEnum_glBufferDataARB => unsafe { crate::buffer::data(m, state, &a, data) },
            FEnum_glBufferSubDataARB => unsafe { crate::buffer::sub_data(m, state, &a, data) },
            _ => unsafe { crate::buffer::delete(m, state, data) },
        };
        return result.map_or_else(|e| e, |()| 0);
    }
    if matches!(
        function,
        FEnum_glDeleteFramebuffers | FEnum_glDeleteRenderbuffers
    ) {
        let data = if bytes == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, bytes as usize) }
        };
        let result = if function == FEnum_glDeleteFramebuffers {
            unsafe { crate::framebuffer::delete_framebuffers(m, state, serial, data) }
        } else {
            unsafe { crate::framebuffer::delete_renderbuffers(m, state, serial, data) }
        };
        return result.map_or_else(|e| e, |()| 0);
    }
    if matches!(
        function,
        FEnum_glDrawRangeElements | FEnum_glDrawRangeElementsEXT
    ) {
        let data = if bytes == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, bytes as usize) }
        };
        return unsafe { buffer_draw(m, state, serial, function, &wide, data) };
    }
    if function == FEnum_glDeleteTextures {
        // A deleted texture leaves this context's bound framebuffers first,
        // as native deletion detaches it from them.
        if bytes != 0 {
            unsafe {
                crate::framebuffer::forget_textures(
                    m,
                    state,
                    serial,
                    core::slice::from_raw_parts(data, bytes as usize),
                )
            };
        }
        // Deleting a texture bound on any unit reverts that unit to default.
        let bindings = unsafe { crate::state::binding_slots(state) };
        unsafe {
            dreamgpu_texture_delete(
                memory,
                (*state).textures,
                data,
                a[0],
                (*state).default_texture,
                (*state).default_texture_1d,
                (*state).default_texture_cube,
                (*state).default_texture_3d,
                bindings.as_ptr(),
                bindings.len() as u32,
            )
        };
        return 0;
    }
    if matches!(function, FEnum_glTexImage3D | FEnum_glTexSubImage3D) {
        let data = if bytes == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, bytes as usize) }
        };
        unsafe {
            if let Err(e) = query::remember(api, errors) {
                return e;
            }
        }
        if function == FEnum_glTexImage3D && wide[0] == GL_PROXY_TEXTURE_3D {
            unsafe {
                let error = crate::texture::volume::proxy(
                    api,
                    &mut *proxy_mask(state, GL_PROXY_TEXTURE_3D),
                    &wide,
                );
                query::store(errors, error);
            }
            return 0;
        }
        unsafe { dreamgpu_texture_wait(m.api, texture, serial) };
        let mut gl_error = 0;
        let e = unsafe {
            crate::texture::volume::upload(
                m,
                texture,
                m.bytes,
                serial,
                function,
                &wide,
                data,
                &mut gl_error,
            )
        };
        unsafe { query::store(errors, gl_error) };
        return e.map_or_else(|e| e, |()| 0);
    }
    if matches!(
        function,
        FEnum_glCompressedTexImage2D | FEnum_glCompressedTexSubImage2D
    ) {
        let data = if bytes == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, bytes as usize) }
        };
        unsafe {
            if let Err(e) = query::remember(api, errors) {
                return e;
            }
        }
        if matches!(a[0], GL_PROXY_TEXTURE_2D | GL_PROXY_TEXTURE_CUBE_MAP) {
            // Shape and limits were validated; native GL judges the format.
            let error = unsafe { crate::texture::compressed_proxy(api, &a) };
            unsafe {
                let mask = proxy_mask(state, a[0]);
                if error == 0 {
                    *mask &= !(1 << a[1]);
                }
                query::store(errors, error);
            }
            return 0;
        }
        unsafe { dreamgpu_texture_wait(m.api, texture, serial) };
        let mut gl_error = 0;
        let e = unsafe {
            crate::texture::compressed(
                m,
                texture,
                m.bytes,
                serial,
                function,
                &a,
                data,
                &mut gl_error,
            )
        };
        unsafe { query::store(errors, gl_error) };
        return e.map_or_else(|e| e, |()| 0);
    }
    if matches!(function, FEnum_glTexImage1D | FEnum_glTexImage2D)
        && matches!(
            a[0],
            GL_PROXY_TEXTURE_1D | GL_PROXY_TEXTURE_2D | GL_PROXY_TEXTURE_CUBE_MAP
        )
    {
        unsafe {
            if let Err(e) = query::remember(api, errors) {
                return e;
            }
            let one = a[0] == GL_PROXY_TEXTURE_1D;
            let limit = (DG_GL_MAX_TEXTURE_DIMENSION >> a[1]) + 2 * a[5];
            let rejected = (a[5] != 0 && !crate::texture::BORDER_IMAGES)
                || a[3] > limit
                || (!one && a[4] > limit)
                || a[3] < 2 * a[5]
                || (!one && a[4] < 2 * a[5]);
            let mask = &mut *proxy_mask(state, a[0]);
            if rejected {
                // Native limits can exceed the guest contract. Reject without
                // native allocation or a driver-dependent fake image request.
                *mask |= 1 << a[1];
            } else {
                let error = crate::texture::proxy(api, &a);
                if error == 0 {
                    *mask &= !(1 << a[1]);
                }
                query::store(errors, error);
            }
        }
        return 0;
    }
    if matches!(function, FEnum_glTexImage1D | FEnum_glTexImage2D)
        && a[5] != 0
        && !crate::texture::BORDER_IMAGES
    {
        unsafe {
            query::store(errors, GL_INVALID_OPERATION);
        }
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
