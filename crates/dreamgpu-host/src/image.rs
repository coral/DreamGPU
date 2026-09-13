// SPDX-License-Identifier: GPL-2.0-or-later
//! Native export image allocation ownership. OS-specific creation/destruction are
//! synchronous adapters; Rust initializes partial handles and owns rollback/free.
use crate::{gl_api::*, texture::names::Memory};
use core::{ffi::c_void, ptr::null_mut};
#[repr(C)]
pub struct NativeImage {
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub offset: u32,
    pub modifier: u64,
    pub surface: *mut c_void,
    pub native_image: *mut c_void,
    pub bo: *mut c_void,
    pub context: *mut c_void,
    pub fence: *mut c_void,
    pub fd: i32,
    pub fence_fd: i32,
}
impl NativeImage {
    const EMPTY: Self = Self {
        width: 0,
        height: 0,
        stride: 0,
        offset: 0,
        modifier: 0,
        surface: null_mut(),
        native_image: null_mut(),
        bo: null_mut(),
        context: null_mut(),
        fence: null_mut(),
        fd: -1,
        fence_fd: -1,
    };
}
pub type Create = unsafe extern "C" fn(*mut c_void, *mut NativeImage) -> u32;
pub type Destroy = unsafe extern "C" fn(*mut c_void, *mut NativeImage);
/// # Safety
/// Memory callbacks allocate aligned raw storage; create fills only this fresh
/// image and returns no success until all native resources exist. Destroy accepts
/// every partially initialized combination and cannot retain the passed pointer.
/// Neither callback reenters image ownership. No Rust borrow crosses callbacks.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_image_new(
    memory: *const Memory,
    width: u32,
    height: u32,
    create: Create,
    destroy: Destroy,
    opaque: *mut c_void,
    error: *mut u32,
) -> *mut NativeImage {
    unsafe { *error = DG_GL_ERROR_LIMIT };
    if width == 0 || height == 0 || width > DG_GL_MAX_DIMENSION || height > DG_GL_MAX_DIMENSION {
        return null_mut();
    }
    let m = unsafe { &*memory };
    let image = unsafe { (m.allocate)(m.opaque, core::mem::size_of::<NativeImage>()) }
        .cast::<NativeImage>();
    if image.is_null() {
        unsafe { *error = DG_GL_ERROR_HOST };
        return null_mut();
    }
    unsafe {
        image.write(NativeImage {
            width,
            height,
            ..NativeImage::EMPTY
        })
    };
    let result = unsafe { create(opaque, image) };
    if result != 0 {
        unsafe {
            dreamgpu_image_free(memory, image, destroy, opaque);
            *error = result
        };
        return null_mut();
    }
    unsafe { *error = 0 };
    image
}
/// # Safety
/// No render/completion/publication reader can still access image fields. Exported
/// OS handles retain backing storage independently. Native destroy synchronously
/// releases context/OS references from an owned snapshot; it cannot retain it.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_image_free(
    memory: *const Memory,
    image: *mut NativeImage,
    destroy: Destroy,
    opaque: *mut c_void,
) {
    if image.is_null() {
        return;
    }
    let m = unsafe { &*memory };
    let mut old = unsafe { image.read() };
    unsafe {
        image.write(NativeImage::EMPTY);
        destroy(opaque, &mut old);
        (m.free)(m.opaque, image.cast())
    };
}
#[cfg(test)]
mod tests;
