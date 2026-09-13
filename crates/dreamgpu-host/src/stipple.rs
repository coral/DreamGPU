// SPDX-License-Identifier: GPL-2.0-or-later
//! The stipple wire is exactly 32 canonical MSB-first rows, independent of guest stores.
use crate::{gl_api::*, query};
pub(crate) unsafe fn set(
    api: &DreamGpuGlApi,
    errors: *mut u32,
    data: *const u8,
    bytes: u32,
) -> Result<(), u32> {
    if bytes != 128 || data.is_null() {
        return Err(DG_GL_ERROR_BATCH);
    }
    let set = api.dg_glPolygonStipple.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    unsafe {
        query::remember(api, errors)?;
    }
    let unpack = unsafe { crate::pixel_image::Unpack::new(api)? };
    unsafe {
        set(data);
    }
    let e = unsafe { error() };
    drop(unpack);
    unsafe {
        query::store(errors, e);
    }
    if e == 0 {
        Ok(())
    } else {
        Err(DG_GL_ERROR_HOST)
    }
}
pub(crate) unsafe fn get(api: &DreamGpuGlApi, errors: *mut u32, out: *mut u8) -> Result<(), u32> {
    let get = api.dg_glGetPolygonStipple.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let state = api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let store = api.dg_glPixelStorei.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    unsafe {
        query::remember(api, errors)?;
    }
    let pack = unsafe { query::Pack::new(api)? };
    let mut lsb = 0;
    let mut pattern = [0; 128];
    unsafe {
        state(GL_PACK_LSB_FIRST, &mut lsb);
        store(GL_PACK_LSB_FIRST, 0);
        get(pattern.as_mut_ptr());
    }
    let e = unsafe { error() };
    unsafe {
        store(GL_PACK_LSB_FIRST, lsb);
    }
    drop(pack);
    unsafe {
        query::store(errors, e);
    }
    if e != 0 {
        return Err(DG_GL_ERROR_HOST);
    }
    unsafe {
        core::ptr::copy_nonoverlapping(pattern.as_ptr(), out, 128);
    }
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    struct State {
        store: [i32; 12],
        pattern: [u8; 128],
        error: u32,
        fail: bool,
    }
    std::thread_local! {static S:RefCell<State>=const{RefCell::new(State{store:[0;12],pattern:[0;128],error:0,fail:false})};}
    const NAMES: [u32; 12] = [
        GL_PACK_ALIGNMENT,
        GL_PACK_ROW_LENGTH,
        GL_PACK_SKIP_ROWS,
        GL_PACK_SKIP_PIXELS,
        GL_PACK_SWAP_BYTES,
        GL_PACK_LSB_FIRST,
        GL_UNPACK_ALIGNMENT,
        GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_PIXELS,
        GL_UNPACK_SWAP_BYTES,
        GL_UNPACK_LSB_FIRST,
    ];
    unsafe extern "C" fn integer(name: u32, out: *mut i32) {
        S.with(|s| unsafe {
            *out = s.borrow().store[NAMES.iter().position(|&n| n == name).unwrap()]
        });
    }
    unsafe extern "C" fn store(name: u32, v: i32) {
        S.with(|s| s.borrow_mut().store[NAMES.iter().position(|&n| n == name).unwrap()] = v);
    }
    unsafe extern "C" fn error() -> u32 {
        S.with(|s| core::mem::take(&mut s.borrow_mut().error))
    }
    unsafe extern "C" fn set(p: *const u8) {
        S.with(|s| {
            let mut s = s.borrow_mut();
            assert_eq!(
                &s.store[6..],
                &[1, 0, 0, 0, i32::from(cfg!(target_endian = "big")), 0]
            );
            unsafe {
                core::ptr::copy_nonoverlapping(p, s.pattern.as_mut_ptr(), 128);
            }
        });
    }
    unsafe extern "C" fn get(p: *mut u8) {
        S.with(|s| {
            let mut s = s.borrow_mut();
            assert_eq!(&s.store[..6], &[1, 0, 0, 0, 0, 0]);
            unsafe {
                core::ptr::copy_nonoverlapping(s.pattern.as_ptr(), p, 128);
            }
            if s.fail {
                s.error = GL_INVALID_OPERATION;
            }
        });
    }
    #[test]
    fn exact_stipple_store_restoration_and_atomic_failed_output() {
        let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
        api.dg_glGetIntegerv = Some(integer);
        api.dg_glPixelStorei = Some(store);
        api.dg_glGetError = Some(error);
        api.dg_glPolygonStipple = Some(set);
        api.dg_glGetPolygonStipple = Some(get);
        let original = [8, 57, 7, 3, 1, 1, 8, 39, 2, 5, 1, 1];
        S.with(|s| s.borrow_mut().store = original);
        let mut errors = 0;
        let pattern = core::array::from_fn::<_, 128, _>(|i| (i * 39) as u8);
        unsafe {
            super::set(&api, &mut errors, pattern.as_ptr(), 128).unwrap();
        }
        S.with(|s| assert_eq!(s.borrow().store, original));
        let mut output = [0xa5; 128];
        unsafe {
            super::get(&api, &mut errors, output.as_mut_ptr()).unwrap();
        }
        assert_eq!(output, pattern);
        S.with(|s| assert_eq!(s.borrow().store, original));
        S.with(|s| s.borrow_mut().fail = true);
        output.fill(0xa5);
        assert!(unsafe { super::get(&api, &mut errors, output.as_mut_ptr()) }.is_err());
        assert_eq!(output, [0xa5; 128]);
        assert_ne!(errors, 0);
        S.with(|s| assert_eq!(s.borrow().store, original));
        api.dg_glPixelStorei = None;
        assert!(unsafe { super::get(&api, &mut errors, output.as_mut_ptr()) }.is_err());
        S.with(|s| assert_eq!(s.borrow().store, original));
    }
}
