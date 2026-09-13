// SPDX-License-Identifier: GPL-2.0-or-later
//! Process-scoped native context/drawable registry and GL command execution.
//! Platform callbacks own CGL/EGL calls, QEMU allocation and sleeping export drains.
use core::ffi::c_void;

const MAX: usize = 32;
const BATCH: u32 = 1;
const CONTEXT: u32 = 4;
const DRAWABLE: u32 = 5;
const HOST: u32 = 6;
const LIMIT: u32 = 9;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Context {
    pub client: u32,
    pub id: u32,
    pub drawable: u32,
    pub native: *mut c_void,
}
impl Context {
    const EMPTY: Self = Self {
        client: 0,
        id: 0,
        drawable: 0,
        native: core::ptr::null_mut(),
    };
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Drawable {
    pub client: u32,
    pub id: u32,
    pub width: u32,
    pub height: u32,
    pub epoch: u64,
    pub generation: u64,
    pub published_epoch: u64,
    pub published_generation: u64,
    pub native: *mut c_void,
}
impl Drawable {
    const EMPTY: Self = Self {
        client: 0,
        id: 0,
        width: 0,
        height: 0,
        epoch: 0,
        generation: 0,
        published_epoch: 0,
        published_generation: 0,
        native: core::ptr::null_mut(),
    };
}

#[repr(C)]
pub struct Resources {
    pub contexts: [Context; MAX],
    pub drawables: [Drawable; MAX],
    pub next_epoch: u64,
}
impl Default for Resources {
    fn default() -> Self {
        Self {
            contexts: [Context::EMPTY; MAX],
            drawables: [Drawable::EMPTY; MAX],
            next_epoch: 0,
        }
    }
}

#[repr(C)]
pub struct Platform {
    pub opaque: *mut c_void,
    pub context_new: unsafe extern "C" fn(*mut c_void, *mut c_void) -> *mut c_void,
    pub drawable_new: unsafe extern "C" fn(*mut c_void, u32, u32) -> *mut c_void,
    pub context_free: unsafe extern "C" fn(*mut c_void, u32),
    pub drawable_free: unsafe extern "C" fn(*mut c_void, u32),
    pub close_begin: unsafe extern "C" fn(*mut c_void),
    pub in_begin: unsafe extern "C" fn(*mut c_void, u32) -> u32,
    pub make_current: unsafe extern "C" fn(*mut c_void, u32, u32) -> u32,
    pub call: unsafe extern "C" fn(*mut c_void, u32, u32, *const u8) -> u32,
    pub data: unsafe extern "C" fn(*mut c_void, u32, u32, *const u8, *const u8, u32) -> u32,
    pub words: unsafe extern "C" fn(*mut c_void, u32) -> u32,
    pub query: unsafe extern "C" fn(*mut c_void, u32, u32, *const u8) -> u32,
    pub present: unsafe extern "C" fn(*mut c_void, u32, u32, u32) -> u32,
    pub desktop: unsafe extern "C" fn(*mut c_void, *const u8) -> u32,
}

// Do not retain a Rust reference into this registry across callbacks: the C export
// workers access these records under their existing locks. Only the render worker
// creates/deletes resources, after the callback has drained pending publication.
unsafe fn context(s: *mut Resources, client: u32, id: u32) -> Option<usize> {
    (0..MAX).find(|&i| {
        let c = unsafe { (*s).contexts[i] };
        !c.native.is_null() && c.client == client && c.id == id
    })
}
unsafe fn drawable(s: *mut Resources, client: u32, id: u32) -> Option<usize> {
    (0..MAX).find(|&i| unsafe {
        !(*s).drawables[i].native.is_null()
            && (*s).drawables[i].client == client
            && (*s).drawables[i].id == id
    })
}

unsafe fn close(s: *mut Resources, p: &Platform, client: u32) {
    unsafe {
        (p.close_begin)(p.opaque);
    }
    for i in 0..MAX {
        let (native, owner) = unsafe { ((*s).drawables[i].native, (*s).drawables[i].client) };
        if !native.is_null() && (client == 0 || owner == client) {
            unsafe {
                (p.drawable_free)(p.opaque, i as u32);
                (*s).drawables[i] = Drawable::EMPTY;
            }
        }
    }
    for i in 0..MAX {
        let c = unsafe { (*s).contexts[i] };
        if !c.native.is_null() && (client == 0 || c.client == client) {
            unsafe {
                (p.context_free)(p.opaque, i as u32);
                (*s).contexts[i] = Context::EMPTY;
            }
        }
    }
}

#[derive(Default)]
pub(crate) struct Lookup {
    key: Option<(u32, u32, u32)>,
    indices: (usize, usize),
}

unsafe fn execute(s: *mut Resources, p: &Platform, r: &[u8], pw: u32, ph: u32) -> Result<(), u32> {
    unsafe { execute_cached(s, p, r, pw, ph, &mut Lookup::default()) }
}

// A cache lives for one immutable batch. Scalar/data/query callbacks can mutate
// native GL resources but cannot create/delete this context/drawable registry.
// Every lifecycle/presentation/unknown opcode invalidates before its callback.
pub(crate) unsafe fn execute_cached(
    s: *mut Resources,
    p: &Platform,
    r: &[u8],
    pw: u32,
    ph: u32,
    lookup: &mut Lookup,
) -> Result<(), u32> {
    if r.len() < 32 {
        return Err(BATCH);
    }
    let word = |i| -> Result<u32, u32> {
        Ok(u32::from_le_bytes(
            r.get(i..i + 4).ok_or(BATCH)?.try_into().unwrap(),
        ))
    };
    let op = word(0)?;
    let client = word(8)?;
    let context_id = word(12)?;
    let drawable_id = word(16)?;
    let flags = word(20)?;
    let reusable = matches!(op, 6 | 10 | 11);
    let key = (client, context_id, drawable_id);
    let cached = reusable && lookup.key == Some(key);
    if !cached {
        lookup.key = None;
    }
    let ci = if cached {
        Some(lookup.indices.0)
    } else {
        unsafe { context(s, client, context_id) }
    };
    // Draw commands use the context's associated drawable below, not the
    // optional header identity. Avoid searching both versions on every record.
    let di = if reusable {
        None
    } else {
        unsafe { drawable(s, client, drawable_id) }
    };
    let code = |n| if n == 0 { Ok(()) } else { Err(n) };
    match op {
        9 => {
            if r.len() != 96 {
                return Err(BATCH);
            }
            code(unsafe { (p.desktop)(p.opaque, r.as_ptr()) })
        }
        1 => {
            let share_id = word(32)?;
            let share = unsafe { context(s, client, share_id) };
            if context_id == 0 || ci.is_some() || (share_id != 0 && share.is_none()) {
                return Err(CONTEXT);
            }
            let slot = (0..MAX)
                .find(|&i| unsafe { (*s).contexts[i].native.is_null() })
                .ok_or(LIMIT)?;
            let native_share = share.map_or(core::ptr::null_mut(), |i| unsafe {
                (*s).contexts[i].native
            });
            let native = unsafe { (p.context_new)(p.opaque, native_share) };
            if native.is_null() {
                return Err(HOST);
            }
            unsafe {
                (*s).contexts[slot] = Context {
                    client,
                    id: context_id,
                    drawable: 0,
                    native,
                };
            }
            Ok(())
        }
        3 => {
            if drawable_id == 0 || di.is_some() {
                return Err(DRAWABLE);
            }
            let (width, height) = (word(32)?, word(36)?);
            if width == 0 || height == 0 || width > 4096 || height > 4096 {
                return Err(DRAWABLE);
            }
            let mut bytes = u64::from(width) * u64::from(height) * 24;
            for i in 0..MAX {
                let (w, h) = unsafe { ((*s).drawables[i].width, (*s).drawables[i].height) };
                bytes += u64::from(w) * u64::from(h) * 24;
            }
            if bytes > 0x20000000 {
                return Err(LIMIT);
            }
            let slot = (0..MAX)
                .find(|&i| unsafe { (*s).drawables[i].native.is_null() })
                .ok_or(LIMIT)?;
            // Never permit a wrapped epoch to alias an old exported resource.
            let epoch = unsafe { (*s).next_epoch }.checked_add(1).ok_or(LIMIT)?;
            let native = unsafe { (p.drawable_new)(p.opaque, width, height) };
            if native.is_null() {
                return Err(HOST);
            }
            unsafe {
                (*s).drawables[slot] = Drawable {
                    client,
                    id: drawable_id,
                    width,
                    height,
                    native,
                    ..Drawable::EMPTY
                };
                (*s).next_epoch = epoch;
                for i in 0..MAX {
                    (*s).drawables[i].epoch = epoch;
                }
            }
            Ok(())
        }
        8 => {
            unsafe {
                close(s, p, client);
            }
            Ok(())
        }
        2 => {
            let i = ci.ok_or(CONTEXT)?;
            unsafe {
                (p.context_free)(p.opaque, i as u32);
                (*s).contexts[i] = Context::EMPTY;
            }
            Ok(())
        }
        4 => {
            let i = di.ok_or(DRAWABLE)?;
            unsafe {
                (p.drawable_free)(p.opaque, i as u32);
                (*s).drawables[i] = Drawable::EMPTY;
            }
            Ok(())
        }
        5 => {
            let (ci, di) = (ci.ok_or(CONTEXT)?, di.ok_or(CONTEXT)?);
            code(unsafe { (p.make_current)(p.opaque, ci as u32, di as u32) })?;
            unsafe {
                (*s).contexts[ci].drawable = drawable_id;
            }
            Ok(())
        }
        6 | 7 | 10 | 11 => {
            let ci = ci.ok_or(CONTEXT)?;
            let associated = unsafe { (*s).contexts[ci].drawable };
            let di = if cached {
                lookup.indices.1
            } else {
                unsafe { drawable(s, client, associated) }.ok_or(DRAWABLE)?
            };
            let (id, width, height) = unsafe {
                (
                    (*s).drawables[di].id,
                    (*s).drawables[di].width,
                    (*s).drawables[di].height,
                )
            };
            if drawable_id != 0 && drawable_id != id {
                return Err(DRAWABLE);
            }
            if reusable {
                lookup.key = Some(key);
                lookup.indices = (ci, di);
            }
            if op == 7 && flags & 16 != 0 && (word(32)? != width || word(36)? != height) {
                return Err(DRAWABLE);
            }
            let list_array = op == 10 && word(32)? == crate::gl_api::FEnum_glCallLists;
            if op != 6 && !list_array && unsafe { (p.in_begin)(p.opaque, ci as u32) } != 0 {
                return Err(CONTEXT);
            }
            code(unsafe { (p.make_current)(p.opaque, ci as u32, di as u32) })?;
            match op {
                6 => {
                    let function = word(32)?;
                    code(unsafe { (p.call)(p.opaque, ci as u32, function, r.as_ptr().add(36)) })
                }
                10 => {
                    let function = word(32)?;
                    let data_bytes = word(36)?;
                    let words = unsafe { (p.words)(p.opaque, function) } & !0x80000000;
                    let offset = 40usize.checked_add(words as usize * 4).ok_or(BATCH)?;
                    if offset > r.len() || data_bytes as usize > r.len() - offset {
                        return Err(BATCH);
                    }
                    code(unsafe {
                        (p.data)(
                            p.opaque,
                            ci as u32,
                            function,
                            r.as_ptr().add(40),
                            r.as_ptr().add(offset),
                            data_bytes,
                        )
                    })
                }
                11 => {
                    let function = word(32)?;
                    code(unsafe { (p.query)(p.opaque, ci as u32, function, r.as_ptr().add(36)) })
                }
                _ => {
                    if flags & 1 != 0 && (width != pw || height != ph) {
                        return Err(DRAWABLE);
                    }
                    code(unsafe { (p.present)(p.opaque, ci as u32, di as u32, flags) })
                }
            }
        }
        _ => Err(3),
    }
}

/// # Safety
/// Registry is initialized to zero. The sole render worker owns mutation; output
/// readers are drained by the platform callbacks before destruction/reassignment.
/// The record is an immutable, validated DMA snapshot, and callbacks are valid for
/// this invocation. Callbacks may inspect registry fields but must not reenter this
/// dispatcher. No borrowed Rust registry references cross a callback boundary.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_execute(
    s: *mut Resources,
    p: *const Platform,
    record: *const u8,
    bytes: usize,
    width: u32,
    height: u32,
) -> u32 {
    if s.is_null() || p.is_null() || record.is_null() || bytes > isize::MAX as usize {
        return BATCH;
    }
    match unsafe {
        execute(
            s,
            &*p,
            core::slice::from_raw_parts(record, bytes),
            width,
            height,
        )
    } {
        Ok(()) => 0,
        Err(error) => error,
    }
}

/// # Safety
/// Same registry and platform ownership contract as `dreamgpu_gl_execute`.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_close_client(
    s: *mut Resources,
    p: *const Platform,
    client: u32,
) {
    if !s.is_null() && !p.is_null() {
        unsafe {
            close(s, &*p, client);
        }
    }
}

#[cfg(test)]
mod tests;
