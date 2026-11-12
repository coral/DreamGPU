// SPDX-License-Identifier: GPL-2.0-or-later
//! ARB_vertex_buffer_object objects. Guest buffer names live in a bounded
//! table owned by the texture namespace, so wglShareLists shares buffers like
//! textures, lists and programs. Records name their buffer: bindings, array
//! pointers and mappings are frontend client state, and the host holds none.
//!
//! Every object has a private native buffer in the host's single native share
//! group, which draws read directly, and a host copy of its contents. The copy
//! lets the host check element-buffer indices of every draw against the
//! buffers they address without reading native storage back (by the maximum
//! values tracked per buffer when they fit, else by a scan); native GL never
//! sees an index or attribute read the host has not bounded. Storage is
//! charged against the shared resource budget at its size.
//!
//! Native binding state is never left changed: every native operation binds
//! the object on ARRAY_BUFFER and restores zero, the only binding native
//! contexts hold between records.
#![allow(non_upper_case_globals)]
use crate::{
    arrays::raw::{index_value, BufferLayout},
    gl_api::*,
    query,
    state::ContextState,
    texture::names::{Memory, Namespace as Textures},
};
use core::ptr::{addr_of_mut, null_mut};

const SLOTS: usize = 2 * DG_GL_MAX_BUFFERS as usize;
const _: () = assert!(SLOTS.is_power_of_two());
/// Nominal native bookkeeping of one buffer object, charged with its storage.
pub(crate) const OBJECT_BYTES: u64 = 256;

#[repr(C)]
pub struct Buffer {
    /// Native buffer object name.
    pub native: u32,
    pub size: u32,
    pub usage: u32,
    /// Host copy of the contents: `size` bytes, null when empty.
    pub data: *mut u8,
    /// Upper bounds of every aligned UNSIGNED_BYTE, UNSIGNED_SHORT and
    /// UNSIGNED_INT value in the contents. BufferSubData only raises them, so
    /// they stay bounds; a buffer-only indexed draw that they already fit
    /// needs no scan of its indices.
    pub bounds: [u32; 3],
}
/// Raise `bounds` by every aligned 1-, 2- and 4-byte value of
/// `contents[start..end]`, widened to whole aligned values within `contents`.
fn raise_bounds(bounds: &mut [u32; 3], contents: &[u8], start: usize, end: usize) {
    for (k, size) in [1usize, 2, 4].into_iter().enumerate() {
        let first = start & !(size - 1);
        let last = ((end + size - 1) & !(size - 1)).min(contents.len() & !(size - 1));
        if first >= last {
            continue;
        }
        let high = contents[first..last]
            .chunks_exact(size)
            .map(index_value)
            .max()
            .unwrap_or(0);
        bounds[k] = bounds[k].max(high);
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
struct Entry {
    name: u32,
    buffer: *mut Buffer,
}
const EMPTY: Entry = Entry {
    name: 0,
    buffer: null_mut(),
};
#[repr(C)]
pub struct Namespace {
    count: u32,
    entries: [Entry; SLOTS],
}
fn namespace_charge() -> u64 {
    core::mem::size_of::<Namespace>() as u64
}
fn hash(name: u32) -> usize {
    (name.wrapping_mul(0x9e3779b1) as usize) & (SLOTS - 1)
}
unsafe fn locate(ns: *const Namespace, name: u32) -> Result<usize, usize> {
    for n in 0..SLOTS {
        let i = (hash(name) + n) & (SLOTS - 1);
        let key = unsafe { (*ns).entries[i].name };
        if key == name {
            return Ok(i);
        }
        if key == 0 {
            return Err(i);
        }
    }
    unreachable!("half-full bounded buffer namespace")
}
/// Backward-shift deletion keeps probe chains intact without tombstones.
unsafe fn remove(ns: *mut Namespace, index: usize) {
    unsafe {
        (*ns).count -= 1;
        (*ns).entries[index] = EMPTY;
    }
    let mut hole = index;
    for step in 1..SLOTS {
        let at = (index + step) & (SLOTS - 1);
        let e = unsafe { (*ns).entries[at] };
        if e.name == 0 {
            break;
        }
        let origin = hash(e.name);
        if hole.wrapping_sub(origin) & (SLOTS - 1) < at.wrapping_sub(origin) & (SLOTS - 1) {
            unsafe {
                (*ns).entries[hole] = e;
                (*ns).entries[at] = EMPTY;
            }
            hole = at;
        }
    }
}

/// Reserve budget bytes; false when the shared resource budget is exhausted.
pub(crate) unsafe fn charge(m: &Memory, bytes: u64) -> bool {
    let used = unsafe { *m.bytes };
    if bytes > u64::from(DG_GL_MAX_TEXTURE_BYTES).saturating_sub(used) {
        return false;
    }
    unsafe { *m.bytes = used + bytes };
    true
}
pub(crate) unsafe fn uncharge(m: &Memory, bytes: u64) {
    unsafe {
        *m.bytes = (*m.bytes)
            .checked_sub(bytes)
            .expect("buffer accounting underflow");
    }
}
unsafe fn error(c: *mut ContextState, code: u32) {
    unsafe { query::store(addr_of_mut!((*c).guest_errors), code) };
}

/// Release a buffer's contents, keeping the object (size zero).
unsafe fn empty(m: &Memory, b: *mut Buffer) {
    unsafe {
        if !(*b).data.is_null() {
            (m.free)(m.opaque, (*b).data.cast());
            (*b).data = null_mut();
        }
        uncharge(m, u64::from((*b).size));
        (*b).size = 0;
    }
}
/// Delete one object: native storage, contents, charge and the object itself.
unsafe fn destroy(m: &Memory, b: *mut Buffer) {
    if b.is_null() {
        return;
    }
    unsafe {
        empty(m, b);
        if (*b).native != 0 {
            (*m.api).dg_glDeleteBuffers.expect("complete API")(1, &(*b).native);
        }
        (m.free)(m.opaque, b.cast());
        uncharge(m, OBJECT_BYTES);
    }
}

/// The share group's buffer table, created on first use. Null when the
/// allocation or its budget charge fails.
unsafe fn namespace(m: &Memory, textures: *mut Textures) -> *mut Namespace {
    let current = unsafe { (*textures).buffers };
    if !current.is_null() || !unsafe { charge(m, namespace_charge()) } {
        return current;
    }
    let ns =
        unsafe { (m.allocate)(m.opaque, core::mem::size_of::<Namespace>()) }.cast::<Namespace>();
    if ns.is_null() {
        unsafe { uncharge(m, namespace_charge()) };
        return ns;
    }
    unsafe {
        core::ptr::write_bytes(ns, 0, 1);
        (*textures).buffers = ns;
    }
    ns
}

/// # Safety
/// The last context of the share group is releasing it, with a native
/// context current.
pub(crate) unsafe fn namespace_release(m: &Memory, ns: *mut Namespace) {
    if ns.is_null() {
        return;
    }
    for i in 0..SLOTS {
        unsafe { destroy(m, (*ns).entries[i].buffer) };
    }
    unsafe {
        (m.free)(m.opaque, ns.cast());
        uncharge(m, namespace_charge());
    }
}

/// The object of a guest name, or null.
///
/// # Safety
/// Read-only render-owned context.
pub(crate) unsafe fn find(c: *const ContextState, name: u32) -> *const Buffer {
    let ns = unsafe { (*(*c).textures).buffers };
    if name == 0 || ns.is_null() {
        return core::ptr::null();
    }
    match unsafe { locate(ns, name) } {
        Ok(i) => unsafe { (*ns).entries[i].buffer },
        Err(_) => core::ptr::null(),
    }
}

/// Native BufferData/BufferSubData of `b` bound on ARRAY_BUFFER, then zero
/// again. Returns the native error.
unsafe fn native(api: &DreamGpuGlApi, b: *const Buffer, upload: impl FnOnce()) -> u32 {
    let bind = api.dg_glBindBuffer.expect("complete API");
    unsafe {
        bind(GL_ARRAY_BUFFER, (*b).native);
        upload();
        bind(GL_ARRAY_BUFFER, 0);
        api.dg_glGetError.expect("complete API")()
    }
}

fn usage(value: u32) -> bool {
    matches!(
        value,
        GL_STREAM_DRAW
            | GL_STREAM_READ
            | GL_STREAM_COPY
            | GL_STATIC_DRAW
            | GL_STATIC_READ
            | GL_STATIC_COPY
            | GL_DYNAMIC_DRAW
            | GL_DYNAMIC_READ
            | GL_DYNAMIC_COPY
    )
}
/// The validated BufferDataARB record: a nonzero name, a usage hint, a size
/// within the protocol bound and exactly that payload or none.
pub(crate) fn data_shape(a: &[u32; 8], data: &[u8]) -> bool {
    a[0] != 0
        && a[1] <= DG_GL_MAX_BUFFER_BYTES
        && usage(a[2])
        && (data.is_empty() || data.len() as u64 == u64::from(a[1]))
}
/// The validated BufferSubDataARB record: a nonzero name and a range within
/// the protocol bound.
pub(crate) fn sub_data_shape(a: &[u32; 8], data: &[u8]) -> bool {
    a[0] != 0 && u64::from(a[1]) + data.len() as u64 <= u64::from(DG_GL_MAX_BUFFER_BYTES)
}

/// BufferDataARB: replace the contents (zero-defined without a payload),
/// creating the object for a new name. Exhausted storage is GL_OUT_OF_MEMORY
/// and leaves the object empty.
///
/// # Safety
/// Render-owned context with its native context current; `a`/`data` passed
/// `data_shape`.
pub(crate) unsafe fn data(
    m: &Memory,
    c: *mut ContextState,
    a: &[u32; 8],
    payload: &[u8],
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let (name, size, hint) = (a[0], a[1], a[2]);
    let gen = api.dg_glGenBuffers.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let upload = api.dg_glBufferData.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    if api.dg_glBindBuffer.is_none()
        || api.dg_glDeleteBuffers.is_none()
        || api.dg_glGetError.is_none()
    {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let ns = unsafe { namespace(m, (*c).textures) };
    if ns.is_null() {
        unsafe { error(c, GL_OUT_OF_MEMORY) };
        return Ok(());
    }
    let b = match unsafe { locate(ns, name) } {
        Ok(i) => unsafe { (*ns).entries[i].buffer },
        Err(slot) => {
            if unsafe { (*ns).count } >= DG_GL_MAX_BUFFERS || !unsafe { charge(m, OBJECT_BYTES) } {
                unsafe { error(c, GL_OUT_OF_MEMORY) };
                return Ok(());
            }
            let b =
                unsafe { (m.allocate)(m.opaque, core::mem::size_of::<Buffer>()) }.cast::<Buffer>();
            let mut native = 0;
            if !b.is_null() {
                unsafe { gen(1, &mut native) };
            }
            if native == 0 {
                unsafe {
                    if !b.is_null() {
                        (m.free)(m.opaque, b.cast());
                    }
                    uncharge(m, OBJECT_BYTES);
                    error(c, GL_OUT_OF_MEMORY);
                }
                return Ok(());
            }
            unsafe {
                b.write(Buffer {
                    native,
                    size: 0,
                    usage: hint,
                    data: null_mut(),
                    bounds: [0; 3],
                });
                (*ns).entries[slot] = Entry { name, buffer: b };
                (*ns).count += 1;
            }
            b
        }
    };
    unsafe {
        query::remember(api, addr_of_mut!((*c).guest_errors))?;
        (*b).usage = hint;
        // The old contents go first: a failed replacement leaves no store.
        if (*b).size != size {
            empty(m, b);
        }
        if size != 0 && (*b).data.is_null() {
            if !charge(m, u64::from(size)) {
                error(c, GL_OUT_OF_MEMORY);
                native(api, b, || {
                    upload(GL_ARRAY_BUFFER, 0, core::ptr::null(), hint)
                });
                return Ok(());
            }
            (*b).data = (m.allocate)(m.opaque, size as usize).cast();
            if (*b).data.is_null() {
                uncharge(m, u64::from(size));
                error(c, GL_OUT_OF_MEMORY);
                native(api, b, || {
                    upload(GL_ARRAY_BUFFER, 0, core::ptr::null(), hint)
                });
                return Ok(());
            }
            (*b).size = size;
        }
        (*b).bounds = [0; 3];
        if size != 0 {
            if payload.is_empty() {
                core::ptr::write_bytes((*b).data, 0, size as usize);
            } else {
                core::ptr::copy_nonoverlapping(payload.as_ptr(), (*b).data, size as usize);
                raise_bounds(&mut (*b).bounds, payload, 0, payload.len());
            }
        }
        let contents = (*b).data.cast_const().cast();
        let code = native(api, b, || {
            upload(GL_ARRAY_BUFFER, size as isize, contents, hint)
        });
        if code != GL_NO_ERROR {
            empty(m, b);
            native(api, b, || {
                upload(GL_ARRAY_BUFFER, 0, core::ptr::null(), hint)
            });
            error(c, code);
        }
    }
    Ok(())
}

/// BufferSubDataARB: store the payload at offset. A range beyond the buffer,
/// or a name without an object, is GL_INVALID_VALUE.
///
/// # Safety
/// Render-owned context with its native context current; `a`/`data` passed
/// `sub_data_shape`.
pub(crate) unsafe fn sub_data(
    m: &Memory,
    c: *mut ContextState,
    a: &[u32; 8],
    payload: &[u8],
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let upload = api.dg_glBufferSubData.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    if api.dg_glBindBuffer.is_none() || api.dg_glGetError.is_none() {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let b = unsafe { find(c, a[0]) }.cast_mut();
    let offset = a[1];
    if b.is_null() || u64::from(offset) + payload.len() as u64 > u64::from(unsafe { (*b).size }) {
        unsafe { error(c, GL_INVALID_VALUE) };
        return Ok(());
    }
    if payload.is_empty() {
        return Ok(());
    }
    unsafe {
        query::remember(api, addr_of_mut!((*c).guest_errors))?;
        core::ptr::copy_nonoverlapping(
            payload.as_ptr(),
            (*b).data.add(offset as usize),
            payload.len(),
        );
        let contents = core::slice::from_raw_parts((*b).data, (*b).size as usize);
        let start = offset as usize;
        raise_bounds(&mut (*b).bounds, contents, start, start + payload.len());
        let code = native(api, b, || {
            upload(
                GL_ARRAY_BUFFER,
                offset as isize,
                payload.len() as isize,
                payload.as_ptr().cast(),
            )
        });
        error(c, code);
    }
    Ok(())
}

/// DeleteBuffersARB: unused names and zero are ignored.
///
/// # Safety
/// Data holds the validated little-endian names; native context current.
pub(crate) unsafe fn delete(m: &Memory, c: *mut ContextState, data: &[u8]) -> Result<(), u32> {
    if unsafe { (*m.api).dg_glDeleteBuffers }.is_none() {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let ns = unsafe { (*(*c).textures).buffers };
    if ns.is_null() {
        return Ok(());
    }
    for raw in data.as_chunks::<4>().0 {
        let name = u32::from_le_bytes(*raw);
        if name == 0 {
            continue;
        }
        let Ok(slot) = (unsafe { locate(ns, name) }) else {
            continue;
        };
        let b = unsafe { (*ns).entries[slot].buffer };
        unsafe {
            remove(ns, slot);
            destroy(m, b);
        }
    }
    Ok(())
}

/// Native buffers of a buffer draw: each buffer slot's and the element
/// buffer's (zero for client data), and the buffer slots whose buffer does
/// not hold every drawn vertex (`unread`), which the draw leaves disabled.
#[derive(Clone, Copy)]
pub(crate) struct Resolved {
    pub slots: [u32; 32],
    pub element: u32,
    pub unread: u32,
}

/// Resolve and bound every buffer read of an admitted buffer draw with a
/// nonzero count. Any state-dependent failure (a missing buffer, an element
/// range beyond its buffer or an element index outside the client vertices)
/// is the GL error returned instead.
///
/// A buffer slot whose buffer does not hold every drawn vertex is never read:
/// it is `unread` and the draw treats its array as disabled. GL raises no
/// error for such reads (they are undefined, and only arrays the vertex
/// stage consumes are fetched), and applications rely on it: Doom 3's ARB2
/// path leaves the texture coordinate array enabled on a small, stale buffer
/// while its vertex program reads generic attributes only.
///
/// # Safety
/// Read-only render-owned context; `layout` is `data`'s admitted layout.
pub(crate) unsafe fn resolve(
    c: *const ContextState,
    a: &[u32; 10],
    layout: &BufferLayout,
    data: &[u8],
) -> Result<Resolved, u32> {
    let [_, base, count, _, _, buffered, vertices, _, element, element_offset] = *a;
    let mut resolved = Resolved {
        slots: [0; 32],
        element: 0,
        unread: 0,
    };
    let mut sizes = [0u32; 32];
    for slot in 0..32 {
        if buffered & (1 << slot) == 0 {
            continue;
        }
        let b = unsafe { find(c, layout.references[slot].name) };
        if b.is_null() {
            return Err(GL_INVALID_OPERATION);
        }
        unsafe {
            resolved.slots[slot] = (*b).native;
            sizes[slot] = (*b).size;
        }
    }
    // The buffer slots whose read of vertex `high` (their last) would end
    // beyond their buffer.
    let beyond = |high: u32| {
        (0..32)
            .filter(|slot| buffered & (1 << slot) != 0)
            .filter(|&slot| {
                let attribute = layout.slots[slot];
                let r = layout.references[slot];
                let element_bytes = u64::from(attribute.size) * attribute.unit as u64;
                let stride = if r.stride == 0 {
                    element_bytes
                } else {
                    u64::from(r.stride)
                };
                u64::from(r.offset) + u64::from(high) * stride + element_bytes
                    > u64::from(sizes[slot])
            })
            .fold(0u32, |mask, slot| mask | 1 << slot)
    };
    // The inclusive range of vertex indices read.
    let (low, high) = if layout.index_size == 0 {
        (base, base + (count - 1))
    } else {
        let size = layout.index_size;
        if element != 0 && vertices == 0 {
            // A buffer-only draw from an element buffer whose every value
            // already fits needs no scan (static index buffers drawn again).
            let b = unsafe { find(c, element) };
            if b.is_null() {
                return Err(GL_INVALID_OPERATION);
            }
            let end = u64::from(element_offset) + u64::from(count) * size as u64;
            if end > u64::from(unsafe { (*b).size }) {
                return Err(GL_INVALID_OPERATION);
            }
            let bound = unsafe { (*b).bounds[size.trailing_zeros() as usize] };
            if beyond(bound) == 0 {
                resolved.element = unsafe { (*b).native };
                return Ok(resolved);
            }
        }
        let indices: &[u8] = if element == 0 {
            &data[layout.indices..layout.indices + count as usize * size]
        } else {
            let b = unsafe { find(c, element) };
            if b.is_null() {
                return Err(GL_INVALID_OPERATION);
            }
            let end = u64::from(element_offset) + u64::from(count) * size as u64;
            if end > u64::from(unsafe { (*b).size }) {
                return Err(GL_INVALID_OPERATION);
            }
            unsafe {
                resolved.element = (*b).native;
                core::slice::from_raw_parts(
                    (*b).data.add(element_offset as usize),
                    count as usize * size,
                )
            }
        };
        let (mut low, mut high) = (u32::MAX, 0);
        for index in indices.chunks_exact(size) {
            let v = index_value(index);
            low = low.min(v);
            high = high.max(v);
        }
        (low, high)
    };
    if vertices != 0 && (low < base || u64::from(high) >= u64::from(base) + u64::from(vertices)) {
        return Err(GL_INVALID_OPERATION);
    }
    resolved.unread = beyond(high);
    Ok(resolved)
}

#[cfg(test)]
mod tests;
