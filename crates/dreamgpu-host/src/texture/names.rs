// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded guest texture namespaces and deleted-but-bound object ownership.
use super::*;
const SLOTS: usize = 8192;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Entry {
    pub name: u32,
    pub texture: *mut Texture,
}
impl Entry {
    const EMPTY: Self = Self {
        name: 0,
        texture: core::ptr::null_mut(),
    };
}
#[repr(C)]
pub struct Namespace {
    pub refs: u32,
    pub entries: [Entry; SLOTS],
}
impl Default for Namespace {
    fn default() -> Self {
        Self {
            refs: 0,
            entries: [Entry::EMPTY; SLOTS],
        }
    }
}

#[repr(C)]
pub struct Memory {
    pub api: *const DreamGpuGlApi,
    pub bytes: *mut u64,
    pub count: *mut u32,
    pub opaque: *mut c_void,
    pub allocate: unsafe extern "C" fn(*mut c_void, usize) -> *mut c_void,
    pub free: unsafe extern "C" fn(*mut c_void, *mut c_void),
    pub forget_read: unsafe extern "C" fn(*mut c_void, *mut Texture),
}

fn hash(name: u32) -> usize {
    (name.wrapping_mul(0x9e3779b1) as usize) & (SLOTS - 1)
}
unsafe fn locate(ns: *mut Namespace, name: u32) -> Result<usize, Option<usize>> {
    if name == 0 {
        return Err(None);
    }
    let mut vacant = None;
    for n in 0..SLOTS {
        let i = (hash(name) + n) & (SLOTS - 1);
        let e = unsafe { (*ns).entries[i] };
        if e.name == 0 {
            return Err(Some(vacant.unwrap_or(i)));
        }
        if e.texture.is_null() {
            vacant.get_or_insert(i);
        } else if e.name == name {
            return Ok(i);
        }
    }
    Err(vacant)
}

unsafe fn remove(ns: *mut Namespace, index: usize) {
    // Backward-shift deletion preserves probe chains without leaving tombstones.
    // Level/load churn cannot gradually turn an empty namespace into 8192 probes
    // per miss. The globally bounded texture count keeps this table <=50% full.
    let mut hole = index;
    unsafe {
        (*ns).entries[hole] = Entry::EMPTY;
    }
    for step in 1..SLOTS {
        let current = (index + step) & (SLOTS - 1);
        let entry = unsafe { (*ns).entries[current] };
        if entry.name == 0 {
            break;
        }
        let origin = hash(entry.name);
        let current_distance = current.wrapping_sub(origin) & (SLOTS - 1);
        let hole_distance = hole.wrapping_sub(origin) & (SLOTS - 1);
        if hole_distance < current_distance {
            unsafe {
                (*ns).entries[hole] = entry;
                (*ns).entries[current] = Entry::EMPTY;
            }
            hole = current;
        }
    }
}

fn flag(error: u32) -> u32 {
    let bit = error.wrapping_sub(GL_INVALID_ENUM);
    1 << if bit < 8 { bit } else { 2 }
}
unsafe fn remember(api: &DreamGpuGlApi, errors: *mut u32) {
    for _ in 0..8 {
        let error = unsafe { api.dg_glGetError.unwrap()() };
        if error == 0 {
            break;
        }
        unsafe {
            *errors |= flag(error);
        }
    }
}

unsafe fn unref(memory: &Memory, t: *mut Texture) {
    if t.is_null() {
        return;
    }
    unsafe {
        // Refcount zero is never a live object. Fail closed on corrupted host state.
        assert!((*t).refs != 0);
        (*t).refs -= 1;
        if (*t).refs != 0 {
            return;
        }
        (memory.forget_read)(memory.opaque, t);
        let api = &*memory.api;
        if !(*t).last_write.is_null() {
            api.dg_glDeleteSync.unwrap()((*t).last_write.cast());
        }
        if (*t).name != 0 && (*t).deleted == 0 {
            api.dg_glDeleteTextures.unwrap()(1, core::ptr::addr_of!((*t).name));
        }
        for level in 0..12 {
            *memory.bytes = (*memory.bytes)
                .checked_sub((*t).levels[level])
                .expect("texture accounting underflow");
        }
        *memory.count = (*memory.count)
            .checked_sub(1)
            .expect("texture count underflow");
        (memory.free)(memory.opaque, t.cast());
    }
}

/// # Safety
/// Memory callbacks and texture belong to the current native render worker.
/// No retained Rust reference crosses allocation/cache callbacks.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_unref(memory: *const Memory, texture: *mut Texture) {
    unsafe {
        unref(&*memory, texture);
    }
}

/// # Safety
/// Namespace has one reference per live native guest context; its members own
/// one texture reference. Memory callbacks are synchronous and cannot reenter.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_namespace_unref(
    memory: *const Memory,
    ns: *mut Namespace,
) {
    if ns.is_null() {
        return;
    }
    unsafe {
        assert!((*ns).refs != 0);
        (*ns).refs -= 1;
        if (*ns).refs != 0 {
            return;
        }
        for i in 0..SLOTS {
            let t = (*ns).entries[i].texture;
            if !t.is_null() {
                unref(&*memory, t);
            }
        }
        ((*memory).free)((*memory).opaque, ns.cast());
    }
}

#[expect(
    clippy::too_many_arguments,
    reason = "Keeps namespace and binding ownership raw across native allocation callbacks."
)]
unsafe fn bind(
    memory: &Memory,
    ns: *mut Namespace,
    target: u32,
    name: u32,
    default: *mut Texture,
    binding: *mut *mut Texture,
    serial: u64,
    errors: *mut u32,
) -> Result<(), u32> {
    if target != GL_TEXTURE_1D && target != GL_TEXTURE_2D {
        return Err(11);
    }
    let located = if name == 0 {
        Err(None)
    } else {
        unsafe { locate(ns, name) }
    };
    let mut texture = match located {
        Ok(i) => unsafe { (*ns).entries[i].texture },
        Err(_) => {
            if name == 0 {
                default
            } else {
                core::ptr::null_mut()
            }
        }
    };
    if !texture.is_null() && unsafe { (*texture).target } != target {
        unsafe {
            *errors |= flag(GL_INVALID_OPERATION);
        }
        return Ok(());
    }
    let api = unsafe { &*memory.api };
    if texture.is_null() {
        let slot = match located {
            Err(Some(i)) => i,
            _ => return Err(9),
        };
        if unsafe { *memory.count } >= DG_GL_MAX_TEXTURES {
            return Err(9);
        }
        texture =
            unsafe { (memory.allocate)(memory.opaque, core::mem::size_of::<Texture>()) }.cast();
        if texture.is_null() {
            return Err(6);
        }
        unsafe {
            core::ptr::write(
                texture,
                Texture {
                    guest_name: name,
                    target,
                    ..Texture::default()
                },
            );
            remember(api, errors);
            api.dg_glGenTextures.unwrap()(1, core::ptr::addr_of_mut!((*texture).name));
            if (*texture).name == 0 {
                (memory.free)(memory.opaque, texture.cast());
                return Err(6);
            }
            (*texture).refs = 1;
            *memory.count += 1;
            (*ns).entries[slot] = Entry { name, texture };
        }
    }
    unsafe {
        wait(api, texture, serial);
        api.dg_glBindTexture.unwrap()(target, (*texture).name);
        if *binding != texture {
            (*texture).refs = (*texture)
                .refs
                .checked_add(1)
                .expect("texture refs overflow");
            unref(memory, *binding);
            *binding = texture;
        }
    }
    Ok(())
}

/// # Safety
/// Memory/namespace/bindings are render-owned. The default and binding are selected
/// by texture target; all pointees stay valid through synchronous platform calls.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_bind(
    memory: *const Memory,
    ns: *mut Namespace,
    target: u32,
    name: u32,
    default: *mut Texture,
    binding: *mut *mut Texture,
    serial: u64,
    errors: *mut u32,
) -> u32 {
    match unsafe { bind(&*memory, ns, target, name, default, binding, serial, errors) } {
        Ok(()) => 0,
        Err(error) => error,
    }
}

/// # Safety
/// Data is a validated immutable list of `count` little-endian names. Native
/// context bindings and namespace obey the same ownership as `texture_bind`.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_delete(
    memory: *const Memory,
    ns: *mut Namespace,
    data: *const u8,
    count: u32,
    default_2d: *mut Texture,
    default_1d: *mut Texture,
    binding_2d: *mut *mut Texture,
    binding_1d: *mut *mut Texture,
) {
    assert!(count <= DG_GL_MAX_TEXTURES);
    let api = unsafe { &*(*memory).api };
    for i in 0..count as usize {
        let name =
            unsafe { u32::from_le(core::ptr::read_unaligned(data.add(i * 4).cast::<u32>())) };
        let Ok(slot) = (unsafe { locate(ns, name) }) else {
            continue;
        };
        let texture = unsafe { (*ns).entries[slot].texture };
        unsafe {
            api.dg_glDeleteTextures.unwrap()(1, core::ptr::addr_of!((*texture).name));
            (*texture).deleted = 1;
            remove(ns, slot);
            for (binding, default) in [(binding_2d, default_2d), (binding_1d, default_1d)] {
                if *binding == texture {
                    (*default).refs = (*default)
                        .refs
                        .checked_add(1)
                        .expect("texture refs overflow");
                    *binding = default;
                    unref(&*memory, texture);
                }
            }
            unref(&*memory, texture); // Drop namespace ownership last.
        }
    }
}

/// # Safety
/// Namespace remains alive and immutable for the render worker's lookup.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_texture_lookup(ns: *mut Namespace, name: u32) -> *mut Texture {
    match unsafe { locate(ns, name) } {
        Ok(i) => unsafe { (*ns).entries[i].texture },
        Err(_) => core::ptr::null_mut(),
    }
}

#[cfg(test)]
mod tests;
