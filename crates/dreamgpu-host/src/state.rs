// SPDX-License-Identifier: GPL-2.0-or-later
//! Render-context guest state and its owned texture/attribute references.
#![allow(non_upper_case_globals)]
use crate::{
    gl_api::*,
    texture::{
        dreamgpu_texture_wait, dreamgpu_texture_written,
        names::{
            dreamgpu_texture_bind, dreamgpu_texture_namespace_unref, dreamgpu_texture_unref,
            Memory, Namespace,
        },
        Texture,
    },
};
use core::{
    ffi::c_void,
    ptr::{addr_of_mut, null_mut},
};
const STACK: usize = 16;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Attrib {
    pub mask: u32,
    pub color_sum: u32,
    pub draw_buffer: u32,
    pub read_buffer: u32,
    pub texture: *mut Texture,
    pub texture_1d: *mut Texture,
}
impl Attrib {
    const EMPTY: Self = Self {
        mask: 0,
        color_sum: 0,
        draw_buffer: 0,
        read_buffer: 0,
        texture: null_mut(),
        texture_1d: null_mut(),
    };
}
#[repr(C)]
pub struct ContextState {
    pub textures: *mut Namespace,
    pub default_texture: *mut Texture,
    pub bound_texture: *mut Texture,
    pub default_texture_1d: *mut Texture,
    pub bound_texture_1d: *mut Texture,
    pub guest_errors: u32,
    pub draw_buffer: u32,
    pub read_buffer: u32,
    pub attrib_depth: u32,
    pub attrib: [Attrib; STACK],
    pub image: crate::pixel_image::State,
    pub capture: crate::selection::State,
    pub lists: *mut crate::lists::State,
    pub list_mode: u32,
}
impl ContextState {
    const EMPTY: Self = Self {
        textures: null_mut(),
        default_texture: null_mut(),
        bound_texture: null_mut(),
        default_texture_1d: null_mut(),
        bound_texture_1d: null_mut(),
        guest_errors: 0,
        draw_buffer: GL_BACK,
        read_buffer: GL_BACK,
        attrib_depth: 0,
        attrib: [Attrib::EMPTY; STACK],
        image: crate::pixel_image::State::EMPTY,
        capture: crate::selection::State::EMPTY,
        lists: null_mut(),
        list_mode: 0,
    };
}

/// # Safety
/// State is fresh render-worker-owned storage. Memory callbacks allocate aligned
/// writable storage and cannot reenter; shared namespace is a live same-client
/// namespace. No allocation/state pointer is published until all allocations pass.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_context_state_init(
    memory: *const Memory,
    state: *mut ContextState,
    shared: *mut Namespace,
) -> u32 {
    if memory.is_null() || state.is_null() {
        return DG_GL_ERROR_CONTEXT;
    }
    let m = unsafe { &*memory };
    unsafe {
        state.write(ContextState::EMPTY);
    }
    if unsafe { *m.count } > DG_GL_MAX_TEXTURES - 2 {
        return DG_GL_ERROR_LIMIT;
    }
    let namespace = if shared.is_null() {
        unsafe { (m.allocate)(m.opaque, core::mem::size_of::<Namespace>()) }.cast()
    } else {
        shared
    };
    if namespace.is_null() {
        return DG_GL_ERROR_HOST;
    }
    let two: *mut Texture =
        unsafe { (m.allocate)(m.opaque, core::mem::size_of::<Texture>()) }.cast();
    let one: *mut Texture = if two.is_null() {
        null_mut()
    } else {
        unsafe { (m.allocate)(m.opaque, core::mem::size_of::<Texture>()) }.cast()
    };
    if two.is_null() || one.is_null() {
        if !two.is_null() {
            unsafe {
                (m.free)(m.opaque, two.cast());
            }
        }
        if shared.is_null() {
            unsafe {
                (m.free)(m.opaque, namespace.cast());
            }
        }
        return DG_GL_ERROR_HOST;
    }
    unsafe {
        if shared.is_null() {
            core::ptr::write_bytes(namespace, 0, 1);
        }
        (*namespace).refs = (*namespace)
            .refs
            .checked_add(1)
            .expect("namespace reference overflow");
        two.write(Texture {
            refs: 2,
            target: GL_TEXTURE_2D,
            ..Texture::default()
        });
        one.write(Texture {
            refs: 2,
            target: GL_TEXTURE_1D,
            ..Texture::default()
        });
        *m.count += 2;
        state.write(ContextState {
            textures: namespace,
            default_texture: two,
            bound_texture: two,
            default_texture_1d: one,
            bound_texture_1d: one,
            ..ContextState::EMPTY
        });
    }
    0
}

/// # Safety
/// Same render-worker ownership as init. Appropriate native context is current.
/// Clear state before free/cache callbacks; release each retained reference once.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_context_state_release(
    memory: *const Memory,
    state: *mut ContextState,
) {
    if memory.is_null() || state.is_null() {
        return;
    }
    let mut old = unsafe { state.read() };
    unsafe {
        state.write(ContextState::EMPTY);
    }
    unsafe { crate::pixel_image::release(&*memory, &mut old.image) };
    unsafe { crate::selection::release(&*memory, &mut old.capture) };
    unsafe { crate::lists::release(&*memory, old.lists) };
    assert!(old.attrib_depth as usize <= STACK);
    for saved in &old.attrib[..old.attrib_depth as usize] {
        unsafe {
            dreamgpu_texture_unref(memory, saved.texture);
            dreamgpu_texture_unref(memory, saved.texture_1d);
        }
    }
    unsafe {
        dreamgpu_texture_unref(memory, old.bound_texture);
        dreamgpu_texture_unref(memory, old.default_texture);
        dreamgpu_texture_unref(memory, old.bound_texture_1d);
        dreamgpu_texture_unref(memory, old.default_texture_1d);
        dreamgpu_texture_namespace_unref(memory, old.textures);
    }
}

fn attachment(buffer: u32) -> u32 {
    if matches!(buffer, GL_BACK | GL_BACK_LEFT) {
        GL_COLOR_ATTACHMENT0
    } else {
        GL_COLOR_ATTACHMENT1
    }
}
unsafe fn select(api: &DreamGpuGlApi, state: *const ContextState) -> Result<(), u32> {
    let draw = api.dg_glDrawBuffer.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let draws = api.dg_glDrawBuffers.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let read = api.dg_glReadBuffer.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let buffer = unsafe { (*state).draw_buffer };
    unsafe {
        if matches!(buffer, GL_FRONT_AND_BACK | GL_LEFT) {
            draws(2, [GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1].as_ptr());
        } else {
            draw(if buffer == GL_NONE {
                GL_NONE
            } else {
                attachment(buffer)
            });
        }
        read(attachment((*state).read_buffer));
    }
    Ok(())
}
/// # Safety
/// Read-only state belongs to current render context; native API is complete.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_context_select_buffers(
    api: *const DreamGpuGlApi,
    state: *const ContextState,
) -> u32 {
    if api.is_null() || state.is_null() {
        return DG_GL_ERROR_CONTEXT;
    }
    match unsafe { select(&*api, state) } {
        Ok(()) => 0,
        Err(e) => e,
    }
}
unsafe fn error(state: *mut ContextState, code: u32) {
    unsafe {
        crate::query::store(addr_of_mut!((*state).guest_errors), code);
    }
}
unsafe fn remember(api: &DreamGpuGlApi, state: *mut ContextState) -> Result<(), u32> {
    unsafe { crate::query::remember(api, addr_of_mut!((*state).guest_errors)) }
}
unsafe fn push(api: &DreamGpuGlApi, state: *mut ContextState, mask: u32) -> Result<(), u32> {
    if mask & !GL_ALL_ATTRIB_BITS != 0 {
        unsafe { error(state, GL_INVALID_VALUE) };
        return Ok(());
    }
    let depth = unsafe { (*state).attrib_depth } as usize;
    if depth == STACK {
        unsafe { error(state, GL_STACK_OVERFLOW) };
        return Ok(());
    }
    assert!(depth < STACK);
    let push = api.dg_glPushAttrib.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let enabled = api.dg_glIsEnabled.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    unsafe {
        remember(api, state)?;
        push(mask);
    }
    let code = unsafe { get_error() };
    if code != 0 {
        unsafe { error(state, code) };
        return Ok(());
    }
    let mut saved = Attrib {
        mask,
        draw_buffer: unsafe { (*state).draw_buffer },
        read_buffer: unsafe { (*state).read_buffer },
        ..Attrib::EMPTY
    };
    if mask & (GL_FOG_BIT | GL_ENABLE_BIT) != 0 {
        saved.color_sum = unsafe { enabled(GL_COLOR_SUM) } as u32;
    }
    if mask & GL_TEXTURE_BIT != 0 {
        unsafe {
            saved.texture = (*state).bound_texture;
            saved.texture_1d = (*state).bound_texture_1d;
            (*saved.texture).refs = (*saved.texture)
                .refs
                .checked_add(1)
                .expect("texture reference overflow");
            (*saved.texture_1d).refs = (*saved.texture_1d)
                .refs
                .checked_add(1)
                .expect("texture reference overflow");
        }
    }
    unsafe {
        (*state).attrib[depth] = saved;
        (*state).attrib_depth += 1;
    }
    Ok(())
}
unsafe fn pop(memory: *const Memory, state: *mut ContextState, serial: u64) -> Result<(), u32> {
    let api = unsafe { &*(*memory).api };
    let depth = unsafe { (*state).attrib_depth } as usize;
    if depth == 0 {
        unsafe { error(state, GL_STACK_UNDERFLOW) };
        return Ok(());
    }
    assert!(depth <= STACK);
    let saved = unsafe { (*state).attrib[depth - 1] };
    let pop = api.dg_glPopAttrib.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    // Precheck restoration before consuming the native attribute stack entry.
    let enable = api.dg_glEnable.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let disable = api.dg_glDisable.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    if api.dg_glDrawBuffer.is_none()
        || api.dg_glDrawBuffers.is_none()
        || api.dg_glReadBuffer.is_none()
        || api.dg_glWaitSync.is_none()
        || api.dg_glFenceSync.is_none()
        || api.dg_glDeleteSync.is_none()
    {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    if !saved.texture.is_null() {
        unsafe {
            dreamgpu_texture_wait(api, saved.texture, serial);
            dreamgpu_texture_wait(api, saved.texture_1d, serial);
        }
    }
    unsafe {
        remember(api, state)?;
        pop();
    }
    let code = unsafe { get_error() };
    if code != 0 {
        unsafe { error(state, code) };
        return Ok(());
    }
    if saved.mask & (GL_FOG_BIT | GL_ENABLE_BIT) != 0 {
        unsafe {
            if saved.color_sum != 0 {
                enable(GL_COLOR_SUM)
            } else {
                disable(GL_COLOR_SUM)
            }
        }
    }
    unsafe {
        if saved.mask & GL_COLOR_BUFFER_BIT != 0 {
            (*state).draw_buffer = saved.draw_buffer;
        }
        if saved.mask & GL_PIXEL_MODE_BIT != 0 {
            (*state).read_buffer = saved.read_buffer;
        }
        if !saved.texture.is_null() {
            let old_two = (*state).bound_texture;
            let old_one = (*state).bound_texture_1d;
            (*state).bound_texture = saved.texture;
            (*state).bound_texture_1d = saved.texture_1d;
            dreamgpu_texture_unref(memory, old_two);
            dreamgpu_texture_unref(memory, old_one);
            dreamgpu_texture_written(api, saved.texture, serial);
            dreamgpu_texture_written(api, saved.texture_1d, serial);
        }
        (*state).attrib[depth - 1] = Attrib::EMPTY;
        (*state).attrib_depth -= 1;
        select(api, state)?;
    }
    Ok(())
}
pub type CopyTexture = unsafe extern "C" fn(*mut c_void, u32, *const u8) -> u32;
unsafe fn scalar(
    memory: *const Memory,
    state: *mut ContextState,
    serial: u64,
    function: u32,
    args: &[u8],
    copy: Option<CopyTexture>,
    opaque: *mut c_void,
) -> Result<(), u32> {
    let api = unsafe { &*(*memory).api };
    let word = |n: usize| u32::from_le_bytes(args[n * 4..n * 4 + 4].try_into().unwrap());
    macro_rules! call{($field:ident($($args:expr),*))=>{unsafe{api.$field.ok_or(DG_GL_ERROR_UNSUPPORTED)?($($args),*)}};}
    match function {
        FEnum_glPushAttrib => return unsafe { push(api, state, word(0)) },
        FEnum_glPopAttrib => return unsafe { pop(memory, state, serial) },
        FEnum_glDrawBuffer | FEnum_glReadBuffer => {
            let mode = word(0);
            let draw = function == FEnum_glDrawBuffer;
            if crate::gl_validation::dreamgpu_gl_buffer_selection(mode, draw as u32) == 0 {
                return Err(DG_GL_ERROR_UNSUPPORTED);
            }
            unsafe {
                if draw {
                    (*state).draw_buffer = mode
                } else {
                    (*state).read_buffer = mode
                }
                select(api, state)?;
            }
        }
        FEnum_glHint => {
            if unsafe { crate::gl_validation::dreamgpu_gl_call_validate(function, args.as_ptr()) }
                != 0
            {
                return Err(DG_GL_ERROR_UNSUPPORTED);
            }
            call!(dg_glHint(word(0), word(1)));
        }
        FEnum_glBindTexture => {
            let target = word(0);
            let (default, binding) = unsafe {
                if target == GL_TEXTURE_1D {
                    (
                        (*state).default_texture_1d,
                        addr_of_mut!((*state).bound_texture_1d),
                    )
                } else {
                    (
                        (*state).default_texture,
                        addr_of_mut!((*state).bound_texture),
                    )
                }
            };
            let result = unsafe {
                dreamgpu_texture_bind(
                    memory,
                    (*state).textures,
                    target,
                    word(1),
                    default,
                    binding,
                    serial,
                    addr_of_mut!((*state).guest_errors),
                )
            };
            if result != 0 {
                return Err(result);
            }
        }
        FEnum_glCopyTexImage2D
        | FEnum_glCopyTexSubImage2D
        | FEnum_glCopyTexImage1D
        | FEnum_glCopyTexSubImage1D => {
            let result =
                unsafe { copy.ok_or(DG_GL_ERROR_UNSUPPORTED)?(opaque, function, args.as_ptr()) };
            if result != 0 {
                return Err(result);
            }
        }
        FEnum_glTexParameteri | FEnum_glTexParameterf => {
            if crate::gl_validation::dreamgpu_gl_texture_params(word(0), word(1)) != 1 {
                return Err(DG_GL_ERROR_TEXTURE);
            }
            let texture = unsafe {
                if word(0) == GL_TEXTURE_1D {
                    (*state).bound_texture_1d
                } else {
                    (*state).bound_texture
                }
            };
            unsafe {
                dreamgpu_texture_wait(api, texture, serial);
            }
            if function == FEnum_glTexParameteri {
                call!(dg_glTexParameteri(word(0), word(1), word(2) as i32));
            } else {
                call!(dg_glTexParameterf(
                    word(0),
                    word(1),
                    f32::from_bits(word(2))
                ));
            }
            unsafe {
                dreamgpu_texture_written(api, texture, serial);
            }
        }
        FEnum_glTexEnvi | FEnum_glTexEnvf => {
            if crate::gl_validation::dreamgpu_gl_texture_env_params(word(0), word(1)) != 1 {
                return Err(DG_GL_ERROR_TEXTURE);
            }
            if function == FEnum_glTexEnvi {
                call!(dg_glTexEnvi(word(0), word(1), word(2) as i32));
            } else {
                call!(dg_glTexEnvf(word(0), word(1), f32::from_bits(word(2))));
            }
        }
        FEnum_glBegin => {
            if word(0) > GL_POLYGON {
                return Err(DG_GL_ERROR_CONTEXT);
            }
            for (texture, cap) in unsafe {
                [
                    ((*state).bound_texture, GL_TEXTURE_2D),
                    ((*state).bound_texture_1d, GL_TEXTURE_1D),
                ]
            } {
                if unsafe { (*texture).undefined_levels } != 0 && call!(dg_glIsEnabled(cap)) != 0 {
                    return Err(DG_GL_ERROR_TEXTURE);
                }
            }
            unsafe {
                dreamgpu_texture_wait(api, (*state).bound_texture, serial);
                dreamgpu_texture_wait(api, (*state).bound_texture_1d, serial);
            }
        }
        _ => return Err(DG_GL_ERROR_UNSUPPORTED),
    }
    Ok(())
}
/// # Safety
/// Current render context owns state. Args are an immutable command snapshot.
/// Copy callback can inspect state but cannot retain pointers/reenter. Rust uses
/// raw state access and holds no exclusive context reference across callbacks.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_context_resource(
    memory: *const Memory,
    state: *mut ContextState,
    serial: u64,
    function: u32,
    args: *const u8,
    bytes: u32,
    copy: Option<CopyTexture>,
    opaque: *mut c_void,
) -> u32 {
    if memory.is_null() || state.is_null() || args.is_null() {
        return DG_GL_ERROR_CONTEXT;
    }
    let words = crate::gl_validation::dreamgpu_gl_function_words(function);
    if words > 32 || words * 4 != bytes {
        return DG_GL_ERROR_BATCH;
    }
    let args = unsafe { core::slice::from_raw_parts(args, bytes as usize) };
    match unsafe { scalar(memory, state, serial, function, args, copy, opaque) } {
        Ok(()) => 0,
        Err(e) => e,
    }
}
#[cfg(test)]
mod tests;
