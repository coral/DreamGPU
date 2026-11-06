// SPDX-License-Identifier: GPL-2.0-or-later
//! Internal framebuffer storage and guest texture copy/clear/read resources.
//! All objects belong to the render worker; native contexts are made current by
//! the OS adapter. No guest VRAM reference or exported-image ownership is here.
#![allow(non_upper_case_globals)]
use crate::{
    gl_api::*,
    query,
    state::{dreamgpu_context_select_buffers, ContextState},
    texture::{
        cube_face, dreamgpu_texture_wait, dreamgpu_texture_written, image_levels, names::Memory,
        owns, set_image_levels, Texture,
    },
};
use core::ptr::{addr_of_mut, null_mut};
macro_rules! gl {($api:expr, $name:ident($($arg:expr),* $(,)?)) => {{#[allow(unused_unsafe)] let result = unsafe { $api.$name.expect("complete native GL table")($($arg),*) }; result }};}
#[repr(C)]
pub struct Drawable {
    pub width: u32,
    pub height: u32,
    pub color: u32,
    pub front: u32,
    pub depth: u32,
    pub last_write: *mut __GLsync,
}
impl Drawable {
    const EMPTY: Self = Self {
        width: 0,
        height: 0,
        color: 0,
        front: 0,
        depth: 0,
        last_write: null_mut(),
    };
}
unsafe fn attachments(a: &DreamGpuGlApi, d: *const Drawable) {
    {
        gl!(
            a,
            dg_glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D,
                (*d).color,
                0
            )
        );
        gl!(
            a,
            dg_glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT1,
                GL_TEXTURE_2D,
                (*d).front,
                0
            )
        );
        gl!(
            a,
            dg_glFramebufferRenderbuffer(
                GL_FRAMEBUFFER,
                GL_DEPTH_ATTACHMENT,
                GL_RENDERBUFFER,
                (*d).depth
            )
        );
        gl!(
            a,
            dg_glFramebufferRenderbuffer(
                GL_FRAMEBUFFER,
                GL_STENCIL_ATTACHMENT,
                GL_RENDERBUFFER,
                (*d).depth
            )
        );
    }
}
/// # Safety
/// Complete native API, fresh render-owned output, internal root context current.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_drawable_init(
    api: *const DreamGpuGlApi,
    d: *mut Drawable,
    width: u32,
    height: u32,
) -> u32 {
    if api.is_null() || d.is_null() {
        return DG_GL_ERROR_CONTEXT;
    }
    unsafe {
        d.write(Drawable::EMPTY);
    }
    if width == 0 || height == 0 || width > DG_GL_MAX_DIMENSION || height > DG_GL_MAX_DIMENSION {
        return DG_GL_ERROR_LIMIT;
    }
    let a = unsafe { &*api };
    unsafe {
        (*d).width = width;
        (*d).height = height;
    }
    let mut colors = [0; 2];
    gl!(a, dg_glGenTextures(2, colors.as_mut_ptr()));
    unsafe {
        (*d).color = colors[0];
        (*d).front = colors[1];
    }
    if colors.contains(&0) {
        unsafe { dreamgpu_drawable_release(api, d) };
        return DG_GL_ERROR_HOST;
    }
    for color in colors {
        gl!(a, dg_glBindTexture(GL_TEXTURE_2D, color));
        gl!(
            a,
            dg_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST as i32)
        );
        gl!(
            a,
            dg_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST as i32)
        );
        gl!(
            a,
            dg_glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA8 as i32,
                width as i32,
                height as i32,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                core::ptr::null()
            )
        );
    }
    {
        gl!(a, dg_glGenRenderbuffers(1, addr_of_mut!((*d).depth)));
        gl!(a, dg_glBindRenderbuffer(GL_RENDERBUFFER, (*d).depth));
    }
    if unsafe { (*d).depth } == 0 {
        unsafe { dreamgpu_drawable_release(api, d) };
        return DG_GL_ERROR_HOST;
    }
    gl!(
        a,
        dg_glRenderbufferStorage(
            GL_RENDERBUFFER,
            GL_DEPTH24_STENCIL8,
            width as i32,
            height as i32
        )
    );
    if gl!(a, dg_glGetError()) != 0 {
        unsafe { dreamgpu_drawable_release(api, d) };
        return DG_GL_ERROR_HOST;
    }
    let mut fb = 0;
    gl!(a, dg_glGenFramebuffers(1, &mut fb));
    if fb == 0 {
        unsafe { dreamgpu_drawable_release(api, d) };
        return DG_GL_ERROR_HOST;
    }
    gl!(a, dg_glBindFramebuffer(GL_FRAMEBUFFER, fb));
    unsafe { attachments(a, d) };
    if gl!(a, dg_glCheckFramebufferStatus(GL_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE {
        gl!(a, dg_glDeleteFramebuffers(1, &fb));
        unsafe { dreamgpu_drawable_release(api, d) };
        return DG_GL_ERROR_HOST;
    }
    gl!(a, dg_glClearColor(0., 0., 0., 0.));
    gl!(a, dg_glClearDepth(1.));
    gl!(a, dg_glClearStencil(0));
    gl!(
        a,
        dg_glDrawBuffers(2, [GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1].as_ptr())
    );
    gl!(
        a,
        dg_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)
    );
    gl!(a, dg_glBindFramebuffer(GL_FRAMEBUFFER, 0));
    gl!(a, dg_glDeleteFramebuffers(1, &fb));
    unsafe { dreamgpu_drawable_flush(api, d) };
    0
}
/// # Safety
/// Same ownership as init; native context in shared group is current.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_drawable_flush(api: *const DreamGpuGlApi, d: *mut Drawable) {
    let a = unsafe { &*api };
    unsafe {
        if !(*d).last_write.is_null() {
            gl!(a, dg_glDeleteSync((*d).last_write));
        }
        (*d).last_write = gl!(a, dg_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
    }
    gl!(a, dg_glFlush());
}
/// # Safety
/// Render-owned object, shared root current, no completion reader retains it.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_drawable_release(api: *const DreamGpuGlApi, d: *mut Drawable) {
    let a = unsafe { &*api };
    let old = unsafe { d.read() };
    unsafe { d.write(Drawable::EMPTY) };
    if old.depth != 0 {
        gl!(a, dg_glDeleteRenderbuffers(1, &old.depth));
    }
    if !old.last_write.is_null() {
        gl!(a, dg_glDeleteSync(old.last_write));
    }
    if old.color != 0 {
        gl!(a, dg_glDeleteTextures(1, &old.color));
    }
    if old.front != 0 {
        gl!(a, dg_glDeleteTextures(1, &old.front));
    }
}
/// # Safety
/// Current render context owns framebuffer/initialized and its state. The
/// drawable framebuffer is bound for both targets, then the context's
/// framebuffer object bindings are restored on top of it.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_drawable_bind(
    api: *const DreamGpuGlApi,
    d: *mut Drawable,
    framebuffer: *mut u32,
    initialized: *mut u32,
    state: *mut ContextState,
    serial: u64,
) -> u32 {
    let a = unsafe { &*api };
    unsafe {
        if !(*d).last_write.is_null() {
            gl!(a, dg_glWaitSync((*d).last_write, 0, u64::MAX));
            gl!(a, dg_glDeleteSync((*d).last_write));
            (*d).last_write = null_mut();
        }
        if *framebuffer == 0 {
            gl!(a, dg_glGenFramebuffers(1, framebuffer));
        }
        if *framebuffer == 0 {
            return DG_GL_ERROR_HOST;
        }
        gl!(a, dg_glBindFramebuffer(GL_FRAMEBUFFER, *framebuffer));
        (*state).window_framebuffer = *framebuffer;
        attachments(a, d);
        let e = dreamgpu_context_select_buffers(api, state);
        if e != 0 {
            return e;
        }
        if gl!(a, dg_glCheckFramebufferStatus(GL_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE {
            return DG_GL_ERROR_HOST;
        }
        if *initialized == 0 {
            gl!(
                a,
                dg_glViewport(0, 0, (*d).width as i32, (*d).height as i32)
            );
            *initialized = 1;
        }
        // Another context may have written this one's attachments meanwhile.
        crate::framebuffer::rebind(a, state);
        crate::framebuffer::order_bound(a, state, serial);
    }
    0
}
/// # Safety
/// Render-owned drawable/current framebuffer. Exchange changes names, not pixels,
/// and leaves the context's framebuffer object bindings as they were.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_drawable_exchange(
    api: *const DreamGpuGlApi,
    d: *mut Drawable,
    framebuffer: u32,
    state: *mut ContextState,
) {
    let a = unsafe { &*api };
    unsafe {
        core::ptr::swap(addr_of_mut!((*d).color), addr_of_mut!((*d).front));
        gl!(a, dg_glBindFramebuffer(GL_FRAMEBUFFER, framebuffer));
        gl!(
            a,
            dg_glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D,
                (*d).color,
                0
            )
        );
        gl!(
            a,
            dg_glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT1,
                GL_TEXTURE_2D,
                (*d).front,
                0
            )
        );
        dreamgpu_context_select_buffers(api, state);
        crate::framebuffer::rebind(a, state);
    }
}
/// # Safety
/// Validated immutable command snapshot; texture/accounting/error fields are
/// disjoint render-owned storage. No context references cross native calls.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_copy(
    api: *const DreamGpuGlApi,
    t: *mut Texture,
    total: *mut u64,
    errors: *mut u32,
    serial: u64,
    has_drawable: u32,
    function: u32,
    args: *const u8,
) -> u32 {
    let a = unsafe { &*api };
    if function == FEnum_glCopyTexSubImage3D {
        // EXT_texture3D: one image of the bound 3D texture's level.
        let Some(words) = (unsafe { crate::gl_validation::args_n::<10>(function, args) }) else {
            return DG_GL_ERROR_UNSUPPORTED;
        };
        return unsafe { crate::texture::volume::copy(a, t, errors, serial, has_drawable, &words) };
    }
    if !matches!(
        function,
        FEnum_glCopyTexImage2D
            | FEnum_glCopyTexSubImage2D
            | FEnum_glCopyTexImage1D
            | FEnum_glCopyTexSubImage1D
    ) {
        return DG_GL_ERROR_UNSUPPORTED;
    }
    let e = unsafe { crate::gl_validation::dreamgpu_gl_call_validate(function, args) };
    if e != 0 {
        return e;
    }
    let mut words = [0u32; 8];
    let one = matches!(function, FEnum_glCopyTexImage1D | FEnum_glCopyTexSubImage1D);
    let image = matches!(function, FEnum_glCopyTexImage1D | FEnum_glCopyTexImage2D);
    for (i, w) in words.iter_mut().enumerate().take(if one {
        if image {
            7
        } else {
            6
        }
    } else {
        8
    }) {
        *w = u32::from_le(unsafe { args.add(i * 4).cast::<u32>().read_unaligned() });
    }
    if image && words[if one { 6 } else { 7 }] != 0 && !crate::texture::BORDER_IMAGES {
        unsafe {
            query::store(errors, GL_INVALID_OPERATION);
        }
        return 0;
    }
    // The copied image array: the bound texture's own, or one cube face.
    if t.is_null() || unsafe { !owns(t, words[0]) } {
        return DG_GL_ERROR_TEXTURE;
    }
    let mut f = unsafe { image_levels(t, words[0]) };
    let level = words[1] as usize;
    let x = words[if image || one { 3 } else { 4 }] as i32;
    let y = words[if image || one { 4 } else { 5 }] as i32;
    let w = words[if image || one { 5 } else { 6 }];
    let h = if one {
        1
    } else {
        words[if image { 6 } else { 7 }]
    };
    let texel_bytes = if one
        || matches!(
            words[2],
            GL_RGB10 | GL_RGB12 | GL_RGB16 | GL_RGBA12 | GL_RGBA16
        ) {
        8
    } else {
        4
    };
    let allocation = u64::from(w) * u64::from(h) * texel_bytes;
    if has_drawable == 0 {
        return DG_GL_ERROR_TEXTURE;
    }
    let next = unsafe {
        if !image && !one {
            let border = i64::from(f.borders[level]);
            let x = i64::from(words[2] as i32);
            let y = i64::from(words[3] as i32);
            if x < -border
                || y < -border
                || x + i64::from(w) > i64::from(f.widths[level]) - border
                || y + i64::from(h) > i64::from(f.heights[level]) - border
            {
                query::store(errors, GL_INVALID_VALUE);
                return 0;
            }
        }
        if image {
            match (*total)
                .checked_sub(f.levels[level])
                .and_then(|n| n.checked_add(allocation))
            {
                Some(n) if n <= u64::from(DG_GL_MAX_TEXTURE_BYTES) => n,
                _ => return DG_GL_ERROR_LIMIT,
            }
        } else {
            *total
        }
    };
    unsafe {
        dreamgpu_texture_wait(api, t, serial);
        if let Err(e) = query::remember(a, errors) {
            return e;
        }
    }
    if one && !image {
        let mut width = 0;
        let mut border = 0;
        gl!(
            a,
            dg_glGetTexLevelParameteriv(GL_TEXTURE_1D, level as i32, GL_TEXTURE_WIDTH, &mut width)
        );
        gl!(
            a,
            dg_glGetTexLevelParameteriv(
                GL_TEXTURE_1D,
                level as i32,
                GL_TEXTURE_BORDER,
                &mut border
            )
        );
        let error = gl!(a, dg_glGetError());
        if error != 0 {
            unsafe { query::store(errors, error) };
            return DG_GL_ERROR_HOST;
        }
        if !(0..=1).contains(&border)
            || width < 2 * border
            || width as u32 > DG_GL_MAX_TEXTURE_DIMENSION + 2
        {
            return DG_GL_ERROR_HOST;
        }
        let offset = i64::from(words[2] as i32);
        if width != 0
            && (offset < -i64::from(border) || offset + i64::from(w) > i64::from(width - border))
        {
            unsafe { query::store(errors, GL_INVALID_VALUE) };
            return 0;
        }
        // Width zero does not distinguish an undefined array from a defined
        // empty image. The native copy call selects its actual GL error.
    }
    if one && image {
        gl!(
            a,
            dg_glCopyTexImage1D(
                words[0],
                level as i32,
                words[2],
                x,
                y,
                w as i32,
                words[6] as i32
            )
        );
    } else if one {
        gl!(
            a,
            dg_glCopyTexSubImage1D(words[0], level as i32, words[2] as i32, x, y, w as i32)
        );
    } else if image {
        gl!(
            a,
            dg_glCopyTexImage2D(
                words[0],
                level as i32,
                words[2],
                x,
                y,
                w as i32,
                h as i32,
                words[7] as i32
            )
        );
    } else {
        gl!(
            a,
            dg_glCopyTexSubImage2D(
                words[0],
                level as i32,
                words[2] as i32,
                words[3] as i32,
                x,
                y,
                w as i32,
                h as i32
            )
        );
    }
    let e = gl!(a, dg_glGetError());
    if e != 0 {
        unsafe { query::store(errors, e) };
        return 0;
    }
    let mut defined_width = w;
    let mut defined_height = h;
    let mut defined_border = 0;
    let mut defined_allocation = allocation;
    let mut defined_total = next;
    if image && (one || words[7] != 0) {
        let mut native_width = 0;
        let mut native_border = 0;
        let mut native_height = 1;
        gl!(
            a,
            dg_glGetTexLevelParameteriv(
                words[0],
                level as i32,
                GL_TEXTURE_WIDTH,
                &mut native_width
            )
        );
        gl!(
            a,
            dg_glGetTexLevelParameteriv(
                words[0],
                level as i32,
                GL_TEXTURE_BORDER,
                &mut native_border
            )
        );
        if !one {
            gl!(
                a,
                dg_glGetTexLevelParameteriv(
                    words[0],
                    level as i32,
                    GL_TEXTURE_HEIGHT,
                    &mut native_height
                )
            );
        }
        let error = gl!(a, dg_glGetError());
        if error != 0 {
            unsafe { query::store(errors, error) };
            return DG_GL_ERROR_TEXTURE;
        }
        if native_width < 0
            || native_width as u32 > w
            || !(0..=1).contains(&native_border)
            || native_width < 2 * native_border
            || native_height < 0
            || native_height as u32 > h
            || (!one && native_height < 2 * native_border)
        {
            return DG_GL_ERROR_HOST;
        }
        // Some native providers discard legacy border texels. Keep exact native
        // dimensions; do not publish requested dimensions the object lacks.
        defined_width = native_width as u32;
        defined_height = native_height as u32;
        defined_border = native_border as u32;
        defined_allocation = u64::from(defined_width) * u64::from(defined_height) * texel_bytes;
        defined_total = next - allocation + defined_allocation;
    }
    unsafe {
        dreamgpu_texture_written(api, t, serial);
        if image {
            f.levels[level] = defined_allocation;
            f.widths[level] = defined_width;
            f.heights[level] = defined_height;
            f.borders[level] = defined_border;
            f.undefined_levels &= !(1 << level);
            *total = defined_total;
            set_image_levels(t, words[0], &f);
        }
    }
    0
}
// Allocation remains a narrow host allocator ABI; the Rust owner always writes
// allocated bytes before exposing them to GL, independent of allocator zeroing.
pub(crate) struct OwnedBytes<'a> {
    memory: &'a Memory,
    pub(crate) p: *mut u8,
}
impl<'a> OwnedBytes<'a> {
    pub(crate) unsafe fn copied(m: &'a Memory, data: *const u8, bytes: usize) -> Option<Self> {
        let p = unsafe { (m.allocate)(m.opaque, bytes) }.cast::<u8>();
        if p.is_null() {
            return None;
        }
        unsafe { core::ptr::copy_nonoverlapping(data, p, bytes) };
        Some(Self { memory: m, p })
    }

    pub(crate) unsafe fn zeroed(m: &'a Memory, bytes: usize) -> Option<Self> {
        let p = unsafe { (m.allocate)(m.opaque, bytes) }.cast::<u8>();
        if p.is_null() {
            return None;
        }
        unsafe { core::ptr::write_bytes(p, 0, bytes) };
        Some(Self { memory: m, p })
    }
}
impl Drop for OwnedBytes<'_> {
    fn drop(&mut self) {
        unsafe { (self.memory.free)(self.memory.opaque, self.p.cast()) }
    }
}
struct ClearState<'a> {
    api: &'a DreamGpuGlApi,
    read: i32,
    draw: i32,
    color: [f32; 4],
    mask: [u8; 4],
    scissor: u8,
    framebuffer: u32,
}
impl Drop for ClearState<'_> {
    fn drop(&mut self) {
        let a = self.api;
        gl!(
            a,
            dg_glBindFramebuffer(GL_READ_FRAMEBUFFER, self.read as u32)
        );
        gl!(
            a,
            dg_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, self.draw as u32)
        );
        gl!(
            a,
            dg_glClearColor(self.color[0], self.color[1], self.color[2], self.color[3])
        );
        gl!(
            a,
            dg_glColorMask(self.mask[0], self.mask[1], self.mask[2], self.mask[3])
        );
        if self.scissor != 0 {
            gl!(a, dg_glEnable(GL_SCISSOR_TEST));
        }
        if self.framebuffer != 0 {
            gl!(a, dg_glDeleteFramebuffers(1, &self.framebuffer));
        }
    }
}
/// # Safety
/// Newly allocated texture is bound with canonical unpack state; dimensions
/// validated. Deleted-but-bound texture names must never be rebound/attached.
/// `target` names the defined image array: the texture's own or a cube face.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_zero(
    memory: *const Memory,
    t: *const Texture,
    target: u32,
    level: u32,
    w: u32,
    h: u32,
) -> u32 {
    let m = unsafe { &*memory };
    let a = unsafe { &*m.api };
    if unsafe { !owns(t, target) } {
        return GL_INVALID_OPERATION;
    }
    let one = unsafe { (*t).target } == GL_TEXTURE_1D;
    // A cube face is cleared and filled through its own face target, a
    // rectangle through TEXTURE_RECTANGLE.
    let target = if cube_face(target).is_some() || target == GL_TEXTURE_RECTANGLE_ARB {
        target
    } else {
        GL_TEXTURE_2D
    };
    if w == 0
        || h == 0
        || w > DG_GL_MAX_TEXTURE_DIMENSION + 2
        || h > DG_GL_MAX_TEXTURE_DIMENSION + 2
        || level > DG_GL_MAX_TEXTURE_LEVEL
    {
        return GL_INVALID_VALUE;
    }
    if one {
        let Some(get_level) = a.dg_glGetTexLevelParameteriv else {
            return GL_INVALID_OPERATION;
        };
        let mut border = 0;
        unsafe { get_level(GL_TEXTURE_1D, level as i32, GL_TEXTURE_BORDER, &mut border) };
        let error = gl!(a, dg_glGetError());
        if error != 0 {
            return error;
        }
        if !(0..=1).contains(&border) || w < 2 * border as u32 {
            return GL_INVALID_OPERATION;
        }
        let _transfer = match unsafe { crate::pixels::Neutral::new(a) } {
            Ok(v) => v,
            Err(_) => return GL_INVALID_OPERATION,
        };
        let Some(zero) = (unsafe { OwnedBytes::zeroed(m, w as usize * 4) }) else {
            return GL_OUT_OF_MEMORY;
        };
        // A depth image (ARB_depth_texture) is zeroed in its own format.
        let (format, kind) = match unsafe { depth_image(a, GL_TEXTURE_1D, level) } {
            Some(true) => (GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8),
            Some(false) => (GL_DEPTH_COMPONENT, GL_UNSIGNED_INT),
            None => (GL_RGBA, GL_UNSIGNED_BYTE),
        };
        gl!(
            a,
            dg_glTexSubImage1D(
                GL_TEXTURE_1D,
                level as i32,
                -border,
                w as i32,
                format,
                kind,
                zero.p.cast()
            )
        );
        return gl!(a, dg_glGetError());
    }
    let border = unsafe { image_levels(t, target).borders[level as usize] } as i32;
    if !(0..=1).contains(&border) || w < 2 * border as u32 || h < 2 * border as u32 {
        return GL_INVALID_OPERATION;
    }
    // A depth (or packed depth/stencil) image (EXT_packed_depth_stencil) is
    // zeroed through its own pixel format: four bytes per texel either way.
    let (format, kind) = match unsafe { depth_image(a, target, level) } {
        Some(stencil) if stencil => (GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8),
        Some(_) => (GL_DEPTH_COMPONENT, GL_UNSIGNED_INT),
        None => (GL_RGBA, GL_UNSIGNED_BYTE),
    };
    if format == GL_RGBA && border == 0 && unsafe { (*t).name != 0 && (*t).deleted == 0 } {
        let mut saved = ClearState {
            api: a,
            read: 0,
            draw: 0,
            color: [0.; 4],
            mask: [0; 4],
            scissor: gl!(a, dg_glIsEnabled(GL_SCISSOR_TEST)),
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
        gl!(
            a,
            dg_glGetFloatv(GL_COLOR_CLEAR_VALUE, saved.color.as_mut_ptr())
        );
        gl!(
            a,
            dg_glGetBooleanv(GL_COLOR_WRITEMASK, saved.mask.as_mut_ptr())
        );
        gl!(a, dg_glGenFramebuffers(1, &mut saved.framebuffer));
        if saved.framebuffer == 0 {
            let e = gl!(a, dg_glGetError());
            return if e != 0 { e } else { GL_OUT_OF_MEMORY };
        }
        gl!(a, dg_glBindFramebuffer(GL_FRAMEBUFFER, saved.framebuffer));
        gl!(
            a,
            dg_glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                target,
                (*t).name,
                level as i32
            )
        );
        let cleared =
            gl!(a, dg_glCheckFramebufferStatus(GL_FRAMEBUFFER)) == GL_FRAMEBUFFER_COMPLETE;
        if cleared {
            gl!(a, dg_glDisable(GL_SCISSOR_TEST));
            gl!(a, dg_glColorMask(1, 1, 1, 1));
            gl!(a, dg_glClearColor(0., 0., 0., 0.));
            gl!(a, dg_glClear(GL_COLOR_BUFFER_BIT));
        }
        let e = gl!(a, dg_glGetError());
        drop(saved);
        if e != 0 || cleared {
            return e;
        }
    }
    let _transfer = match unsafe { crate::pixels::Neutral::new(a) } {
        Ok(v) => v,
        Err(_) => return GL_INVALID_OPERATION,
    };
    // Legacy non-renderable internal formats use a <=64 KiB zero tile. This is
    // allocation initialization only, never CPU rendering or presentation.
    let row = w as usize * 4;
    // Bands start on 4-row block boundaries: compressed (S3TC) storage
    // rejects subimages that start or end inside a block.
    let rows = match (h as usize).min(65536 / row) {
        r if r < h as usize && r >= 4 => r & !3,
        r => r,
    };
    let Some(zero) = (unsafe { OwnedBytes::zeroed(m, rows * row) }) else {
        return GL_OUT_OF_MEMORY;
    };
    for y in (0..h).step_by(rows) {
        gl!(
            a,
            dg_glTexSubImage2D(
                target,
                level as i32,
                -border,
                y as i32 - border,
                w as i32,
                (rows as u32).min(h - y) as i32,
                format,
                kind,
                zero.p.cast()
            )
        );
        let e = gl!(a, dg_glGetError());
        if e != 0 {
            return e;
        }
    }
    0
}
/// Whether the bound image of `target` at `level` is a depth image, and if so
/// whether it has stencil: Some(stencil). Only 1D, 2D and rectangle images
/// can be depth images.
unsafe fn depth_image(a: &DreamGpuGlApi, target: u32, level: u32) -> Option<bool> {
    if !matches!(
        target,
        GL_TEXTURE_1D | GL_TEXTURE_2D | GL_TEXTURE_RECTANGLE_ARB
    ) {
        return None;
    }
    let get = a.dg_glGetTexLevelParameteriv?;
    let (mut depth, mut stencil) = (0, 0);
    unsafe {
        get(target, level as i32, GL_TEXTURE_DEPTH_SIZE, &mut depth);
        if a.dg_glGetError.expect("complete API")() != GL_NO_ERROR || depth <= 0 {
            return None;
        }
        get(target, level as i32, GL_TEXTURE_STENCIL_SIZE, &mut stencil);
        let error = a.dg_glGetError.expect("complete API")();
        Some(error == GL_NO_ERROR && stencil > 0)
    }
}
#[repr(C)]
pub struct ReadCache {
    pub texture: *mut Texture,
    pub version: u64,
    pub level: u32,
    pub bytes: u32,
    pub pixels: *mut u8,
}
impl ReadCache {
    const EMPTY: Self = Self {
        texture: null_mut(),
        version: 0,
        level: 0,
        bytes: 0,
        pixels: null_mut(),
    };
}
/// # Safety
/// Cache belongs to the render worker; callbacks cannot reenter cache ownership.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_read_cache_release(memory: *const Memory, cache: *mut ReadCache) {
    let old = unsafe { cache.read() };
    unsafe { cache.write(ReadCache::EMPTY) };
    if !old.pixels.is_null() {
        let m = unsafe { &*memory };
        unsafe { (m.free)(m.opaque, old.pixels.cast()) };
    }
}
/// # Safety
/// Same ownership as release; forgotten texture is only compared as weak identity.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_read_cache_forget(
    memory: *const Memory,
    cache: *mut ReadCache,
    t: *const Texture,
) {
    if unsafe { (*cache).texture.cast_const() == t } {
        unsafe { dreamgpu_read_cache_release(memory, cache) }
    }
}
/// # Safety
/// Bound current texture, cache and error field are disjoint render-owned state;
/// result is writable capacity bytes and cannot alias cache allocation/state.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_read(
    memory: *const Memory,
    cache: *mut ReadCache,
    t: *mut Texture,
    errors: *mut u32,
    serial: u64,
    target: u32,
    level: u32,
    first: u32,
    capacity: u32,
    result: *mut u8,
) -> u32 {
    let m = unsafe { &*memory };
    let a = unsafe { &*m.api };
    if t.is_null() || unsafe { !owns(t, target) } {
        return DG_GL_ERROR_TEXTURE;
    }
    // One cube face is a separate image: it has its own cache identity.
    let encoded_level = level | (cube_face(target).map_or(0, |f| f as u32 + 1) << 16);
    let compressed = level & crate::texture::READ_COMPRESSED != 0;
    // DEPTH (ARB_depth_texture) reads float32 depth, or with FLOAT the packed
    // UNSIGNED_INT_24_8 words of a DEPTH_STENCIL level: four bytes either way.
    let depth = level & DG_GL_TEXTURE_READ_DEPTH != 0;
    let (format, kind, pixel_bytes) = match (depth, level & DG_GL_TEXTURE_READ_FLOAT != 0) {
        (true, true) => (GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4),
        (true, false) => (GL_DEPTH_COMPONENT, GL_FLOAT, 4),
        (false, true) => (GL_RGBA, GL_FLOAT, 16),
        (false, false) => (GL_RGBA, GL_UNSIGNED_BYTE, 4),
    };
    let level = level
        & !(DG_GL_TEXTURE_READ_FLOAT | DG_GL_TEXTURE_READ_DEPTH | crate::texture::READ_COMPRESSED);
    if level > DG_GL_MAX_TEXTURE_LEVEL
        || result.is_null()
        || (compressed && (pixel_bytes != 4 || depth))
        || (depth && !crate::gl_validation::depth_target(target))
    {
        return DG_GL_ERROR_TEXTURE;
    }
    let level = level as usize;
    let f = unsafe { image_levels(t, target) };
    // A 3D level is read through all of its images (EXT_texture3D).
    let volume = target == GL_TEXTURE_3D;
    let images = if volume {
        unsafe { (*t).depths[level] }
    } else {
        1
    };
    let bytes = if compressed {
        // The level's own compressed size, from the native image. `first`
        // and the cache then count 32-bit words of that image. Pending guest
        // errors are drained first so these probes never consume them.
        if let Err(e) = unsafe { query::remember(a, errors) } {
            return e;
        }
        let mut is_compressed = 0;
        let mut size = 0;
        gl!(
            a,
            dg_glGetTexLevelParameteriv(
                target,
                level as i32,
                GL_TEXTURE_COMPRESSED,
                &mut is_compressed
            )
        );
        gl!(
            a,
            dg_glGetTexLevelParameteriv(
                target,
                level as i32,
                GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                &mut size
            )
        );
        let e = gl!(a, dg_glGetError());
        if e != 0 || is_compressed == 0 || size <= 0 || size % 4 != 0 {
            return DG_GL_ERROR_TEXTURE;
        }
        size as u64
    } else {
        u64::from(f.widths[level])
            * u64::from(f.heights[level])
            * u64::from(images)
            * u64::from(pixel_bytes)
    };
    if bytes == 0
        || u64::from(first) * u64::from(pixel_bytes) >= bytes
        || f.undefined_levels & (1 << level) != 0
    {
        return DG_GL_ERROR_TEXTURE;
    }
    // Cache length and offsets are u32 even on a 64-bit host.
    let largest = if volume {
        u64::from(DG_GL_MAX_3D_TEXTURE_SIZE).pow(3)
    } else {
        u64::from(DG_GL_MAX_TEXTURE_DIMENSION + 2) * u64::from(DG_GL_MAX_TEXTURE_DIMENSION + 2)
    };
    if bytes > u64::from(u32::MAX) || bytes > largest * u64::from(pixel_bytes) {
        return DG_GL_ERROR_LIMIT;
    }
    let reload = unsafe {
        first == 0
            || (*cache).texture != t
            || (*cache).version != (*t).version
            || (*cache).level != encoded_level
            || u64::from((*cache).bytes) != bytes
    };
    if reload {
        unsafe { dreamgpu_read_cache_release(memory, cache) };
        let Some(pixels) = (unsafe { OwnedBytes::zeroed(m, bytes as usize) }) else {
            return DG_GL_ERROR_LIMIT;
        };
        unsafe {
            dreamgpu_texture_wait(m.api, t, serial);
            if let Err(e) = query::remember(a, errors) {
                return e;
            }
        }
        let pack = match unsafe { query::Pack::new(a) } {
            Ok(p) => p,
            Err(e) => return e,
        };
        if compressed {
            gl!(
                a,
                dg_glGetCompressedTexImage(target, level as i32, pixels.p.cast())
            );
        } else {
            gl!(
                a,
                dg_glGetTexImage(target, level as i32, format, kind, pixels.p.cast())
            );
        }
        let e = gl!(a, dg_glGetError());
        drop(pack);
        if e != 0 {
            unsafe { query::store(errors, e) };
            return DG_GL_ERROR_HOST;
        }
        unsafe {
            cache.write(ReadCache {
                texture: t,
                version: (*t).version,
                level: encoded_level,
                bytes: bytes as u32,
                pixels: pixels.p,
            })
        };
        core::mem::forget(pixels);
    }
    let remaining = unsafe { (*cache).bytes } - first * pixel_bytes;
    unsafe {
        core::ptr::write_bytes(result, 0, capacity as usize);
        core::ptr::copy_nonoverlapping(
            (*cache).pixels.add(first as usize * pixel_bytes as usize),
            result,
            remaining.min(capacity) as usize,
        );
    }
    if remaining <= capacity {
        unsafe { dreamgpu_read_cache_release(memory, cache) }
    }
    0
}
#[cfg(test)]
mod tests;
