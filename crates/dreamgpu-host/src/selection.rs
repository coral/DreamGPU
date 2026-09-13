// SPDX-License-Identifier: GPL-2.0-or-later
//! Native selection/feedback retains only bounded render-context-owned pointers.
//! RenderMode executes once; subsequent queries copy its completed prefix. GL1.1
//! sections 5.2/5.3 specify partial writes on overflow and deferred buffer updates.
#![allow(non_upper_case_globals)]
use crate::{gl_api::*, query, texture::names::Memory};
use core::ptr::null_mut;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct State {
    pub selection: *mut u32,
    pub feedback: *mut u32,
    pub select_size: u32,
    pub feedback_size: u32,
    pub mode: u32,
    pub completed_mode: u32,
    pub completed_count: u32,
}
impl State {
    pub const EMPTY: Self = Self {
        selection: null_mut(),
        feedback: null_mut(),
        select_size: 0,
        feedback_size: 0,
        mode: GL_RENDER,
        completed_mode: 0,
        completed_count: 0,
    };
}
pub(crate) fn query_function(f: u32) -> bool {
    matches!(
        f,
        FEnum_glSelectBuffer | FEnum_glFeedbackBuffer | FEnum_glRenderMode
    )
}
pub(crate) fn shape(f: u32, a: [u32; 3]) -> u32 {
    if f == FEnum_glRenderMode {
        if a[0] == 0
            && a[2] > 0
            && a[1] <= DG_GL_MAX_CAPTURE_VALUES
            && a[2] <= DG_GL_MAX_CAPTURE_VALUES - a[1]
        {
            a[2]
        } else if a[0] != 0 && a[1] == 0 && a[2] == 0 {
            3
        } else {
            0
        }
    } else if a[0] <= DG_GL_MAX_CAPTURE_VALUES
        && a[2] == 0
        && (f == FEnum_glFeedbackBuffer || a[1] == 0)
    {
        1
    } else {
        0
    }
}
fn allocation_bytes(size: u32) -> u64 {
    u64::from(size.max(1)) * 4
}
unsafe fn free(m: &Memory, p: *mut u32, size: u32) {
    if !p.is_null() {
        unsafe {
            *m.image_bytes -= allocation_bytes(size);
            (m.free)(m.opaque, p.cast());
        }
    }
}
/// Native context remains current throughout teardown; end selection/feedback
/// before freeing buffers. No GL operation is issued for an unused context.
pub(crate) unsafe fn release(m: &Memory, s: &mut State) {
    let old = *s;
    *s = State::EMPTY;
    if old.selection.is_null() && old.feedback.is_null() {
        return;
    }
    unsafe {
        let api = &*m.api;
        api.dg_glRenderMode.expect("retained capture API")(GL_RENDER);
        // Native client pointers must no longer refer to storage about to die.
        if !old.selection.is_null() {
            api.dg_glSelectBuffer.expect("retained capture API")(0, null_mut());
        }
        if !old.feedback.is_null() {
            api.dg_glFeedbackBuffer.expect("retained capture API")(0, GL_2D, null_mut());
        }
        free(m, old.selection, old.select_size);
        free(m, old.feedback, old.feedback_size);
    }
}
unsafe fn put(out: *mut u8, at: usize, v: u32) {
    unsafe {
        core::ptr::copy_nonoverlapping(v.to_le_bytes().as_ptr(), out.add(at * 4), 4);
    }
}
/// Raw state fields avoid Rust references spanning native callbacks. The caller
/// validates query shape/output capacity and holds render-worker ownership.
pub(crate) unsafe fn query(
    m: &Memory,
    s: *mut State,
    errors: *mut u32,
    f: u32,
    a: [u32; 3],
    out: *mut u8,
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    if f == FEnum_glRenderMode && a[0] == 0 {
        let old = unsafe { *s };
        if a[1] > old.completed_count || a[2] > old.completed_count - a[1] {
            return Err(DG_GL_ERROR_BATCH);
        }
        let p = if old.completed_mode == GL_SELECT {
            old.selection
        } else if old.completed_mode == GL_FEEDBACK {
            old.feedback
        } else {
            return Err(DG_GL_ERROR_CONTEXT);
        };
        if p.is_null() {
            return Err(DG_GL_ERROR_CONTEXT);
        }
        for i in 0..a[2] {
            unsafe {
                put(out, i as usize, *p.add((a[1] + i) as usize));
            }
        }
        return Ok(());
    }
    let get = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let render = api.dg_glRenderMode.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let select = api.dg_glSelectBuffer.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let feedback = api.dg_glFeedbackBuffer.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    unsafe {
        query::remember(api, errors)?;
    }
    if f == FEnum_glRenderMode {
        let old = unsafe { *s };
        // Reject invalid transitions before providers can flush/reset the old
        // mode. Mesa 26.1 feedback.c changes mode even after its missing-buffer
        // error, so forwarding that known-invalid transition loses real state.
        let invalid = if !matches!(a[0], GL_RENDER | GL_SELECT | GL_FEEDBACK) {
            GL_INVALID_ENUM
        } else if (a[0] == GL_SELECT && old.selection.is_null())
            || (a[0] == GL_FEEDBACK && old.feedback.is_null())
        {
            GL_INVALID_OPERATION
        } else {
            0
        };
        if invalid != 0 {
            unsafe {
                put(out, 0, invalid);
                put(out, 1, 0);
                put(out, 2, 0);
            }
            return Ok(());
        }
        let result = unsafe { render(a[0]) };
        let error = unsafe { get() };
        let mut count = 0;
        if error == 0 {
            let size = if old.mode == GL_SELECT {
                old.select_size
            } else if old.mode == GL_FEEDBACK {
                old.feedback_size
            } else {
                0
            };
            if result < 0 {
                count = size;
            } else if old.mode == GL_FEEDBACK {
                count = result as u32;
                if count > size {
                    return Err(DG_GL_ERROR_HOST);
                }
            } else if old.mode == GL_SELECT {
                // Each hit has three header words followed by its name stack.
                // Validate native output before walking/copying its prefix.
                for _ in 0..result {
                    if count > size || size - count < 3 || old.selection.is_null() {
                        return Err(DG_GL_ERROR_HOST);
                    }
                    let names = unsafe { *old.selection.add(count as usize) };
                    if names > size - count - 3 {
                        return Err(DG_GL_ERROR_HOST);
                    }
                    count += 3 + names;
                }
            }
            unsafe {
                (*s).mode = a[0];
                (*s).completed_mode = old.mode;
                (*s).completed_count = count;
            }
        }
        unsafe {
            put(out, 0, error);
            put(out, 1, result as u32);
            put(out, 2, count);
        }
        return Ok(());
    }
    let old = unsafe { *s };
    let wanted = if f == FEnum_glSelectBuffer {
        GL_SELECT
    } else {
        GL_FEEDBACK
    };
    let local_error = if old.mode == wanted {
        GL_INVALID_OPERATION
    } else if f == FEnum_glFeedbackBuffer
        && !matches!(
            a[1],
            GL_2D | GL_3D | GL_3D_COLOR | GL_3D_COLOR_TEXTURE | GL_4D_COLOR_TEXTURE
        )
    {
        GL_INVALID_ENUM
    } else {
        0
    };
    if local_error != 0 {
        unsafe {
            put(out, 0, local_error);
        }
        return Ok(());
    }
    // Empty client buffers have one private native word. Every GL1.1 feedback
    // primitive/token has >=2 words and every selection hit >=3, hence any
    // emitted record still produces native overflow. Logical size remains zero
    // and no private word is ever published. This avoids providers conflating
    // a configured empty buffer with an absent buffer.
    let bytes = allocation_bytes(a[0]);
    if unsafe { *m.image_bytes } > u64::from(crate::pixel_image::LIMIT) - bytes {
        unsafe {
            put(out, 0, GL_OUT_OF_MEMORY);
        }
        return Ok(());
    }
    let p = unsafe { (m.allocate)(m.opaque, bytes as usize) }.cast::<u32>();
    if p.is_null() {
        unsafe {
            put(out, 0, GL_OUT_OF_MEMORY);
        }
        return Ok(());
    }
    unsafe {
        core::ptr::write_bytes(p, 0, bytes as usize / 4);
        *m.image_bytes += bytes;
    }
    unsafe {
        if wanted == GL_SELECT {
            select(a[0].max(1) as i32, p);
        } else {
            feedback(a[0].max(1) as i32, a[1], p.cast());
        }
    }
    let error = unsafe { get() };
    if error == 0 {
        unsafe {
            (*s).completed_mode = 0;
            (*s).completed_count = 0;
            if wanted == GL_SELECT {
                (*s).selection = p;
                (*s).select_size = a[0];
                free(m, old.selection, old.select_size);
            } else {
                (*s).feedback = p;
                (*s).feedback_size = a[0];
                free(m, old.feedback, old.feedback_size);
            }
        }
    } else {
        unsafe {
            free(m, p, a[0]);
        }
    }
    unsafe {
        put(out, 0, error);
    }
    Ok(())
}
#[cfg(test)]
mod tests;
