// SPDX-License-Identifier: GPL-2.0-or-later
//! GPU-only export transaction: temporary attachments, GL state rollback and
//! completion polling. OS adapters only bind their image and export their fence.
use crate::{
    gl_api::*,
    resource::{dreamgpu_drawable_exchange, Drawable},
    state::ContextState,
};
use core::ffi::c_void;
macro_rules! gl {($api:expr,$name:ident($($arg:expr),*$(,)?))=>{{#[allow(unused_unsafe)] let value=unsafe{$api.$name.expect("complete native GL API")($($arg),*)};value}};}
pub type OsStep = unsafe extern "C" fn(*mut c_void) -> u32;
pub type Clock = unsafe extern "C" fn(*mut c_void) -> i64;
pub type Sleep = unsafe extern "C" fn(*mut c_void, u64);
struct Restore<'a> {
    api: &'a DreamGpuGlApi,
    read: i32,
    draw: i32,
    read_buffer: i32,
    binding: i32,
    scissor: u8,
    rectangle: bool,
    attachment: u32,
    framebuffer: u32,
}
impl Drop for Restore<'_> {
    fn drop(&mut self) {
        let a = self.api;
        gl!(
            a,
            dg_glBindFramebuffer(GL_READ_FRAMEBUFFER, self.read as u32)
        );
        gl!(a, dg_glReadBuffer(self.read_buffer as u32));
        gl!(
            a,
            dg_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, self.draw as u32)
        );
        if self.scissor != 0 {
            gl!(a, dg_glEnable(GL_SCISSOR_TEST));
        }
        if self.rectangle {
            gl!(
                a,
                dg_glBindTexture(GL_TEXTURE_RECTANGLE_ARB, self.binding as u32)
            );
        } else {
            gl!(
                a,
                dg_glBindRenderbuffer(GL_RENDERBUFFER, self.binding as u32)
            );
        }
        if self.framebuffer != 0 {
            gl!(a, dg_glDeleteFramebuffers(1, &self.framebuffer));
        }
        if self.attachment != 0 {
            if self.rectangle {
                gl!(a, dg_glDeleteTextures(1, &self.attachment));
            } else {
                gl!(a, dg_glDeleteRenderbuffers(1, &self.attachment));
            }
        }
    }
}
/// # Safety
/// Complete native API/current render context. State and drawable belong to the
/// render worker; callbacks synchronously touch only OS image/fence/context refs.
/// Rust holds no reference into the callback-owned C context or image. Rectangle
/// selects the IOSurface attachment ABI; otherwise use EGL renderbuffer storage.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_export(
    api: *const DreamGpuGlApi,
    d: *mut Drawable,
    state: *const ContextState,
    framebuffer: u32,
    exchange: u32,
    rectangle: u32,
    bind: Option<OsStep>,
    fence: Option<OsStep>,
    opaque: *mut c_void,
) -> u32 {
    if api.is_null() || d.is_null() || state.is_null() || bind.is_none() || fence.is_none() {
        return DG_GL_ERROR_CONTEXT;
    }
    let a = unsafe { &*api };
    let (width, height) = unsafe { ((*d).width, (*d).height) };
    let mut saved = Restore {
        api: a,
        read: 0,
        draw: 0,
        read_buffer: 0,
        binding: 0,
        scissor: gl!(a, dg_glIsEnabled(GL_SCISSOR_TEST)),
        rectangle: rectangle != 0,
        attachment: 0,
        framebuffer: 0,
    };
    gl!(
        a,
        dg_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &mut saved.read)
    );
    gl!(
        a,
        dg_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &mut saved.draw)
    );
    gl!(a, dg_glGetIntegerv(GL_READ_BUFFER, &mut saved.read_buffer));
    if exchange != 0 {
        unsafe { dreamgpu_drawable_exchange(api, d, framebuffer, state) }
    }
    if saved.rectangle {
        gl!(
            a,
            dg_glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB, &mut saved.binding)
        );
        gl!(a, dg_glGenTextures(1, &mut saved.attachment));
        gl!(
            a,
            dg_glBindTexture(GL_TEXTURE_RECTANGLE_ARB, saved.attachment)
        );
    } else {
        // A temporary guest texture rebind would lose a deleted-but-bound object.
        // EGL imports therefore use a renderbuffer, never guest texture state.
        gl!(
            a,
            dg_glGetIntegerv(GL_RENDERBUFFER_BINDING, &mut saved.binding)
        );
        gl!(a, dg_glGenRenderbuffers(1, &mut saved.attachment));
        gl!(a, dg_glBindRenderbuffer(GL_RENDERBUFFER, saved.attachment));
    }
    if saved.attachment == 0 {
        return DG_GL_ERROR_HOST;
    }
    let error = unsafe { bind.unwrap()(opaque) };
    if error != 0 {
        return error;
    }
    gl!(a, dg_glGenFramebuffers(1, &mut saved.framebuffer));
    if saved.framebuffer == 0 {
        return DG_GL_ERROR_HOST;
    }
    gl!(
        a,
        dg_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved.framebuffer)
    );
    if saved.rectangle {
        gl!(
            a,
            dg_glFramebufferTexture2D(
                GL_DRAW_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_RECTANGLE_ARB,
                saved.attachment,
                0
            )
        );
    } else {
        gl!(
            a,
            dg_glFramebufferRenderbuffer(
                GL_DRAW_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_RENDERBUFFER,
                saved.attachment
            )
        );
    }
    if gl!(a, dg_glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE {
        return DG_GL_ERROR_HOST;
    }
    gl!(a, dg_glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer));
    gl!(a, dg_glReadBuffer(GL_COLOR_ATTACHMENT1));
    gl!(a, dg_glDisable(GL_SCISSOR_TEST));
    // Guest GL bottom-left becomes exported image top-left with one GPU blit.
    gl!(
        a,
        dg_glBlitFramebuffer(
            0,
            height as i32,
            width as i32,
            0,
            0,
            0,
            width as i32,
            height as i32,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST
        )
    );
    unsafe { fence.unwrap()(opaque) }
}
/// # Safety
/// Current completion context owns sync; callbacks provide monotonic microseconds
/// and a sleeping wait (not a busy loop). Fence is consumed on every outcome.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_export_wait(
    api: *const DreamGpuGlApi,
    sync: *mut __GLsync,
    clock: Clock,
    sleep: Sleep,
    opaque: *mut c_void,
) -> u32 {
    let a = unsafe { &*api };
    let deadline = unsafe { clock(opaque) }.saturating_add(5_000_000);
    let result = loop {
        // Positive Apple GL timeouts spin internally. Immediate query plus sleep
        // leaves render/input threads independent and completed images immediate.
        let status = gl!(a, dg_glClientWaitSync(sync, 0, 0));
        if status != GL_TIMEOUT_EXPIRED || unsafe { clock(opaque) } >= deadline {
            break status;
        }
        unsafe { sleep(opaque, 100) };
    };
    gl!(a, dg_glDeleteSync(sync));
    result
}
#[cfg(test)]
mod tests;
