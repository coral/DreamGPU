// SPDX-License-Identifier: GPL-2.0-or-later
//! Native texture storage ownership, cross-context ordering and uploads.
use crate::gl_api::*;
use core::ffi::c_void;
pub(crate) mod border;
pub(crate) mod control;
pub(crate) mod names;
pub(crate) mod volume;
// Linux uses provider border storage; CGL uses the private fragment sampler.
pub(crate) const BORDER_IMAGES: bool = true;

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
    pub borders: [u32; 12],
    pub levels: [u64; 12],
    /// A cube map's per-face level tracking, in the texture's own allocation;
    /// null for 1D and 2D textures, whose only image is tracked above. For a
    /// cube map the arrays above summarize its faces instead: `levels` holds
    /// each level's charge over all six faces and `undefined_levels` their
    /// union, so accounting and draw admission treat every texture alike.
    pub faces: *mut [Face; FACES],
    /// Image count (depth) of each level of a 3D texture (EXT_texture3D);
    /// zero for other targets. `widths`/`heights` give each image's size and
    /// `levels` charges all of a level's images.
    pub depths: [u32; 12],
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
            borders: [0; 12],
            levels: [0; 12],
            faces: core::ptr::null_mut(),
            depths: [0; 12],
        }
    }
}

/// Faces of a cube map (ARB_texture_cube_map), POSITIVE_X through NEGATIVE_Z.
pub(crate) const FACES: usize = 6;
/// Level tracking of one image array: a 1D/2D texture's image or one cube face.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Face {
    pub widths: [u32; 12],
    pub heights: [u32; 12],
    pub borders: [u32; 12],
    pub undefined_levels: u32,
    pub levels: [u64; 12],
}
impl Face {
    pub(crate) const EMPTY: Self = Self {
        widths: [0; 12],
        heights: [0; 12],
        borders: [0; 12],
        undefined_levels: 0,
        levels: [0; 12],
    };
}
/// The face index of a cube face target.
pub(crate) fn cube_face(target: u32) -> Option<usize> {
    let face = target.wrapping_sub(GL_TEXTURE_CUBE_MAP_POSITIVE_X) as usize;
    (face < FACES).then_some(face)
}
/// The binding target owning an image target: a cube face belongs to its cube map.
pub(crate) fn binding_target(target: u32) -> u32 {
    if cube_face(target).is_some() {
        GL_TEXTURE_CUBE_MAP
    } else {
        target
    }
}
/// Level tracking of the image array `target` names in `t`: the texture's own
/// for 1D and 2D, or one face of a cube map.
///
/// # Safety
/// `t` is a live render-owned texture; a non-null face table is its own.
pub(crate) unsafe fn image_levels(t: *const Texture, target: u32) -> Face {
    unsafe {
        if (*t).faces.is_null() {
            Face {
                widths: (*t).widths,
                heights: (*t).heights,
                borders: (*t).borders,
                undefined_levels: (*t).undefined_levels,
                levels: (*t).levels,
            }
        } else {
            (*(*t).faces)[cube_face(target).unwrap_or(0)]
        }
    }
}
/// Store the image array `target` names, keeping a cube map's summary exact.
///
/// # Safety
/// Same ownership as `image`; no reference into `t` is live across the call.
pub(crate) unsafe fn set_image_levels(t: *mut Texture, target: u32, value: &Face) {
    unsafe {
        if (*t).faces.is_null() {
            (*t).widths = value.widths;
            (*t).heights = value.heights;
            (*t).borders = value.borders;
            (*t).undefined_levels = value.undefined_levels;
            (*t).levels = value.levels;
            return;
        }
        let faces = &mut *(*t).faces;
        faces[cube_face(target).unwrap_or(0)] = *value;
        (*t).undefined_levels = faces.iter().fold(0, |all, f| all | f.undefined_levels);
        for level in 0..12 {
            (*t).levels[level] = faces.iter().map(|f| f.levels[level]).sum();
        }
    }
}
/// A cube map and its face table, allocated (and freed) as one object.
#[repr(C)]
struct CubeStorage {
    texture: Texture,
    faces: [Face; FACES],
}
/// Allocate and initialize a texture object of a binding target. A cube map
/// carries its face table in the same allocation, released with the texture.
///
/// # Safety
/// Memory callbacks belong to the render worker; the result is uninitialized
/// only on failure (null).
pub(crate) unsafe fn allocate(m: &names::Memory, target: u32, value: Texture) -> *mut Texture {
    let cube = target == GL_TEXTURE_CUBE_MAP;
    let bytes = if cube {
        core::mem::size_of::<CubeStorage>()
    } else {
        core::mem::size_of::<Texture>()
    };
    let t: *mut Texture = unsafe { (m.allocate)(m.opaque, bytes) }.cast();
    if t.is_null() {
        return t;
    }
    unsafe {
        t.write(Texture {
            target,
            faces: core::ptr::null_mut(),
            ..value
        });
        if cube {
            let storage = t.cast::<CubeStorage>();
            let faces = core::ptr::addr_of_mut!((*storage).faces);
            faces.write([Face::EMPTY; FACES]);
            (*t).faces = faces;
        }
    }
    t
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

/// Native proxy evaluation is allocation-free. The caller separately records
/// rejection by virtual limits, which may be smaller than this host's limits.
pub(crate) unsafe fn proxy(api: &DreamGpuGlApi, a: &[u32; 8]) -> u32 {
    unsafe {
        if a[0] == GL_PROXY_TEXTURE_1D {
            api.dg_glTexImage1D.expect("complete API")(
                a[0],
                a[1] as i32,
                a[2] as i32,
                a[3] as i32,
                a[5] as i32,
                a[6],
                a[7],
                core::ptr::null(),
            );
        } else {
            api.dg_glTexImage2D.expect("complete API")(
                a[0],
                a[1] as i32,
                a[2] as i32,
                a[3] as i32,
                a[4] as i32,
                a[5] as i32,
                a[6],
                a[7],
                core::ptr::null(),
            );
        }
        api.dg_glGetError.expect("complete API")()
    }
}

/// Zero-define a level of the image array `target` (texture, target, level,
/// width, height); the level's border is already recorded in the texture.
pub type Zero = unsafe extern "C" fn(*mut c_void, *mut Texture, u32, u32, u32, u32) -> u32;

/// Whether the bound texture regenerates its mip chain on level changes
/// (SGIS_generate_mipmap). The native query runs after pending errors were
/// drained, so its own error is consumed here and never reaches the guest.
unsafe fn generates_mipmaps(api: &DreamGpuGlApi, target: u32) -> bool {
    let (Some(get), Some(error)) = (api.dg_glGetTexParameteriv, api.dg_glGetError) else {
        return false;
    };
    let mut value = 0;
    unsafe {
        get(target, GL_GENERATE_MIPMAP, &mut value);
        error() == 0 && value != 0
    }
}

/// Host-internal texture-read level flag: read the native compressed image in
/// 32-bit words instead of RGBA pixels. Never part of the wire encoding, whose
/// level word uses bits 14 (DG_GL_TEXTURE_READ_DEPTH) and 15 (READ_FLOAT).
pub(crate) const READ_COMPRESSED: u32 = 0x2000;

/// Bytes per 4x4 block of an S3TC internal format, zero for anything else.
pub(crate) fn s3tc_block_bytes(format: u32) -> u32 {
    match format {
        GL_COMPRESSED_RGB_S3TC_DXT1_EXT | GL_COMPRESSED_RGBA_S3TC_DXT1_EXT => 8,
        GL_COMPRESSED_RGBA_S3TC_DXT3_EXT | GL_COMPRESSED_RGBA_S3TC_DXT5_EXT => 16,
        _ => 0,
    }
}
/// Exact compressed image size of a `w`x`h` S3TC image (partial blocks count).
pub(crate) fn s3tc_bytes(w: u32, h: u32, block: u32) -> u64 {
    u64::from(w.div_ceil(4)) * u64::from(h.div_ceil(4)) * u64::from(block)
}

/// Native levels generated below a defined level cost at most a third more.
fn with_chain(bytes: u64, chain: bool) -> u64 {
    if chain {
        bytes + bytes.div_ceil(3)
    } else {
        bytes
    }
}

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
    if unsafe { !owns(texture, a[0]) } {
        return Err(DG_GL_ERROR_TEXTURE);
    }
    let level = a[1] as usize;
    let w = a[if image { 3 } else { 4 }];
    let h = a[if image { 4 } else { 5 }];
    if level > DG_GL_MAX_TEXTURE_LEVEL as usize
        || (!image && (w == 0 || h == 0))
        || w > DG_GL_MAX_TEXTURE_DIMENSION + 2
        || h > DG_GL_MAX_TEXTURE_DIMENSION + 2
    {
        return Err(11);
    }
    // High-precision internal formats may use RGBA16 native storage. Charge
    // that capacity for both texture targets before changing the live image.
    let texel_bytes = if matches!(a[2], GL_RGB10 | GL_RGB12 | GL_RGB16 | GL_RGBA12 | GL_RGBA16) {
        8
    } else {
        4
    };
    // Generated levels are charged with the image that produces them. The
    // parameter belongs to the binding target, not to one cube face.
    let chain = image && unsafe { generates_mipmaps(api, binding_target(a[0])) };
    // S3TC storage is charged at its compressed size.
    let block = if image { s3tc_block_bytes(a[2]) } else { 0 };
    let level_bytes = |w: u32, h: u32| {
        if block != 0 {
            s3tc_bytes(w, h, block)
        } else {
            u64::from(w) * u64::from(h) * texel_bytes
        }
    };
    let allocation = with_chain(level_bytes(w, h), chain);
    // The image array this record defines: the texture's own, or one face.
    let mut f = unsafe { image_levels(texture, a[0]) };
    let old = f.levels[level];
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
        let (width, height, border) = (f.widths[level], f.heights[level], f.borders[level]);
        let x = i64::from(a[2] as i32);
        let y = i64::from(a[3] as i32);
        if !one
            && width != 0
            && height != 0
            && (x < -i64::from(border)
                || y < -i64::from(border)
                || x + i64::from(w) > i64::from(width) - i64::from(border)
                || y + i64::from(h) > i64::from(height) - i64::from(border))
        {
            unsafe { *gl_error = GL_INVALID_VALUE };
            return Ok(());
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
        || api.dg_glGetTexLevelParameteriv.is_none()
    {
        return Err(3);
    }
    unsafe {
        wait(api, texture, serial);
    }
    let mut sub_start = -(f.borders[level] as i32);
    if one && !image {
        let mut width = 0;
        let mut border = 0;
        unsafe {
            api.dg_glGetTexLevelParameteriv.unwrap()(
                a[0],
                level as i32,
                GL_TEXTURE_WIDTH,
                &mut width,
            );
            api.dg_glGetTexLevelParameteriv.unwrap()(
                a[0],
                level as i32,
                GL_TEXTURE_BORDER,
                &mut border,
            );
        }
        let error = unsafe { api.dg_glGetError.unwrap()() };
        if error != 0 {
            unsafe { *gl_error = error };
            return Err(11);
        }
        let offset = i64::from(a[2] as i32);
        if !(0..=1).contains(&border)
            || width < 2 * border
            || width as u32 > DG_GL_MAX_TEXTURE_DIMENSION + 2
        {
            return Err(11);
        }
        if width != 0
            && (offset < -i64::from(border) || offset + i64::from(w) > i64::from(width - border))
        {
            // Valid wire command, invalid current GL image rectangle. Consume
            // it without mutation and preserve the context for later commands.
            unsafe { *gl_error = GL_INVALID_VALUE };
            return Ok(());
        }
        // Width zero does not distinguish an undefined native array from a
        // defined empty image. Let the actual native API select its GL error.
        sub_start = -border;
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
            // Captured component words are little-endian regardless of the
            // client's swap state. Byte payloads ignore native byte swapping.
            let value =
                i32::from(i == 0 || (*key == GL_UNPACK_SWAP_BYTES && cfg!(target_endian = "big")));
            api.dg_glPixelStorei.unwrap()(*key, value);
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
                a[5] as i32,
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
                a[5] as i32,
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
    let mut defined_width = w;
    let mut defined_height = h;
    let mut defined_border = a[5];
    let mut defined_allocation = allocation;
    let mut defined_total = new_total;
    if allocated {
        let mut width = 0;
        let mut height = 1;
        let mut border = 0;
        unsafe {
            api.dg_glGetTexLevelParameteriv.unwrap()(
                a[0],
                level as i32,
                GL_TEXTURE_WIDTH,
                &mut width,
            );
            api.dg_glGetTexLevelParameteriv.unwrap()(
                a[0],
                level as i32,
                GL_TEXTURE_BORDER,
                &mut border,
            );
            if !one {
                api.dg_glGetTexLevelParameteriv.unwrap()(
                    a[0],
                    level as i32,
                    GL_TEXTURE_HEIGHT,
                    &mut height,
                );
            }
            error = api.dg_glGetError.unwrap()();
        }
        if error != 0
            || !(0..=1).contains(&border)
            || width < 2 * border
            || width as u32 > w
            || height < 0
            || height as u32 > h
            || (!one && height < 2 * border)
        {
            // Keep the conservative allocation charge if native allocation
            // succeeded but its dimensions could not be established. No
            // requested dimensions are exposed as an actual native image.
            defined_width = 0;
            defined_height = 0;
            defined_border = 0;
            if error == 0 {
                error = GL_INVALID_OPERATION;
            }
        } else {
            defined_width = width as u32;
            defined_height = height as u32;
            defined_border = border as u32;
            defined_allocation = with_chain(level_bytes(defined_width, defined_height), chain);
            defined_total = new_total - allocation + defined_allocation;
        }
    }
    if allocated {
        f.borders[level] = defined_border;
        unsafe { set_image_levels(texture, a[0], &f) };
    }
    if allocated && error == 0 && bytes == 0 && defined_width != 0 && defined_height != 0 {
        error = unsafe {
            zero(
                opaque,
                texture,
                a[0],
                level as u32,
                defined_width,
                defined_height,
            )
        };
    }
    for (i, key) in stores.iter().enumerate() {
        unsafe {
            api.dg_glPixelStorei.unwrap()(*key, saved[i]);
        }
    }
    if allocated {
        f.levels[level] = defined_allocation;
        f.widths[level] = defined_width;
        f.heights[level] = defined_height;
        f.borders[level] = defined_border;
        if error != 0 {
            f.undefined_levels |= 1 << level;
        } else {
            f.undefined_levels &= !(1 << level);
        }
        unsafe {
            *total = defined_total;
            set_image_levels(texture, a[0], &f);
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
        return if !allocated { Ok(()) } else { Err(11) };
    }
    if !image
        && a[2] as i32 == sub_start
        && a[3] as i32 == if one { 0 } else { sub_start }
        && w == f.widths[level]
        && h == f.heights[level]
    {
        f.undefined_levels &= !(1 << level);
        unsafe { set_image_levels(texture, a[0], &f) };
    }
    Ok(())
}

/// Whether `t` owns the image array `target` names: its own 1D/2D image, or
/// a face of a cube map with its face table.
///
/// # Safety
/// `t` is a live render-owned texture.
pub(crate) unsafe fn owns(t: *const Texture, target: u32) -> bool {
    unsafe {
        (*t).target == binding_target(target)
            && (*t).faces.is_null() == (cube_face(target).is_none())
    }
}

/// Native proxy evaluation of a validated S3TC CompressedTexImage2D (2D or
/// cube map proxy).
pub(crate) unsafe fn compressed_proxy(api: &DreamGpuGlApi, a: &[u32; 8]) -> u32 {
    let (Some(image), Some(error)) = (api.dg_glCompressedTexImage2D, api.dg_glGetError) else {
        return GL_INVALID_OPERATION;
    };
    unsafe {
        image(
            a[0],
            a[1] as i32,
            a[2],
            a[3] as i32,
            a[4] as i32,
            0,
            a[6] as i32,
            core::ptr::null(),
        );
        error()
    }
}

/// CompressedTexImage2D/CompressedTexSubImage2D on the bound 2D texture or
/// one face of the bound cube map.
/// Arguments passed `data_validate`: S3TC format, exact image size, block-aligned
/// offsets and a payload that is empty (zero-defined image) or exactly the image.
/// Rules that depend on the level's current image are GL errors, never record
/// faults; a failed native call leaves the tracked level unchanged.
///
/// # Safety
/// Texture/accounting belong to the render worker; the bound texture is current
/// and its pending writers were waited on and native errors drained.
#[expect(
    clippy::too_many_arguments,
    reason = "Mirrors the texture upload entry without borrowing render state across calls."
)]
pub(crate) unsafe fn compressed(
    m: &names::Memory,
    texture: *mut Texture,
    total: *mut u64,
    serial: u64,
    function: u32,
    a: &[u32; 8],
    data: &[u8],
    gl_error: &mut u32,
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let level = a[1] as usize;
    if level > DG_GL_MAX_TEXTURE_LEVEL as usize || unsafe { !owns(texture, a[0]) } {
        return Err(DG_GL_ERROR_TEXTURE);
    }
    let mut f = unsafe { image_levels(texture, a[0]) };
    if function == FEnum_glCompressedTexImage2D {
        let native = api
            .dg_glCompressedTexImage2D
            .ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let size = u64::from(a[6]);
        let n = unsafe { *total }
            .checked_sub(f.levels[level])
            .and_then(|v| v.checked_add(size))
            .ok_or(9u32)?;
        if n > u64::from(DG_GL_MAX_TEXTURE_BYTES) {
            return Err(9);
        }
        // An omitted payload defines the image as zero blocks (opaque black,
        // or transparent where the format carries alpha), never stale memory.
        let zeros;
        let pixels = if data.is_empty() {
            zeros = unsafe { crate::resource::OwnedBytes::zeroed(m, a[6] as usize) }
                .ok_or(DG_GL_ERROR_LIMIT)?;
            zeros.p.cast_const()
        } else {
            data.as_ptr()
        };
        unsafe {
            native(
                a[0],
                level as i32,
                a[2],
                a[3] as i32,
                a[4] as i32,
                0,
                a[6] as i32,
                pixels.cast(),
            );
        }
        let error = unsafe { get_error() };
        if error != 0 {
            *gl_error = error;
            return Ok(());
        }
        f.levels[level] = size;
        f.widths[level] = a[3];
        f.heights[level] = a[4];
        f.borders[level] = 0;
        f.undefined_levels &= !(1 << level);
        unsafe {
            *total = n;
            set_image_levels(texture, a[0], &f);
            written(api, texture, serial);
        }
        return Ok(());
    }
    if function != FEnum_glCompressedTexSubImage2D {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let sub = api
        .dg_glCompressedTexSubImage2D
        .ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let (width, height) = (f.widths[level], f.heights[level]);
    let [target, _, x, y, w, h, format, size] = *a;
    // EXT_texture_compression_s3tc: partial blocks only where the update
    // reaches the level's edge; the update must lie inside the image.
    if width == 0
        || height == 0
        || u64::from(x) + u64::from(w) > u64::from(width)
        || u64::from(y) + u64::from(h) > u64::from(height)
        || (!w.is_multiple_of(4) && x + w != width)
        || (!h.is_multiple_of(4) && y + h != height)
    {
        *gl_error = GL_INVALID_OPERATION;
        return Ok(());
    }
    unsafe {
        sub(
            target,
            level as i32,
            x as i32,
            y as i32,
            w as i32,
            h as i32,
            format,
            size as i32,
            data.as_ptr().cast(),
        );
    }
    let error = unsafe { get_error() };
    if error != 0 {
        // Format mismatch with the level's storage, as reported natively.
        *gl_error = error;
        return Ok(());
    }
    unsafe { written(api, texture, serial) };
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
