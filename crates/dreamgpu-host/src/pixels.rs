// SPDX-License-Identifier: GPL-2.0-or-later
//! GL1.1 pixel transfer/maps. Native GL applies guest semantics; only internal
//! zero initialization temporarily neutralizes transfer state (never guest I/O).
#![allow(non_upper_case_globals)]
use crate::{gl_api::*, query};
pub(crate) const MAP_LIMIT: u32 = 256;
pub(crate) fn map_size_name(map: u32) -> Option<u32> {
    if (GL_PIXEL_MAP_I_TO_I..=GL_PIXEL_MAP_A_TO_A).contains(&map) {
        Some(map + 0x40)
    } else {
        None
    }
}
pub(crate) fn map_count(map: u32, count: u32) -> bool {
    map_size_name(map).is_some()
        && (1..=MAP_LIMIT).contains(&count)
        && (map > GL_PIXEL_MAP_I_TO_A || count.is_power_of_two())
}
pub(crate) fn map_input(function: u32) -> Option<usize> {
    match function {
        FEnum_glPixelMapfv | FEnum_glPixelMapuiv => Some(4),
        FEnum_glPixelMapusv => Some(2),
        _ => None,
    }
}
pub(crate) fn map_query(function: u32) -> bool {
    matches!(
        function,
        FEnum_glGetPixelMapfv | FEnum_glGetPixelMapuiv | FEnum_glGetPixelMapusv
    )
}
const INTEGER: [u32; 4] = [
    GL_MAP_COLOR,
    GL_MAP_STENCIL,
    GL_INDEX_SHIFT,
    GL_INDEX_OFFSET,
];
const FLOAT: [u32; 10] = [
    GL_RED_SCALE,
    GL_GREEN_SCALE,
    GL_BLUE_SCALE,
    GL_ALPHA_SCALE,
    GL_DEPTH_SCALE,
    GL_RED_BIAS,
    GL_GREEN_BIAS,
    GL_BLUE_BIAS,
    GL_ALPHA_BIAS,
    GL_DEPTH_BIAS,
];
pub(crate) fn transfer_name(name: u32) -> bool {
    INTEGER.contains(&name) || FLOAT.contains(&name)
}
pub(crate) fn state_name(name: u32) -> bool {
    transfer_name(name)
        || matches!(name, GL_ZOOM_X | GL_ZOOM_Y | GL_MAX_PIXEL_MAP_TABLE)
        || (GL_PIXEL_MAP_I_TO_I_SIZE..=GL_PIXEL_MAP_A_TO_A_SIZE).contains(&name)
}
pub(crate) unsafe fn limit(api: &DreamGpuGlApi) -> Result<u32, u32> {
    let mut value = 0;
    unsafe {
        api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(GL_MAX_PIXEL_MAP_TABLE, &mut value)
    };
    Ok((value.max(0) as u32).min(MAP_LIMIT))
}
pub(crate) unsafe fn set_map(
    api: &DreamGpuGlApi,
    errors: *mut u32,
    function: u32,
    map: u32,
    count: u32,
    data: &[u8],
) -> Result<(), u32> {
    let size = map_input(function).ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    if !map_count(map, count) || data.len() != count as usize * size {
        return Err(DG_GL_ERROR_BATCH);
    }
    if count > unsafe { limit(api)? } {
        return Err(DG_GL_ERROR_LIMIT);
    }
    unsafe { query::remember(api, errors)? };
    let mut words = [0u32; MAP_LIMIT as usize];
    let mut floats = [0f32; MAP_LIMIT as usize];
    let mut shorts = [0u16; MAP_LIMIT as usize];
    for (i, chunk) in data.chunks_exact(size).enumerate() {
        if size == 2 {
            shorts[i] = u16::from_le_bytes(chunk.try_into().unwrap())
        } else {
            words[i] = u32::from_le_bytes(chunk.try_into().unwrap());
            floats[i] = f32::from_bits(words[i]);
        }
    }
    unsafe {
        match function {
            FEnum_glPixelMapfv => api.dg_glPixelMapfv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(
                map,
                count as i32,
                floats.as_ptr(),
            ),
            FEnum_glPixelMapuiv => api.dg_glPixelMapuiv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(
                map,
                count as i32,
                words.as_ptr(),
            ),
            FEnum_glPixelMapusv => api.dg_glPixelMapusv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(
                map,
                count as i32,
                shorts.as_ptr(),
            ),
            _ => unreachable!(),
        }
    };
    let error = unsafe { api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?() };
    if error != 0 {
        unsafe { query::store(errors, error) };
        return Err(DG_GL_ERROR_HOST);
    }
    Ok(())
}
pub(crate) unsafe fn get_map(
    api: &DreamGpuGlApi,
    errors: *mut u32,
    function: u32,
    map: u32,
    count: u32,
    result: *mut u8,
) -> Result<(), u32> {
    let name = map_size_name(map).ok_or(DG_GL_ERROR_BATCH)?;
    if !map_count(map, count) || count > unsafe { limit(api)? } {
        return Err(DG_GL_ERROR_LIMIT);
    }
    let mut actual = 0;
    unsafe { api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(name, &mut actual) };
    // Query size is guest supplied, never an allocation authorization by itself.
    if actual != count as i32 {
        return Err(DG_GL_ERROR_LIMIT);
    }
    unsafe { query::remember(api, errors)? };
    let mut words = [0u32; MAP_LIMIT as usize];
    let mut floats = [0f32; MAP_LIMIT as usize];
    let mut shorts = [0u16; MAP_LIMIT as usize];
    unsafe {
        match function {
            FEnum_glGetPixelMapfv => {
                api.dg_glGetPixelMapfv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(map, floats.as_mut_ptr())
            }
            FEnum_glGetPixelMapuiv => {
                api.dg_glGetPixelMapuiv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(map, words.as_mut_ptr())
            }
            FEnum_glGetPixelMapusv => {
                api.dg_glGetPixelMapusv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(map, shorts.as_mut_ptr())
            }
            _ => return Err(DG_GL_ERROR_UNSUPPORTED),
        }
    };
    let error = unsafe { api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?() };
    if error != 0 {
        unsafe { query::store(errors, error) };
        return Err(DG_GL_ERROR_HOST);
    }
    for i in 0..count as usize {
        let value = match function {
            FEnum_glGetPixelMapfv => floats[i].to_bits(),
            FEnum_glGetPixelMapusv => u32::from(shorts[i]),
            _ => words[i],
        };
        unsafe {
            core::ptr::copy_nonoverlapping(value.to_le_bytes().as_ptr(), result.add(i * 4), 4)
        };
    }
    Ok(())
}
// Does not use PushAttrib: an internal operation cannot consume guest stack
// capacity or fail when the guest has legitimately filled its attribute stack.
pub(crate) struct Neutral<'a> {
    api: &'a DreamGpuGlApi,
    integer: [i32; 4],
    float: [f32; 10],
}
impl<'a> Neutral<'a> {
    pub(crate) unsafe fn new(api: &'a DreamGpuGlApi) -> Result<Self, u32> {
        let geti = api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let getf = api.dg_glGetFloatv.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let seti = api.dg_glPixelTransferi.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let setf = api.dg_glPixelTransferf.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let mut s = Self {
            api,
            integer: [0; 4],
            float: [0.; 10],
        };
        for (i, name) in INTEGER.into_iter().enumerate() {
            unsafe { geti(name, &mut s.integer[i]) }
        }
        for (i, name) in FLOAT.into_iter().enumerate() {
            unsafe { getf(name, &mut s.float[i]) }
        }
        for name in INTEGER {
            unsafe { seti(name, 0) }
        }
        for (i, name) in FLOAT.into_iter().enumerate() {
            unsafe { setf(name, if i < 5 { 1. } else { 0. }) }
        }
        Ok(s)
    }
}
impl Drop for Neutral<'_> {
    fn drop(&mut self) {
        for (name, value) in INTEGER.into_iter().zip(self.integer) {
            unsafe { self.api.dg_glPixelTransferi.unwrap()(name, value) }
        }
        for (name, value) in FLOAT.into_iter().zip(self.float) {
            unsafe { self.api.dg_glPixelTransferf.unwrap()(name, value) }
        }
    }
}
#[cfg(test)]
mod tests;
