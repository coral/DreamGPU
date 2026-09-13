// SPDX-License-Identifier: GPL-2.0-or-later
//! Native texture storage ownership, cross-context ordering and uploads.
use crate::gl_api::*;
use core::ffi::c_void;
pub(crate) mod control;
pub(crate) mod names;

#[repr(C)]
pub struct Texture {
    pub name: u32,
    pub target: u32,
    pub version: u64,
    pub guest_name: u32,
    pub refs: u32,
    pub deleted: u32,
    pub undefined_levels: u32,
    pub last_write: *mut c_void,
    pub writer_serial: u64,
    pub waiter_serial: u64,
    pub widths: [u32; 12],
    pub heights: [u32; 12],
    pub levels: [u64; 12],
}
impl Default for Texture {
    fn default() -> Self {
        Self {
            name: 0,
            target: GL_TEXTURE_2D,
            version: 0,
            guest_name: 0,
            refs: 0,
            deleted: 0,
            undefined_levels: 0,
            last_write: core::ptr::null_mut(),
            writer_serial: 0,
            waiter_serial: 0,
            widths: [0; 12],
            heights: [0; 12],
            levels: [0; 12],
        }
    }
}

unsafe fn wait(api: &DreamGpuGlApi, t: *mut Texture, serial: u64) {
    unsafe {
        if !(*t).last_write.is_null()
            && (*t).writer_serial != serial
            && (*t).waiter_serial != serial
        {
            api.dg_glWaitSync.unwrap()((*t).last_write.cast(), 0, u64::MAX);
            (*t).waiter_serial = serial;
        }
    }
}
unsafe fn written(api: &DreamGpuGlApi, t: *mut Texture, serial: u64) {
    unsafe {
        (*t).version = (*t).version.wrapping_add(1);
        (*t).writer_serial = serial;
        (*t).waiter_serial = 0;
        if !(*t).last_write.is_null() {
            api.dg_glDeleteSync.unwrap()((*t).last_write.cast());
        }
        (*t).last_write = api.dg_glFenceSync.unwrap()(GL_SYNC_GPU_COMMANDS_COMPLETE, 0).cast();
    }
}

/// # Safety
/// Render-owned texture and initialized GL API/current native context; callbacks do
/// not reenter resource handling. GL sync pointers remain opaque platform objects.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_wait(
    api: *const DreamGpuGlApi,
    texture: *mut Texture,
    serial: u64,
) {
    unsafe {
        wait(&*api, texture, serial);
    }
}
/// # Safety
/// Same ownership as `dreamgpu_texture_wait`.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_written(
    api: *const DreamGpuGlApi,
    texture: *mut Texture,
    serial: u64,
) {
    unsafe {
        written(&*api, texture, serial);
    }
}

pub type Zero = unsafe extern "C" fn(*mut c_void, *mut Texture, u32, u32, u32) -> u32;

#[expect(
    clippy::too_many_arguments,
    reason = "Mirrors native texture upload arguments without borrowing state across callbacks."
)]
unsafe fn upload(
    api: &DreamGpuGlApi,
    texture: *mut Texture,
    total: *mut u64,
    serial: u64,
    function: u32,
    a: &[u32; 8],
    data: *const u8,
    bytes: u32,
    zero: Zero,
    opaque: *mut c_void,
    gl_error: *mut u32,
) -> Result<(), u32> {
    let image = function == FEnum_glTexImage2D || function == FEnum_glTexImage1D;
    let one = function == FEnum_glTexImage1D || function == FEnum_glTexSubImage1D;
    if !image && function != FEnum_glTexSubImage1D && function != FEnum_glTexSubImage2D {
        return Err(3);
    }
    let level = a[1] as usize;
    let w = a[if image { 3 } else { 4 }];
    let h = a[if image { 4 } else { 5 }];
    if level > DG_GL_MAX_TEXTURE_LEVEL as usize
        || w == 0
        || h == 0
        || w > DG_GL_MAX_TEXTURE_DIMENSION
        || h > DG_GL_MAX_TEXTURE_DIMENSION
    {
        return Err(11);
    }
    let allocation = u64::from(w) * u64::from(h) * 4;
    let old = unsafe { (*texture).levels[level] };
    let new_total = if image {
        let n = unsafe { *total }
            .checked_sub(old)
            .and_then(|v| v.checked_add(allocation))
            .ok_or(9u32)?;
        if n > u64::from(DG_GL_MAX_TEXTURE_BYTES) {
            return Err(9);
        }
        n
    } else {
        let (width, height) = unsafe { ((*texture).widths[level], (*texture).heights[level]) };
        if a[2] > width || a[3] > height || w > width - a[2] || h > height - a[3] {
            return Err(11);
        }
        unsafe { *total }
    };
    if api.dg_glGetIntegerv.is_none()
        || api.dg_glPixelStorei.is_none()
        || api.dg_glGetError.is_none()
        || api.dg_glTexImage1D.is_none()
        || api.dg_glTexImage2D.is_none()
        || api.dg_glTexSubImage1D.is_none()
        || api.dg_glTexSubImage2D.is_none()
        || api.dg_glWaitSync.is_none()
        || api.dg_glDeleteSync.is_none()
        || api.dg_glFenceSync.is_none()
    {
        return Err(3);
    }
    unsafe {
        wait(api, texture, serial);
    }
    let stores = [
        GL_UNPACK_ALIGNMENT,
        GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_PIXELS,
        GL_UNPACK_SWAP_BYTES,
    ];
    let mut saved = [0i32; 5];
    for (i, key) in stores.iter().enumerate() {
        unsafe {
            api.dg_glGetIntegerv.unwrap()(*key, &mut saved[i]);
        }
    }
    for (i, key) in stores.iter().enumerate() {
        unsafe {
            api.dg_glPixelStorei.unwrap()(*key, if i == 0 { 1 } else { 0 });
        }
    }
    let pixels = if bytes == 0 {
        core::ptr::null()
    } else {
        data.cast()
    };
    unsafe {
        if one && image {
            api.dg_glTexImage1D.unwrap()(
                a[0],
                level as i32,
                a[2] as i32,
                w as i32,
                0,
                a[6],
                a[7],
                pixels,
            );
        } else if one {
            api.dg_glTexSubImage1D.unwrap()(
                a[0],
                level as i32,
                a[2] as i32,
                w as i32,
                a[6],
                a[7],
                pixels,
            );
        } else if image {
            api.dg_glTexImage2D.unwrap()(
                a[0],
                level as i32,
                a[2] as i32,
                w as i32,
                h as i32,
                0,
                a[6],
                a[7],
                pixels,
            );
        } else {
            api.dg_glTexSubImage2D.unwrap()(
                a[0],
                level as i32,
                a[2] as i32,
                a[3] as i32,
                w as i32,
                h as i32,
                a[6],
                a[7],
                pixels,
            );
        }
    }
    let mut error = unsafe { api.dg_glGetError.unwrap()() };
    let allocated = error == 0 && image;
    if allocated && bytes == 0 {
        error = unsafe { zero(opaque, texture, level as u32, w, h) };
    }
    for (i, key) in stores.iter().enumerate() {
        unsafe {
            api.dg_glPixelStorei.unwrap()(*key, saved[i]);
        }
    }
    if allocated {
        unsafe {
            *total = new_total;
            (*texture).levels[level] = allocation;
            (*texture).widths[level] = w;
            (*texture).heights[level] = h;
            if error != 0 {
                (*texture).undefined_levels |= 1 << level;
            } else {
                (*texture).undefined_levels &= !(1 << level);
            }
        }
    }
    if allocated || error == 0 {
        unsafe {
            written(api, texture, serial);
        }
    }
    if error != 0 {
        unsafe {
            *gl_error = error;
        }
        return Err(11);
    }
    if !image
        && a[2] == 0
        && a[3] == 0
        && w == unsafe { (*texture).widths[level] }
        && h == unsafe { (*texture).heights[level] }
    {
        unsafe {
            (*texture).undefined_levels &= !(1 << level);
        }
    }
    Ok(())
}

/// # Safety
/// Texture/accounting belong to the render worker. Args/data have passed canonical
/// data validation and remain immutable throughout GL consumption. The zero hook
/// may inspect the texture, with no Rust reference retained across that callback.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_upload(
    api: *const DreamGpuGlApi,
    texture: *mut Texture,
    total: *mut u64,
    serial: u64,
    function: u32,
    args: *const u32,
    data: *const u8,
    bytes: u32,
    zero: Option<Zero>,
    opaque: *mut c_void,
    gl_error: *mut u32,
) -> u32 {
    if api.is_null()
        || texture.is_null()
        || total.is_null()
        || args.is_null()
        || (data.is_null() && bytes != 0)
        || zero.is_none()
        || gl_error.is_null()
    {
        return 1;
    }
    unsafe {
        *gl_error = 0;
    }
    match unsafe {
        upload(
            &*api,
            texture,
            total,
            serial,
            function,
            &*args.cast::<[u32; 8]>(),
            data,
            bytes,
            zero.unwrap(),
            opaque,
            gl_error,
        )
    } {
        Ok(()) => 0,
        Err(error) => error,
    }
}

#[cfg(test)]
mod tests;
