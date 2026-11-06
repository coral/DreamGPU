// SPDX-License-Identifier: GPL-2.0-or-later
//! ARB_fragment_program and ARB_vertex_program objects. Guest program names
//! live in a bounded table owned by the texture namespace, so wglShareLists
//! shares programs exactly like textures and lists. Both targets share the
//! names; an object keeps the target of its first bind. Native objects belong
//! to the host's single native share group: every guest name maps to a private
//! native object, and each target's guest default object (name zero) becomes a
//! private per-namespace object the first time it is loaded, so no program
//! state crosses guest namespaces.
//!
//! Loads are checked before native GL sees them (transferred length, ASCII,
//! texture unit and coordinate set limits, and generic vertex attributes) and
//! then parsed into a private validation object. Only a string that parses
//! there replaces the bound program: a failed native parse may otherwise
//! disturb the previous program (Mesa clears its input/output usage before
//! parsing). A fragment load reparses into the program's own object; a vertex
//! load's validation object becomes the program's new native object instead,
//! so no native vertex program object ever translates a second string.
#![allow(non_upper_case_globals)]
use crate::{
    gl_api::*,
    query,
    state::ContextState,
    texture::names::{Memory, Namespace as Textures},
};
use core::ptr::{addr_of_mut, null_mut};

const SLOTS: usize = 8192;
const NAMES: u32 = DG_GL_MAX_PROGRAMS;
/// Nominal native storage of one program object, charged against the shared
/// texture resource budget with its string.
pub(crate) const OBJECT_BYTES: u64 = 4096;
/// Host-detected load failures that override the native error position/string.
pub(crate) const ERROR_LENGTH: u32 = 1;
pub(crate) const ERROR_ASCII: u32 = 2;
pub(crate) const ERROR_UNITS: u32 = 3;
pub(crate) const ERROR_ATTRIBS: u32 = 4;
/// The program targets: FRAGMENT_PROGRAM_ARB is 0, VERTEX_PROGRAM_ARB 1.
const TARGETS: usize = 2;
fn target_slot(target: u32) -> Option<usize> {
    match target {
        GL_FRAGMENT_PROGRAM_ARB => Some(0),
        GL_VERTEX_PROGRAM_ARB => Some(1),
        _ => None,
    }
}

#[repr(C)]
pub struct Program {
    /// Native program object name.
    pub name: u32,
    pub guest_name: u32,
    pub target: u32,
    pub refs: u32,
    pub deleted: u32,
    /// Charged program string bytes, beyond `OBJECT_BYTES`.
    pub length: u32,
    /// One past the highest local parameter ever set; a vertex load copies
    /// these into the replacement native object.
    pub locals: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct Entry {
    name: u32,
    /// Null for a name reserved by GenProgramsARB but never bound.
    program: *mut Program,
}
const EMPTY: Entry = Entry {
    name: 0,
    program: null_mut(),
};
#[repr(C)]
pub struct Namespace {
    count: u32,
    next: u32,
    /// Each target's materialized default object for every name-zero binding.
    default: [*mut Program; TARGETS],
    /// The fragment target's native validation object; first parse of every
    /// fragment load (vertex loads use a fresh object each).
    scratch: u32,
    entries: [Entry; SLOTS],
}
fn namespace_charge() -> u64 {
    core::mem::size_of::<Namespace>() as u64 + OBJECT_BYTES + u64::from(DG_GL_MAX_PROGRAM_BYTES)
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
    unreachable!("half-full bounded program namespace")
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
unsafe fn charge(m: &Memory, bytes: u64) -> bool {
    let used = unsafe { *m.bytes };
    if bytes > u64::from(DG_GL_MAX_TEXTURE_BYTES).saturating_sub(used) {
        return false;
    }
    unsafe {
        *m.bytes = used + bytes;
    }
    true
}
unsafe fn uncharge(m: &Memory, bytes: u64) {
    unsafe {
        *m.bytes = (*m.bytes)
            .checked_sub(bytes)
            .expect("program accounting underflow");
    }
}
unsafe fn error(c: *mut ContextState, code: u32) {
    unsafe { query::store(addr_of_mut!((*c).guest_errors), code) };
}

unsafe fn unref(m: &Memory, p: *mut Program) {
    if p.is_null() {
        return;
    }
    unsafe {
        // Refcount zero is never a live object. Fail closed on corrupted state.
        assert!((*p).refs != 0);
        (*p).refs -= 1;
        if (*p).refs != 0 {
            return;
        }
        if (*p).deleted == 0 && (*p).name != 0 {
            (*m.api).dg_glDeleteProgramsARB.expect("complete API")(1, &(*p).name);
        }
        uncharge(m, OBJECT_BYTES + u64::from((*p).length));
        (m.free)(m.opaque, p.cast());
    }
}
unsafe fn retain(p: *mut Program) -> *mut Program {
    if !p.is_null() {
        unsafe {
            (*p).refs = (*p).refs.checked_add(1).expect("program refs overflow");
        }
    }
    p
}
/// Replace a binding slot, moving one reference.
unsafe fn rebind(m: &Memory, slot: *mut *mut Program, p: *mut Program) {
    unsafe {
        if *slot != p {
            let old = core::mem::replace(&mut *slot, retain(p));
            unref(m, old);
        }
    }
}

/// A new native object owned by one reference (the caller's).
unsafe fn create(m: &Memory, target: u32, guest_name: u32) -> Result<*mut Program, u32> {
    let api = unsafe { &*m.api };
    let gen = api.dg_glGenProgramsARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    if !unsafe { charge(m, OBJECT_BYTES) } {
        return Ok(null_mut());
    }
    let p = unsafe { (m.allocate)(m.opaque, core::mem::size_of::<Program>()) }.cast::<Program>();
    if p.is_null() {
        unsafe { uncharge(m, OBJECT_BYTES) };
        return Ok(null_mut());
    }
    let mut name = 0;
    unsafe { gen(1, &mut name) };
    if name == 0 {
        unsafe {
            (m.free)(m.opaque, p.cast());
            uncharge(m, OBJECT_BYTES);
        }
        return Ok(null_mut());
    }
    unsafe {
        p.write(Program {
            name,
            guest_name,
            target,
            refs: 1,
            deleted: 0,
            length: 0,
            locals: 0,
        });
    }
    Ok(p)
}

/// The share group's program table, created on first use. Null when the
/// allocation or its budget charge fails.
unsafe fn namespace(m: &Memory, textures: *mut Textures) -> *mut Namespace {
    let current = unsafe { (*textures).programs };
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
        (*ns).next = 1;
        (*textures).programs = ns;
    }
    ns
}

/// # Safety
/// The last context of the share group is releasing it, with a native context
/// current. Every binding of its programs has already been released.
pub(crate) unsafe fn namespace_release(m: &Memory, ns: *mut Namespace) {
    if ns.is_null() {
        return;
    }
    for i in 0..SLOTS {
        let p = unsafe { (*ns).entries[i].program };
        unsafe { unref(m, p) };
    }
    unsafe {
        for default in (*ns).default {
            unref(m, default);
        }
        if (*ns).scratch != 0 {
            (*m.api).dg_glDeleteProgramsARB.expect("complete API")(1, &(*ns).scratch);
        }
        (m.free)(m.opaque, ns.cast());
        uncharge(m, namespace_charge());
    }
}

/// # Safety
/// Context release: drops the context's binding reference.
pub(crate) unsafe fn release_binding(m: &Memory, p: *mut Program) {
    unsafe { unref(m, p) };
}

unsafe fn native_bind(api: &DreamGpuGlApi, target: u32, p: *const Program) -> Result<(), u32> {
    let bind = api.dg_glBindProgramARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    unsafe { bind(target, if p.is_null() { 0 } else { (*p).name }) };
    Ok(())
}

/// The context's binding of a target (one reference, null for the
/// unmaterialized default object).
unsafe fn binding_slot(c: *mut ContextState, target: u32) -> *mut *mut Program {
    if target == GL_VERTEX_PROGRAM_ARB {
        unsafe { addr_of_mut!((*c).vertex_program) }
    } else {
        unsafe { addr_of_mut!((*c).program) }
    }
}

/// BindProgramARB. A missing name creates an object of `target`; a name of
/// another target is GL_INVALID_OPERATION; exhausted storage GL_OUT_OF_MEMORY.
///
/// # Safety
/// Render-owned context with its native context current.
pub(crate) unsafe fn bind(
    m: &Memory,
    c: *mut ContextState,
    target: u32,
    name: u32,
) -> Result<(), u32> {
    let Some(slot) = target_slot(target) else {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    };
    let api = unsafe { &*m.api };
    let textures = unsafe { (*c).textures };
    if name == 0 {
        let ns = unsafe { (*textures).programs };
        let default = if ns.is_null() {
            null_mut()
        } else {
            unsafe { (*ns).default[slot] }
        };
        unsafe {
            native_bind(api, target, default)?;
            rebind(m, binding_slot(c, target), default);
        }
        return Ok(());
    }
    let ns = unsafe { namespace(m, textures) };
    if ns.is_null() {
        unsafe { error(c, GL_OUT_OF_MEMORY) };
        return Ok(());
    }
    let located = unsafe { locate(ns, name) };
    let mut p = match located {
        Ok(i) => unsafe { (*ns).entries[i].program },
        Err(_) => null_mut(),
    };
    if !p.is_null() && unsafe { (*p).target } != target {
        unsafe { error(c, GL_INVALID_OPERATION) };
        return Ok(());
    }
    if p.is_null() {
        if located.is_err() && unsafe { (*ns).count } >= NAMES {
            unsafe { error(c, GL_OUT_OF_MEMORY) };
            return Ok(());
        }
        p = unsafe { create(m, target, name)? };
        if p.is_null() {
            unsafe { error(c, GL_OUT_OF_MEMORY) };
            return Ok(());
        }
        let slot = match located {
            Ok(i) => i,
            Err(i) => {
                unsafe { (*ns).count += 1 };
                i
            }
        };
        unsafe {
            (*ns).entries[slot] = Entry { name, program: p };
        }
    }
    unsafe {
        native_bind(api, target, p)?;
        rebind(m, binding_slot(c, target), p);
    }
    Ok(())
}

/// The bound object for a mutation (ProgramString, ProgramLocalParameter). A
/// default binding materializes the share group's private default object and
/// binds it natively. Null after a GL_OUT_OF_MEMORY guest error.
unsafe fn bound_object(m: &Memory, c: *mut ContextState, target: u32) -> Result<*mut Program, u32> {
    let slot = target_slot(target).ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let current = unsafe { *binding_slot(c, target) };
    if !current.is_null() {
        return Ok(current);
    }
    let ns = unsafe { namespace(m, (*c).textures) };
    if ns.is_null() {
        unsafe { error(c, GL_OUT_OF_MEMORY) };
        return Ok(null_mut());
    }
    if unsafe { (*ns).default[slot] }.is_null() {
        let p = unsafe { create(m, target, 0)? };
        if p.is_null() {
            unsafe { error(c, GL_OUT_OF_MEMORY) };
            return Ok(null_mut());
        }
        unsafe { (*ns).default[slot] = p };
    }
    let p = unsafe { (*ns).default[slot] };
    unsafe {
        native_bind(&*m.api, target, p)?;
        rebind(m, binding_slot(c, target), p);
    }
    Ok(p)
}

/// DeleteProgramsARB: unused names and zero are ignored. A program bound in
/// this context reverts to its target's default object; other contexts keep
/// using a deleted program until they rebind (the native object follows the
/// same rule).
///
/// # Safety
/// Data holds `count` validated little-endian names.
pub(crate) unsafe fn delete(m: &Memory, c: *mut ContextState, data: &[u8]) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let native_delete = api.dg_glDeleteProgramsARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let ns = unsafe { (*(*c).textures).programs };
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
        let p = unsafe { (*ns).entries[slot].program };
        unsafe { remove(ns, slot) };
        if p.is_null() {
            continue;
        }
        unsafe {
            native_delete(1, &(*p).name);
            (*p).deleted = 1;
            let target = (*p).target;
            let binding = binding_slot(c, target);
            if *binding == p {
                // Native deletion bound program zero; restore the guest default.
                let default = (*ns).default[target_slot(target).expect("program target")];
                native_bind(api, target, default)?;
                rebind(m, binding, default);
            }
            unref(m, p); // Drop namespace ownership last.
        }
    }
    Ok(())
}

/// GenProgramsARB: `count` unused names, reserved until deleted. Names become
/// objects only when bound. All zero (with GL_OUT_OF_MEMORY) when the table
/// cannot hold them.
///
/// # Safety
/// `out` has `count` writable little-endian words.
pub(crate) unsafe fn generate(m: &Memory, c: *mut ContextState, count: u32, out: *mut u8) {
    unsafe { core::ptr::write_bytes(out, 0, count as usize * 4) };
    let ns = unsafe { namespace(m, (*c).textures) };
    if ns.is_null() || count > NAMES - unsafe { (*ns).count } {
        unsafe { error(c, GL_OUT_OF_MEMORY) };
        return;
    }
    for i in 0..count as usize {
        // At most NAMES names are in use, so a free name is always found.
        let (name, slot) = loop {
            let candidate = unsafe { (*ns).next };
            unsafe { (*ns).next = candidate.wrapping_add(1).max(1) };
            if candidate == 0 {
                continue;
            }
            if let Err(slot) = unsafe { locate(ns, candidate) } {
                break (candidate, slot);
            }
        };
        unsafe {
            (*ns).entries[slot] = Entry {
                name,
                program: null_mut(),
            };
            (*ns).count += 1;
            core::ptr::copy_nonoverlapping(name.to_le_bytes().as_ptr(), out.add(i * 4), 4);
        }
    }
}

/// IsProgramARB: true only for a bound-at-least-once object of this namespace.
///
/// # Safety
/// Read-only render-owned context.
pub(crate) unsafe fn is_program(c: *const ContextState, name: u32) -> bool {
    let ns = unsafe { (*(*c).textures).programs };
    name != 0
        && !ns.is_null()
        && matches!(unsafe { locate(ns, name) }, Ok(i) if !unsafe { (*ns).entries[i].program }.is_null())
}

/// The guest name of the target's bound program (zero for the default).
///
/// # Safety
/// Read-only render-owned context.
pub(crate) unsafe fn binding(c: *const ContextState, target: u32) -> u32 {
    let p = unsafe { *binding_slot(c.cast_mut(), target) };
    if p.is_null() {
        0
    } else {
        unsafe { (*p).guest_name }
    }
}

/// Native texture coordinate sets and image units, clamped to the logical
/// texture units the device implements.
///
/// # Safety
/// Render-owned context with its native context current.
pub(crate) unsafe fn limits(api: &DreamGpuGlApi, c: *mut ContextState) -> Result<(u32, u32), u32> {
    let get = api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let units = unsafe { crate::state::unit_limit(api, c)? };
    let mut coords = 0;
    let mut images = 0;
    unsafe {
        get(GL_MAX_TEXTURE_COORDS_ARB, &mut coords);
        get(GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &mut images);
    }
    let clamp = |v: i32| (v.max(1) as u32).min(units);
    Ok((clamp(coords), clamp(images)))
}

/// Native generic vertex attributes, clamped to the protocol's
/// DG_GL_MAX_VERTEX_ATTRIBS.
///
/// # Safety
/// Native context current.
pub(crate) unsafe fn attrib_limit(api: &DreamGpuGlApi) -> Result<u32, u32> {
    let get = api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let mut attribs = 0;
    unsafe { get(GL_MAX_VERTEX_ATTRIBS_ARB, &mut attribs) };
    Ok((attribs.max(1) as u32).min(DG_GL_MAX_VERTEX_ATTRIBS))
}

fn identifier_start(b: u8) -> bool {
    b.is_ascii_alphabetic() || b == b'_' || b == b'$'
}
fn identifier(b: u8) -> bool {
    identifier_start(b) || b.is_ascii_digit()
}
/// Skip whitespace and `#` comments (to a newline or carriage return).
fn skip(s: &[u8], mut i: usize) -> usize {
    while i < s.len() {
        match s[i] {
            b' ' | b'\t' | b'\n' | b'\r' => i += 1,
            b'#' => {
                while i < s.len() && s[i] != b'\n' && s[i] != b'\r' {
                    i += 1;
                }
            }
            _ => break,
        }
    }
    i
}
/// Offset of the first byte outside 1..=127; ARB program strings are ASCII.
pub(crate) fn non_ascii(s: &[u8]) -> Option<usize> {
    s.iter().position(|&b| b == 0 || b >= 0x80)
}
/// The first reference beyond the reported limits and its error kind: a
/// `texture[n]` image unit (a TEX instruction operand) with n >= `images` or a
/// `.texcoord[n]` coordinate set with n >= `coords` (`ERROR_UNITS`), or a
/// `.attrib[n]` generic vertex attribute with n >= `attribs` (`ERROR_ATTRIBS`).
/// Comments are skipped; `state.matrix.texture[n]` is a coordinate set.
/// `u32::MAX` leaves a kind unchecked. Native GL may accept more of each than
/// the device reports.
pub(crate) fn beyond(s: &[u8], coords: u32, images: u32, attribs: u32) -> Option<(usize, u32)> {
    let mut i = 0;
    let mut member = false;
    loop {
        i = skip(s, i);
        if i >= s.len() {
            return None;
        }
        if !identifier_start(s[i]) {
            member = s[i] == b'.';
            // Digits of a number never begin an identifier mid-token.
            if s[i].is_ascii_digit() {
                while i < s.len() && (identifier(s[i]) || s[i] == b'.') {
                    i += 1;
                }
            } else {
                i += 1;
            }
            continue;
        }
        let start = i;
        while i < s.len() && identifier(s[i]) {
            i += 1;
        }
        let word = &s[start..i];
        let (limit, kind) = match word {
            b"texture" if member => (coords, ERROR_UNITS),
            b"texture" => (images, ERROR_UNITS),
            b"texcoord" if member => (coords, ERROR_UNITS),
            b"attrib" if member => (attribs, ERROR_ATTRIBS),
            _ => (u32::MAX, 0),
        };
        member = false;
        if limit == u32::MAX {
            continue;
        }
        let open = skip(s, i);
        if open >= s.len() || s[open] != b'[' {
            continue;
        }
        let digits = skip(s, open + 1);
        let mut end = digits;
        let mut value = 0u64;
        while end < s.len() && s[end].is_ascii_digit() {
            value = (value * 10 + u64::from(s[end] - b'0')).min(u64::from(u32::MAX));
            end += 1;
        }
        if end > digits && value >= u64::from(limit) {
            return Some((digits, kind));
        }
        i = open + 1;
    }
}

unsafe fn fail(c: *mut ContextState, kind: u32, position: usize) {
    unsafe {
        (*c).program_error_kind = kind;
        (*c).program_error = position.min(i32::MAX as usize) as i32;
        error(c, GL_INVALID_OPERATION);
    }
}

/// ProgramStringARB with validated target, format and length words.
///
/// # Safety
/// Render-owned context with its native context current; data is the
/// immutable payload accepted by data validation.
pub(crate) unsafe fn string(
    m: &Memory,
    c: *mut ContextState,
    a: &[u32; 8],
    data: &[u8],
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let (target, format, length) = (a[0], a[1], a[2]);
    if target_slot(target).is_none() || format != GL_PROGRAM_FORMAT_ASCII_ARB {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let load = api.dg_glProgramStringARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    if data.len() as u64 != u64::from(length) {
        unsafe { fail(c, ERROR_LENGTH, length as usize) };
        return Ok(());
    }
    if let Some(at) = non_ascii(data) {
        unsafe { fail(c, ERROR_ASCII, at) };
        return Ok(());
    }
    let (coords, images) = unsafe { limits(api, c)? };
    // A vertex program samples no image units (ARBvp1.0 has no TEX); its
    // coordinate sets and generic attributes are checked.
    let found = if target == GL_VERTEX_PROGRAM_ARB {
        beyond(data, coords, u32::MAX, unsafe { attrib_limit(api)? })
    } else {
        beyond(data, coords, images, u32::MAX)
    };
    if let Some((at, kind)) = found {
        unsafe { fail(c, kind, at) };
        return Ok(());
    }
    let p = unsafe { bound_object(m, c, target)? };
    if p.is_null() {
        return Ok(());
    }
    let ns = unsafe { (*(*c).textures).programs };
    let old = unsafe { (*p).length };
    if length > old && !unsafe { charge(m, u64::from(length - old)) } {
        unsafe { error(c, GL_OUT_OF_MEMORY) };
        return Ok(());
    }
    // The replacement charge is held while loading; settle it afterwards.
    let settle = |loaded: bool| unsafe {
        if loaded {
            if length < old {
                uncharge(m, u64::from(old - length));
            }
            (*p).length = length;
        } else if length > old {
            uncharge(m, u64::from(length - old));
        }
    };
    if target == GL_VERTEX_PROGRAM_ARB {
        let loaded = unsafe { replace_vertex(m, c, p, data) };
        settle(loaded == Ok(true));
        return loaded.map(|_| ());
    }
    unsafe {
        if (*ns).scratch == 0 {
            api.dg_glGenProgramsARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?(1, addr_of_mut!((*ns).scratch));
        }
        if (*ns).scratch == 0 {
            settle(false);
            error(c, GL_OUT_OF_MEMORY);
            return Ok(());
        }
        query::remember(api, addr_of_mut!((*c).guest_errors))?;
        // Native GL now owns the error position and string again.
        (*c).program_error_kind = 0;
        native_bind_name(api, target, (*ns).scratch)?;
        load(target, format, length as i32, data.as_ptr().cast());
        let code = get_error();
        native_bind(api, target, p)?;
        if code != GL_NO_ERROR {
            settle(false);
            error(c, code);
            return Ok(());
        }
        load(target, format, length as i32, data.as_ptr().cast());
        let code = get_error();
        settle(code == GL_NO_ERROR);
        if code != GL_NO_ERROR {
            error(c, code);
        }
    }
    Ok(())
}
unsafe fn native_bind_name(api: &DreamGpuGlApi, target: u32, name: u32) -> Result<(), u32> {
    unsafe { api.dg_glBindProgramARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?(target, name) };
    Ok(())
}

/// A vertex load into a fresh native object, bound (natively and in the
/// context) program `p` keeping its object on failure. Mesa 26.1's
/// st_translate_vertex_program frees `base_serialized_nir` without clearing
/// it, so the third string translated in one native vertex program object
/// frees it twice (and the second leaves it dangling). On success the fresh
/// object, with `p`'s local parameters copied, replaces `p`'s native object.
/// True when the string loaded.
unsafe fn replace_vertex(
    m: &Memory,
    c: *mut ContextState,
    p: *mut Program,
    data: &[u8],
) -> Result<bool, u32> {
    let api = unsafe { &*m.api };
    let target = GL_VERTEX_PROGRAM_ARB;
    let gen = api.dg_glGenProgramsARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let native_delete = api.dg_glDeleteProgramsARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let load = api.dg_glProgramStringARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_local = api
        .dg_glGetProgramLocalParameterfvARB
        .ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let set_local = api
        .dg_glProgramLocalParameter4fARB
        .ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_iv = api.dg_glGetProgramivARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    unsafe {
        query::remember(api, addr_of_mut!((*c).guest_errors))?;
        // Native GL now owns the error position and string again.
        (*c).program_error_kind = 0;
    }
    // The locals of the bound (old) object, read while it is still bound:
    // a deleted program's native name no longer reaches it.
    let mut limit = 0;
    unsafe { get_iv(target, GL_MAX_PROGRAM_LOCAL_PARAMETERS_ARB, &mut limit) };
    let count = (unsafe { (*p).locals } as usize).min(limit.max(0) as usize);
    let Some(locals) = (unsafe { crate::resource::OwnedBytes::zeroed(m, count.max(1) * 16) })
    else {
        unsafe { error(c, GL_OUT_OF_MEMORY) };
        return Ok(false);
    };
    let saved = locals.p.cast::<f32>();
    for i in 0..count {
        unsafe { get_local(target, i as u32, saved.add(i * 4)) };
    }
    let mut fresh = 0;
    unsafe { gen(1, &mut fresh) };
    if fresh == 0 {
        unsafe { error(c, GL_OUT_OF_MEMORY) };
        return Ok(false);
    }
    unsafe {
        native_bind_name(api, target, fresh)?;
        load(
            target,
            GL_PROGRAM_FORMAT_ASCII_ARB,
            data.len() as i32,
            data.as_ptr().cast(),
        );
    }
    let code = unsafe { get_error() };
    if code != GL_NO_ERROR {
        unsafe {
            native_bind(api, target, p)?;
            native_delete(1, &fresh);
            error(c, code);
        }
        return Ok(false);
    }
    unsafe {
        for i in 0..count {
            let v = saved.add(i * 4);
            set_local(target, i as u32, *v, *v.add(1), *v.add(2), *v.add(3));
        }
        // A deleted program's native name is gone already; the fresh object
        // is owned again and deleted with the program.
        if (*p).deleted == 0 {
            native_delete(1, &(*p).name);
        }
        (*p).name = fresh;
        (*p).deleted = 0;
    }
    Ok(true)
}

/// ProgramLocalParameter4fARB on the bound object (materializing the default).
///
/// # Safety
/// Render-owned context with its native context current.
pub(crate) unsafe fn local(
    m: &Memory,
    c: *mut ContextState,
    target: u32,
    index: u32,
    v: [f32; 4],
) -> Result<(), u32> {
    if target_slot(target).is_none() {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let set = unsafe { (*m.api).dg_glProgramLocalParameter4fARB }.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let p = unsafe { bound_object(m, c, target)? };
    if p.is_null() {
        return Ok(());
    }
    unsafe {
        set(target, index, v[0], v[1], v[2], v[3]);
        // Bounded by the largest native limit a load ever copies.
        (*p).locals = (*p).locals.max(index.saturating_add(1)).min(1 << 16);
    }
    Ok(())
}

/// PROGRAM_ERROR_POSITION_ARB: a host-detected failure, else native.
///
/// # Safety
/// Render-owned context with its native context current.
pub(crate) unsafe fn error_position(
    api: &DreamGpuGlApi,
    c: *const ContextState,
) -> Result<i32, u32> {
    if unsafe { (*c).program_error_kind } != 0 {
        return Ok(unsafe { (*c).program_error });
    }
    let mut position = -1;
    unsafe {
        api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(
            GL_PROGRAM_ERROR_POSITION_ARB,
            &mut position,
        )
    };
    Ok(position)
}

/// PROGRAM_ERROR_STRING_ARB as one NUL-terminated string of printable ASCII,
/// returning its byte count including the terminator (at most `capacity`).
/// The guest drivers accept a STRING result only when every byte before its
/// final NUL is printable (32..=126), so the string ends exactly at its
/// terminator (no padding) and native text maps whitespace (Mesa's messages
/// contain newlines) to spaces and any other byte to '?'. The message is the
/// host-detected failure's, else native, truncated to `capacity - 1` bytes.
///
/// # Safety
/// `out` has `capacity` (at least one) writable bytes; native context current.
pub(crate) unsafe fn error_string(
    api: &DreamGpuGlApi,
    c: *const ContextState,
    out: *mut u8,
    capacity: usize,
) -> Result<u32, u32> {
    let host: &[u8] = match unsafe { (*c).program_error_kind } {
        ERROR_LENGTH => {
            b"DreamGPU: the program string was not transferred (DG_GL_MAX_PROGRAM_BYTES)"
        }
        ERROR_ASCII => b"DreamGPU: program string byte outside ASCII 1-127",
        ERROR_UNITS => b"DreamGPU: texture unit or coordinate set beyond the supported limit",
        ERROR_ATTRIBS => b"DreamGPU: generic vertex attribute beyond MAX_VERTEX_ATTRIBS_ARB",
        _ => b"",
    };
    let mut n = 0;
    if !host.is_empty() {
        n = host.len().min(capacity - 1);
        unsafe { core::ptr::copy_nonoverlapping(host.as_ptr(), out, n) };
    } else {
        let native = unsafe {
            api.dg_glGetString.ok_or(DG_GL_ERROR_UNSUPPORTED)?(GL_PROGRAM_ERROR_STRING_ARB)
        };
        while !native.is_null() && n < capacity - 1 {
            let b = unsafe { *native.add(n) };
            if b == 0 {
                break;
            }
            unsafe { *out.add(n) = printable(b) };
            n += 1;
        }
    }
    unsafe { *out.add(n) = 0 };
    Ok(n as u32 + 1)
}
/// The STRING result alphabet of the guest drivers' result validation.
fn printable(b: u8) -> u8 {
    match b {
        32..=126 => b,
        b'\t' | b'\n' | 0x0b | 0x0c | b'\r' => b' ',
        _ => b'?',
    }
}

/// GetProgramStringARB words [first, first + words) of the bound program's
/// string, zero-padded past its end.
///
/// # Safety
/// `out` has `words * 4` writable bytes; native context current.
pub(crate) unsafe fn read_string(
    m: &Memory,
    target: u32,
    words: u32,
    first: u32,
    out: *mut u8,
) -> Result<(), u32> {
    let api = unsafe { &*m.api };
    let get_iv = api.dg_glGetProgramivARB.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_string = api
        .dg_glGetProgramStringARB
        .ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let bytes = words as usize * 4;
    unsafe { core::ptr::write_bytes(out, 0, bytes) };
    let mut length = 0;
    unsafe { get_iv(target, GL_PROGRAM_LENGTH_ARB, &mut length) };
    // Strings are loaded only through this host, within the protocol bound.
    let length = (length.max(0) as usize).min(DG_GL_MAX_PROGRAM_BYTES as usize);
    let start = first as usize * 4;
    if start >= length {
        return Ok(());
    }
    // Native GL writes a terminator for an empty string; reserve one byte.
    let Some(copy) = (unsafe { crate::resource::OwnedBytes::zeroed(m, length + 1) }) else {
        return Err(DG_GL_ERROR_HOST);
    };
    unsafe {
        get_string(target, GL_PROGRAM_STRING_ARB, copy.p.cast());
        core::ptr::copy_nonoverlapping(copy.p.add(start), out, (length - start).min(bytes));
    }
    Ok(())
}

#[cfg(test)]
mod tests;
