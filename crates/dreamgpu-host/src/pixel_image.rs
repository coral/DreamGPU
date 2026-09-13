// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded immutable image assembly. No GL call, raster movement or image
//! allocation is repeated after commit; native GL performs one whole-image draw.
#![allow(non_upper_case_globals)]
use crate::{gl_api::*, query, texture::names::Memory};
use core::ptr::null_mut;
pub(crate) const LIMIT: u32 = 64 * 1024 * 1024;
pub(crate) const FIRST: u32 = 1;
pub(crate) const LAST: u32 = 2;
pub(crate) const ABORT: u32 = 4;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct State {
    pub pixels: *mut u8,
    pub active: u32,
    pub function: u32,
    pub descriptor: [u32; 4],
    pub bitmap: [u32; 4],
    pub total: u32,
    pub received: u32,
    pub id: u32,
    pub last_id: u32,
}
impl State {
    pub const EMPTY: Self = Self {
        pixels: null_mut(),
        active: 0,
        function: 0,
        descriptor: [0; 4],
        bitmap: [0; 4],
        total: 0,
        received: 0,
        id: 0,
        last_id: 0,
    };
}
pub(crate) fn image_function(f: u32) -> bool {
    matches!(f, FEnum_glBitmap | FEnum_glDrawPixels)
}
pub(crate) fn byte_count(function: u32, w: u32, h: u32, format: u32, kind: u32) -> Option<u32> {
    if !image_function(function) || w > i32::MAX as u32 || h > i32::MAX as u32 {
        return None;
    }
    if function == FEnum_glBitmap && (format != GL_COLOR_INDEX || kind != GL_BITMAP) {
        return None;
    }
    let components = match format {
        GL_COLOR_INDEX | GL_STENCIL_INDEX | GL_DEPTH_COMPONENT | GL_RED | GL_GREEN | GL_BLUE
        | GL_ALPHA | GL_LUMINANCE => 1u64,
        GL_LUMINANCE_ALPHA => 2,
        GL_RGB => 3,
        GL_RGBA => 4,
        _ => return None,
    };
    let row = if kind == GL_BITMAP {
        if !matches!(format, GL_COLOR_INDEX | GL_STENCIL_INDEX) {
            return None;
        }
        u64::from(w).div_ceil(8)
    } else {
        let size = match kind {
            GL_BYTE | GL_UNSIGNED_BYTE => 1,
            GL_SHORT | GL_UNSIGNED_SHORT => 2,
            GL_INT | GL_UNSIGNED_INT | GL_FLOAT => 4,
            _ => return None,
        };
        u64::from(w) * components * size
    };
    let bytes = row.checked_mul(u64::from(h))?;
    if bytes > u64::from(LIMIT) {
        None
    } else {
        Some(bytes as u32)
    }
}
// Some native compatibility drivers cannot ingest DrawPixels bitmap indices.
// Expand the canonical packed 0/1 indices in-place at commit; this is a format
// conversion, not rendering. Charge actual allocation size at FIRST.
fn storage_bytes(function: u32, descriptor: [u32; 4], total: u32) -> Option<u32> {
    let bytes = if function == FEnum_glDrawPixels && descriptor[3] == GL_BITMAP {
        u64::from(descriptor[0]) * u64::from(descriptor[1])
    } else {
        u64::from(total)
    };
    (bytes <= u64::from(LIMIT)).then_some(bytes as u32)
}
pub(crate) fn validate(function: u32, a: &[u32; 8], data: &[u8]) -> u32 {
    let flags = a[6];
    if a[7] == 0
        || !matches!(flags, 0 | FIRST | LAST | 3 | ABORT)
        || byte_count(function, a[0], a[1], a[2], a[3]) != Some(a[4])
        || storage_bytes(function, a[..4].try_into().unwrap(), a[4]).is_none()
        || a[5] > a[4]
    {
        return DG_GL_ERROR_BATCH;
    }
    if flags == ABORT {
        return if data.is_empty() {
            0
        } else {
            DG_GL_ERROR_BATCH
        };
    }
    let prefix = usize::from(function == FEnum_glBitmap && flags & FIRST != 0) * 16;
    if data.len() < prefix {
        return DG_GL_ERROR_BATCH;
    }
    let n = data.len() - prefix;
    if n > a[4] as usize - a[5] as usize
        || (flags & FIRST != 0 && a[5] != 0)
        || (flags & LAST != 0 && a[5] as usize + n != a[4] as usize)
        || (n == 0 && !(a[4] == 0 && flags == (FIRST | LAST)))
    {
        return DG_GL_ERROR_BATCH;
    }
    0
}
/// Clear ownership before allocator callbacks; last_id is never rolled back.
pub(crate) unsafe fn release(m: &Memory, s: *mut State) {
    let old = unsafe { s.read() };
    unsafe {
        s.write(State {
            last_id: old.last_id,
            ..State::EMPTY
        })
    };
    if !old.pixels.is_null() {
        unsafe {
            *m.image_bytes = (*m.image_bytes)
                .checked_sub(u64::from(
                    storage_bytes(old.function, old.descriptor, old.total).unwrap(),
                ))
                .expect("image budget underflow");
            (m.free)(m.opaque, old.pixels.cast())
        };
    }
}
pub(crate) unsafe fn interleave(m: &Memory, s: *mut State) -> u32 {
    if unsafe { (*s).active } == 0 {
        return 0;
    }
    unsafe { release(m, s) };
    DG_GL_ERROR_CONTEXT
}
struct Unpack<'a> {
    api: &'a DreamGpuGlApi,
    values: [i32; 6],
}
const UNPACK: [u32; 6] = [
    GL_UNPACK_ALIGNMENT,
    GL_UNPACK_ROW_LENGTH,
    GL_UNPACK_SKIP_ROWS,
    GL_UNPACK_SKIP_PIXELS,
    GL_UNPACK_SWAP_BYTES,
    GL_UNPACK_LSB_FIRST,
];
impl<'a> Unpack<'a> {
    unsafe fn new(api: &'a DreamGpuGlApi) -> Result<Self, u32> {
        let get = api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let set = api.dg_glPixelStorei.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let mut s = Self {
            api,
            values: [0; 6],
        };
        for (i, name) in UNPACK.into_iter().enumerate() {
            unsafe { get(name, &mut s.values[i]) }
        }
        for (i, name) in UNPACK.into_iter().enumerate() {
            unsafe {
                set(
                    name,
                    if i == 0 || (i == 4 && cfg!(target_endian = "big")) {
                        1
                    } else {
                        0
                    },
                )
            }
        }
        Ok(s)
    }
}
impl Drop for Unpack<'_> {
    fn drop(&mut self) {
        for (name, value) in UNPACK.into_iter().zip(self.values) {
            unsafe { self.api.dg_glPixelStorei.unwrap()(name, value) }
        }
    }
}
unsafe fn draw(m: &Memory, s: &State, errors: *mut u32) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    // Resolve all needed APIs before touching native state. Void GL functions can
    // report native errors, but malformed/incomplete streams never reach them.
    if (s.function == FEnum_glBitmap && api.dg_glBitmap.is_none())
        || (s.function == FEnum_glDrawPixels && api.dg_glDrawPixels.is_none())
    {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    unsafe { query::remember(api, errors)? };
    let _unpack = unsafe { Unpack::new(api)? };
    unsafe {
        if s.function == FEnum_glBitmap {
            api.dg_glBitmap.unwrap()(
                s.descriptor[0] as i32,
                s.descriptor[1] as i32,
                f32::from_bits(s.bitmap[0]),
                f32::from_bits(s.bitmap[1]),
                f32::from_bits(s.bitmap[2]),
                f32::from_bits(s.bitmap[3]),
                s.pixels,
            );
        } else {
            api.dg_glDrawPixels.unwrap()(
                s.descriptor[0] as i32,
                s.descriptor[1] as i32,
                s.descriptor[2],
                s.descriptor[3],
                s.pixels.cast(),
            );
        }
    };
    let error = unsafe { get_error() };
    if error != 0 {
        unsafe { query::store(errors, error) };
        return Err(DG_GL_ERROR_HOST);
    };
    Ok(())
}
pub(crate) unsafe fn stream(
    m: &Memory,
    s: *mut State,
    errors: *mut u32,
    function: u32,
    a: &[u32; 8],
    data: &[u8],
) -> u32 {
    if m.image_bytes.is_null() || m.api.is_null() || s.is_null() || errors.is_null() {
        return DG_GL_ERROR_CONTEXT;
    }
    let valid = validate(function, a, data);
    if valid != 0 {
        if unsafe { (*s).active != 0 && (*s).id == a[7] } && a[6] & FIRST == 0 {
            unsafe { release(m, s) }
        };
        return valid;
    }
    let flags = a[6];
    let first = flags & FIRST != 0;
    if first {
        // A failed new admission never replaces an existing owner or consumes credit.
        if unsafe { (*s).active != 0 || a[7] <= (*s).last_id } {
            return DG_GL_ERROR_CONTEXT;
        }
        let storage = storage_bytes(function, a[..4].try_into().unwrap(), a[4]).unwrap();
        let Some(total) = (unsafe { *m.image_bytes }).checked_add(u64::from(storage)) else {
            return DG_GL_ERROR_LIMIT;
        };
        if total > u64::from(LIMIT) {
            return DG_GL_ERROR_LIMIT;
        }
        let p = if a[4] == 0 {
            null_mut()
        } else {
            unsafe { (m.allocate)(m.opaque, storage as usize) }.cast::<u8>()
        };
        if a[4] != 0 && p.is_null() {
            return DG_GL_ERROR_LIMIT;
        }
        let bitmap = if function == FEnum_glBitmap {
            core::array::from_fn(|i| u32::from_le_bytes(data[i * 4..i * 4 + 4].try_into().unwrap()))
        } else {
            [0; 4]
        };
        unsafe {
            *m.image_bytes = total;
            s.write(State {
                pixels: p,
                active: 1,
                function,
                descriptor: a[..4].try_into().unwrap(),
                bitmap,
                total: a[4],
                received: 0,
                id: a[7],
                last_id: a[7],
            })
        };
    } else {
        if unsafe { (*s).active == 0 || (*s).id != a[7] } {
            return DG_GL_ERROR_CONTEXT;
        }
        if unsafe {
            (*s).function != function
                || (*s).descriptor != a[..4]
                || (*s).total != a[4]
                || (*s).received != a[5]
        } {
            unsafe { release(m, s) };
            return DG_GL_ERROR_BATCH;
        }
    }
    if flags == ABORT {
        unsafe { release(m, s) };
        return 0;
    }
    let prefix = usize::from(first && function == FEnum_glBitmap) * 16;
    let bytes = &data[prefix..];
    if !bytes.is_empty() {
        unsafe {
            core::ptr::copy_nonoverlapping(
                bytes.as_ptr(),
                (*s).pixels.add((*s).received as usize),
                bytes.len(),
            );
            (*s).received += bytes.len() as u32
        }
    };
    if flags & LAST == 0 {
        return 0;
    }
    // Consume stream identity before native draw, even on GL failure/lost reply.
    let old = unsafe { s.read() };
    unsafe {
        s.write(State {
            last_id: old.last_id,
            ..State::EMPTY
        })
    };
    let mut native = old;
    if old.function == FEnum_glDrawPixels && old.descriptor[3] == GL_BITMAP {
        let width = old.descriptor[0] as usize;
        let rows = old.descriptor[1] as usize;
        let stride = width.div_ceil(8);
        // Every output index is at or beyond its packed source byte. Walking
        // backward therefore never overwrites a bit that is still needed.
        for y in (0..if width == 0 { 0 } else { rows }).rev() {
            for x in (0..width).rev() {
                unsafe {
                    let value = (*old.pixels.add(y * stride + x / 8) >> (7 - x % 8)) & 1;
                    old.pixels.add(y * width + x).write(value);
                }
            }
        }
        native.descriptor[3] = GL_UNSIGNED_BYTE;
    }
    let error = unsafe { draw(m, &native, errors) }.err().unwrap_or(0);
    let mut old = old;
    unsafe { release(m, &mut old) };
    error
}
/// # Safety
/// Render-worker-owned state and counter; callbacks must not reenter this owner.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_pixel_image_interleave(m: *const Memory, s: *mut State) -> u32 {
    if m.is_null() || s.is_null() {
        return DG_GL_ERROR_CONTEXT;
    }
    unsafe { interleave(&*m, s) }
}
/// # Safety
/// Same ownership as interleave; may be called repeatedly during teardown.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_pixel_image_release(m: *const Memory, s: *mut State) {
    if !m.is_null() && !s.is_null() {
        unsafe { release(&*m, s) }
    }
}
#[cfg(test)]
mod adversarial;
