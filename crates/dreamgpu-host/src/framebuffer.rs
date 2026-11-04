// SPDX-License-Identifier: GPL-2.0-or-later
//! EXT_framebuffer_object with EXT_framebuffer_blit and the depth attachments
//! of EXT_packed_depth_stencil.
//!
//! Guest framebuffer names are virtual. Each context owns a bounded table of
//! framebuffer objects, each with a private native framebuffer created in that
//! context's native context; name zero is the window, whose native framebuffer
//! is the drawable framebuffer the platform binds on every make-current
//! (`ContextState::window_framebuffer`). The draw and read bindings are host
//! state: whenever the platform rebinds the drawable it calls back here, so the
//! native bindings always equal the guest's. Renderbuffer names and objects
//! live in the share group's table beside the textures; every object has a
//! private native renderbuffer whose storage is zero-initialized and charged
//! against the resource budget.
//!
//! Attachments hold a reference to their texture or renderbuffer, so an image
//! deleted while attached to a framebuffer that is not bound stays alive until
//! it is detached, exactly like the native object behind it. Rendering into an
//! attachment is fenced like any other write once the context stops drawing
//! into it (rebinding, detaching, deleting or switching contexts), and every
//! read of a framebuffer object refuses images the host never initialized.
#![allow(non_upper_case_globals)]
use crate::{
    buffer::{charge, uncharge},
    gl_api::*,
    query,
    state::ContextState,
    texture::{
        cube_face, dreamgpu_texture_wait, dreamgpu_texture_written, image_levels,
        names::{dreamgpu_texture_lookup, dreamgpu_texture_unref, Memory},
        set_image_levels, Texture,
    },
};
use core::{
    ffi::c_void,
    ptr::{addr_of_mut, null_mut},
};

/// COLOR_ATTACHMENT0..n, then DEPTH and STENCIL.
pub(crate) const ATTACHMENTS: usize = DG_GL_MAX_COLOR_ATTACHMENTS as usize + 2;
const DEPTH: usize = ATTACHMENTS - 2;
const STENCIL: usize = ATTACHMENTS - 1;
const FRAMEBUFFER_SLOTS: usize = 2 * DG_GL_MAX_FRAMEBUFFERS as usize;
const RENDERBUFFER_SLOTS: usize = 2 * DG_GL_MAX_RENDERBUFFERS as usize;
const _: () = assert!(FRAMEBUFFER_SLOTS.is_power_of_two() && RENDERBUFFER_SLOTS.is_power_of_two());
/// Nominal native bookkeeping of one object, charged with its storage.
pub(crate) const OBJECT_BYTES: u64 = 256;
/// FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE of a texture attachment.
const TEXTURE: u32 = 0x1702;

/// One renderbuffer object of a share group.
#[repr(C)]
pub struct Renderbuffer {
    pub native: u32,
    pub guest_name: u32,
    /// The namespace's, the binding's and every attachment's reference.
    pub refs: u32,
    /// The guest deleted the name; the native object is already deleted and
    /// lives on only while attached.
    pub deleted: u32,
    /// The internal format of the storage, zero before any.
    pub format: u32,
    pub width: u32,
    pub height: u32,
    /// Zero when native storage exists that the host could not initialize.
    pub defined: u32,
    /// Storage bytes charged against the resource budget.
    pub bytes: u64,
    pub last_write: *mut c_void,
    pub writer_serial: u64,
    pub waiter_serial: u64,
}

/// What one attachment point holds: a texture image, a renderbuffer, or
/// neither (both null).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Attachment {
    pub texture: *mut Texture,
    pub renderbuffer: *mut Renderbuffer,
    /// The image target: TEXTURE_1D, TEXTURE_2D, a cube face or TEXTURE_3D.
    pub target: u32,
    pub level: u32,
    pub zoffset: u32,
}
const DETACHED: Attachment = Attachment {
    texture: null_mut(),
    renderbuffer: null_mut(),
    target: 0,
    level: 0,
    zoffset: 0,
};

/// One framebuffer object of a context, with its own draw and read buffers.
#[repr(C)]
pub struct Framebuffer {
    pub name: u32,
    pub native: u32,
    pub draw_buffer: u32,
    pub read_buffer: u32,
    pub attachments: [Attachment; ATTACHMENTS],
}

#[repr(C)]
struct Slot<T> {
    name: u32,
    object: *mut T,
}
impl<T> Clone for Slot<T> {
    fn clone(&self) -> Self {
        *self
    }
}
impl<T> Copy for Slot<T> {}

/// A bounded open-addressing name table, never more than half full.
#[repr(C)]
pub struct Table<T, const N: usize> {
    count: u32,
    slots: [Slot<T>; N],
}
pub type Framebuffers = Table<Framebuffer, FRAMEBUFFER_SLOTS>;
pub type Renderbuffers = Table<Renderbuffer, RENDERBUFFER_SLOTS>;

impl<T, const N: usize> Table<T, N> {
    const LIMIT: u32 = (N / 2) as u32;
    fn hash(name: u32) -> usize {
        (name.wrapping_mul(0x9e3779b1) as usize) & (N - 1)
    }
    unsafe fn locate(table: *const Self, name: u32) -> Result<usize, usize> {
        for n in 0..N {
            let i = (Self::hash(name) + n) & (N - 1);
            let key = unsafe { (*table).slots[i].name };
            if key == name {
                return Ok(i);
            }
            if key == 0 {
                return Err(i);
            }
        }
        unreachable!("half-full bounded name table")
    }
    unsafe fn find(table: *const Self, name: u32) -> *mut T {
        if table.is_null() || name == 0 {
            return null_mut();
        }
        match unsafe { Self::locate(table, name) } {
            Ok(i) => unsafe { (*table).slots[i].object },
            Err(_) => null_mut(),
        }
    }
    /// Backward-shift deletion keeps probe chains intact without tombstones.
    unsafe fn remove(table: *mut Self, index: usize) {
        unsafe {
            (*table).count -= 1;
            (*table).slots[index] = Slot {
                name: 0,
                object: null_mut(),
            };
        }
        let mut hole = index;
        for step in 1..N {
            let at = (index + step) & (N - 1);
            let e = unsafe { (*table).slots[at] };
            if e.name == 0 {
                break;
            }
            let origin = Self::hash(e.name);
            if hole.wrapping_sub(origin) & (N - 1) < at.wrapping_sub(origin) & (N - 1) {
                unsafe {
                    (*table).slots[hole] = e;
                    (*table).slots[at] = Slot {
                        name: 0,
                        object: null_mut(),
                    };
                }
                hole = at;
            }
        }
    }
    fn charge_bytes() -> u64 {
        core::mem::size_of::<Self>() as u64
    }
    /// The table in `slot`, created on first use; null when the allocation or
    /// its budget charge fails.
    unsafe fn ensure(m: &Memory, slot: *mut *mut Self) -> *mut Self {
        let current = unsafe { *slot };
        if !current.is_null() || !unsafe { charge(m, Self::charge_bytes()) } {
            return current;
        }
        let table = unsafe { (m.allocate)(m.opaque, core::mem::size_of::<Self>()) }.cast::<Self>();
        if table.is_null() {
            unsafe { uncharge(m, Self::charge_bytes()) };
            return table;
        }
        unsafe {
            core::ptr::write_bytes(table, 0, 1);
            *slot = table;
        }
        table
    }
}

macro_rules! gl {($api:expr, $name:ident($($arg:expr),* $(,)?)) => {{#[allow(unused_unsafe)] let result = unsafe { $api.$name.expect("complete native GL table")($($arg),*) }; result }};}

unsafe fn error(c: *mut ContextState, code: u32) {
    unsafe { query::store(addr_of_mut!((*c).guest_errors), code) };
}
unsafe fn remember(api: &DreamGpuGlApi, c: *mut ContextState) -> Result<(), u32> {
    unsafe { query::remember(api, addr_of_mut!((*c).guest_errors)) }
}
fn word(args: &[u8], n: usize) -> u32 {
    u32::from_le_bytes(args[n * 4..n * 4 + 4].try_into().unwrap())
}

/// Every native entry point any framebuffer operation uses, checked before a
/// record changes anything.
fn complete(api: &DreamGpuGlApi) -> bool {
    api.dg_glGenFramebuffers.is_some()
        && api.dg_glDeleteFramebuffers.is_some()
        && api.dg_glBindFramebuffer.is_some()
        && api.dg_glFramebufferTexture1D.is_some()
        && api.dg_glFramebufferTexture2D.is_some()
        && api.dg_glFramebufferTexture3D.is_some()
        && api.dg_glFramebufferRenderbuffer.is_some()
        && api.dg_glCheckFramebufferStatus.is_some()
        && api.dg_glGenRenderbuffers.is_some()
        && api.dg_glDeleteRenderbuffers.is_some()
        && api.dg_glBindRenderbuffer.is_some()
        && api.dg_glRenderbufferStorage.is_some()
        && api.dg_glGetRenderbufferParameteriv.is_some()
        && api.dg_glBlitFramebuffer.is_some()
        && api.dg_glGenerateMipmap.is_some()
        && api.dg_glGetError.is_some()
        && api.dg_glGetIntegerv.is_some()
        && api.dg_glGetFloatv.is_some()
        && api.dg_glGetDoublev.is_some()
        && api.dg_glGetBooleanv.is_some()
        && api.dg_glIsEnabled.is_some()
        && api.dg_glEnable.is_some()
        && api.dg_glDisable.is_some()
        && api.dg_glClear.is_some()
        && api.dg_glClearColor.is_some()
        && api.dg_glClearDepth.is_some()
        && api.dg_glClearStencil.is_some()
        && api.dg_glColorMask.is_some()
        && api.dg_glDepthMask.is_some()
        && api.dg_glStencilMask.is_some()
        && api.dg_glCopyPixels.is_some()
        && api.dg_glGetTexParameteriv.is_some()
        && api.dg_glGetTexLevelParameteriv.is_some()
        && api.dg_glWaitSync.is_some()
        && api.dg_glFenceSync.is_some()
        && api.dg_glDeleteSync.is_some()
}

/// Framebuffer binding targets.
pub(crate) fn framebuffer_target(target: u32) -> bool {
    matches!(
        target,
        GL_FRAMEBUFFER | GL_DRAW_FRAMEBUFFER | GL_READ_FRAMEBUFFER
    )
}
/// The attachment slot of an attachment point.
pub(crate) fn slot(attachment: u32) -> Option<usize> {
    match attachment {
        GL_DEPTH_ATTACHMENT => Some(DEPTH),
        GL_STENCIL_ATTACHMENT => Some(STENCIL),
        a => {
            let n = a.wrapping_sub(GL_COLOR_ATTACHMENT0);
            (n < DG_GL_MAX_COLOR_ATTACHMENTS).then_some(n as usize)
        }
    }
}
/// A colour attachment point, as a draw or read buffer.
pub(crate) fn color_attachment(buffer: u32) -> bool {
    buffer.wrapping_sub(GL_COLOR_ATTACHMENT0) < DG_GL_MAX_COLOR_ATTACHMENTS
}
/// Renderbuffer storage formats of EXT_framebuffer_object and
/// EXT_packed_depth_stencil, with their charged bytes per pixel.
pub(crate) fn renderbuffer_bytes(format: u32) -> u64 {
    match format {
        GL_RGB10 | GL_RGB12 | GL_RGB16 | GL_RGBA12 | GL_RGBA16 => 8,
        GL_RGB | GL_RGBA | GL_R3_G3_B2 | GL_RGB4 | GL_RGB5 | GL_RGB8 | GL_RGBA2 | GL_RGBA4
        | GL_RGB5_A1 | GL_RGBA8 | GL_RGB10_A2 | GL_DEPTH_COMPONENT | GL_DEPTH_COMPONENT16
        | GL_DEPTH_COMPONENT24 | GL_DEPTH_COMPONENT32 | GL_STENCIL_INDEX | GL_STENCIL_INDEX1
        | GL_STENCIL_INDEX4 | GL_STENCIL_INDEX8 | GL_STENCIL_INDEX16 | GL_DEPTH_STENCIL
        | GL_DEPTH24_STENCIL8 => 4,
        _ => 0,
    }
}
fn depth_format(format: u32) -> bool {
    matches!(
        format,
        GL_DEPTH_COMPONENT | GL_DEPTH_COMPONENT16 | GL_DEPTH_COMPONENT24 | GL_DEPTH_COMPONENT32
    )
}
fn stencil_format(format: u32) -> bool {
    matches!(
        format,
        GL_STENCIL_INDEX
            | GL_STENCIL_INDEX1
            | GL_STENCIL_INDEX4
            | GL_STENCIL_INDEX8
            | GL_STENCIL_INDEX16
    )
}
fn packed_format(format: u32) -> bool {
    matches!(format, GL_DEPTH_STENCIL | GL_DEPTH24_STENCIL8)
}
/// The 2D depth texture internal formats this capability adds.
pub(crate) fn depth_texture_format(format: u32) -> bool {
    depth_format(format) || packed_format(format)
}
/// Bytes per pixel of a depth image on the wire (TexImage2D/TexSubImage2D of
/// TEXTURE_2D, DrawPixels), zero for anything else.
pub(crate) fn depth_pixel_bytes(format: u32, kind: u32) -> u32 {
    match (format, kind) {
        (GL_DEPTH_COMPONENT, GL_BYTE | GL_UNSIGNED_BYTE) => 1,
        (GL_DEPTH_COMPONENT, GL_SHORT | GL_UNSIGNED_SHORT) => 2,
        (GL_DEPTH_COMPONENT, GL_INT | GL_UNSIGNED_INT | GL_FLOAT) => 4,
        (GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8) => 4,
        _ => 0,
    }
}

/// The record shape of the framebuffer CALL records. Everything that depends
/// on state is checked at execution and reported as a GL error.
pub(crate) fn call_shape(function: u32, a: &[u32; 10]) -> bool {
    match function {
        FEnum_glBindFramebuffer => framebuffer_target(a[0]),
        FEnum_glBindRenderbuffer => a[0] == GL_RENDERBUFFER,
        FEnum_glRenderbufferStorage => {
            a[0] == GL_RENDERBUFFER
                && renderbuffer_bytes(a[1]) != 0
                && a[2] <= DG_GL_MAX_RENDERBUFFER_SIZE
                && a[3] <= DG_GL_MAX_RENDERBUFFER_SIZE
        }
        FEnum_glFramebufferTexture1D
        | FEnum_glFramebufferTexture2D
        | FEnum_glFramebufferTexture3D => {
            let textarget = match function {
                FEnum_glFramebufferTexture1D => a[2] == GL_TEXTURE_1D,
                FEnum_glFramebufferTexture2D => a[2] == GL_TEXTURE_2D || cube_face(a[2]).is_some(),
                _ => a[2] == GL_TEXTURE_3D && a[5] < DG_GL_MAX_3D_TEXTURE_SIZE,
            };
            framebuffer_target(a[0])
                && slot(a[1]).is_some()
                && textarget
                && a[4] <= DG_GL_MAX_TEXTURE_LEVEL
        }
        FEnum_glFramebufferRenderbuffer => {
            framebuffer_target(a[0]) && slot(a[1]).is_some() && a[2] == GL_RENDERBUFFER
        }
        FEnum_glGenerateMipmap => matches!(
            a[0],
            GL_TEXTURE_1D | GL_TEXTURE_2D | GL_TEXTURE_CUBE_MAP | GL_TEXTURE_3D
        ),
        FEnum_glBlitFramebuffer => {
            let mask = a[8];
            mask & !(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT) == 0
                && (a[9] == GL_NEAREST
                    || (a[9] == GL_LINEAR
                        && mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT) == 0))
        }
        _ => false,
    }
}
/// GetFramebufferAttachmentParameteriv names.
pub(crate) fn attachment_parameter(pname: u32) -> bool {
    matches!(
        pname,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE
            | GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME
            | GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL
            | GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE
            | GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT
    )
}
/// GetRenderbufferParameteriv names.
pub(crate) fn renderbuffer_parameter(pname: u32) -> bool {
    matches!(
        pname,
        GL_RENDERBUFFER_WIDTH
            | GL_RENDERBUFFER_HEIGHT
            | GL_RENDERBUFFER_INTERNAL_FORMAT
            | GL_RENDERBUFFER_RED_SIZE
            | GL_RENDERBUFFER_GREEN_SIZE
            | GL_RENDERBUFFER_BLUE_SIZE
            | GL_RENDERBUFFER_ALPHA_SIZE
            | GL_RENDERBUFFER_DEPTH_SIZE
            | GL_RENDERBUFFER_STENCIL_SIZE
    )
}

/// The native framebuffer of a binding: an object's, or the window's.
///
/// # Safety
/// Read-only render-owned state; a non-null `fb` is one of its objects.
unsafe fn native(c: *const ContextState, fb: *const Framebuffer) -> u32 {
    if fb.is_null() {
        unsafe { (*c).window_framebuffer }
    } else {
        unsafe { (*fb).native }
    }
}
fn name(fb: *const Framebuffer) -> u32 {
    if fb.is_null() {
        0
    } else {
        unsafe { (*fb).name }
    }
}
/// The guest names of the draw and read bindings (zero: the window), saved
/// with each attribute stack entry.
///
/// # Safety
/// Read-only render-owned state.
pub(crate) unsafe fn binding_names(c: *const ContextState) -> [u32; 2] {
    unsafe { [name((*c).draw_framebuffer), name((*c).read_framebuffer)] }
}
/// The guest name of the renderbuffer binding.
///
/// # Safety
/// Read-only render-owned state.
pub(crate) unsafe fn renderbuffer_name(c: *const ContextState) -> u32 {
    let rb = unsafe { (*c).renderbuffer };
    if rb.is_null() {
        0
    } else {
        unsafe { (*rb).guest_name }
    }
}
/// The framebuffer an attachment or status command of `target` addresses.
unsafe fn bound(c: *const ContextState, target: u32) -> *mut Framebuffer {
    unsafe {
        if target == GL_READ_FRAMEBUFFER {
            (*c).read_framebuffer
        } else {
            (*c).draw_framebuffer
        }
    }
}

unsafe fn rb_wait(api: &DreamGpuGlApi, rb: *mut Renderbuffer, serial: u64) {
    unsafe {
        if !(*rb).last_write.is_null()
            && (*rb).writer_serial != serial
            && (*rb).waiter_serial != serial
        {
            gl!(api, dg_glWaitSync((*rb).last_write.cast(), 0, u64::MAX));
            (*rb).waiter_serial = serial;
        }
    }
}
unsafe fn rb_written(api: &DreamGpuGlApi, rb: *mut Renderbuffer, serial: u64) {
    unsafe {
        (*rb).writer_serial = serial;
        (*rb).waiter_serial = 0;
        if !(*rb).last_write.is_null() {
            gl!(api, dg_glDeleteSync((*rb).last_write.cast()));
        }
        (*rb).last_write = gl!(api, dg_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)).cast();
    }
}
/// Drop one reference; the last one releases the native object (unless the
/// guest already deleted it), its fence, its charge and the object.
unsafe fn rb_unref(m: &Memory, rb: *mut Renderbuffer) {
    if rb.is_null() {
        return;
    }
    unsafe {
        assert!((*rb).refs != 0, "renderbuffer reference underflow");
        (*rb).refs -= 1;
        if (*rb).refs != 0 {
            return;
        }
        let api = &*m.api;
        if (*rb).deleted == 0 && (*rb).native != 0 {
            gl!(api, dg_glDeleteRenderbuffers(1, &(*rb).native));
        }
        if !(*rb).last_write.is_null() {
            gl!(api, dg_glDeleteSync((*rb).last_write.cast()));
        }
        uncharge(m, (*rb).bytes + OBJECT_BYTES);
        (m.free)(m.opaque, rb.cast());
    }
}
fn add_ref(refs: &mut u32) {
    *refs = refs.checked_add(1).expect("attachment reference overflow");
}
unsafe fn hold(a: &Attachment) {
    unsafe {
        if !a.texture.is_null() {
            add_ref(&mut (*a.texture).refs);
        }
        if !a.renderbuffer.is_null() {
            add_ref(&mut (*a.renderbuffer).refs);
        }
    }
}
unsafe fn release(m: &Memory, a: Attachment) {
    unsafe {
        dreamgpu_texture_unref(m, a.texture);
        rb_unref(m, a.renderbuffer);
    }
}
/// Order later readers after what was rendered into an attachment.
unsafe fn fence(api: &DreamGpuGlApi, a: &Attachment, serial: u64) {
    unsafe {
        if !a.texture.is_null() {
            dreamgpu_texture_written(api, a.texture, serial);
        }
        if !a.renderbuffer.is_null() {
            rb_written(api, a.renderbuffer, serial);
        }
    }
}
/// Order this context after other contexts' writes of an attachment.
unsafe fn order(api: &DreamGpuGlApi, a: &Attachment, serial: u64) {
    unsafe {
        if !a.texture.is_null() {
            dreamgpu_texture_wait(api, a.texture, serial);
        }
        if !a.renderbuffer.is_null() {
            rb_wait(api, a.renderbuffer, serial);
        }
    }
}
unsafe fn order_all(api: &DreamGpuGlApi, fb: *const Framebuffer, serial: u64) {
    if fb.is_null() {
        return;
    }
    for a in unsafe { (*fb).attachments } {
        unsafe { order(api, &a, serial) };
    }
}
/// Fence every attachment of the draw framebuffer object: called whenever
/// the context stops drawing into it.
unsafe fn settle_draw(api: &DreamGpuGlApi, c: *const ContextState, serial: u64) {
    let fb = unsafe { (*c).draw_framebuffer };
    if fb.is_null() {
        return;
    }
    for a in unsafe { (*fb).attachments } {
        unsafe { fence(api, &a, serial) };
    }
}
/// Replace an attachment slot's tracking; the old image is fenced first when
/// the framebuffer is the draw binding.
unsafe fn replace(
    m: &Memory,
    c: *const ContextState,
    fb: *mut Framebuffer,
    index: usize,
    new: Attachment,
    serial: u64,
) {
    unsafe {
        let old = (*fb).attachments[index];
        if fb == (*c).draw_framebuffer {
            fence(&*m.api, &old, serial);
        }
        hold(&new);
        (*fb).attachments[index] = new;
        release(m, old);
    }
}

/// Whether every image of a framebuffer object holds host-initialized
/// contents; the window always does.
///
/// # Safety
/// Read-only render-owned objects.
unsafe fn initialized(fb: *const Framebuffer) -> bool {
    if fb.is_null() {
        return true;
    }
    unsafe { (*fb).attachments }.iter().all(|a| unsafe {
        if !a.renderbuffer.is_null() {
            return (*a.renderbuffer).defined != 0;
        }
        if a.texture.is_null() || a.level > DG_GL_MAX_TEXTURE_LEVEL {
            return true;
        }
        image_levels(a.texture, a.target).undefined_levels & (1 << a.level) == 0
    })
}
/// Read admission: a read of the read framebuffer may proceed. Reading an
/// image the host never initialized is GL_INVALID_FRAMEBUFFER_OPERATION.
///
/// # Safety
/// Render-owned state of the current context.
pub(crate) unsafe fn readable(c: *mut ContextState) -> bool {
    if unsafe { initialized((*c).read_framebuffer) } {
        return true;
    }
    unsafe { error(c, GL_INVALID_FRAMEBUFFER_OPERATION) };
    false
}
/// The size of a framebuffer object for reads: the smallest image attached
/// (zero without attachments).
unsafe fn size(fb: *const Framebuffer) -> (u32, u32) {
    let mut extent: Option<(u32, u32)> = None;
    for a in unsafe { (*fb).attachments } {
        let (w, h) = unsafe {
            if !a.renderbuffer.is_null() {
                ((*a.renderbuffer).width, (*a.renderbuffer).height)
            } else if !a.texture.is_null() && a.level <= DG_GL_MAX_TEXTURE_LEVEL {
                let f = image_levels(a.texture, a.target);
                let level = a.level as usize;
                let border = 2 * f.borders[level];
                let height = if a.target == GL_TEXTURE_1D {
                    1
                } else {
                    f.heights[level].saturating_sub(border)
                };
                (f.widths[level].saturating_sub(border), height)
            } else {
                continue;
            }
        };
        extent = Some(extent.map_or((w, h), |(ew, eh)| (ew.min(w), eh.min(h))));
    }
    extent.unwrap_or((0, 0))
}

/// Restore the native framebuffer bindings after the platform bound the
/// drawable framebuffer for both targets.
///
/// # Safety
/// Render-owned state; its native context is current.
pub(crate) unsafe fn rebind(api: &DreamGpuGlApi, c: *const ContextState) {
    let (draw, read) = unsafe { ((*c).draw_framebuffer, (*c).read_framebuffer) };
    if draw.is_null() && read.is_null() {
        return;
    }
    let (draw, read) = unsafe { (native(c, draw), native(c, read)) };
    gl!(api, dg_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw));
    gl!(api, dg_glBindFramebuffer(GL_READ_FRAMEBUFFER, read));
}
/// Order this context after other contexts' writes of the attachments of its
/// bound framebuffers (on make-current).
///
/// # Safety
/// Render-owned state; its native context is current.
pub(crate) unsafe fn order_bound(api: &DreamGpuGlApi, c: *const ContextState, serial: u64) {
    let (draw, read) = unsafe { ((*c).draw_framebuffer, (*c).read_framebuffer) };
    unsafe {
        order_all(api, draw, serial);
        if read != draw {
            order_all(api, read, serial);
        }
    }
}

/// Fence what the current context rendered into its draw framebuffer object
/// before another context may run.
///
/// # Safety
/// Render-owned state whose native context is current, outside Begin/End.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_framebuffer_settle(
    api: *const DreamGpuGlApi,
    state: *mut ContextState,
    serial: u64,
) {
    if api.is_null() || state.is_null() {
        return;
    }
    let a = unsafe { &*api };
    if a.dg_glFenceSync.is_none() || a.dg_glDeleteSync.is_none() {
        return;
    }
    unsafe { settle_draw(a, state, serial) };
}

/// New versions for every texture attached to the draw framebuffer, so a
/// texture read in this context never returns a cached image older than what
/// was rendered into it.
///
/// # Safety
/// Render-owned state.
pub(crate) unsafe fn touch(c: *const ContextState) {
    if c.is_null() {
        return;
    }
    let fb = unsafe { (*c).draw_framebuffer };
    if fb.is_null() {
        return;
    }
    for a in unsafe { (*fb).attachments } {
        if !a.texture.is_null() {
            unsafe { (*a.texture).version = (*a.texture).version.wrapping_add(1) };
        }
    }
}

/// The logical draw and read buffers of the bound framebuffers.
///
/// # Safety
/// Read-only render-owned state.
pub(crate) unsafe fn buffers(c: *const ContextState) -> (u32, u32) {
    unsafe {
        let draw = (*c).draw_framebuffer;
        let read = (*c).read_framebuffer;
        (
            if draw.is_null() {
                (*c).draw_buffer
            } else {
                (*draw).draw_buffer
            },
            if read.is_null() {
                (*c).read_buffer
            } else {
                (*read).read_buffer
            },
        )
    }
}
/// Where DrawBuffer (`draw`) or ReadBuffer stores its value: the window's
/// buffer or the bound object's. Window buffers on an object, attachments on
/// the window and NONE as the window's read buffer are GL errors.
///
/// # Safety
/// Render-owned state.
pub(crate) unsafe fn select_buffer(c: *mut ContextState, mode: u32, draw: bool) -> bool {
    let fb = unsafe {
        if draw {
            (*c).draw_framebuffer
        } else {
            (*c).read_framebuffer
        }
    };
    let attachment = color_attachment(mode);
    unsafe {
        if fb.is_null() {
            if attachment {
                error(c, GL_INVALID_OPERATION);
                return false;
            }
            if !draw && mode == GL_NONE {
                error(c, GL_INVALID_ENUM);
                return false;
            }
            if draw {
                (*c).draw_buffer = mode;
            } else {
                (*c).read_buffer = mode;
            }
        } else {
            if !attachment && mode != GL_NONE {
                error(c, GL_INVALID_OPERATION);
                return false;
            }
            if draw {
                (*fb).draw_buffer = mode;
            } else {
                (*fb).read_buffer = mode;
            }
        }
    }
    true
}
/// PopAttrib: restore a saved draw (or read) buffer only into the framebuffer
/// that was bound when it was saved.
///
/// # Safety
/// Render-owned state.
pub(crate) unsafe fn restore_buffer(c: *mut ContextState, saved: u32, owner: u32, draw: bool) {
    unsafe {
        let fb = if draw {
            (*c).draw_framebuffer
        } else {
            (*c).read_framebuffer
        };
        if name(fb) != owner {
            return;
        }
        match (fb.is_null(), draw) {
            (true, true) => (*c).draw_buffer = saved,
            (true, false) => (*c).read_buffer = saved,
            (false, true) => (*fb).draw_buffer = saved,
            (false, false) => (*fb).read_buffer = saved,
        }
    }
}

/// Destroy a framebuffer object: release its attachments, its native object
/// (in the owning context), its charge and the object.
unsafe fn destroy(m: &Memory, fb: *mut Framebuffer) {
    if fb.is_null() {
        return;
    }
    unsafe {
        for a in (*fb).attachments {
            release(m, a);
        }
        if (*fb).native != 0 {
            gl!(&*m.api, dg_glDeleteFramebuffers(1, &(*fb).native));
        }
        (m.free)(m.opaque, fb.cast());
        uncharge(m, OBJECT_BYTES);
    }
}

/// Release every framebuffer object of a context and its renderbuffer
/// binding. The context's native context is current.
///
/// # Safety
/// The state was already cleared; `framebuffers` and `renderbuffer` were its.
pub(crate) unsafe fn release_context(
    m: &Memory,
    framebuffers: *mut Framebuffers,
    renderbuffer: *mut Renderbuffer,
) {
    unsafe { rb_unref(m, renderbuffer) };
    if framebuffers.is_null() {
        return;
    }
    for i in 0..FRAMEBUFFER_SLOTS {
        unsafe { destroy(m, (*framebuffers).slots[i].object) };
    }
    unsafe {
        (m.free)(m.opaque, framebuffers.cast());
        uncharge(m, Framebuffers::charge_bytes());
    }
}
/// The last context of a share group releases its renderbuffers.
///
/// # Safety
/// No framebuffer or binding of the group references them any more.
pub(crate) unsafe fn release_renderbuffers(m: &Memory, table: *mut Renderbuffers) {
    if table.is_null() {
        return;
    }
    for i in 0..RENDERBUFFER_SLOTS {
        unsafe { rb_unref(m, (*table).slots[i].object) };
    }
    unsafe {
        (m.free)(m.opaque, table.cast());
        uncharge(m, Renderbuffers::charge_bytes());
    }
}

/// BindFramebuffer: a new name creates the context's object.
unsafe fn bind_framebuffer(
    m: &Memory,
    c: *mut ContextState,
    serial: u64,
    target: u32,
    name: u32,
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let mut fb = null_mut();
    if name != 0 {
        let table = unsafe { Framebuffers::ensure(m, addr_of_mut!((*c).framebuffers)) };
        if table.is_null() {
            unsafe { error(c, GL_OUT_OF_MEMORY) };
            return Ok(());
        }
        fb = match unsafe { Framebuffers::locate(table, name) } {
            Ok(i) => unsafe { (*table).slots[i].object },
            Err(at) => {
                if unsafe { (*table).count } >= Framebuffers::LIMIT.min(DG_GL_MAX_FRAMEBUFFERS)
                    || !unsafe { charge(m, OBJECT_BYTES) }
                {
                    unsafe { error(c, GL_OUT_OF_MEMORY) };
                    return Ok(());
                }
                let object = unsafe { (m.allocate)(m.opaque, core::mem::size_of::<Framebuffer>()) }
                    .cast::<Framebuffer>();
                let mut native = 0;
                if !object.is_null() {
                    gl!(api, dg_glGenFramebuffers(1, &mut native));
                }
                if native == 0 {
                    unsafe {
                        if !object.is_null() {
                            (m.free)(m.opaque, object.cast());
                        }
                        uncharge(m, OBJECT_BYTES);
                        error(c, GL_OUT_OF_MEMORY);
                    }
                    return Ok(());
                }
                unsafe {
                    object.write(Framebuffer {
                        name,
                        native,
                        draw_buffer: GL_COLOR_ATTACHMENT0,
                        read_buffer: GL_COLOR_ATTACHMENT0,
                        attachments: [DETACHED; ATTACHMENTS],
                    });
                    (*table).slots[at] = Slot { name, object };
                    (*table).count += 1;
                }
                object
            }
        };
    }
    unsafe {
        if target != GL_READ_FRAMEBUFFER && (*c).draw_framebuffer != fb {
            settle_draw(api, c, serial);
            (*c).draw_framebuffer = fb;
        }
        if target != GL_DRAW_FRAMEBUFFER {
            (*c).read_framebuffer = fb;
        }
        gl!(api, dg_glBindFramebuffer(target, native(c, fb)));
        order_all(api, fb, serial);
    }
    Ok(())
}

/// BindRenderbuffer: a new name creates the share group's object.
unsafe fn bind_renderbuffer(m: &Memory, c: *mut ContextState, name: u32) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let mut rb = null_mut();
    if name != 0 {
        let table =
            unsafe { Renderbuffers::ensure(m, addr_of_mut!((*(*c).textures).renderbuffers)) };
        if table.is_null() {
            unsafe { error(c, GL_OUT_OF_MEMORY) };
            return Ok(());
        }
        rb = match unsafe { Renderbuffers::locate(table, name) } {
            Ok(i) => unsafe { (*table).slots[i].object },
            Err(at) => {
                if unsafe { (*table).count } >= Renderbuffers::LIMIT.min(DG_GL_MAX_RENDERBUFFERS)
                    || !unsafe { charge(m, OBJECT_BYTES) }
                {
                    unsafe { error(c, GL_OUT_OF_MEMORY) };
                    return Ok(());
                }
                let object =
                    unsafe { (m.allocate)(m.opaque, core::mem::size_of::<Renderbuffer>()) }
                        .cast::<Renderbuffer>();
                let mut native = 0;
                if !object.is_null() {
                    gl!(api, dg_glGenRenderbuffers(1, &mut native));
                }
                if native == 0 {
                    unsafe {
                        if !object.is_null() {
                            (m.free)(m.opaque, object.cast());
                        }
                        uncharge(m, OBJECT_BYTES);
                        error(c, GL_OUT_OF_MEMORY);
                    }
                    return Ok(());
                }
                unsafe {
                    object.write(Renderbuffer {
                        native,
                        guest_name: name,
                        refs: 1,
                        deleted: 0,
                        format: 0,
                        width: 0,
                        height: 0,
                        defined: 1,
                        bytes: 0,
                        last_write: null_mut(),
                        writer_serial: 0,
                        waiter_serial: 0,
                    });
                    (*table).slots[at] = Slot { name, object };
                    (*table).count += 1;
                }
                object
            }
        };
    }
    unsafe {
        if (*c).renderbuffer != rb {
            if !rb.is_null() {
                add_ref(&mut (*rb).refs);
            }
            rb_unref(m, (*c).renderbuffer);
            (*c).renderbuffer = rb;
        }
        gl!(
            api,
            dg_glBindRenderbuffer(GL_RENDERBUFFER, if rb.is_null() { 0 } else { (*rb).native })
        );
    }
    Ok(())
}

/// Saved clear state; dropping it restores every value an initialization
/// clear changes, including the guest's framebuffer bindings.
struct Clearing<'a> {
    api: &'a DreamGpuGlApi,
    read: i32,
    draw: i32,
    color: [f32; 4],
    depth: f64,
    stencil: i32,
    color_mask: [u8; 4],
    depth_mask: u8,
    /// The guest sets both faces' masks at once (StencilMask).
    stencil_mask: i32,
    scissor: u8,
    discard: u8,
    framebuffer: u32,
}
impl Drop for Clearing<'_> {
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
        gl!(a, dg_glClearDepth(self.depth));
        gl!(a, dg_glClearStencil(self.stencil));
        gl!(
            a,
            dg_glColorMask(
                self.color_mask[0],
                self.color_mask[1],
                self.color_mask[2],
                self.color_mask[3]
            )
        );
        gl!(a, dg_glDepthMask(self.depth_mask));
        gl!(a, dg_glStencilMask(self.stencil_mask as u32));
        if self.scissor != 0 {
            gl!(a, dg_glEnable(GL_SCISSOR_TEST));
        }
        if self.discard != 0 {
            gl!(a, dg_glEnable(GL_RASTERIZER_DISCARD_EXT));
        }
        if self.framebuffer != 0 {
            gl!(a, dg_glDeleteFramebuffers(1, &self.framebuffer));
        }
    }
}
/// Zero-initialize a renderbuffer's new storage through a private native
/// framebuffer. False when the native framebuffer cannot clear it.
unsafe fn zero(api: &DreamGpuGlApi, rb: *const Renderbuffer) -> bool {
    let mut saved = Clearing {
        api,
        read: 0,
        draw: 0,
        color: [0.; 4],
        depth: 0.,
        stencil: 0,
        color_mask: [0; 4],
        depth_mask: 0,
        stencil_mask: 0,
        scissor: gl!(api, dg_glIsEnabled(GL_SCISSOR_TEST)),
        discard: 0,
        framebuffer: 0,
    };
    unsafe {
        // Rasterizer discard (GL 3.0) would skip the clear; native GL alone
        // knows whether the guest enabled it, and a legacy context reports
        // an error for the name, which is consumed here.
        saved.discard = gl!(api, dg_glIsEnabled(GL_RASTERIZER_DISCARD_EXT));
        let _ = gl!(api, dg_glGetError());
        gl!(
            api,
            dg_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &mut saved.read)
        );
        gl!(
            api,
            dg_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &mut saved.draw)
        );
        gl!(
            api,
            dg_glGetFloatv(GL_COLOR_CLEAR_VALUE, saved.color.as_mut_ptr())
        );
        gl!(api, dg_glGetDoublev(GL_DEPTH_CLEAR_VALUE, &mut saved.depth));
        gl!(
            api,
            dg_glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &mut saved.stencil)
        );
        gl!(
            api,
            dg_glGetBooleanv(GL_COLOR_WRITEMASK, saved.color_mask.as_mut_ptr())
        );
        gl!(
            api,
            dg_glGetBooleanv(GL_DEPTH_WRITEMASK, &mut saved.depth_mask)
        );
        gl!(
            api,
            dg_glGetIntegerv(GL_STENCIL_WRITEMASK, &mut saved.stencil_mask)
        );
        gl!(api, dg_glGenFramebuffers(1, &mut saved.framebuffer));
        if saved.framebuffer == 0 {
            return false;
        }
        gl!(api, dg_glBindFramebuffer(GL_FRAMEBUFFER, saved.framebuffer));
        let format = (*rb).format;
        let (points, bits): (&[u32], u32) = if packed_format(format) {
            (
                &[GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT],
                GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
            )
        } else if depth_format(format) {
            (&[GL_DEPTH_ATTACHMENT], GL_DEPTH_BUFFER_BIT)
        } else if stencil_format(format) {
            (&[GL_STENCIL_ATTACHMENT], GL_STENCIL_BUFFER_BIT)
        } else {
            (&[GL_COLOR_ATTACHMENT0], GL_COLOR_BUFFER_BIT)
        };
        for &point in points {
            gl!(
                api,
                dg_glFramebufferRenderbuffer(GL_FRAMEBUFFER, point, GL_RENDERBUFFER, (*rb).native)
            );
        }
        if gl!(api, dg_glCheckFramebufferStatus(GL_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE {
            let _ = gl!(api, dg_glGetError());
            return false;
        }
        gl!(api, dg_glDisable(GL_SCISSOR_TEST));
        if saved.discard != 0 {
            gl!(api, dg_glDisable(GL_RASTERIZER_DISCARD_EXT));
        }
        gl!(api, dg_glColorMask(1, 1, 1, 1));
        gl!(api, dg_glDepthMask(1));
        gl!(api, dg_glStencilMask(u32::MAX));
        gl!(api, dg_glClearColor(0., 0., 0., 0.));
        gl!(api, dg_glClearDepth(0.));
        gl!(api, dg_glClearStencil(0));
        gl!(api, dg_glClear(bits));
        gl!(api, dg_glGetError()) == GL_NO_ERROR
    }
}

/// RenderbufferStorage of the bound renderbuffer.
unsafe fn storage(
    m: &Memory,
    c: *mut ContextState,
    serial: u64,
    format: u32,
    width: u32,
    height: u32,
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let rb = unsafe { (*c).renderbuffer };
    if rb.is_null() {
        unsafe { error(c, GL_INVALID_OPERATION) };
        return Ok(());
    }
    let mut limit = 0;
    gl!(api, dg_glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &mut limit));
    let limit = (limit.max(0) as u32).min(DG_GL_MAX_RENDERBUFFER_SIZE);
    if width > limit || height > limit {
        unsafe { error(c, GL_INVALID_VALUE) };
        return Ok(());
    }
    let bytes = u64::from(width) * u64::from(height) * renderbuffer_bytes(format);
    unsafe {
        remember(api, c)?;
        let budget = u64::from(DG_GL_MAX_TEXTURE_BYTES)
            .saturating_sub(*m.bytes)
            .saturating_add((*rb).bytes);
        let (width, height, bytes, exhausted) = if bytes > budget {
            (0, 0, 0, true)
        } else {
            (width, height, bytes, false)
        };
        rb_wait(api, rb, serial);
        gl!(
            api,
            dg_glRenderbufferStorage(GL_RENDERBUFFER, format, width as i32, height as i32)
        );
        let code = gl!(api, dg_glGetError());
        if code != GL_NO_ERROR {
            // Native storage is unchanged; so is the tracking.
            error(c, code);
            return Ok(());
        }
        uncharge(m, (*rb).bytes);
        assert!(
            charge(m, bytes),
            "renderbuffer budget checked before storage"
        );
        (*rb).bytes = bytes;
        (*rb).format = format;
        (*rb).width = width;
        (*rb).height = height;
        (*rb).defined = u32::from(bytes == 0 || zero(api, rb));
        rb_written(api, rb, serial);
        if exhausted {
            error(c, GL_OUT_OF_MEMORY);
        }
    }
    Ok(())
}

/// FramebufferTexture1D/2D/3D.
unsafe fn attach_texture(
    m: &Memory,
    c: *mut ContextState,
    serial: u64,
    function: u32,
    a: &[u32; 10],
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let [target, attachment, textarget, name, level, zoffset, ..] = *a;
    let fb = unsafe { bound(c, target) };
    let index = slot(attachment).ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    if fb.is_null() {
        unsafe { error(c, GL_INVALID_OPERATION) };
        return Ok(());
    }
    let t = if name == 0 {
        null_mut()
    } else {
        unsafe { dreamgpu_texture_lookup((*c).textures, name) }
    };
    if name != 0 {
        let expected = match function {
            FEnum_glFramebufferTexture1D => GL_TEXTURE_1D,
            FEnum_glFramebufferTexture3D => GL_TEXTURE_3D,
            _ if cube_face(textarget).is_some() => GL_TEXTURE_CUBE_MAP,
            _ => GL_TEXTURE_2D,
        };
        if t.is_null() || unsafe { (*t).target } != expected {
            unsafe { error(c, GL_INVALID_OPERATION) };
            return Ok(());
        }
        let limit = if expected == GL_TEXTURE_3D {
            DG_GL_MAX_3D_TEXTURE_SIZE.trailing_zeros()
        } else {
            DG_GL_MAX_TEXTURE_LEVEL
        };
        if level > limit {
            unsafe { error(c, GL_INVALID_VALUE) };
            return Ok(());
        }
    }
    let native_name = if t.is_null() { 0 } else { unsafe { (*t).name } };
    unsafe {
        remember(api, c)?;
        match function {
            FEnum_glFramebufferTexture1D => gl!(
                api,
                dg_glFramebufferTexture1D(target, attachment, textarget, native_name, level as i32)
            ),
            FEnum_glFramebufferTexture2D => gl!(
                api,
                dg_glFramebufferTexture2D(target, attachment, textarget, native_name, level as i32)
            ),
            _ => gl!(
                api,
                dg_glFramebufferTexture3D(
                    target,
                    attachment,
                    textarget,
                    native_name,
                    level as i32,
                    zoffset as i32
                )
            ),
        }
        let code = gl!(api, dg_glGetError());
        if code != GL_NO_ERROR {
            error(c, code);
            return Ok(());
        }
        let new = if t.is_null() {
            DETACHED
        } else {
            dreamgpu_texture_wait(api, t, serial);
            Attachment {
                texture: t,
                renderbuffer: null_mut(),
                target: textarget,
                level,
                zoffset: if function == FEnum_glFramebufferTexture3D {
                    zoffset
                } else {
                    0
                },
            }
        };
        replace(m, c, fb, index, new, serial);
    }
    Ok(())
}

/// FramebufferRenderbuffer.
unsafe fn attach_renderbuffer(
    m: &Memory,
    c: *mut ContextState,
    serial: u64,
    target: u32,
    attachment: u32,
    name: u32,
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let fb = unsafe { bound(c, target) };
    let index = slot(attachment).ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    if fb.is_null() {
        unsafe { error(c, GL_INVALID_OPERATION) };
        return Ok(());
    }
    let rb = unsafe { Renderbuffers::find((*(*c).textures).renderbuffers, name) };
    if name != 0 && rb.is_null() {
        unsafe { error(c, GL_INVALID_OPERATION) };
        return Ok(());
    }
    unsafe {
        remember(api, c)?;
        gl!(
            api,
            dg_glFramebufferRenderbuffer(
                target,
                attachment,
                GL_RENDERBUFFER,
                if rb.is_null() { 0 } else { (*rb).native }
            )
        );
        let code = gl!(api, dg_glGetError());
        if code != GL_NO_ERROR {
            error(c, code);
            return Ok(());
        }
        let new = if rb.is_null() {
            DETACHED
        } else {
            rb_wait(api, rb, serial);
            Attachment {
                renderbuffer: rb,
                ..DETACHED
            }
        };
        replace(m, c, fb, index, new, serial);
    }
    Ok(())
}

/// GenerateMipmap of the active unit's binding: levels base+1 up to the
/// texture's maximum level are defined (and charged) from the base level.
unsafe fn generate_mipmap(
    m: &Memory,
    c: *mut ContextState,
    serial: u64,
    target: u32,
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let t = unsafe { crate::state::bound(c, target) };
    if t.is_null() {
        return Err(DG_GL_ERROR_TEXTURE);
    }
    unsafe { remember(api, c)? };
    let (mut base, mut max) = (0, 0);
    gl!(
        api,
        dg_glGetTexParameteriv(target, GL_TEXTURE_BASE_LEVEL, &mut base)
    );
    gl!(
        api,
        dg_glGetTexParameteriv(target, GL_TEXTURE_MAX_LEVEL, &mut max)
    );
    let _ = gl!(api, dg_glGetError());
    let volume = target == GL_TEXTURE_3D;
    let top = if volume {
        DG_GL_MAX_3D_TEXTURE_SIZE.trailing_zeros()
    } else {
        DG_GL_MAX_TEXTURE_LEVEL
    } as i32;
    if !(0..=top).contains(&base) {
        // Nothing the host tracks; native GL decides (and reports) the rest.
        gl!(api, dg_glGenerateMipmap(target));
        unsafe { error(c, gl!(api, dg_glGetError())) };
        return Ok(());
    }
    let base = base as usize;
    let last = (max.clamp(0, top) as usize).max(base);
    let faces: &[u32] = if target == GL_TEXTURE_CUBE_MAP {
        &[
            GL_TEXTURE_CUBE_MAP_POSITIVE_X,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
        ]
    } else {
        core::slice::from_ref(&target)
    };
    // The generated chain of every image array, its charges and the change.
    let mut plans = [crate::texture::Face::EMPTY; crate::texture::FACES];
    let mut depths = unsafe { (*t).depths };
    let (mut old, mut new) = (0u64, 0u64);
    for (i, &image) in faces.iter().enumerate() {
        let mut f = unsafe { image_levels(t, image) };
        let (w, h) = (f.widths[base], f.heights[base]);
        let d = if volume { depths[base] } else { 1 };
        if w == 0 || h == 0 || d == 0 {
            plans[i] = f;
            continue;
        }
        let mut compressed = 0;
        gl!(
            api,
            dg_glGetTexLevelParameteriv(image, base as i32, GL_TEXTURE_COMPRESSED, &mut compressed)
        );
        let _ = gl!(api, dg_glGetError());
        let texels = u64::from(w) * u64::from(h) * u64::from(d);
        let charged = f.levels[base];
        // The base charge of an SGIS_generate_mipmap image already covers its
        // chain; otherwise each generated level is charged at its own size.
        let (texel, chained) = match [4u64, 8]
            .into_iter()
            .find(|&b| charged == texels * b || charged == texels * b + (texels * b).div_ceil(3))
        {
            Some(b) => (b, charged != texels * b),
            None => (0, false),
        };
        if compressed != 0 || texel == 0 || f.borders[base] != 0 {
            unsafe { error(c, GL_INVALID_OPERATION) };
            return Ok(());
        }
        let undefined = f.undefined_levels & (1 << base);
        let (mut lw, mut lh, mut ld) = (w, h, d);
        for level in base + 1..=last {
            if lw == 1 && lh == 1 && ld == 1 {
                break;
            }
            lw = (lw >> 1).max(1);
            lh = if target == GL_TEXTURE_1D {
                1
            } else {
                (lh >> 1).max(1)
            };
            ld = (ld >> 1).max(1);
            old += f.levels[level];
            let bytes = if chained {
                0
            } else {
                u64::from(lw) * u64::from(lh) * u64::from(ld) * texel
            };
            new += bytes;
            f.widths[level] = lw;
            f.heights[level] = lh;
            f.borders[level] = 0;
            f.levels[level] = bytes;
            f.undefined_levels =
                (f.undefined_levels & !(1 << level)) | ((undefined >> base) << level);
            if volume {
                depths[level] = ld;
            }
        }
        plans[i] = f;
    }
    let total = unsafe { *m.bytes };
    let next = total.checked_sub(old).and_then(|v| v.checked_add(new));
    match next {
        Some(n) if n <= u64::from(DG_GL_MAX_TEXTURE_BYTES) => {}
        _ => {
            unsafe { error(c, GL_OUT_OF_MEMORY) };
            return Ok(());
        }
    }
    unsafe { dreamgpu_texture_wait(api, t, serial) };
    gl!(api, dg_glGenerateMipmap(target));
    let code = gl!(api, dg_glGetError());
    if code != GL_NO_ERROR {
        unsafe { error(c, code) };
        return Ok(());
    }
    unsafe {
        for (i, &image) in faces.iter().enumerate() {
            set_image_levels(t, image, &plans[i]);
        }
        if volume {
            (*t).depths = depths;
        }
        *m.bytes = next.unwrap();
        dreamgpu_texture_written(api, t, serial);
    }
    Ok(())
}

/// BlitFramebuffer between the bound read and draw framebuffers.
unsafe fn blit(api: &DreamGpuGlApi, c: *mut ContextState, a: &[u32; 10]) -> Result<(), u32> {
    if !unsafe { readable(c) } {
        return Ok(());
    }
    unsafe { remember(api, c)? };
    let s = |i: usize| a[i] as i32;
    gl!(
        api,
        dg_glBlitFramebuffer(s(0), s(1), s(2), s(3), s(4), s(5), s(6), s(7), a[8], a[9])
    );
    unsafe { error(c, gl!(api, dg_glGetError())) };
    Ok(())
}

/// The framebuffer CALL records (and CopyPixels, which reads the read
/// framebuffer). Shapes passed `call_shape` in the batch validator.
///
/// # Safety
/// Render-owned state whose native context is current; args hold the
/// function's validated words.
pub(crate) unsafe fn call(
    memory: *const Memory,
    c: *mut ContextState,
    serial: u64,
    function: u32,
    args: &[u8],
) -> Result<(), u32> {
    let m = unsafe { &*memory };
    let api = unsafe { &*m.api };
    if !complete(api) {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let mut a = [0u32; 10];
    for (i, v) in a.iter_mut().enumerate().take(args.len() / 4) {
        *v = word(args, i);
    }
    if function == FEnum_glCopyPixels {
        if unsafe { readable(c) } {
            gl!(
                api,
                dg_glCopyPixels(a[0] as i32, a[1] as i32, a[2] as i32, a[3] as i32, a[4])
            );
        }
        return Ok(());
    }
    if !call_shape(function, &a) {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    unsafe {
        match function {
            FEnum_glBindFramebuffer => bind_framebuffer(m, c, serial, a[0], a[1]),
            FEnum_glBindRenderbuffer => bind_renderbuffer(m, c, a[1]),
            FEnum_glRenderbufferStorage => storage(m, c, serial, a[1], a[2], a[3]),
            FEnum_glFramebufferTexture1D
            | FEnum_glFramebufferTexture2D
            | FEnum_glFramebufferTexture3D => attach_texture(m, c, serial, function, &a),
            FEnum_glFramebufferRenderbuffer => attach_renderbuffer(m, c, serial, a[0], a[1], a[3]),
            FEnum_glGenerateMipmap => generate_mipmap(m, c, serial, a[0]),
            _ => blit(api, c, &a),
        }
    }
}

/// Detach `matches` from this context's bound framebuffers (a deleted image
/// is detached from them only, as native deletion does).
unsafe fn detach_bound(
    m: &Memory,
    c: *mut ContextState,
    serial: u64,
    matches: impl Fn(&Attachment) -> bool,
) {
    let (draw, read) = unsafe { ((*c).draw_framebuffer, (*c).read_framebuffer) };
    let distinct_read = if read == draw { null_mut() } else { read };
    for fb in [draw, distinct_read] {
        if fb.is_null() {
            continue;
        }
        for index in 0..ATTACHMENTS {
            let a = unsafe { (*fb).attachments[index] };
            if matches(&a) {
                unsafe { replace(m, c, fb, index, DETACHED, serial) };
            }
        }
    }
}

/// DeleteFramebuffers: a bound one first rebinds the window.
///
/// # Safety
/// Data holds the validated little-endian names; native context current.
pub(crate) unsafe fn delete_framebuffers(
    m: &Memory,
    c: *mut ContextState,
    serial: u64,
    data: &[u8],
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    if !complete(api) {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let table = unsafe { (*c).framebuffers };
    if table.is_null() {
        return Ok(());
    }
    for raw in data.as_chunks::<4>().0 {
        let name = u32::from_le_bytes(*raw);
        if name == 0 {
            continue;
        }
        let Ok(at) = (unsafe { Framebuffers::locate(table, name) }) else {
            continue;
        };
        let fb = unsafe { (*table).slots[at].object };
        unsafe {
            if (*c).draw_framebuffer == fb {
                settle_draw(api, c, serial);
                (*c).draw_framebuffer = null_mut();
                gl!(
                    api,
                    dg_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (*c).window_framebuffer)
                );
            }
            if (*c).read_framebuffer == fb {
                (*c).read_framebuffer = null_mut();
                gl!(
                    api,
                    dg_glBindFramebuffer(GL_READ_FRAMEBUFFER, (*c).window_framebuffer)
                );
            }
            Framebuffers::remove(table, at);
            destroy(m, fb);
        }
    }
    Ok(())
}

/// DeleteRenderbuffers: unbound if bound, detached from this context's bound
/// framebuffers; an image attached elsewhere lives until detached.
///
/// # Safety
/// Data holds the validated little-endian names; native context current.
pub(crate) unsafe fn delete_renderbuffers(
    m: &Memory,
    c: *mut ContextState,
    serial: u64,
    data: &[u8],
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    if !complete(api) {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let table = unsafe { (*(*c).textures).renderbuffers };
    if table.is_null() {
        return Ok(());
    }
    for raw in data.as_chunks::<4>().0 {
        let name = u32::from_le_bytes(*raw);
        if name == 0 {
            continue;
        }
        let Ok(at) = (unsafe { Renderbuffers::locate(table, name) }) else {
            continue;
        };
        let rb = unsafe { (*table).slots[at].object };
        unsafe {
            detach_bound(m, c, serial, |a| a.renderbuffer == rb);
            if (*c).renderbuffer == rb {
                (*c).renderbuffer = null_mut();
                rb_unref(m, rb);
            }
            Renderbuffers::remove(table, at);
            gl!(api, dg_glDeleteRenderbuffers(1, &(*rb).native));
            (*rb).deleted = 1;
            rb_unref(m, rb);
        }
    }
    Ok(())
}

/// DeleteTextures: detach the deleted textures from this context's bound
/// framebuffers before the namespace releases them (native deletion detaches
/// the same images).
///
/// # Safety
/// Data holds `count` validated little-endian names.
pub(crate) unsafe fn forget_textures(m: &Memory, c: *mut ContextState, serial: u64, data: &[u8]) {
    let api = unsafe { &*m.api };
    let (draw, read) = unsafe { ((*c).draw_framebuffer, (*c).read_framebuffer) };
    if (draw.is_null() && read.is_null()) || api.dg_glFenceSync.is_none() {
        return;
    }
    for raw in data.as_chunks::<4>().0 {
        let name = u32::from_le_bytes(*raw);
        let t = if name == 0 {
            null_mut()
        } else {
            unsafe { dreamgpu_texture_lookup((*c).textures, name) }
        };
        if !t.is_null() {
            unsafe { detach_bound(m, c, serial, |a| a.texture == t) };
        }
    }
}

/// CheckFramebufferStatus, GetFramebufferAttachmentParameteriv and
/// GetRenderbufferParameteriv: one value. Shapes were validated; every state
/// problem is a GL error with value zero.
///
/// # Safety
/// Render-owned state whose native context is current.
pub(crate) unsafe fn query(
    api: &DreamGpuGlApi,
    c: *mut ContextState,
    function: u32,
    [a, b, d]: [u32; 3],
) -> Result<u32, u32> {
    if !complete(api) {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    match function {
        FEnum_glCheckFramebufferStatus => {
            let fb = unsafe { bound(c, a) };
            if fb.is_null() {
                return Ok(GL_FRAMEBUFFER_COMPLETE);
            }
            unsafe { remember(api, c)? };
            let status = gl!(api, dg_glCheckFramebufferStatus(a));
            unsafe { error(c, gl!(api, dg_glGetError())) };
            Ok(match status {
                GL_FRAMEBUFFER_COMPLETE
                | GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT
                | GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT
                | GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT
                | GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT
                | GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER
                | GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER
                | GL_FRAMEBUFFER_UNSUPPORTED => status,
                0 => 0,
                // GL 3 states (layers, multisampling, no default framebuffer)
                // have no EXT equivalent; none can arise from EXT commands.
                _ => GL_FRAMEBUFFER_UNSUPPORTED,
            })
        }
        FEnum_glGetFramebufferAttachmentParameteriv => {
            let fb = unsafe { bound(c, a) };
            let index = slot(b).ok_or(DG_GL_ERROR_UNSUPPORTED)?;
            if fb.is_null() {
                unsafe { error(c, GL_INVALID_OPERATION) };
                return Ok(0);
            }
            let at = unsafe { (*fb).attachments[index] };
            let kind = if !at.texture.is_null() {
                TEXTURE
            } else if !at.renderbuffer.is_null() {
                GL_RENDERBUFFER
            } else {
                GL_NONE
            };
            let value = match d {
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE => Some(kind),
                _ if kind == GL_NONE => None,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME => Some(unsafe {
                    if kind == TEXTURE {
                        (*at.texture).guest_name
                    } else {
                        (*at.renderbuffer).guest_name
                    }
                }),
                _ if kind != TEXTURE => None,
                GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL => Some(at.level),
                GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE => {
                    Some(if cube_face(at.target).is_some() {
                        at.target
                    } else {
                        0
                    })
                }
                _ => Some(at.zoffset),
            };
            Ok(value.unwrap_or_else(|| {
                unsafe { error(c, GL_INVALID_ENUM) };
                0
            }))
        }
        _ => {
            let rb = unsafe { (*c).renderbuffer };
            if rb.is_null() {
                unsafe { error(c, GL_INVALID_OPERATION) };
                return Ok(0);
            }
            Ok(match b {
                GL_RENDERBUFFER_WIDTH => unsafe { (*rb).width },
                GL_RENDERBUFFER_HEIGHT => unsafe { (*rb).height },
                // The format the guest requested; RGBA before any storage.
                GL_RENDERBUFFER_INTERNAL_FORMAT => match unsafe { (*rb).format } {
                    0 => GL_RGBA,
                    f => f,
                },
                pname => {
                    unsafe { remember(api, c)? };
                    let mut value = 0;
                    gl!(
                        api,
                        dg_glGetRenderbufferParameteriv(GL_RENDERBUFFER, pname, &mut value)
                    );
                    unsafe { error(c, gl!(api, dg_glGetError())) };
                    value.max(0) as u32
                }
            })
        }
    }
}

/// ReadPixels from a framebuffer object: the part of the rectangle inside
/// every attachment, zero elsewhere. `result` holds `w * h * pixel` bytes.
///
/// # Safety
/// Render-owned state whose native context is current; `result` is writable.
pub(crate) unsafe fn read_pixels(
    api: &DreamGpuGlApi,
    c: *mut ContextState,
    [x, y, w, h]: [u32; 4],
    format: u32,
    kind: u32,
    pixel: u32,
    result: *mut u8,
) -> Result<(), u32> {
    let read = api.dg_glReadPixels.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let fb = unsafe { (*c).read_framebuffer };
    unsafe { core::ptr::write_bytes(result, 0, (w * h * pixel) as usize) };
    if !unsafe { readable(c) } {
        return Ok(());
    }
    unsafe { remember(api, c)? };
    if gl!(api, dg_glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE {
        let _ = gl!(api, dg_glGetError());
        unsafe { error(c, GL_INVALID_FRAMEBUFFER_OPERATION) };
        return Ok(());
    }
    let (fw, fh) = unsafe { size(fb) };
    let right = (x + w).min(fw);
    let top = (y + h).min(fh);
    if right <= x || top <= y {
        return Ok(());
    }
    let pack = unsafe { query::Pack::new(api)? };
    gl!(api, dg_glPixelStorei(GL_PACK_ROW_LENGTH, w as i32));
    unsafe {
        read(
            x as i32,
            y as i32,
            (right - x) as i32,
            (top - y) as i32,
            format,
            kind,
            result.cast(),
        )
    };
    let code = gl!(api, dg_glGetError());
    drop(pack);
    if code != GL_NO_ERROR {
        unsafe {
            core::ptr::write_bytes(result, 0, (w * h * pixel) as usize);
            error(c, code);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests;
