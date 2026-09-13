// SPDX-License-Identifier: GPL-2.0-or-later
//! Residency and priority use native texture objects resolved from the owner's
//! namespace. No inferred residency, private binding mutation, or persistent
//! query scratch crosses the native ABI.
use super::names::{dreamgpu_texture_lookup, Namespace};
use crate::{gl_api::*, query};

pub(crate) unsafe fn resident(
    api: &DreamGpuGlApi,
    ns: *mut Namespace,
    errors: *mut u32,
    names: [u32; 3],
) -> Result<[u8; 5], u32> {
    if ns.is_null() {
        return Err(DG_GL_ERROR_CONTEXT);
    }
    let native = api
        .dg_glAreTexturesResident
        .ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let mut objects = [0; 3];
    for (i, name) in names.into_iter().enumerate() {
        let t = unsafe { dreamgpu_texture_lookup(ns, name) };
        if name == 0 || t.is_null() {
            unsafe { query::store(errors, GL_INVALID_VALUE) };
            return Ok([0; 5]);
        }
        objects[i] = unsafe { (*t).name };
    }
    unsafe { query::remember(api, errors)? };
    let mut residence = [0; 3];
    let all = unsafe { native(3, objects.as_ptr(), residence.as_mut_ptr()) };
    let error = unsafe { get_error() };
    if error != 0 {
        unsafe { query::store(errors, error) };
        return Ok([0; 5]);
    }
    if all != 0 {
        residence.fill(1);
    }
    Ok([
        1,
        u8::from(all != 0),
        u8::from(residence[0] != 0),
        u8::from(residence[1] != 0),
        u8::from(residence[2] != 0),
    ])
}

pub(crate) unsafe fn prioritize(
    api: &DreamGpuGlApi,
    ns: *mut Namespace,
    errors: *mut u32,
    data: &[u8],
) -> Result<(), u32> {
    if ns.is_null() {
        return Err(DG_GL_ERROR_CONTEXT);
    }
    let native = api.dg_glPrioritizeTextures.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    unsafe { query::remember(api, errors)? };
    for pair in data.as_chunks::<8>().0 {
        let name = u32::from_le_bytes(pair[..4].try_into().unwrap());
        let t = unsafe { dreamgpu_texture_lookup(ns, name) };
        // GL explicitly ignores zero and names which are not texture objects.
        if name == 0 || t.is_null() {
            continue;
        }
        let object = unsafe { (*t).name };
        let priority = f32::from_bits(u32::from_le_bytes(pair[4..].try_into().unwrap()));
        unsafe { native(1, &object, &priority) };
    }
    let error = unsafe { get_error() };
    if error != 0 {
        unsafe { query::store(errors, error) };
        return Err(DG_GL_ERROR_TEXTURE);
    }
    Ok(())
}
#[cfg(test)]
mod tests;
