// SPDX-License-Identifier: GPL-2.0-or-later
//! Typed guest queries. Native FBO names never escape the logical device state.
#![allow(non_upper_case_globals)] // Preserve generated GL function identifiers.
use crate::gl_api::*;
use core::ffi::c_void;

#[repr(C)]
pub struct QueryState {
    pub in_begin: u32,
    pub has_drawable: u32,
    pub width: u32,
    pub height: u32,
    pub draw_buffer: u32,
    pub read_buffer: u32,
    pub binding_1d: u32,
    pub binding_2d: u32,
    pub attrib_depth: u32,
    pub textures: *mut crate::texture::names::Namespace,
    pub capture: *mut crate::selection::State,
    pub memory: *const crate::texture::names::Memory,
    pub context: *mut crate::state::ContextState,
}
pub type TextureRead = unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, *mut u8) -> u32;

pub(crate) unsafe fn remember(api: &DreamGpuGlApi, errors: *mut u32) -> Result<(), u32> {
    let get = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    for _ in 0..8 {
        let error = unsafe { get() };
        if error == GL_NO_ERROR {
            break;
        }
        unsafe {
            store(errors, error);
        }
    }
    Ok(())
}
pub(crate) unsafe fn store(errors: *mut u32, error: u32) {
    if error != GL_NO_ERROR {
        let bit = error.wrapping_sub(GL_INVALID_ENUM);
        unsafe {
            *errors |= 1 << if bit < 8 { bit } else { 2 };
        }
    }
}

// Drop restores the five pack values on both success and native read failure.
// Functions are checked before any state is changed. GL does not retain values.
pub(crate) struct Pack<'a> {
    api: &'a DreamGpuGlApi,
    values: [i32; 5],
}
const PACK_NAMES: [u32; 5] = [
    GL_PACK_ALIGNMENT,
    GL_PACK_ROW_LENGTH,
    GL_PACK_SKIP_ROWS,
    GL_PACK_SKIP_PIXELS,
    GL_PACK_SWAP_BYTES,
];
impl<'a> Pack<'a> {
    pub(crate) unsafe fn new(api: &'a DreamGpuGlApi) -> Result<Self, u32> {
        let get = api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let set = api.dg_glPixelStorei.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let mut values = [0; 5];
        for (i, name) in PACK_NAMES.into_iter().enumerate() {
            unsafe {
                get(name, &mut values[i]);
            }
        }
        for (i, name) in PACK_NAMES.into_iter().enumerate() {
            unsafe {
                set(name, if i == 0 { 1 } else { 0 });
            }
        }
        Ok(Self { api, values })
    }
}
impl Drop for Pack<'_> {
    fn drop(&mut self) {
        for (name, value) in PACK_NAMES.into_iter().zip(self.values) {
            unsafe {
                self.api.dg_glPixelStorei.unwrap()(name, value);
            }
        }
    }
}

#[expect(
    clippy::too_many_arguments,
    reason = "Mirrors the native query ABI with independent buffers and callback ownership."
)]
unsafe fn execute(
    api: &DreamGpuGlApi,
    state: &QueryState,
    errors: *mut u32,
    function: u32,
    args: *const u8,
    result: *mut u8,
    capacity: u32,
    texture_read: Option<TextureRead>,
    opaque: *mut c_void,
    bytes: *mut u32,
    kind: *mut u32,
) -> Result<(), u32> {
    use crate::gl_validation::{
        dreamgpu_gl_query_result_bytes, dreamgpu_gl_query_shape, dreamgpu_gl_query_string,
        dreamgpu_gl_query_validate,
    };
    let valid = unsafe { dreamgpu_gl_query_validate(function, args) };
    if valid != 0 {
        return Err(valid);
    }
    let count = unsafe { dreamgpu_gl_query_shape(function, args, kind) } as usize;
    if count == 0 || state.in_begin != 0 {
        return Err(DG_GL_ERROR_CONTEXT);
    }
    let required = unsafe { dreamgpu_gl_query_result_bytes(function, args) };
    if required > capacity || required > DG_GL_MAX_READBACK_BYTES {
        return Err(DG_GL_ERROR_LIMIT);
    }
    let words = unsafe { core::slice::from_raw_parts(args, 12) };
    let a = u32::from_le_bytes(words[0..4].try_into().unwrap());
    let b = u32::from_le_bytes(words[4..8].try_into().unwrap());
    let d = u32::from_le_bytes(words[8..12].try_into().unwrap());
    if crate::lists::query_function(function) {
        if state.context.is_null() || state.memory.is_null() {
            return Err(DG_GL_ERROR_CONTEXT);
        }
        unsafe {
            crate::lists::query(&*state.memory, state.context, function, [a, b, d], result)?;
            *bytes = required;
        }
        return Ok(());
    }
    if crate::selection::query_function(function) {
        if state.capture.is_null() || state.memory.is_null() {
            return Err(DG_GL_ERROR_CONTEXT);
        }
        unsafe {
            crate::selection::query(
                &*state.memory,
                state.capture,
                errors,
                function,
                [a, b, d],
                result,
            )?;
            *bytes = required;
        }
        return Ok(());
    }
    if crate::evaluator::map_query(function) {
        unsafe {
            crate::evaluator::get(api, errors, function, a, b, d, result)?;
            *bytes = required;
        }
        return Ok(());
    }
    if function == FEnum_glGetPolygonStipple {
        unsafe {
            crate::stipple::get(api, errors, result)?;
            *bytes = 128;
        }
        return Ok(());
    }
    if crate::pixels::map_query(function) {
        unsafe {
            crate::pixels::get_map(api, errors, function, a, b, result)?;
            *bytes = required;
        }
        return Ok(());
    }
    if function == FEnum_glAreTexturesResident {
        let values =
            unsafe { crate::texture::control::resident(api, state.textures, errors, [a, b, d])? };
        unsafe {
            core::ptr::copy_nonoverlapping(values.as_ptr(), result, 5);
            *bytes = 5;
        }
        return Ok(());
    }
    if function == FEnum_glGetTexImage {
        let read = texture_read.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let error = unsafe { read(opaque, a, b & 0xffff, d, required, result) };
        if error != 0 {
            return Err(error);
        }
        unsafe {
            *bytes = required;
        }
        return Ok(());
    }
    if function == FEnum_glReadPixels {
        let mode = a & !DG_GL_READ_X_MASK;
        let x = a & DG_GL_READ_X_MASK;
        let (format, pixel_type) = match mode {
            DG_GL_READ_DEPTH => (GL_DEPTH_COMPONENT, GL_FLOAT),
            DG_GL_READ_STENCIL => (GL_STENCIL_INDEX, GL_UNSIGNED_INT),
            DG_GL_READ_RGBA_FLOAT => (GL_RGBA, GL_FLOAT),
            _ => (GL_RGBA, GL_UNSIGNED_BYTE),
        };
        let width = d & 0xffff;
        let height = d >> 16;
        if state.has_drawable == 0
            || x >= state.width
            || b >= state.height
            || width > state.width - x
            || height > state.height - b
        {
            return Err(DG_GL_ERROR_DRAWABLE);
        }
        let read = api.dg_glReadPixels.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        api.dg_glPixelStorei.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        unsafe {
            remember(api, errors)?;
        }
        let pack = unsafe { Pack::new(api)? };
        unsafe {
            read(
                x as i32,
                b as i32,
                width as i32,
                height as i32,
                format,
                pixel_type,
                result.cast(),
            );
        }
        let error = unsafe { get_error() };
        drop(pack);
        if error != GL_NO_ERROR {
            unsafe {
                store(errors, error);
            }
            return Err(DG_GL_ERROR_HOST);
        }
        unsafe {
            *bytes = required;
        }
        return Ok(());
    }
    if function == FEnum_glGetString {
        let string = dreamgpu_gl_query_string(a);
        if string.is_null() {
            return Err(DG_GL_ERROR_UNSUPPORTED);
        }
        unsafe {
            core::ptr::copy_nonoverlapping(string.cast(), result, required as usize);
            *bytes = required;
        }
        return Ok(());
    }
    if count > 16 {
        return Err(DG_GL_ERROR_LIMIT);
    }
    let mut integers = [0i32; 16];
    let mut floats = [0f32; 16];
    let mut doubles = [0f64; 16];
    let mut booleans = [0u8; 16];
    let mut logical = false;
    macro_rules! call { ($field:ident($($args:expr),*)) => {
        unsafe { api.$field.ok_or(DG_GL_ERROR_UNSUPPORTED)?($($args),*) }
    }; }
    match function {
        FEnum_glGetError => {
            unsafe {
                remember(api, errors)?;
            }
            for i in 0..8 {
                if unsafe { *errors } & (1 << i) != 0 {
                    integers[0] = (GL_INVALID_ENUM + i) as i32;
                    unsafe {
                        *errors &= !(1 << i);
                    }
                    break;
                }
            }
        }
        FEnum_glIsTexture => {
            booleans[0] = u8::from(
                a != 0
                    && !unsafe {
                        crate::texture::names::dreamgpu_texture_lookup(state.textures, a)
                    }
                    .is_null(),
            )
        }
        FEnum_glIsEnabled => booleans[0] = call!(dg_glIsEnabled(a)),
        FEnum_glGetTexParameteriv => call!(dg_glGetTexParameteriv(a, b, integers.as_mut_ptr())),
        FEnum_glGetTexParameterfv => call!(dg_glGetTexParameterfv(a, b, floats.as_mut_ptr())),
        FEnum_glGetTexLevelParameteriv => call!(dg_glGetTexLevelParameteriv(
            a,
            b as i32,
            d,
            integers.as_mut_ptr()
        )),
        FEnum_glGetTexLevelParameterfv => call!(dg_glGetTexLevelParameterfv(
            a,
            b as i32,
            d,
            floats.as_mut_ptr()
        )),
        FEnum_glGetTexEnviv => call!(dg_glGetTexEnviv(a, b, integers.as_mut_ptr())),
        FEnum_glGetTexEnvfv => call!(dg_glGetTexEnvfv(a, b, floats.as_mut_ptr())),
        FEnum_glGetLightfv => call!(dg_glGetLightfv(a, b, floats.as_mut_ptr())),
        FEnum_glGetLightiv => call!(dg_glGetLightiv(a, b, integers.as_mut_ptr())),
        FEnum_glGetMaterialfv => call!(dg_glGetMaterialfv(a, b, floats.as_mut_ptr())),
        FEnum_glGetMaterialiv => call!(dg_glGetMaterialiv(a, b, integers.as_mut_ptr())),
        FEnum_glGetTexGenfv => call!(dg_glGetTexGenfv(a, b, floats.as_mut_ptr())),
        FEnum_glGetTexGeniv => call!(dg_glGetTexGeniv(a, b, integers.as_mut_ptr())),
        FEnum_glGetTexGendv => call!(dg_glGetTexGendv(a, b, doubles.as_mut_ptr())),
        FEnum_glGetClipPlane => call!(dg_glGetClipPlane(a, doubles.as_mut_ptr())),
        _ => {
            logical = true;
            match a {
                GL_LIST_INDEX | GL_LIST_MODE | GL_MAX_LIST_NESTING => {
                    if state.context.is_null() {
                        return Err(DG_GL_ERROR_CONTEXT);
                    }
                    integers[0] = unsafe { crate::lists::state_value(state.context, a) } as i32;
                }
                GL_SELECTION_BUFFER_SIZE | GL_FEEDBACK_BUFFER_SIZE => {
                    if state.capture.is_null() {
                        return Err(DG_GL_ERROR_CONTEXT);
                    }
                    integers[0] = unsafe {
                        if a == GL_SELECTION_BUFFER_SIZE {
                            (*state.capture).select_size
                        } else {
                            (*state.capture).feedback_size
                        }
                    } as i32;
                }
                GL_DRAW_BUFFER => integers[0] = state.draw_buffer as i32,
                GL_READ_BUFFER => integers[0] = state.read_buffer as i32,
                GL_DOUBLEBUFFER => integers[0] = 1,
                GL_STEREO | GL_AUX_BUFFERS => (),
                GL_TEXTURE_BINDING_1D => integers[0] = state.binding_1d as i32,
                GL_TEXTURE_BINDING_2D => integers[0] = state.binding_2d as i32,
                GL_ATTRIB_STACK_DEPTH => integers[0] = state.attrib_depth as i32,
                GL_MAX_ATTRIB_STACK_DEPTH => integers[0] = 16,
                GL_MAX_EVAL_ORDER => {
                    integers[0] = unsafe { crate::evaluator::limit(api)? } as i32;
                }
                GL_MAX_PIXEL_MAP_TABLE => {
                    integers[0] = unsafe { crate::pixels::limit(api)? } as i32
                }
                GL_MAX_TEXTURE_SIZE => integers[0] = DG_GL_MAX_TEXTURE_DIMENSION as i32,
                GL_MAX_VIEWPORT_DIMS => {
                    integers[0] = DG_GL_MAX_DIMENSION as i32;
                    integers[1] = integers[0];
                }
                GL_MAX_LIGHTS => integers[0] = 8,
                GL_MAX_CLIP_PLANES => integers[0] = 6,
                _ => logical = false,
            }
            if !logical {
                match unsafe { *kind } {
                    DG_GL_RESULT_INT => call!(dg_glGetIntegerv(a, integers.as_mut_ptr())),
                    DG_GL_RESULT_FLOAT => call!(dg_glGetFloatv(a, floats.as_mut_ptr())),
                    DG_GL_RESULT_DOUBLE => call!(dg_glGetDoublev(a, doubles.as_mut_ptr())),
                    DG_GL_RESULT_BOOL => call!(dg_glGetBooleanv(a, booleans.as_mut_ptr())),
                    _ => return Err(DG_GL_ERROR_UNSUPPORTED),
                }
                // Secondary alpha is zero by EXT_secondary_color, including on
                // Apple's legacy driver which reports one in native state.
                if a == GL_CURRENT_SECONDARY_COLOR {
                    integers[3] = 0;
                    floats[3] = 0.0;
                    doubles[3] = 0.0;
                    booleans[3] = 0;
                }
            }
        }
    }
    if logical {
        for i in 0..count {
            doubles[i] = if a == GL_TEXTURE_BINDING_2D {
                integers[i] as u32 as f64
            } else {
                integers[i] as f64
            };
            floats[i] = doubles[i] as f32;
            booleans[i] = u8::from(integers[i] != 0);
        }
    }
    // C may allocate the result with malloc rather than calloc. Write through
    // raw pointers; a Rust byte slice would assert initialized elements.
    for i in 0..count {
        match unsafe { *kind } {
            DG_GL_RESULT_BOOL => unsafe {
                result.add(i).write(u8::from(booleans[i] != 0));
            },
            DG_GL_RESULT_INT => unsafe {
                core::ptr::copy_nonoverlapping(
                    integers[i].to_le_bytes().as_ptr(),
                    result.add(i * 4),
                    4,
                );
            },
            DG_GL_RESULT_FLOAT => unsafe {
                core::ptr::copy_nonoverlapping(
                    floats[i].to_bits().to_le_bytes().as_ptr(),
                    result.add(i * 4),
                    4,
                );
            },
            DG_GL_RESULT_DOUBLE => unsafe {
                core::ptr::copy_nonoverlapping(
                    doubles[i].to_bits().to_le_bytes().as_ptr(),
                    result.add(i * 8),
                    8,
                );
            },
            _ => return Err(DG_GL_ERROR_UNSUPPORTED),
        }
    }
    unsafe {
        *bytes = required;
    }
    Ok(())
}

/// # Safety
/// The render worker owns state and its namespace. Args are 12 immutable copied
/// bytes. Result owns capacity writable bytes, disjoint from args/state/outputs.
/// Native GL is current. Texture callback cannot retain pointers or reenter this
/// query; its opaque context may touch disjoint cache/error storage. No output
/// reference or context reference is held across that callback.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_query(
    api: *const DreamGpuGlApi,
    state: *const QueryState,
    errors: *mut u32,
    function: u32,
    args: *const u8,
    result: *mut u8,
    capacity: u32,
    texture_read: Option<TextureRead>,
    opaque: *mut c_void,
    bytes: *mut u32,
    kind: *mut u32,
) -> u32 {
    if api.is_null()
        || state.is_null()
        || errors.is_null()
        || args.is_null()
        || result.is_null()
        || bytes.is_null()
        || kind.is_null()
    {
        return DG_GL_ERROR_BATCH;
    }
    unsafe {
        *bytes = 0;
        *kind = 0;
    }
    match unsafe {
        execute(
            &*api,
            &*state,
            errors,
            function,
            args,
            result,
            capacity,
            texture_read,
            opaque,
            bytes,
            kind,
        )
    } {
        Ok(()) => 0,
        Err(error) => error,
    }
}

#[cfg(test)]
mod tests;
