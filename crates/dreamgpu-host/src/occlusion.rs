// SPDX-License-Identifier: GPL-2.0-or-later
//! ARB_occlusion_query (SAMPLES_PASSED) and NV_fence (ALL_COMPLETED_NV).
//!
//! Query and fence names belong to one context, as both extensions require
//! (neither object is shared by wglShareLists). Each context owns two bounded
//! name tables, created on first use: an entry is a reserved name (GenQueries,
//! GenFencesNV) or an object, which a query becomes at its first BeginQuery
//! and a fence at its first SetFenceNV, whatever the name's origin. A query
//! object owns a private native query of the context's native context; a fence
//! owns the native sync object of its last SetFenceNV until that signals.
//!
//! Results stay asynchronous. Native GL keeps counting while the guest keeps
//! submitting; only QUERY_RESULT and FinishFenceNV wait. An unavailable answer
//! flushes native GL so availability arrives in finite time. Every EndQuery is
//! also queued, oldest first, until it has been reported once: an availability
//! answer carries the results of earlier ended queries that have completed, so
//! a guest polling several queries usually pays one round trip. Native queries
//! complete in order, so the queue is read only up to its first pending entry.
//!
//! Everything that depends on GL state is a GL error; records the host would
//! reject as malformed never reach this module. Objects, tables and the
//! context's query state are charged against the resource budget.
#![allow(non_upper_case_globals)]
use crate::{
    buffer::{charge, uncharge},
    gl_api::*,
    query,
    state::ContextState,
    texture::names::Memory,
};
use core::ptr::{addr_of_mut, null_mut};

pub(crate) const ALL_COMPLETED_NV: u32 = 0x84f2;
pub(crate) const FENCE_STATUS_NV: u32 = 0x84f3;
pub(crate) const FENCE_CONDITION_NV: u32 = 0x84f4;
const SLOTS: usize = 2 * DG_GL_MAX_QUERIES as usize;
const _: () = assert!(SLOTS.is_power_of_two());
/// Ended queries awaiting their one report.
const ENDED: usize = DG_GL_QUERY_REPORTS as usize + 1;
/// Nominal native bookkeeping of one query or fence object.
pub(crate) const OBJECT_BYTES: u64 = 256;
/// The name has an object (a query begun once, a fence set once).
const OBJECT: u32 = 1;
/// A fence whose status is known TRUE until its next SetFenceNV.
const SIGNALED: u32 = 2;
/// Nanoseconds of one bounded native wait of FinishFenceNV.
const WAIT_NS: u64 = 1_000_000_000;
/// Words of the largest answer: GetQueryObjectuiv with every report.
pub(crate) const MAX_ANSWER_WORDS: usize = 2 + 2 * DG_GL_QUERY_REPORTS as usize;

#[repr(C)]
#[derive(Clone, Copy)]
struct Entry {
    name: u32,
    /// The native query name of a query object.
    native: u32,
    flags: u32,
    /// A fence's pending native sync object; null once signalled.
    sync: *mut __GLsync,
}
const EMPTY: Entry = Entry {
    name: 0,
    native: 0,
    flags: 0,
    sync: null_mut(),
};

/// A bounded open-addressing name table, never more than half full, with
/// backward-shift deletion.
#[repr(C)]
pub struct Names {
    count: u32,
    /// The next candidate name of Gen.
    next: u32,
    slots: [Entry; SLOTS],
}
impl Names {
    fn hash(name: u32) -> usize {
        (name.wrapping_mul(0x9e37_79b1) as usize) & (SLOTS - 1)
    }
    /// The entry of `name`, or the empty slot ending its probe chain.
    unsafe fn locate(table: *const Self, name: u32) -> Result<usize, usize> {
        for n in 0..SLOTS {
            let i = (Self::hash(name) + n) & (SLOTS - 1);
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
    unsafe fn find(table: *mut Self, name: u32) -> *mut Entry {
        if table.is_null() || name == 0 {
            return null_mut();
        }
        match unsafe { Self::locate(table, name) } {
            Ok(i) => unsafe { addr_of_mut!((*table).slots[i]) },
            Err(_) => null_mut(),
        }
    }
    /// A reserved entry of `name` at its free slot; false when full.
    unsafe fn insert(table: *mut Self, slot: usize, name: u32) -> bool {
        unsafe {
            if (*table).count >= DG_GL_MAX_QUERIES {
                return false;
            }
            (*table).count += 1;
            (*table).slots[slot] = Entry { name, ..EMPTY };
        }
        true
    }
    unsafe fn remove(table: *mut Self, index: usize) {
        unsafe {
            (*table).count -= 1;
            (*table).slots[index] = EMPTY;
        }
        let mut hole = index;
        for step in 1..SLOTS {
            let at = (index + step) & (SLOTS - 1);
            let e = unsafe { (*table).slots[at] };
            if e.name == 0 {
                break;
            }
            let origin = Self::hash(e.name);
            if hole.wrapping_sub(origin) & (SLOTS - 1) < at.wrapping_sub(origin) & (SLOTS - 1) {
                unsafe {
                    (*table).slots[hole] = e;
                    (*table).slots[at] = EMPTY;
                }
                hole = at;
            }
        }
    }
    /// The table in `slot`, created on first use; null when its allocation or
    /// budget charge fails.
    unsafe fn ensure(m: &Memory, slot: *mut *mut Self) -> *mut Self {
        let current = unsafe { *slot };
        let bytes = core::mem::size_of::<Self>();
        if !current.is_null() || !unsafe { charge(m, bytes as u64) } {
            return current;
        }
        let table = unsafe { (m.allocate)(m.opaque, bytes) }.cast::<Self>();
        if table.is_null() {
            unsafe { uncharge(m, bytes as u64) };
            return table;
        }
        unsafe {
            core::ptr::write_bytes(table.cast::<u8>(), 0, bytes);
            (*table).next = 1;
            *slot = table;
        }
        table
    }
}

/// One context's queries and fences.
#[repr(C)]
pub struct Queries {
    queries: *mut Names,
    fences: *mut Names,
    /// The active query's name; zero when none is active.
    active: u32,
    ended_count: u32,
    /// Names of ended queries not yet reported, oldest first.
    ended: [u32; ENDED],
}

macro_rules! gl {($api:expr, $name:ident($($arg:expr),* $(,)?)) => {{#[allow(unused_unsafe)] let result = unsafe { $api.$name.expect("complete native GL table")($($arg),*) }; result }};}

unsafe fn error(c: *mut ContextState, code: u32) {
    unsafe { query::store(addr_of_mut!((*c).guest_errors), code) };
}
fn word(args: &[u8], n: usize) -> u32 {
    u32::from_le_bytes(args[n * 4..n * 4 + 4].try_into().unwrap())
}

/// Every native entry point this module uses, checked before a record
/// changes anything.
fn complete(api: &DreamGpuGlApi) -> bool {
    api.dg_glGenQueries.is_some()
        && api.dg_glDeleteQueries.is_some()
        && api.dg_glBeginQuery.is_some()
        && api.dg_glEndQuery.is_some()
        && api.dg_glGetQueryiv.is_some()
        && api.dg_glGetQueryObjectuiv.is_some()
        && api.dg_glFenceSync.is_some()
        && api.dg_glClientWaitSync.is_some()
        && api.dg_glDeleteSync.is_some()
        && api.dg_glFlush.is_some()
        && api.dg_glFinish.is_some()
}

/// The QUERY records of this module.
pub(crate) fn query_function(function: u32) -> bool {
    matches!(
        function,
        FEnum_glGenQueries
            | FEnum_glIsQuery
            | FEnum_glGetQueryiv
            | FEnum_glGetQueryObjectuiv
            | FEnum_glGenFencesNV
            | FEnum_glIsFenceNV
            | FEnum_glTestFenceNV
            | FEnum_glFinishFenceNV
            | FEnum_glGetFenceivNV
    )
}
/// CALL record shapes: the one target and the one condition.
pub(crate) fn call_shape(function: u32, a: &[u32; 2]) -> bool {
    match function {
        FEnum_glBeginQuery | FEnum_glEndQuery => a[0] == GL_SAMPLES_PASSED,
        FEnum_glSetFenceNV => a[1] == ALL_COMPLETED_NV,
        _ => false,
    }
}
/// DeleteQueries and DeleteFencesNV: a count, then exactly that many names.
pub(crate) fn delete_shape(count: u32, bytes: usize) -> bool {
    count <= DG_GL_MAX_QUERIES && bytes as u64 == u64::from(count) * 4
}
/// QUERY record shapes: (result values, result kind); zero values rejects.
pub(crate) fn query_shape(function: u32, [a, b, d]: [u32; 3]) -> (u32, u32) {
    match function {
        FEnum_glGenQueries | FEnum_glGenFencesNV => (
            if b == 0 && d == 0 && (1..=DG_GL_QUERY_NAME_BATCH).contains(&a) {
                a
            } else {
                0
            },
            DG_GL_RESULT_INT,
        ),
        FEnum_glIsQuery | FEnum_glIsFenceNV => (u32::from(b == 0 && d == 0), DG_GL_RESULT_BOOL),
        // One, then the status; a single zero word for a name that is not a
        // fence (gl.h).
        FEnum_glTestFenceNV | FEnum_glFinishFenceNV => {
            (if b == 0 && d == 0 { 2 } else { 0 }, DG_GL_RESULT_INT)
        }
        FEnum_glGetQueryiv => (
            u32::from(
                a == GL_SAMPLES_PASSED
                    && matches!(b, GL_CURRENT_QUERY | GL_QUERY_COUNTER_BITS)
                    && d == 0,
            ),
            DG_GL_RESULT_INT,
        ),
        // A capacity: the state, the result and every requested report.
        FEnum_glGetQueryObjectuiv => (
            if matches!(b, GL_QUERY_RESULT | GL_QUERY_RESULT_AVAILABLE) && d <= DG_GL_QUERY_REPORTS
            {
                2 + 2 * d
            } else {
                0
            },
            DG_GL_RESULT_INT,
        ),
        FEnum_glGetFenceivNV => (
            if matches!(b, FENCE_STATUS_NV | FENCE_CONDITION_NV) && d == 0 {
                2
            } else {
                0
            },
            DG_GL_RESULT_INT,
        ),
        _ => (0, 0),
    }
}

/// The context's state, created on first use; null (after OUT_OF_MEMORY)
/// when its allocation or charge fails.
unsafe fn ensure(m: &Memory, c: *mut ContextState) -> *mut Queries {
    let current = unsafe { (*c).queries };
    if !current.is_null() {
        return current;
    }
    let bytes = core::mem::size_of::<Queries>();
    if !unsafe { charge(m, bytes as u64) } {
        unsafe { error(c, GL_OUT_OF_MEMORY) };
        return null_mut();
    }
    let q = unsafe { (m.allocate)(m.opaque, bytes) }.cast::<Queries>();
    if q.is_null() {
        unsafe {
            uncharge(m, bytes as u64);
            error(c, GL_OUT_OF_MEMORY);
        }
        return q;
    }
    unsafe {
        q.write(Queries {
            queries: null_mut(),
            fences: null_mut(),
            active: 0,
            ended_count: 0,
            ended: [0; ENDED],
        });
        (*c).queries = q;
    }
    q
}
/// The table of queries or fences, created on first use (null after
/// OUT_OF_MEMORY).
unsafe fn table(m: &Memory, c: *mut ContextState, fences: bool) -> *mut Names {
    let q = unsafe { ensure(m, c) };
    if q.is_null() {
        return null_mut();
    }
    let slot = unsafe {
        if fences {
            addr_of_mut!((*q).fences)
        } else {
            addr_of_mut!((*q).queries)
        }
    };
    let names = unsafe { Names::ensure(m, slot) };
    if names.is_null() {
        unsafe { error(c, GL_OUT_OF_MEMORY) };
    }
    names
}
/// The existing table, or null.
unsafe fn existing(c: *const ContextState, fences: bool) -> *mut Names {
    let q = unsafe { (*c).queries };
    if q.is_null() {
        null_mut()
    } else if fences {
        unsafe { (*q).fences }
    } else {
        unsafe { (*q).queries }
    }
}
/// The entry of `name`, an object or reserved, created as a reserved entry if
/// missing; with whether it was created. None after OUT_OF_MEMORY.
unsafe fn obtain(
    m: &Memory,
    c: *mut ContextState,
    fences: bool,
    name: u32,
) -> Option<(*mut Entry, bool)> {
    let names = unsafe { table(m, c, fences) };
    if names.is_null() {
        return None;
    }
    match unsafe { Names::locate(names, name) } {
        Ok(i) => Some((unsafe { addr_of_mut!((*names).slots[i]) }, false)),
        Err(i) => {
            if unsafe { Names::insert(names, i, name) } {
                Some((unsafe { addr_of_mut!((*names).slots[i]) }, true))
            } else {
                unsafe { error(c, GL_OUT_OF_MEMORY) };
                None
            }
        }
    }
}
/// Forget an entry created by `obtain` whose object could not be made.
unsafe fn abandon(c: *const ContextState, fences: bool, name: u32) {
    let names = unsafe { existing(c, fences) };
    if let Ok(i) = unsafe { Names::locate(names, name) } {
        unsafe { Names::remove(names, i) };
    }
}

/// Drop `name` from the queue of unreported ended queries.
unsafe fn retire(q: *mut Queries, name: u32) {
    let n = unsafe { (*q).ended_count } as usize;
    let ended = unsafe { &mut (*q).ended };
    if let Some(at) = ended[..n].iter().position(|&e| e == name) {
        ended.copy_within(at + 1..n, at);
        unsafe { (*q).ended_count -= 1 };
    }
}
/// Queue an ended query; a full queue forgets its oldest entry, which is
/// then reported only when queried itself.
unsafe fn queue(q: *mut Queries, name: u32) {
    unsafe {
        if (*q).ended_count as usize == ENDED {
            (*q).ended.copy_within(1.., 0);
            (*q).ended_count -= 1;
        }
        (*q).ended[(*q).ended_count as usize] = name;
        (*q).ended_count += 1;
    }
}

unsafe fn begin(m: &Memory, c: *mut ContextState, id: u32) {
    let api = unsafe { &*m.api };
    let q = unsafe { ensure(m, c) };
    if q.is_null() {
        return;
    }
    if unsafe { (*q).active } != 0 || id == 0 {
        unsafe { error(c, GL_INVALID_OPERATION) };
        return;
    }
    let Some((entry, created)) = (unsafe { obtain(m, c, false, id) }) else {
        return;
    };
    if unsafe { (*entry).flags } & OBJECT == 0 {
        let mut native = 0;
        if unsafe { charge(m, OBJECT_BYTES) } {
            gl!(api, dg_glGenQueries(1, &mut native));
            if native == 0 {
                unsafe { uncharge(m, OBJECT_BYTES) };
            }
        }
        if native == 0 {
            unsafe {
                if created {
                    abandon(c, false, id);
                }
                error(c, GL_OUT_OF_MEMORY);
            }
            return;
        }
        unsafe {
            (*entry).native = native;
            (*entry).flags |= OBJECT;
        }
    }
    unsafe { retire(q, id) };
    gl!(api, dg_glBeginQuery(GL_SAMPLES_PASSED, (*entry).native));
    unsafe { (*q).active = id };
}
unsafe fn end(m: &Memory, c: *mut ContextState) {
    let q = unsafe { (*c).queries };
    if q.is_null() || unsafe { (*q).active } == 0 {
        unsafe { error(c, GL_INVALID_OPERATION) };
        return;
    }
    gl!(&*m.api, dg_glEndQuery(GL_SAMPLES_PASSED));
    unsafe {
        queue(q, (*q).active);
        (*q).active = 0;
    }
}
/// A fence's status is TRUE from here until its next SetFenceNV.
unsafe fn signal(api: &DreamGpuGlApi, e: *mut Entry) {
    unsafe {
        if !(*e).sync.is_null() {
            gl!(api, dg_glDeleteSync((*e).sync));
            (*e).sync = null_mut();
        }
        (*e).flags |= SIGNALED;
    }
}
unsafe fn set_fence(m: &Memory, c: *mut ContextState, name: u32) {
    let api = unsafe { &*m.api };
    if name == 0 {
        unsafe { error(c, GL_INVALID_OPERATION) };
        return;
    }
    let Some((entry, created)) = (unsafe { obtain(m, c, true, name) }) else {
        return;
    };
    if unsafe { (*entry).flags } & OBJECT == 0 {
        if !unsafe { charge(m, OBJECT_BYTES) } {
            unsafe {
                if created {
                    abandon(c, true, name);
                }
                error(c, GL_OUT_OF_MEMORY);
            }
            return;
        }
        unsafe { (*entry).flags |= OBJECT };
    }
    unsafe {
        if !(*entry).sync.is_null() {
            gl!(api, dg_glDeleteSync((*entry).sync));
        }
        (*entry).flags &= !SIGNALED;
        (*entry).sync = gl!(api, dg_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
        if (*entry).sync.is_null() {
            // Without a native sync object, completing everything now is
            // the fence completing.
            gl!(api, dg_glFinish());
            (*entry).flags |= SIGNALED;
        }
    }
}

/// BeginQuery, EndQuery and SetFenceNV, compiled into display lists.
///
/// # Safety
/// Render-owned state whose native context is current; `args` holds the
/// record's validated words.
pub(crate) unsafe fn call(
    memory: *const Memory,
    c: *mut ContextState,
    function: u32,
    args: &[u8],
) -> Result<(), u32> {
    let m = unsafe { &*memory };
    if !complete(unsafe { &*m.api }) {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    match function {
        FEnum_glBeginQuery => unsafe { begin(m, c, word(args, 1)) },
        FEnum_glEndQuery => unsafe { end(m, c) },
        FEnum_glSetFenceNV => unsafe { set_fence(m, c, word(args, 0)) },
        _ => return Err(DG_GL_ERROR_UNSUPPORTED),
    }
    Ok(())
}

/// Delete one entry: the native object, its charge and the name.
unsafe fn destroy(m: &Memory, names: *mut Names, index: usize) {
    let api = unsafe { &*m.api };
    let e = unsafe { (*names).slots[index] };
    if e.flags & OBJECT != 0 {
        if e.native != 0 {
            gl!(api, dg_glDeleteQueries(1, &e.native));
        }
        if !e.sync.is_null() {
            gl!(api, dg_glDeleteSync(e.sync));
        }
        unsafe { uncharge(m, OBJECT_BYTES) };
    }
    unsafe { Names::remove(names, index) };
}
/// DeleteQueries and DeleteFencesNV, executed immediately.
///
/// # Safety
/// Render-owned state whose native context is current; `data` holds the
/// validated names.
pub(crate) unsafe fn delete(
    m: &Memory,
    c: *mut ContextState,
    function: u32,
    data: &[u8],
) -> Result<(), u32> {
    if !complete(unsafe { &*m.api }) {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let fences = function == FEnum_glDeleteFencesNV;
    let q = unsafe { (*c).queries };
    if q.is_null() {
        return Ok(());
    }
    // ARB_occlusion_query: no deletion while any query is active.
    if !fences && unsafe { (*q).active } != 0 {
        unsafe { error(c, GL_INVALID_OPERATION) };
        return Ok(());
    }
    let names = unsafe { existing(c, fences) };
    if names.is_null() {
        return Ok(());
    }
    for raw in data.as_chunks::<4>().0 {
        let name = u32::from_le_bytes(*raw);
        if name == 0 {
            continue;
        }
        if let Ok(i) = unsafe { Names::locate(names, name) } {
            unsafe { destroy(m, names, i) };
            if !fences {
                unsafe { retire(q, name) };
            }
        }
    }
    Ok(())
}

/// Reserve `count` unused names into `out`, all or none (all zero).
unsafe fn generate(m: &Memory, c: *mut ContextState, fences: bool, count: u32, out: &mut [u32]) {
    out[..count as usize].fill(0);
    if !fences {
        let q = unsafe { (*c).queries };
        if !q.is_null() && unsafe { (*q).active } != 0 {
            unsafe { error(c, GL_INVALID_OPERATION) };
            return;
        }
    }
    let names = unsafe { table(m, c, fences) };
    if names.is_null() {
        return;
    }
    if count > DG_GL_MAX_QUERIES - unsafe { (*names).count } {
        unsafe { error(c, GL_OUT_OF_MEMORY) };
        return;
    }
    let mut i = 0;
    while i < count as usize {
        let candidate = unsafe { (*names).next };
        unsafe { (*names).next = candidate.wrapping_add(1) };
        if candidate == 0 {
            continue;
        }
        if let Err(slot) = unsafe { Names::locate(names, candidate) } {
            let inserted = unsafe { Names::insert(names, slot, candidate) };
            debug_assert!(inserted);
            out[i] = candidate;
            i += 1;
        }
    }
}
/// A fence's status, signalling it once native GL completed it. Pending
/// fences are flushed, so the status turns TRUE in finite time.
unsafe fn status(api: &DreamGpuGlApi, e: *mut Entry) -> bool {
    if unsafe { (*e).flags } & SIGNALED != 0 {
        return true;
    }
    let r = gl!(
        api,
        dg_glClientWaitSync((*e).sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0)
    );
    if r == GL_TIMEOUT_EXPIRED {
        return false;
    }
    // ALREADY_SIGNALED, CONDITION_SATISFIED; WAIT_FAILED cannot wait again.
    unsafe { signal(api, e) };
    true
}
unsafe fn finish(api: &DreamGpuGlApi, e: *mut Entry) {
    if unsafe { (*e).flags } & SIGNALED != 0 {
        return;
    }
    loop {
        let r = gl!(
            api,
            dg_glClientWaitSync((*e).sync, GL_SYNC_FLUSH_COMMANDS_BIT, WAIT_NS)
        );
        match r {
            GL_TIMEOUT_EXPIRED => continue,
            GL_WAIT_FAILED => gl!(api, dg_glFinish()),
            _ => (),
        }
        break;
    }
    unsafe { signal(api, e) };
}

/// GetQueryObjectuiv: the state, the result and reports (gl.h). Returns the
/// number of words written.
unsafe fn object(
    api: &DreamGpuGlApi,
    c: *mut ContextState,
    id: u32,
    pname: u32,
    reports: u32,
    out: &mut [u32],
) -> usize {
    let q = unsafe { (*c).queries };
    let entry = unsafe { Names::find(existing(c, false), id) };
    if entry.is_null() || unsafe { (*entry).flags } & OBJECT == 0 || unsafe { (*q).active } == id {
        unsafe { error(c, GL_INVALID_OPERATION) };
        out[0] = 0;
        return 1;
    }
    let native = unsafe { (*entry).native };
    let mut available = u32::from(pname == GL_QUERY_RESULT);
    if available == 0 {
        gl!(
            api,
            dg_glGetQueryObjectuiv(native, GL_QUERY_RESULT_AVAILABLE, &mut available)
        );
    }
    if available != 0 {
        let mut value = 0;
        gl!(
            api,
            dg_glGetQueryObjectuiv(native, GL_QUERY_RESULT, &mut value)
        );
        out[0] = DG_GL_QUERY_AVAILABLE;
        out[1] = value;
        unsafe { retire(q, id) };
    } else {
        out[0] = DG_GL_QUERY_PENDING;
        out[1] = 0;
        gl!(api, dg_glFlush());
    }
    let mut words = 2;
    let mut reported = 0;
    while reported < reports && unsafe { (*q).ended_count } != 0 {
        let name = unsafe { (*q).ended[0] };
        // A pending id stays queued: it and everything after it is newer than
        // what was just found pending, even if it completes meanwhile.
        if name == id {
            break;
        }
        let e = unsafe { Names::find((*q).queries, name) };
        assert!(!e.is_null(), "queued query {name} without an object");
        let mut ready = 0;
        gl!(
            api,
            dg_glGetQueryObjectuiv((*e).native, GL_QUERY_RESULT_AVAILABLE, &mut ready)
        );
        // Queries complete in order: nothing after a pending one is ready.
        if ready == 0 {
            break;
        }
        let mut value = 0;
        gl!(
            api,
            dg_glGetQueryObjectuiv((*e).native, GL_QUERY_RESULT, &mut value)
        );
        out[words] = name;
        out[words + 1] = value;
        words += 2;
        reported += 1;
        unsafe { retire(q, name) };
    }
    words
}

/// The QUERY records of this module; writes `bytes` of `result`.
///
/// # Safety
/// Render-owned state whose native context is current; `result` holds the
/// record's validated capacity.
pub(crate) unsafe fn query(
    memory: *const Memory,
    c: *mut ContextState,
    function: u32,
    [a, b, d]: [u32; 3],
    result: *mut u8,
    bytes: *mut u32,
) -> Result<(), u32> {
    let m = unsafe { &*memory };
    let api = unsafe { &*m.api };
    if !complete(api) {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let mut out = [0u32; MAX_ANSWER_WORDS];
    let (words, kind) = match function {
        FEnum_glGenQueries | FEnum_glGenFencesNV => {
            unsafe { generate(m, c, function == FEnum_glGenFencesNV, a, &mut out) };
            (a as usize, DG_GL_RESULT_INT)
        }
        FEnum_glIsQuery | FEnum_glIsFenceNV => {
            let e = unsafe { Names::find(existing(c, function == FEnum_glIsFenceNV), a) };
            out[0] = u32::from(!e.is_null() && unsafe { (*e).flags } & OBJECT != 0);
            (1, DG_GL_RESULT_BOOL)
        }
        FEnum_glGetQueryiv => {
            out[0] = if b == GL_CURRENT_QUERY {
                let q = unsafe { (*c).queries };
                if q.is_null() {
                    0
                } else {
                    unsafe { (*q).active }
                }
            } else {
                let mut bits = 0;
                gl!(
                    api,
                    dg_glGetQueryiv(GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &mut bits)
                );
                bits.clamp(0, 32) as u32
            };
            (1, DG_GL_RESULT_INT)
        }
        FEnum_glGetQueryObjectuiv => (
            unsafe { object(api, c, a, b, d, &mut out) },
            DG_GL_RESULT_INT,
        ),
        _ => {
            // NV_fence: one, then the status or condition; a name that is not
            // a fence answers a single zero word after the error (and the
            // frontend's TestFenceNV answers TRUE).
            let e = unsafe { Names::find(existing(c, true), a) };
            if e.is_null() || unsafe { (*e).flags } & OBJECT == 0 {
                unsafe { error(c, GL_INVALID_OPERATION) };
                (1, DG_GL_RESULT_INT)
            } else {
                out[0] = 1;
                out[1] = match function {
                    FEnum_glFinishFenceNV => {
                        unsafe { finish(api, e) };
                        1
                    }
                    FEnum_glGetFenceivNV if b == FENCE_CONDITION_NV => ALL_COMPLETED_NV,
                    _ => u32::from(unsafe { status(api, e) }),
                };
                (2, DG_GL_RESULT_INT)
            }
        }
    };
    debug_assert_eq!(kind, query_shape(function, [a, b, d]).1);
    unsafe {
        if kind == DG_GL_RESULT_BOOL {
            *result = out[0] as u8;
            *bytes = 1;
        } else {
            for (i, value) in out[..words].iter().enumerate() {
                core::ptr::copy_nonoverlapping(value.to_le_bytes().as_ptr(), result.add(i * 4), 4);
            }
            *bytes = words as u32 * 4;
        }
    }
    Ok(())
}

/// Delete every native query and sync object of a released context and free
/// its state.
///
/// # Safety
/// The context's native context is current; `q` is the released state's.
pub(crate) unsafe fn release(m: &Memory, q: *mut Queries) {
    if q.is_null() {
        return;
    }
    let api = unsafe { &*m.api };
    if unsafe { (*q).active } != 0 && api.dg_glEndQuery.is_some() {
        gl!(api, dg_glEndQuery(GL_SAMPLES_PASSED));
    }
    for names in unsafe { [(*q).queries, (*q).fences] } {
        if names.is_null() {
            continue;
        }
        for i in 0..SLOTS {
            let e = unsafe { (*names).slots[i] };
            if e.name == 0 || e.flags & OBJECT == 0 {
                continue;
            }
            if e.native != 0 && api.dg_glDeleteQueries.is_some() {
                gl!(api, dg_glDeleteQueries(1, &e.native));
            }
            if !e.sync.is_null() && api.dg_glDeleteSync.is_some() {
                gl!(api, dg_glDeleteSync(e.sync));
            }
            unsafe { uncharge(m, OBJECT_BYTES) };
        }
        unsafe {
            (m.free)(m.opaque, names.cast());
            uncharge(m, core::mem::size_of::<Names>() as u64);
        }
    }
    unsafe {
        (m.free)(m.opaque, q.cast());
        uncharge(m, core::mem::size_of::<Queries>() as u64);
    }
}

#[cfg(test)]
mod tests;
