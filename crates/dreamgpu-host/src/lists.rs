// SPDX-License-Identifier: GPL-2.0-or-later
//! Display lists own immutable logical commands, not native object names. Replay
//! passes through the same resource callbacks as ordinary commands, so texture
//! storage accounting and deleted/bound ownership remain authoritative.
#![allow(non_upper_case_globals)]
use crate::{
    gl_api::*,
    state::ContextState,
    texture::names::{Memory, Namespace as Textures},
};
use core::{
    ffi::c_void,
    ptr::{addr_of_mut, null_mut},
};
const SLOTS: usize = 8192;
const NAMES: u32 = 4096;
const NESTING: u32 = 64;
const WORK: u32 = 1_048_576;
#[repr(C)]
#[derive(Clone, Copy)]
struct Entry {
    name: u32,
    head: *mut Node,
}
#[repr(C)]
pub struct Namespace {
    count: u32,
    next: u32,
    entries: [Entry; SLOTS],
}
#[repr(C)]
struct Node {
    next: *mut Node,
    function: u32,
    words: u32,
    bytes: u32,
    kind: u32,
}
pub struct State {
    name: u32,
    mode: u32,
    begin: u32,
    failed: u32,
    head: *mut Node,
    tail: *mut Node,
    image_function: u32,
    image: [u32; 8],
    image_received: u32,
}
impl State {
    const EMPTY: Self = Self {
        name: 0,
        mode: 0,
        begin: 0,
        failed: 0,
        head: null_mut(),
        tail: null_mut(),
        image_function: 0,
        image: [0; 8],
        image_received: 0,
    };
}
pub type Execute = unsafe extern "C" fn(*mut c_void, u32, u32, *const u8, *const u8, u32) -> u32;
fn hash(name: u32) -> usize {
    (name.wrapping_mul(0x9e3779b1) as usize) & (SLOTS - 1)
}
unsafe fn locate(ns: *mut Namespace, name: u32) -> Result<usize, usize> {
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
    unreachable!("half-full bounded list namespace")
}
unsafe fn alloc(m: &Memory, n: usize) -> *mut u8 {
    if n as u64 > u64::from(crate::pixel_image::LIMIT)
        || unsafe { *m.image_bytes } > u64::from(crate::pixel_image::LIMIT) - n as u64
    {
        return null_mut();
    }
    let p = unsafe { (m.allocate)(m.opaque, n) }.cast::<u8>();
    if !p.is_null() {
        unsafe {
            *m.image_bytes += n as u64;
        }
    }
    p
}
unsafe fn free(m: &Memory, p: *mut u8, n: usize) {
    if !p.is_null() {
        unsafe {
            *m.image_bytes -= n as u64;
            (m.free)(m.opaque, p.cast());
        }
    }
}
unsafe fn drop_nodes(m: &Memory, mut p: *mut Node) {
    while !p.is_null() {
        let next = unsafe { (*p).next };
        let n =
            unsafe { core::mem::size_of::<Node>() + (*p).words as usize * 4 + (*p).bytes as usize };
        unsafe {
            free(m, p.cast(), n);
        }
        p = next;
    }
}
pub(crate) unsafe fn namespace_release(m: &Memory, ns: *mut Namespace) {
    if ns.is_null() {
        return;
    }
    for i in 0..SLOTS {
        unsafe {
            drop_nodes(m, (*ns).entries[i].head);
        }
    }
    unsafe {
        free(m, ns.cast(), core::mem::size_of::<Namespace>());
    }
}
pub(crate) unsafe fn release(m: &Memory, state: *mut State) {
    if !state.is_null() {
        unsafe {
            drop_nodes(m, (*state).head);
            free(m, state.cast(), core::mem::size_of::<State>());
        }
    }
}
unsafe fn namespace(m: &Memory, textures: *mut Textures) -> *mut Namespace {
    let current = unsafe { (*textures).lists };
    if !current.is_null() {
        return current;
    }
    let ns = unsafe { alloc(m, core::mem::size_of::<Namespace>()) }.cast::<Namespace>();
    if !ns.is_null() {
        unsafe {
            core::ptr::write_bytes(ns, 0, 1);
            (*ns).next = 1;
            (*textures).lists = ns;
        }
    }
    ns
}
unsafe fn remove(m: &Memory, ns: *mut Namespace, index: usize) {
    unsafe {
        drop_nodes(m, (*ns).entries[index].head);
        (*ns).count -= 1;
    }
    let mut hole = index;
    unsafe {
        (*ns).entries[hole] = Entry {
            name: 0,
            head: null_mut(),
        };
    }
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
                (*ns).entries[at] = Entry {
                    name: 0,
                    head: null_mut(),
                };
            }
            hole = at;
        }
    }
}
fn word(args: &[u8], n: usize) -> u32 {
    u32::from_le_bytes(args[n * 4..n * 4 + 4].try_into().unwrap())
}
pub(crate) fn query_function(f: u32) -> bool {
    matches!(
        f,
        FEnum_glNewList | FEnum_glEndList | FEnum_glGenLists | FEnum_glDeleteLists | FEnum_glIsList
    )
}
pub(crate) fn query_shape(f: u32, a: [u32; 3]) -> u32 {
    if a[2] != 0 {
        return 0;
    }
    match f {
        FEnum_glNewList if a[0] != 0 && matches!(a[1], GL_COMPILE | GL_COMPILE_AND_EXECUTE) => 1,
        FEnum_glEndList if a[0] == 0 && a[1] == 0 => 1,
        FEnum_glGenLists if a[0] <= NAMES && a[1] == 0 => 2,
        FEnum_glDeleteLists if a[1] <= i32::MAX as u32 => 1,
        FEnum_glIsList if a[1] == 0 => 1,
        _ => 0,
    }
}
unsafe fn put(out: *mut u8, at: usize, v: u32) {
    unsafe {
        core::ptr::copy_nonoverlapping(v.to_le_bytes().as_ptr(), out.add(at * 4), 4);
    }
}
/// Validated bounded query; state is render-worker-owned and allocator callbacks
/// cannot enter guest execution. Entry publication occurs only at successful EndList.
pub(crate) unsafe fn query(
    m: &Memory,
    c: *mut ContextState,
    f: u32,
    a: [u32; 3],
    out: *mut u8,
) -> Result<(), u32> {
    if f == FEnum_glIsList {
        let ns = unsafe { (*(*c).textures).lists };
        let found = a[0] != 0 && !ns.is_null() && unsafe { locate(ns, a[0]).is_ok() };
        unsafe {
            *out = u8::from(found);
        }
        return Ok(());
    }
    if (f == FEnum_glGenLists && a[0] == 0) || (f == FEnum_glDeleteLists && a[1] == 0) {
        unsafe {
            put(out, 0, 0);
            if f == FEnum_glGenLists {
                put(out, 1, 0);
            }
        }
        return Ok(());
    }
    if f == FEnum_glEndList && unsafe { (*c).list_mode } == 0 {
        unsafe {
            put(out, 0, GL_INVALID_OPERATION);
        }
        return Ok(());
    }
    let ns = unsafe { namespace(m, (*c).textures) };
    if ns.is_null() {
        unsafe {
            put(out, 0, GL_OUT_OF_MEMORY);
            if f == FEnum_glGenLists {
                put(out, 1, 0);
            }
        }
        return Ok(());
    }
    let mut error = 0;
    if f == FEnum_glNewList {
        if unsafe { (*c).list_mode } != 0 {
            error = GL_INVALID_OPERATION;
        } else {
            let mut s = unsafe { (*c).lists };
            if s.is_null() {
                s = unsafe { alloc(m, core::mem::size_of::<State>()) }.cast();
                if !s.is_null() {
                    unsafe {
                        s.write(State::EMPTY);
                        (*c).lists = s;
                    }
                }
            }
            if s.is_null() {
                error = GL_OUT_OF_MEMORY;
            } else {
                unsafe {
                    (*s).name = a[0];
                    (*s).mode = a[1];
                    (*c).list_mode = a[1];
                }
            }
        }
    } else if f == FEnum_glEndList {
        let s = unsafe { (*c).lists };
        if s.is_null() || unsafe { (*c).list_mode } == 0 || unsafe { (*s).begin } != 0 {
            error = GL_INVALID_OPERATION;
        } else {
            error = unsafe { (*s).failed };
            if unsafe { (*s).image_function } != 0 {
                error = GL_INVALID_OPERATION;
            }
            let location = unsafe { locate(ns, (*s).name) };
            if error == 0 && location.is_err() && unsafe { (*ns).count } == NAMES {
                error = GL_OUT_OF_MEMORY;
            }
            let old = unsafe { s.read() };
            unsafe {
                s.write(State::EMPTY);
                (*c).list_mode = 0;
            }
            if error == 0 {
                let index = match location {
                    Ok(i) => i,
                    Err(i) => {
                        unsafe {
                            (*ns).count += 1;
                        }
                        i
                    }
                };
                let previous = unsafe { (*ns).entries[index].head };
                unsafe {
                    (*ns).entries[index] = Entry {
                        name: old.name,
                        head: old.head,
                    };
                    drop_nodes(m, previous);
                }
            } else {
                unsafe {
                    drop_nodes(m, old.head);
                }
            }
        }
    } else if f == FEnum_glDeleteLists {
        let end = u64::from(a[0]) + u64::from(a[1]);
        let mut at = 0;
        while at < SLOTS {
            let name = unsafe { (*ns).entries[at].name };
            if name != 0 && name >= a[0] && u64::from(name) < end {
                unsafe {
                    remove(m, ns, at);
                }
            } else {
                at += 1;
            }
        }
    } else if f == FEnum_glGenLists {
        let n = a[0];
        let mut first = 0;
        if n != 0 {
            if n > NAMES - unsafe { (*ns).count } {
                error = GL_OUT_OF_MEMORY;
            } else {
                let mut start = unsafe { (*ns).next }.max(1);
                let mut found = 0;
                // At most NAMES occupied names can interrupt a candidate run. Search at
                // most that many interruptions, with u64 endpoints and one defined wrap.
                for _ in 0..=NAMES + 1 {
                    if u64::from(start) + u64::from(n) > u64::from(u32::MAX) + 1 {
                        start = 1;
                    }
                    found = 0;
                    for i in 0..n {
                        if unsafe { locate(ns, start + i).is_ok() } {
                            start = start.wrapping_add(i).wrapping_add(1).max(1);
                            break;
                        }
                        found += 1;
                    }
                    if found == n {
                        break;
                    }
                }
                if found == n {
                    first = start;
                    for i in 0..n {
                        let slot = unsafe { locate(ns, start + i).unwrap_err() };
                        unsafe {
                            (*ns).entries[slot] = Entry {
                                name: start + i,
                                head: null_mut(),
                            };
                            (*ns).count += 1;
                        }
                    }
                    unsafe {
                        (*ns).next = start.wrapping_add(n).max(1);
                    }
                }
            }
        }
        unsafe {
            put(out, 1, first);
        }
    }
    unsafe {
        put(out, 0, error);
    }
    Ok(())
}
unsafe fn error(c: *mut ContextState, e: u32) {
    unsafe {
        crate::query::store(addr_of_mut!((*c).guest_errors), e);
    }
}
// Commands which depend on client state are already canonicalized into their
// DATA payload. Deletion and PixelStore are explicitly immediate in GL1.1.
fn immediate(f: u32) -> bool {
    matches!(
        f,
        FEnum_glFlush | FEnum_glFinish | FEnum_glPixelStorei | FEnum_glDeleteTextures
    )
}
unsafe fn capture(
    m: &Memory,
    c: *mut ContextState,
    kind: u32,
    f: u32,
    args: &[u8],
    data: &[u8],
) -> bool {
    let s = unsafe { (*c).lists };
    let proxy = matches!(f, FEnum_glTexImage1D | FEnum_glTexImage2D)
        && args.len() >= 4
        && matches!(word(args, 0), GL_PROXY_TEXTURE_1D | GL_PROXY_TEXTURE_2D);
    if s.is_null() || unsafe { (*s).mode } == 0 || immediate(f) || proxy {
        return true;
    }
    let execute = unsafe { (*s).mode } == GL_COMPILE_AND_EXECUTE;
    if f == FEnum_glBegin {
        let code = if unsafe { (*s).begin } != 0 {
            GL_INVALID_OPERATION
        } else if word(args, 0) > GL_POLYGON {
            GL_INVALID_ENUM
        } else {
            0
        };
        if code != 0 {
            if unsafe { capture(m, c, 0, DG_GL_RECORD_ERROR, &code.to_le_bytes(), &[]) } {
                unsafe {
                    error(c, code);
                }
            }
            return false;
        }
        unsafe {
            (*s).begin = 1;
        }
    } else if f == FEnum_glEnd {
        if unsafe { (*s).begin } == 0 {
            if unsafe {
                capture(
                    m,
                    c,
                    0,
                    DG_GL_RECORD_ERROR,
                    &GL_INVALID_OPERATION.to_le_bytes(),
                    &[],
                )
            } {
                unsafe {
                    error(c, GL_INVALID_OPERATION);
                }
            }
            return false;
        }
        unsafe {
            (*s).begin = 0;
        }
    }
    if crate::pixel_image::image_function(f) {
        let a = core::array::from_fn::<_, 8, _>(|i| word(args, i));
        if crate::pixel_image::validate(f, &a, data) != 0 {
            unsafe {
                (*s).failed = GL_INVALID_OPERATION;
            }
            return false;
        }
        if a[6] & crate::pixel_image::FIRST != 0 {
            if unsafe { (*s).image_function } != 0 {
                unsafe {
                    (*s).failed = GL_INVALID_OPERATION;
                }
                return false;
            }
            unsafe {
                (*s).image_function = f;
                (*s).image = a;
                (*s).image_received = 0;
            }
        }
        if unsafe { (*s).image_function } != f
            || unsafe { (*s).image }[..5] != a[..5]
            || unsafe { (*s).image[7] } != a[7]
            || unsafe { (*s).image_received } != a[5]
        {
            unsafe {
                (*s).failed = GL_INVALID_OPERATION;
            }
            return false;
        }
        if a[6] == crate::pixel_image::ABORT {
            unsafe {
                (*s).image_function = 0;
            }
        } else {
            let prefix = usize::from(f == FEnum_glBitmap && a[6] & 1 != 0) * 16;
            unsafe {
                (*s).image_received += (data.len() - prefix) as u32;
                if a[6] & 2 != 0 {
                    (*s).image_function = 0;
                }
            }
        }
    } else if unsafe { (*s).image_function } != 0 {
        unsafe {
            (*s).failed = GL_INVALID_OPERATION;
        }
        return false;
    }
    if unsafe { (*s).failed } != 0 {
        return execute;
    }
    let bytes = core::mem::size_of::<Node>() + args.len() + data.len();
    let node = unsafe { alloc(m, bytes) }.cast::<Node>();
    if node.is_null() {
        unsafe {
            (*s).failed = GL_OUT_OF_MEMORY;
        }
        return execute;
    }
    unsafe {
        node.write(Node {
            next: null_mut(),
            function: f,
            words: (args.len() / 4) as u32,
            bytes: data.len() as u32,
            kind,
        });
        let p = node.add(1).cast::<u8>();
        core::ptr::copy_nonoverlapping(args.as_ptr(), p, args.len());
        core::ptr::copy_nonoverlapping(data.as_ptr(), p.add(args.len()), data.len());
        if (*s).tail.is_null() {
            (*s).head = node;
        } else {
            (*(*s).tail).next = node;
        }
        (*s).tail = node;
    }
    execute
}
struct Replay<'a> {
    m: &'a Memory,
    c: *mut ContextState,
    in_begin: *mut u32,
    callback: Execute,
    opaque: *mut c_void,
    work: u32,
}
impl Replay<'_> {
    unsafe fn list(&mut self, name: u32, depth: u32) -> Result<(), u32> {
        if name == 0 {
            unsafe {
                error(self.c, GL_INVALID_VALUE);
            }
            return Ok(());
        }
        if depth == NESTING {
            return Ok(());
        }
        let ns = unsafe { (*(*self.c).textures).lists };
        if ns.is_null() {
            return Ok(());
        }
        let Ok(index) = (unsafe { locate(ns, name) }) else {
            return Ok(());
        };
        let mut node = unsafe { (*ns).entries[index].head };
        while !node.is_null() {
            if self.work == WORK {
                unsafe {
                    error(self.c, GL_OUT_OF_MEMORY);
                }
                return Ok(());
            }
            self.work += 1;
            let n = unsafe { &*node };
            let args = unsafe {
                core::slice::from_raw_parts(node.add(1).cast::<u8>(), n.words as usize * 4)
            };
            let data = unsafe {
                core::slice::from_raw_parts(args.as_ptr().add(args.len()), n.bytes as usize)
            };
            unsafe {
                self.command(n.kind, n.function, args, data, depth + 1)?;
            }
            node = n.next;
        }
        Ok(())
    }
    unsafe fn command(
        &mut self,
        kind: u32,
        f: u32,
        args: &[u8],
        data: &[u8],
        depth: u32,
    ) -> Result<(), u32> {
        if f == DG_GL_RECORD_ERROR {
            let code = word(args, 0);
            if !(GL_INVALID_ENUM..=GL_OUT_OF_MEMORY).contains(&code) {
                return Err(DG_GL_ERROR_BATCH);
            }
            unsafe {
                error(self.c, code);
            }
            return Ok(());
        }
        if f == FEnum_glCallList {
            return unsafe { self.list(word(args, 0), depth) };
        }
        if f == FEnum_glCallLists {
            let mut base = 0i32;
            let api = unsafe { &*self.m.api };
            unsafe {
                api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(GL_LIST_BASE, &mut base);
            }
            for raw in data.as_chunks::<4>().0 {
                unsafe {
                    self.list((base as u32).wrapping_add(u32::from_le_bytes(*raw)), depth)?;
                }
            }
            return Ok(());
        }
        if unsafe { *self.in_begin } != 0
            && !crate::scalar::allowed_in_begin(f)
            && !(kind == 1 && f == FEnum_glMaterialfv)
        {
            unsafe {
                error(self.c, GL_INVALID_OPERATION);
            }
            return Ok(());
        }
        // Replaying an immutable image is a new internal transaction. Preserve the
        // external transport's retired ID and never advance it with replay IDs.
        if depth > 0
            && crate::pixel_image::image_function(f)
            && word(args, 6) & crate::pixel_image::FIRST != 0
        {
            if unsafe { (*self.c).image.active } != 0 {
                return Err(DG_GL_ERROR_CONTEXT);
            }
            unsafe {
                (*self.c).image.last_id = 0;
            }
        }
        let result = unsafe {
            (self.callback)(
                self.opaque,
                kind,
                f,
                args.as_ptr(),
                data.as_ptr(),
                data.len() as u32,
            )
        };
        if result == 0 {
            Ok(())
        } else {
            Err(result)
        }
    }
}
/// # Safety
/// Immutable validated command, render-worker-owned context and memory. Native
/// callbacks execute synchronously and cannot mutate display-list storage.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_list_dispatch(
    m: *const Memory,
    c: *mut ContextState,
    in_begin: *mut u32,
    callback: Execute,
    opaque: *mut c_void,
    kind: u32,
    f: u32,
    args: *const u8,
    words: u32,
    data: *const u8,
    bytes: u32,
) -> u32 {
    let m = unsafe { &*m };
    let args = unsafe { core::slice::from_raw_parts(args, words as usize * 4) };
    let data = if bytes == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(data, bytes as usize) }
    };
    if matches!(f, FEnum_glCallList | FEnum_glCallLists) {
        let invalid = unsafe { crate::pixel_image::interleave(m, addr_of_mut!((*c).image)) };
        if invalid != 0 {
            return invalid;
        }
    }
    if !unsafe { capture(m, c, kind, f, args, data) } {
        return 0;
    }
    let retired = unsafe { (*c).image.last_id };
    let mut replay = Replay {
        m,
        c,
        in_begin,
        callback,
        opaque,
        work: 0,
    };
    let result = unsafe { replay.command(kind, f, args, data, 0) };
    if matches!(f, FEnum_glCallList | FEnum_glCallLists) {
        unsafe {
            if (*c).image.active != 0 {
                crate::pixel_image::release(m, addr_of_mut!((*c).image));
            }
            (*c).image.last_id = retired;
        }
    }
    result.err().unwrap_or(0)
}

pub(crate) unsafe fn state_value(c: *mut ContextState, pname: u32) -> u32 {
    if pname == GL_MAX_LIST_NESTING {
        return NESTING;
    }
    let s = unsafe { (*c).lists };
    if s.is_null() {
        return 0;
    }
    unsafe {
        if pname == GL_LIST_INDEX {
            (*s).name
        } else {
            (*s).mode
        }
    }
}

#[cfg(test)]
mod tests;
