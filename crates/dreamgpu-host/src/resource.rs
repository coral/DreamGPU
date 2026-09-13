// SPDX-License-Identifier: GPL-2.0-or-later
//! Internal framebuffer storage and guest texture copy/clear/read resources.
//! All objects belong to the render worker; native contexts are made current by
//! the OS adapter. No guest VRAM reference or exported-image ownership is here.
#![allow(non_upper_case_globals)]
use crate::{
    gl_api::*,
    query,
    state::{dreamgpu_context_select_buffers, ContextState},
    texture::{dreamgpu_texture_wait, dreamgpu_texture_written, names::Memory, Texture},
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
/// Current render context owns framebuffer/initialized; logical state immutable.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_drawable_bind(
    api: *const DreamGpuGlApi,
    d: *mut Drawable,
    framebuffer: *mut u32,
    initialized: *mut u32,
    state: *const ContextState,
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
    }
    0
}
/// # Safety
/// Render-owned drawable/current framebuffer. Exchange changes names, not pixels.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_drawable_exchange(
    api: *const DreamGpuGlApi,
    d: *mut Drawable,
    framebuffer: u32,
    state: *const ContextState,
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
    let level = words[1] as usize;
    let x = words[if image || one { 3 } else { 4 }] as i32;
    let y = words[if image || one { 4 } else { 5 }] as i32;
    let w = words[if image || one { 5 } else { 6 }];
    let h = if one {
        1
    } else {
        words[if image { 6 } else { 7 }]
    };
    // Full GL1.1 CopyImage1D formats include RGBA16: conservatively charge eight bytes.
    let allocation = u64::from(w) * u64::from(h) * if one { 8 } else { 4 };
    if has_drawable == 0 {
        return DG_GL_ERROR_TEXTURE;
    }
    let next = unsafe {
        if !image
            && !one
            && (words[2] > (*t).widths[level]
                || words[3] > (*t).heights[level]
                || w > (*t).widths[level] - words[2]
                || h > (*t).heights[level] - words[3])
        {
            return DG_GL_ERROR_TEXTURE;
        }
        if image {
            match (*total)
                .checked_sub((*t).levels[level])
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
                0
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
        return if one { 0 } else { DG_GL_ERROR_TEXTURE };
    }
    let mut defined_width = w;
    let mut defined_allocation = allocation;
    let mut defined_total = next;
    if one && image {
        let mut native_width = 0;
        let mut native_border = 0;
        gl!(
            a,
            dg_glGetTexLevelParameteriv(
                GL_TEXTURE_1D,
                level as i32,
                GL_TEXTURE_WIDTH,
                &mut native_width
            )
        );
        gl!(
            a,
            dg_glGetTexLevelParameteriv(
                GL_TEXTURE_1D,
                level as i32,
                GL_TEXTURE_BORDER,
                &mut native_border
            )
        );
        let error = gl!(a, dg_glGetError());
        if error != 0 {
            unsafe { query::store(errors, error) };
            return DG_GL_ERROR_TEXTURE;
        }
        if native_width < 0
            || native_width as u32 > w
            || !(0..=1).contains(&native_border)
            || native_width < 2 * native_border
        {
            return DG_GL_ERROR_HOST;
        }
        // Some native providers discard legacy border texels. Keep exact native
        // dimensions; do not publish requested dimensions the object lacks.
        defined_width = native_width as u32;
        defined_allocation = u64::from(defined_width) * 8;
        defined_total = next - allocation + defined_allocation;
    }
    unsafe {
        dreamgpu_texture_written(api, t, serial);
        if image {
            *total = defined_total;
            (*t).levels[level] = defined_allocation;
            (*t).widths[level] = defined_width;
            (*t).heights[level] = h;
            (*t).undefined_levels &= !(1 << level);
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

    unsafe fn zeroed(m: &'a Memory, bytes: usize) -> Option<Self> {
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
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_zero(
    memory: *const Memory,
    t: *const Texture,
    level: u32,
    w: u32,
    h: u32,
) -> u32 {
    let m = unsafe { &*memory };
    let a = unsafe { &*m.api };
    let one = unsafe { (*t).target } == GL_TEXTURE_1D;
    if w == 0
        || h == 0
        || w > DG_GL_MAX_TEXTURE_DIMENSION + if one { 2 } else { 0 }
        || h > DG_GL_MAX_TEXTURE_DIMENSION
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
        gl!(
            a,
            dg_glTexSubImage1D(
                GL_TEXTURE_1D,
                level as i32,
                -border,
                w as i32,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                zero.p.cast()
            )
        );
        return gl!(a, dg_glGetError());
    }
    if unsafe { (*t).name != 0 && (*t).deleted == 0 } {
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
                GL_TEXTURE_2D,
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
    let rows = (h as usize).min(65536 / row);
    let Some(zero) = (unsafe { OwnedBytes::zeroed(m, rows * row) }) else {
        return GL_OUT_OF_MEMORY;
    };
    for y in (0..h).step_by(rows) {
        gl!(
            a,
            dg_glTexSubImage2D(
                GL_TEXTURE_2D,
                level as i32,
                0,
                y as i32,
                w as i32,
                (rows as u32).min(h - y) as i32,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
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
    if level > DG_GL_MAX_TEXTURE_LEVEL || result.is_null() {
        return DG_GL_ERROR_TEXTURE;
    }
    let level = level as usize;
    let bytes = unsafe { u64::from((*t).widths[level]) * u64::from((*t).heights[level]) * 4 };
    if bytes == 0
        || u64::from(first) * 4 >= bytes
        || unsafe { (*t).undefined_levels } & (1 << level) != 0
    {
        return DG_GL_ERROR_TEXTURE;
    }
    if bytes > u64::from(DG_GL_MAX_TEXTURE_DIMENSION) * u64::from(DG_GL_MAX_TEXTURE_DIMENSION) * 4 {
        return DG_GL_ERROR_LIMIT;
    }
    let reload = unsafe {
        first == 0
            || (*cache).texture != t
            || (*cache).version != (*t).version
            || (*cache).level != level as u32
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
        gl!(
            a,
            dg_glGetTexImage(
                target,
                level as i32,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.p.cast()
            )
        );
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
                level: level as u32,
                bytes: bytes as u32,
                pixels: pixels.p,
            })
        };
        core::mem::forget(pixels);
    }
    let remaining = unsafe { (*cache).bytes } - first * 4;
    unsafe {
        core::ptr::write_bytes(result, 0, capacity as usize);
        core::ptr::copy_nonoverlapping(
            (*cache).pixels.add(first as usize * 4),
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
