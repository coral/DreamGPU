// SPDX-License-Identifier: GPL-2.0-or-later
//! EXT_texture3D: TEXTURE_3D images, subimages, copies, proxies and the
//! zero-defined storage of an image without payload.
//!
//! A 3D texture is an ordinary texture object (no face table) whose levels
//! also track their image count in `Texture::depths`. Everything the record
//! fixes is checked by `validate`/`validate_copy` (and by the frontend before
//! it sends anything); rules that depend on the level's current image are GL
//! errors, never record faults.
use super::*;
use crate::{gl_validation, query};

/// Largest width, height or depth of `level`.
pub(crate) fn limit(level: u32) -> u32 {
    DG_GL_MAX_3D_TEXTURE_SIZE.checked_shr(level).unwrap_or(0)
}

/// Native storage per texel: high-precision formats may use RGBA16.
fn texel_bytes(internal_format: u32) -> u64 {
    if matches!(
        internal_format,
        GL_RGB10 | GL_RGB12 | GL_RGB16 | GL_RGBA12 | GL_RGBA16
    ) {
        8
    } else {
        4
    }
}

fn pixels(w: u32, h: u32, d: u32) -> u64 {
    u64::from(w) * u64::from(h) * u64::from(d)
}

/// Record shape of TexImage3D (nine words: target, level, internalformat,
/// width, height, depth, border, format, type) and TexSubImage3D (ten words:
/// target, level, xoffset, yoffset, zoffset, width, height, depth, format,
/// type). An image carries exactly its pixels or none (zero-defined storage);
/// a proxy carries none. A border of one is a valid record that execution
/// refuses.
pub(crate) fn validate(function: u32, a: &[u32; 10], data: &[u8]) -> u32 {
    let ok = if function == FEnum_glTexImage3D {
        let [target, level, internal, w, h, d, border, format, kind, _] = *a;
        let pixel_bytes = gl_validation::texture_pixel_bytes(format, kind);
        // A proxy beyond the virtual limits is a valid query whose level the
        // host rejects; an image must fit them.
        let proxy = target == GL_PROXY_TEXTURE_3D;
        let bound = |n: u32| {
            n >= 2 * border
                && if proxy {
                    n <= i32::MAX as u32
                } else {
                    n - 2 * border <= limit(level)
                }
        };
        matches!(target, GL_TEXTURE_3D | GL_PROXY_TEXTURE_3D)
            && level <= DG_GL_MAX_TEXTURE_LEVEL
            && border <= 1
            && bound(w)
            && bound(h)
            && bound(d)
            && pixel_bytes != 0
            && gl_validation::texture_internal_format(internal)
            && (data.is_empty()
                || (target == GL_TEXTURE_3D
                    && pixels(w, h, d) * u64::from(pixel_bytes) == data.len() as u64))
    } else if function == FEnum_glTexSubImage3D {
        let [target, level, _, _, _, w, h, d, format, kind] = *a;
        let pixel_bytes = gl_validation::texture_pixel_bytes(format, kind);
        let extent = |n: u32| (1..=DG_GL_MAX_3D_TEXTURE_SIZE).contains(&n);
        target == GL_TEXTURE_3D
            && level <= DG_GL_MAX_TEXTURE_LEVEL
            && extent(w)
            && extent(h)
            && extent(d)
            && pixel_bytes != 0
            && pixels(w, h, d) * u64::from(pixel_bytes) == data.len() as u64
    } else {
        false
    };
    if ok {
        0
    } else {
        DG_GL_ERROR_TEXTURE
    }
}

/// Record shape of CopyTexSubImage3D (nine words: target, level, xoffset,
/// yoffset, zoffset, x, y, width, height). The framebuffer rectangle and the
/// level's bounds are execution rules.
pub(crate) fn validate_copy(a: &[u32; 10]) -> u32 {
    if a[0] == GL_TEXTURE_3D
        && a[1] <= DG_GL_MAX_TEXTURE_LEVEL
        && a[7] <= DG_GL_MAX_3D_TEXTURE_SIZE
        && a[8] <= DG_GL_MAX_3D_TEXTURE_SIZE
    {
        0
    } else {
        DG_GL_ERROR_TEXTURE
    }
}

/// The native unpack state of a canonical payload (tight, unskipped rows and
/// images of little-endian components), restored when dropped.
struct Unpack<'a> {
    api: &'a DreamGpuGlApi,
    saved: [i32; 7],
}
const UNPACK: [u32; 7] = [
    GL_UNPACK_ALIGNMENT,
    GL_UNPACK_ROW_LENGTH,
    GL_UNPACK_SKIP_ROWS,
    GL_UNPACK_SKIP_PIXELS,
    GL_UNPACK_SWAP_BYTES,
    GL_UNPACK_IMAGE_HEIGHT,
    GL_UNPACK_SKIP_IMAGES,
];
impl<'a> Unpack<'a> {
    unsafe fn new(api: &'a DreamGpuGlApi) -> Result<Self, u32> {
        let get = api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let set = api.dg_glPixelStorei.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let mut saved = [0; 7];
        for (i, key) in UNPACK.into_iter().enumerate() {
            unsafe { get(key, &mut saved[i]) };
        }
        for key in UNPACK {
            let value = i32::from(
                key == GL_UNPACK_ALIGNMENT
                    || (key == GL_UNPACK_SWAP_BYTES && cfg!(target_endian = "big")),
            );
            unsafe { set(key, value) };
        }
        Ok(Self { api, saved })
    }
}
impl Drop for Unpack<'_> {
    fn drop(&mut self) {
        for (key, value) in UNPACK.into_iter().zip(self.saved) {
            unsafe { self.api.dg_glPixelStorei.unwrap()(key, value) };
        }
    }
}

/// Native dimensions of a defined level of the bound 3D texture.
unsafe fn native_size(api: &DreamGpuGlApi, level: u32) -> Option<[u32; 3]> {
    let get = api.dg_glGetTexLevelParameteriv?;
    let mut size = [0i32; 3];
    for (value, name) in
        size.iter_mut()
            .zip([GL_TEXTURE_WIDTH, GL_TEXTURE_HEIGHT, GL_TEXTURE_DEPTH])
    {
        unsafe { get(GL_TEXTURE_3D, level as i32, name, value) };
    }
    if unsafe { api.dg_glGetError?() } != 0 {
        return None;
    }
    size.iter()
        .all(|&n| (0..=DG_GL_MAX_3D_TEXTURE_SIZE as i32).contains(&n))
        .then(|| size.map(|n| n as u32))
}

/// Bytes of the zero tile: whole images when one fits, else rows of one.
const ZERO_TILE: usize = 1 << 20;

/// Define every texel of a level as zero (opaque black, or transparent where
/// the format has alpha) with bounded subimages of one zero tile, under the
/// canonical unpack state and neutral pixel transfer.
unsafe fn zero(m: &names::Memory, level: u32, w: u32, h: u32, d: u32) -> u32 {
    let api = unsafe { &*m.api };
    let Some(sub) = api.dg_glTexSubImage3D else {
        return GL_INVALID_OPERATION;
    };
    let Some(get_error) = api.dg_glGetError else {
        return GL_INVALID_OPERATION;
    };
    if w == 0 || h == 0 || d == 0 {
        return 0;
    }
    let Ok(_transfer) = (unsafe { crate::pixels::Neutral::new(api) }) else {
        return GL_INVALID_OPERATION;
    };
    let row = w as usize * 4;
    let image = row * h as usize;
    let (rows, images) = if image <= ZERO_TILE {
        (h as usize, (ZERO_TILE / image).min(d as usize))
    } else {
        ((ZERO_TILE / row).min(h as usize), 1)
    };
    let Some(tile) = (unsafe { crate::resource::OwnedBytes::zeroed(m, rows * row * images) })
    else {
        return GL_OUT_OF_MEMORY;
    };
    for z in (0..d).step_by(images) {
        for y in (0..h).step_by(rows) {
            unsafe {
                sub(
                    GL_TEXTURE_3D,
                    level as i32,
                    0,
                    y as i32,
                    z as i32,
                    w as i32,
                    (rows as u32).min(h - y) as i32,
                    (images as u32).min(d - z) as i32,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    tile.p.cast(),
                )
            };
            let e = unsafe { get_error() };
            if e != 0 {
                return e;
            }
        }
    }
    0
}

/// PROXY_TEXTURE_3D: rejected without native work beyond the virtual limits
/// (or with a border), else judged natively. `mask` holds rejected levels.
///
/// # Safety
/// Native GL current, pending errors already drained into the guest's.
pub(crate) unsafe fn proxy(api: &DreamGpuGlApi, mask: &mut u32, a: &[u32; 10]) -> u32 {
    let [_, level, internal, w, h, d, border, format, kind, _] = *a;
    if border != 0 || [w, h, d].iter().any(|&n| n > limit(level)) {
        *mask |= 1 << level;
        return 0;
    }
    let (Some(image), Some(error)) = (api.dg_glTexImage3D, api.dg_glGetError) else {
        return GL_INVALID_OPERATION;
    };
    let e = unsafe {
        image(
            GL_PROXY_TEXTURE_3D,
            level as i32,
            native_internal_format(api, internal) as i32,
            w as i32,
            h as i32,
            d as i32,
            0,
            format,
            kind,
            core::ptr::null(),
        );
        error()
    };
    if e == 0 {
        *mask &= !(1 << level);
    }
    e
}

/// TexImage3D/TexSubImage3D of a validated record on the bound 3D texture.
/// An image charges `width*height*depth` texels (and generated levels);
/// without payload it is zero-defined. A subimage outside the defined level
/// is GL_INVALID_VALUE; a native error leaves the level as it was.
///
/// # Safety
/// The bound texture and accounting belong to the render worker; its pending
/// writers were waited on and native errors drained.
#[expect(
    clippy::too_many_arguments,
    reason = "Mirrors the 2D upload entry without borrowing render state across calls."
)]
pub(crate) unsafe fn upload(
    m: &names::Memory,
    texture: *mut Texture,
    total: *mut u64,
    serial: u64,
    function: u32,
    a: &[u32; 10],
    data: &[u8],
    gl_error: &mut u32,
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let level = a[1] as usize;
    if texture.is_null() || unsafe { !owns(texture, GL_TEXTURE_3D) } || level > 11 {
        return Err(DG_GL_ERROR_TEXTURE);
    }
    let pixels_ptr = if data.is_empty() {
        core::ptr::null()
    } else {
        data.as_ptr().cast()
    };
    if function == FEnum_glTexSubImage3D {
        let sub = api.dg_glTexSubImage3D.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let [_, _, x, y, z, w, h, d, format, kind] = *a;
        let t = unsafe { &*texture };
        let extent = [t.widths[level], t.heights[level], t.depths[level]];
        let inside = |offset: u32, n: u32, size: u32| {
            let offset = i64::from(offset as i32);
            offset >= 0 && offset + i64::from(n) <= i64::from(size)
        };
        // A level without an image is the native GL error below.
        if extent[0] != 0
            && !(inside(x, w, extent[0]) && inside(y, h, extent[1]) && inside(z, d, extent[2]))
        {
            *gl_error = GL_INVALID_VALUE;
            return Ok(());
        }
        let unpack = unsafe { Unpack::new(api)? };
        unsafe {
            sub(
                GL_TEXTURE_3D,
                level as i32,
                x as i32,
                y as i32,
                z as i32,
                w as i32,
                h as i32,
                d as i32,
                format,
                kind,
                pixels_ptr,
            )
        };
        let e = unsafe { get_error() };
        drop(unpack);
        if e != 0 {
            *gl_error = e;
            return Ok(());
        }
        unsafe {
            let t = &mut *texture;
            if x == 0 && y == 0 && z == 0 && [w, h, d] == extent {
                t.undefined_levels &= !(1 << level);
            }
            written(api, texture, serial);
        }
        return Ok(());
    }
    if function != FEnum_glTexImage3D {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let image = api.dg_glTexImage3D.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let [_, _, internal, w, h, d, border, format, kind, _] = *a;
    if border != 0 {
        // 3D images are borderless here (see gl.h); the record stays valid.
        *gl_error = GL_INVALID_OPERATION;
        return Ok(());
    }
    let chain = unsafe { generates_mipmaps(api, GL_TEXTURE_3D) };
    let charge =
        |w: u32, h: u32, d: u32| with_chain(pixels(w, h, d) * texel_bytes(internal), chain);
    let allocation = charge(w, h, d);
    let old = unsafe { (*texture).levels[level] };
    let next = unsafe { *total }
        .checked_sub(old)
        .and_then(|v| v.checked_add(allocation))
        .ok_or(DG_GL_ERROR_LIMIT)?;
    if next > u64::from(DG_GL_MAX_TEXTURE_BYTES) {
        return Err(DG_GL_ERROR_LIMIT);
    }
    let unpack = unsafe { Unpack::new(api)? };
    unsafe {
        image(
            GL_TEXTURE_3D,
            level as i32,
            native_internal_format(api, internal) as i32,
            w as i32,
            h as i32,
            d as i32,
            0,
            format,
            kind,
            pixels_ptr,
        )
    };
    let mut e = unsafe { get_error() };
    if e != 0 {
        // Nothing was (re)defined: the level keeps its image and charge.
        drop(unpack);
        *gl_error = e;
        return Ok(());
    }
    // The native image is what was defined; never publish requested sizes
    // the object lacks, but keep the conservative charge if they are unknown.
    // An empty definition releases the level's storage.
    let (size, charged) = match unsafe { native_size(api, level as u32) } {
        Some([width, height, depth]) if width <= w && height <= h && depth <= d => {
            ([width, height, depth], charge(width, height, depth))
        }
        _ => {
            e = GL_INVALID_OPERATION;
            ([0; 3], allocation)
        }
    };
    if e == 0 && data.is_empty() {
        e = unsafe { zero(m, level as u32, size[0], size[1], size[2]) };
    }
    drop(unpack);
    unsafe {
        let t = &mut *texture;
        *total = next - allocation + charged;
        t.levels[level] = charged;
        [t.widths[level], t.heights[level], t.depths[level]] = size;
        t.borders[level] = 0;
        if e != 0 {
            t.undefined_levels |= 1 << level;
        } else {
            t.undefined_levels &= !(1 << level);
        }
        written(api, texture, serial);
    }
    if e != 0 {
        *gl_error = e;
        // Storage exists whose contents could not be established.
        return Err(DG_GL_ERROR_TEXTURE);
    }
    Ok(())
}

/// CopyTexSubImage3D of a validated nine-word record into one image of the
/// bound 3D texture's level. Rectangles outside the level are
/// GL_INVALID_VALUE; native errors (an undefined level, a framebuffer
/// problem) are the guest's GL errors.
///
/// # Safety
/// Render-owned texture, accounting and error fields; a drawable is current.
pub(crate) unsafe fn copy(
    api: &DreamGpuGlApi,
    t: *mut Texture,
    errors: *mut u32,
    serial: u64,
    has_drawable: u32,
    a: &[u32; 10],
) -> u32 {
    if validate_copy(a) != 0 || t.is_null() || unsafe { !owns(t, GL_TEXTURE_3D) } {
        return DG_GL_ERROR_TEXTURE;
    }
    if has_drawable == 0 {
        return DG_GL_ERROR_TEXTURE;
    }
    let (Some(copy), Some(get_error)) = (api.dg_glCopyTexSubImage3D, api.dg_glGetError) else {
        return DG_GL_ERROR_UNSUPPORTED;
    };
    let [_, level, x, y, z, sx, sy, w, h, _] = *a;
    let level = level as usize;
    let [width, height, depth] =
        unsafe { [(*t).widths[level], (*t).heights[level], (*t).depths[level]] };
    let offset = |v: u32| i64::from(v as i32);
    if width != 0
        && (offset(x) < 0
            || offset(y) < 0
            || offset(z) < 0
            || offset(x) + i64::from(w) > i64::from(width)
            || offset(y) + i64::from(h) > i64::from(height)
            || offset(z) >= i64::from(depth))
    {
        unsafe { query::store(errors, GL_INVALID_VALUE) };
        return 0;
    }
    unsafe {
        wait(api, t, serial);
        if let Err(e) = query::remember(api, errors) {
            return e;
        }
        copy(
            GL_TEXTURE_3D,
            level as i32,
            x as i32,
            y as i32,
            z as i32,
            sx as i32,
            sy as i32,
            w as i32,
            h as i32,
        );
    }
    let e = unsafe { get_error() };
    if e != 0 {
        unsafe { query::store(errors, e) };
        return 0;
    }
    unsafe { written(api, t, serial) };
    0
}

#[cfg(test)]
mod tests;
