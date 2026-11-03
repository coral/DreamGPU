// SPDX-License-Identifier: GPL-2.0-or-later
//! Render-context guest state and its owned texture/attribute references.
#![allow(non_upper_case_globals)]
use crate::{
    gl_api::*,
    texture::{
        dreamgpu_texture_wait, dreamgpu_texture_written,
        names::{
            dreamgpu_texture_bind, dreamgpu_texture_namespace_unref, dreamgpu_texture_unref,
            Memory, Namespace,
        },
        Texture,
    },
};
use core::{
    ffi::c_void,
    ptr::{addr_of_mut, null_mut},
};
const STACK: usize = 16;
/// Logical texture units; bindings follow the active unit (ARB_multitexture).
pub(crate) const UNITS: usize = DG_GL_MAX_TEXTURE_UNITS as usize;
/// Bindings of every unit except the active one, saved by TEXTURE_BIT.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct UnitAttrib {
    pub active: u32,
    pub units: [[*mut Texture; 2]; UNITS],
}
impl UnitAttrib {
    const EMPTY: Self = Self {
        active: 0,
        units: [[null_mut(); 2]; UNITS],
    };
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Attrib {
    pub mask: u32,
    pub color_sum: u32,
    pub draw_buffer: u32,
    pub read_buffer: u32,
    pub texture: *mut Texture,
    pub texture_1d: *mut Texture,
}
impl Attrib {
    const EMPTY: Self = Self {
        mask: 0,
        color_sum: 0,
        draw_buffer: 0,
        read_buffer: 0,
        texture: null_mut(),
        texture_1d: null_mut(),
    };
}
#[repr(C)]
pub struct ContextState {
    pub textures: *mut Namespace,
    pub default_texture: *mut Texture,
    pub bound_texture: *mut Texture,
    pub default_texture_1d: *mut Texture,
    pub bound_texture_1d: *mut Texture,
    pub guest_errors: u32,
    pub draw_buffer: u32,
    pub read_buffer: u32,
    pub attrib_depth: u32,
    pub attrib: [Attrib; STACK],
    pub image: crate::pixel_image::State,
    pub capture: crate::selection::State,
    pub lists: *mut crate::lists::State,
    pub list_mode: u32,
    pub proxy_rejected: [u32; 2],
    pub border_sampler: *mut crate::texture::border::State,
    /// Rust-only multitexture state, appended after the C-visible prefix.
    /// `bound_texture`/`bound_texture_1d` hold the active unit's bindings, so
    /// single-unit consumers keep operating on the active unit. `units` holds
    /// each other unit's (2D, 1D) references; the active unit's entry is null.
    pub active_unit: u32,
    pub unit_limit: u32,
    pub units: [[*mut Texture; 2]; UNITS],
    pub unit_attrib: [UnitAttrib; STACK],
    /// Rust-only cube map state (ARB_texture_cube_map), appended after the
    /// multitexture state with the same ownership: `bound_texture_cube` is the
    /// active unit's binding and `units_cube` holds every other unit's (the
    /// active unit's entry is null). `cube_attrib` saves every unit's cube
    /// binding, in unit order, with each TEXTURE_BIT entry of `attrib`.
    pub default_texture_cube: *mut Texture,
    pub bound_texture_cube: *mut Texture,
    pub units_cube: [*mut Texture; UNITS],
    pub cube_attrib: [[*mut Texture; UNITS]; STACK],
    /// PROXY_TEXTURE_CUBE_MAP levels rejected by the virtual limits.
    pub proxy_rejected_cube: u32,
    /// Rust-only ARB_fragment_program state: the bound FRAGMENT_PROGRAM_ARB
    /// object (one reference; null is the default object before it is first
    /// loaded, natively program zero), and a host-detected load failure
    /// (`program::ERROR_*`, zero for none) whose position overrides the native
    /// PROGRAM_ERROR_POSITION/STRING until the next native load.
    pub program: *mut crate::program::Program,
    pub program_error: i32,
    pub program_error_kind: u32,
    /// The bound VERTEX_PROGRAM_ARB object (ARB_vertex_program), with the
    /// fragment binding's ownership; the error state above is shared.
    pub vertex_program: *mut crate::program::Program,
    /// Rust-only 3D texture state (EXT_texture3D), with the cube map state's
    /// ownership: `bound_texture_3d` is the active unit's binding, `units_3d`
    /// every other unit's (null for the active unit), and `attrib_3d` saves
    /// every unit's binding, in unit order, with each TEXTURE_BIT entry.
    pub default_texture_3d: *mut Texture,
    pub bound_texture_3d: *mut Texture,
    pub units_3d: [*mut Texture; UNITS],
    pub attrib_3d: [[*mut Texture; UNITS]; STACK],
    /// PROXY_TEXTURE_3D levels rejected by the virtual limits.
    pub proxy_rejected_3d: u32,
}
impl ContextState {
    /// Every unit's (2D, 1D, cube map, 3D) binding, including the active unit's.
    pub(crate) fn unit_bindings(&self) -> [[*mut Texture; 4]; UNITS] {
        let mut all = [[null_mut(); 4]; UNITS];
        for (unit, out) in all.iter_mut().enumerate() {
            *out = [
                self.units[unit][0],
                self.units[unit][1],
                self.units_cube[unit],
                self.units_3d[unit],
            ];
        }
        all[self.active_unit as usize] = [
            self.bound_texture,
            self.bound_texture_1d,
            self.bound_texture_cube,
            self.bound_texture_3d,
        ];
        all
    }
    /// Every unit's cube map binding, in unit order.
    fn cube_bindings(&self) -> [*mut Texture; UNITS] {
        let mut all = self.units_cube;
        all[self.active_unit as usize] = self.bound_texture_cube;
        all
    }
    /// Every unit's 3D texture binding, in unit order.
    fn bindings_3d(&self) -> [*mut Texture; UNITS] {
        let mut all = self.units_3d;
        all[self.active_unit as usize] = self.bound_texture_3d;
        all
    }
    const EMPTY: Self = Self {
        textures: null_mut(),
        default_texture: null_mut(),
        bound_texture: null_mut(),
        default_texture_1d: null_mut(),
        bound_texture_1d: null_mut(),
        guest_errors: 0,
        draw_buffer: GL_BACK,
        read_buffer: GL_BACK,
        attrib_depth: 0,
        attrib: [Attrib::EMPTY; STACK],
        image: crate::pixel_image::State::EMPTY,
        capture: crate::selection::State::EMPTY,
        lists: null_mut(),
        list_mode: 0,
        proxy_rejected: [0; 2],
        border_sampler: null_mut(),
        active_unit: 0,
        unit_limit: 0,
        units: [[null_mut(); 2]; UNITS],
        unit_attrib: [UnitAttrib::EMPTY; STACK],
        default_texture_cube: null_mut(),
        bound_texture_cube: null_mut(),
        units_cube: [null_mut(); UNITS],
        cube_attrib: [[null_mut(); UNITS]; STACK],
        proxy_rejected_cube: 0,
        program: null_mut(),
        program_error: -1,
        program_error_kind: 0,
        vertex_program: null_mut(),
        default_texture_3d: null_mut(),
        bound_texture_3d: null_mut(),
        units_3d: [null_mut(); UNITS],
        attrib_3d: [[null_mut(); UNITS]; STACK],
        proxy_rejected_3d: 0,
    };
}
/// The active unit's binding that owns an image or binding target: a cube
/// face belongs to the cube map; anything else but 1D and 3D selects the 2D
/// binding.
///
/// # Safety
/// Read-only render-owned state.
pub(crate) unsafe fn bound(state: *const ContextState, target: u32) -> *mut Texture {
    unsafe {
        match crate::texture::binding_target(target) {
            GL_TEXTURE_1D => (*state).bound_texture_1d,
            GL_TEXTURE_CUBE_MAP => (*state).bound_texture_cube,
            GL_TEXTURE_3D => (*state).bound_texture_3d,
            _ => (*state).bound_texture,
        }
    }
}
/// The active unit's texture for an image or binding target, for the
/// platform's copy and read hooks (see `bound`).
///
/// # Safety
/// Read-only render-owned state of the current context.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_context_texture(
    state: *const ContextState,
    target: u32,
) -> *mut Texture {
    if state.is_null() {
        return null_mut();
    }
    unsafe { bound(state, target) }
}

/// Size of the Rust-owned state that C embeds; checked against its mirror.
#[no_mangle]
pub extern "C" fn dreamgpu_context_state_size() -> usize {
    core::mem::size_of::<ContextState>()
}
/// # Safety
/// State is fresh render-worker-owned storage. Memory callbacks allocate aligned
/// writable storage and cannot reenter; shared namespace is a live same-client
/// namespace. No allocation/state pointer is published until all allocations pass.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_context_state_init(
    memory: *const Memory,
    state: *mut ContextState,
    shared: *mut Namespace,
) -> u32 {
    if memory.is_null() || state.is_null() {
        return DG_GL_ERROR_CONTEXT;
    }
    let m = unsafe { &*memory };
    unsafe {
        state.write(ContextState::EMPTY);
    }
    if unsafe { *m.count } > DG_GL_MAX_TEXTURES - 4 {
        return DG_GL_ERROR_LIMIT;
    }
    let namespace = if shared.is_null() {
        unsafe { (m.allocate)(m.opaque, core::mem::size_of::<Namespace>()) }.cast()
    } else {
        shared
    };
    if namespace.is_null() {
        return DG_GL_ERROR_HOST;
    }
    // Defaults: one owner reference plus one binding per texture unit.
    let initial = || Texture {
        refs: 1 + UNITS as u32,
        ..Texture::default()
    };
    let two = unsafe { crate::texture::allocate(m, GL_TEXTURE_2D, initial()) };
    let one = if two.is_null() {
        null_mut()
    } else {
        unsafe { crate::texture::allocate(m, GL_TEXTURE_1D, initial()) }
    };
    let cube = if one.is_null() {
        null_mut()
    } else {
        unsafe { crate::texture::allocate(m, GL_TEXTURE_CUBE_MAP, initial()) }
    };
    let volume = if cube.is_null() {
        null_mut()
    } else {
        unsafe { crate::texture::allocate(m, GL_TEXTURE_3D, initial()) }
    };
    if volume.is_null() {
        for texture in [two, one, cube] {
            if !texture.is_null() {
                unsafe {
                    (m.free)(m.opaque, texture.cast());
                }
            }
        }
        if shared.is_null() {
            unsafe {
                (m.free)(m.opaque, namespace.cast());
            }
        }
        return DG_GL_ERROR_HOST;
    }
    unsafe {
        if shared.is_null() {
            core::ptr::write_bytes(namespace, 0, 1);
        }
        (*namespace).refs = (*namespace)
            .refs
            .checked_add(1)
            .expect("namespace reference overflow");
        *m.count += 4;
        let mut units = [[two, one]; UNITS];
        units[0] = [null_mut(); 2];
        let mut units_cube = [cube; UNITS];
        units_cube[0] = null_mut();
        let mut units_3d = [volume; UNITS];
        units_3d[0] = null_mut();
        state.write(ContextState {
            textures: namespace,
            default_texture: two,
            bound_texture: two,
            default_texture_1d: one,
            bound_texture_1d: one,
            units,
            default_texture_cube: cube,
            bound_texture_cube: cube,
            units_cube,
            default_texture_3d: volume,
            bound_texture_3d: volume,
            units_3d,
            ..ContextState::EMPTY
        });
    }
    0
}

/// # Safety
/// Same render-worker ownership as init. Appropriate native context is current.
/// Clear state before free/cache callbacks; release each retained reference once.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_context_state_release(
    memory: *const Memory,
    state: *mut ContextState,
) {
    if memory.is_null() || state.is_null() {
        return;
    }
    let mut old = unsafe { state.read() };
    unsafe {
        state.write(ContextState::EMPTY);
    }
    unsafe { crate::texture::border::release(&*memory, old.border_sampler) };
    unsafe { crate::pixel_image::release(&*memory, &mut old.image) };
    unsafe { crate::selection::release(&*memory, &mut old.capture) };
    unsafe { crate::lists::release(&*memory, old.lists) };
    assert!(old.attrib_depth as usize <= STACK);
    for (((saved, units), cube), volume) in old.attrib[..old.attrib_depth as usize]
        .iter()
        .zip(&old.unit_attrib)
        .zip(&old.cube_attrib)
        .zip(&old.attrib_3d)
    {
        unsafe {
            dreamgpu_texture_unref(memory, saved.texture);
            dreamgpu_texture_unref(memory, saved.texture_1d);
            for texture in units.units.iter().flatten().chain(cube).chain(volume) {
                dreamgpu_texture_unref(memory, *texture);
            }
        }
    }
    for texture in old
        .units
        .iter()
        .flatten()
        .chain(&old.units_cube)
        .chain(&old.units_3d)
    {
        unsafe { dreamgpu_texture_unref(memory, *texture) };
    }
    // Before the namespace: the last context's release frees its programs.
    unsafe {
        crate::program::release_binding(&*memory, old.program);
        crate::program::release_binding(&*memory, old.vertex_program);
    }
    unsafe {
        dreamgpu_texture_unref(memory, old.bound_texture);
        dreamgpu_texture_unref(memory, old.default_texture);
        dreamgpu_texture_unref(memory, old.bound_texture_1d);
        dreamgpu_texture_unref(memory, old.default_texture_1d);
        dreamgpu_texture_unref(memory, old.bound_texture_cube);
        dreamgpu_texture_unref(memory, old.default_texture_cube);
        dreamgpu_texture_unref(memory, old.bound_texture_3d);
        dreamgpu_texture_unref(memory, old.default_texture_3d);
        dreamgpu_texture_namespace_unref(memory, old.textures);
    }
}

fn attachment(buffer: u32) -> u32 {
    if matches!(buffer, GL_BACK | GL_BACK_LEFT) {
        GL_COLOR_ATTACHMENT0
    } else {
        GL_COLOR_ATTACHMENT1
    }
}
unsafe fn select(api: &DreamGpuGlApi, state: *const ContextState) -> Result<(), u32> {
    let draw = api.dg_glDrawBuffer.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let draws = api.dg_glDrawBuffers.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let read = api.dg_glReadBuffer.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let buffer = unsafe { (*state).draw_buffer };
    unsafe {
        if matches!(buffer, GL_FRONT_AND_BACK | GL_LEFT) {
            draws(2, [GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1].as_ptr());
        } else {
            draw(if buffer == GL_NONE {
                GL_NONE
            } else {
                attachment(buffer)
            });
        }
        read(attachment((*state).read_buffer));
    }
    Ok(())
}
/// # Safety
/// Read-only state belongs to current render context; native API is complete.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_context_select_buffers(
    api: *const DreamGpuGlApi,
    state: *const ContextState,
) -> u32 {
    if api.is_null() || state.is_null() {
        return DG_GL_ERROR_CONTEXT;
    }
    match unsafe { select(&*api, state) } {
        Ok(()) => 0,
        Err(e) => e,
    }
}
unsafe fn error(state: *mut ContextState, code: u32) {
    unsafe {
        crate::query::store(addr_of_mut!((*state).guest_errors), code);
    }
}
unsafe fn remember(api: &DreamGpuGlApi, state: *mut ContextState) -> Result<(), u32> {
    unsafe { crate::query::remember(api, addr_of_mut!((*state).guest_errors)) }
}
unsafe fn push(api: &DreamGpuGlApi, state: *mut ContextState, mask: u32) -> Result<(), u32> {
    if mask & !GL_ALL_ATTRIB_BITS != 0 {
        unsafe { error(state, GL_INVALID_VALUE) };
        return Ok(());
    }
    let depth = unsafe { (*state).attrib_depth } as usize;
    if depth == STACK {
        unsafe { error(state, GL_STACK_OVERFLOW) };
        return Ok(());
    }
    assert!(depth < STACK);
    let push = api.dg_glPushAttrib.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let enabled = api.dg_glIsEnabled.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    unsafe {
        remember(api, state)?;
        push(mask);
    }
    let code = unsafe { get_error() };
    if code != 0 {
        unsafe { error(state, code) };
        return Ok(());
    }
    let mut saved = Attrib {
        mask,
        draw_buffer: unsafe { (*state).draw_buffer },
        read_buffer: unsafe { (*state).read_buffer },
        ..Attrib::EMPTY
    };
    if mask & (GL_FOG_BIT | GL_ENABLE_BIT) != 0 {
        saved.color_sum = unsafe { enabled(GL_COLOR_SUM) } as u32;
    }
    let mut units = UnitAttrib::EMPTY;
    let mut cube = [null_mut(); UNITS];
    let mut volume = [null_mut(); UNITS];
    if mask & GL_TEXTURE_BIT != 0 {
        unsafe {
            saved.texture = (*state).bound_texture;
            saved.texture_1d = (*state).bound_texture_1d;
            // TEXTURE_BIT also covers ACTIVE_TEXTURE and every unit's bindings.
            units = UnitAttrib {
                active: (*state).active_unit,
                units: (*state).units,
            };
            cube = (*state).cube_bindings();
            volume = (*state).bindings_3d();
            for texture in [saved.texture, saved.texture_1d]
                .iter()
                .chain(units.units.iter().flatten())
                .chain(&cube)
                .chain(&volume)
            {
                if !texture.is_null() {
                    (**texture).refs = (**texture)
                        .refs
                        .checked_add(1)
                        .expect("texture reference overflow");
                }
            }
        }
    }
    unsafe {
        (*state).attrib[depth] = saved;
        (*state).unit_attrib[depth] = units;
        (*state).cube_attrib[depth] = cube;
        (*state).attrib_3d[depth] = volume;
        (*state).attrib_depth += 1;
    }
    Ok(())
}
unsafe fn pop(memory: *const Memory, state: *mut ContextState, serial: u64) -> Result<(), u32> {
    let api = unsafe { &*(*memory).api };
    let depth = unsafe { (*state).attrib_depth } as usize;
    if depth == 0 {
        unsafe { error(state, GL_STACK_UNDERFLOW) };
        return Ok(());
    }
    assert!(depth <= STACK);
    let saved = unsafe { (*state).attrib[depth - 1] };
    let pop = api.dg_glPopAttrib.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    // Precheck restoration before consuming the native attribute stack entry.
    let enable = api.dg_glEnable.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let disable = api.dg_glDisable.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    if api.dg_glDrawBuffer.is_none()
        || api.dg_glDrawBuffers.is_none()
        || api.dg_glReadBuffer.is_none()
        || api.dg_glWaitSync.is_none()
        || api.dg_glFenceSync.is_none()
        || api.dg_glDeleteSync.is_none()
    {
        return Err(DG_GL_ERROR_UNSUPPORTED);
    }
    let saved_units = unsafe { (*state).unit_attrib[depth - 1] };
    let saved_cube = unsafe { (*state).cube_attrib[depth - 1] };
    let saved_3d = unsafe { (*state).attrib_3d[depth - 1] };
    let (restored, restored_count) = distinct(&saved, &saved_units, &saved_cube, &saved_3d);
    if !saved.texture.is_null() {
        for texture in &restored[..restored_count] {
            unsafe { dreamgpu_texture_wait(api, *texture, serial) };
        }
    }
    unsafe {
        remember(api, state)?;
        pop();
    }
    let code = unsafe { get_error() };
    if code != 0 {
        unsafe { error(state, code) };
        return Ok(());
    }
    if saved.mask & (GL_FOG_BIT | GL_ENABLE_BIT) != 0 {
        unsafe {
            if saved.color_sum != 0 {
                enable(GL_COLOR_SUM)
            } else {
                disable(GL_COLOR_SUM)
            }
        }
    }
    unsafe {
        if saved.mask & GL_COLOR_BUFFER_BIT != 0 {
            (*state).draw_buffer = saved.draw_buffer;
        }
        if saved.mask & GL_PIXEL_MODE_BIT != 0 {
            (*state).read_buffer = saved.read_buffer;
        }
        if !saved.texture.is_null() {
            let old_two = (*state).bound_texture;
            let old_one = (*state).bound_texture_1d;
            let old_units = (*state).units;
            let old_cube = (*state).cube_bindings();
            let old_3d = (*state).bindings_3d();
            (*state).bound_texture = saved.texture;
            (*state).bound_texture_1d = saved.texture_1d;
            (*state).units = saved_units.units;
            (*state).active_unit = saved_units.active;
            let active = saved_units.active as usize;
            (*state).bound_texture_cube = saved_cube[active];
            (*state).units_cube = saved_cube;
            (*state).units_cube[active] = null_mut();
            (*state).bound_texture_3d = saved_3d[active];
            (*state).units_3d = saved_3d;
            (*state).units_3d[active] = null_mut();
            dreamgpu_texture_unref(memory, old_two);
            dreamgpu_texture_unref(memory, old_one);
            for texture in old_units.iter().flatten().chain(&old_cube).chain(&old_3d) {
                dreamgpu_texture_unref(memory, *texture);
            }
            // Native PopAttrib restored parameters of every unit's binding.
            for texture in &restored[..restored_count] {
                dreamgpu_texture_written(api, *texture, serial);
            }
        }
        (*state).attrib[depth - 1] = Attrib::EMPTY;
        (*state).unit_attrib[depth - 1] = UnitAttrib::EMPTY;
        (*state).cube_attrib[depth - 1] = [null_mut(); UNITS];
        (*state).attrib_3d[depth - 1] = [null_mut(); UNITS];
        (*state).attrib_depth -= 1;
        select(api, state)?;
    }
    Ok(())
}
/// Distinct non-null textures of a saved TEXTURE_BIT entry, so each storage
/// object is fenced once even when one default is bound on many units.
fn distinct(
    saved: &Attrib,
    units: &UnitAttrib,
    cube: &[*mut Texture; UNITS],
    volume: &[*mut Texture; UNITS],
) -> ([*mut Texture; 2 + 4 * UNITS], usize) {
    let mut out = [null_mut(); 2 + 4 * UNITS];
    let mut n = 0;
    for &texture in [saved.texture, saved.texture_1d]
        .iter()
        .chain(units.units.iter().flatten())
        .chain(cube)
        .chain(volume)
    {
        if !texture.is_null() && !out[..n].contains(&texture) {
            out[n] = texture;
            n += 1;
        }
    }
    (out, n)
}
/// Every unit's live (2D, 1D, cube map, 3D) binding slot, for texture deletion.
pub(crate) unsafe fn binding_slots(state: *mut ContextState) -> [*mut *mut Texture; 4 * UNITS] {
    let mut slots = [null_mut(); 4 * UNITS];
    unsafe {
        let active = (*state).active_unit as usize;
        for (unit, quad) in slots.as_chunks_mut::<4>().0.iter_mut().enumerate() {
            if unit == active {
                quad[0] = addr_of_mut!((*state).bound_texture);
                quad[1] = addr_of_mut!((*state).bound_texture_1d);
                quad[2] = addr_of_mut!((*state).bound_texture_cube);
                quad[3] = addr_of_mut!((*state).bound_texture_3d);
            } else {
                quad[0] = addr_of_mut!((*state).units[unit][0]);
                quad[1] = addr_of_mut!((*state).units[unit][1]);
                quad[2] = addr_of_mut!((*state).units_cube[unit]);
                quad[3] = addr_of_mut!((*state).units_3d[unit]);
            }
        }
    }
    slots
}
/// Native fixed-function unit count, clamped to the logical table.
pub(crate) unsafe fn unit_limit(api: &DreamGpuGlApi, state: *mut ContextState) -> Result<u32, u32> {
    unsafe {
        if (*state).unit_limit == 0 {
            let mut units = 0i32;
            api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(GL_MAX_TEXTURE_UNITS, &mut units);
            (*state).unit_limit = (units.max(1) as u32).min(UNITS as u32);
        }
        Ok((*state).unit_limit)
    }
}
/// Move binding ownership so `bound_texture*` always describe the active unit.
unsafe fn switch_unit(state: *mut ContextState, unit: u32) {
    let s = unsafe { &mut *state };
    if unit == s.active_unit {
        return;
    }
    s.units[s.active_unit as usize] = [s.bound_texture, s.bound_texture_1d];
    [s.bound_texture, s.bound_texture_1d] = s.units[unit as usize];
    s.units[unit as usize] = [null_mut(); 2];
    s.units_cube[s.active_unit as usize] = s.bound_texture_cube;
    s.bound_texture_cube = core::mem::replace(&mut s.units_cube[unit as usize], null_mut());
    s.units_3d[s.active_unit as usize] = s.bound_texture_3d;
    s.bound_texture_3d = core::mem::replace(&mut s.units_3d[unit as usize], null_mut());
    s.active_unit = unit;
}
/// Draw admission for every texture unit: an enabled target must not sample
/// undefined levels, and each bound texture's producer fence is honored.
/// Only a unit holding a texture with undefined levels needs a native enable
/// query, so ordinary draws issue no active-unit switches.
pub(crate) unsafe fn prepare_textures(
    api: &DreamGpuGlApi,
    state: *const ContextState,
    serial: u64,
) -> Result<(), u32> {
    let (all, active) = unsafe { ((*state).unit_bindings(), (*state).active_unit) };
    let enabled = api.dg_glIsEnabled.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    // An enabled fragment program samples any unit's bound textures regardless
    // of their fixed-function enables; asked once, only when it matters.
    let mut program = None;
    for (unit, triple) in all.iter().enumerate() {
        // Undefined levels of any face poison an enabled cube map (the
        // texture's summary covers all six faces).
        for (texture, cap) in [
            (triple[0], GL_TEXTURE_2D),
            (triple[1], GL_TEXTURE_1D),
            (triple[2], GL_TEXTURE_CUBE_MAP),
            (triple[3], GL_TEXTURE_3D),
        ] {
            if texture.is_null() || unsafe { (*texture).undefined_levels } == 0 {
                continue;
            }
            let on = if unit as u32 == active {
                unsafe { enabled(cap) }
            } else {
                let select = api.dg_glActiveTexture.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
                unsafe {
                    select(GL_TEXTURE0 + unit as u32);
                    let on = enabled(cap);
                    select(GL_TEXTURE0 + active);
                    on
                }
            };
            if on != 0
                || *program.get_or_insert_with(|| unsafe { enabled(GL_FRAGMENT_PROGRAM_ARB) } != 0)
            {
                return Err(DG_GL_ERROR_TEXTURE);
            }
        }
    }
    for texture in all.iter().flatten() {
        if !texture.is_null() {
            unsafe { dreamgpu_texture_wait(api, *texture, serial) };
        }
    }
    Ok(())
}
pub type CopyTexture = unsafe extern "C" fn(*mut c_void, u32, *const u8) -> u32;
unsafe fn scalar(
    memory: *const Memory,
    state: *mut ContextState,
    serial: u64,
    function: u32,
    args: &[u8],
    copy: Option<CopyTexture>,
    opaque: *mut c_void,
) -> Result<(), u32> {
    let api = unsafe { &*(*memory).api };
    let word = |n: usize| u32::from_le_bytes(args[n * 4..n * 4 + 4].try_into().unwrap());
    macro_rules! call{($field:ident($($args:expr),*))=>{unsafe{api.$field.ok_or(DG_GL_ERROR_UNSUPPORTED)?($($args),*)}};}
    match function {
        FEnum_glPushAttrib => return unsafe { push(api, state, word(0)) },
        FEnum_glPopAttrib => return unsafe { pop(memory, state, serial) },
        FEnum_glDrawBuffer | FEnum_glReadBuffer => {
            let mode = word(0);
            let draw = function == FEnum_glDrawBuffer;
            if crate::gl_validation::dreamgpu_gl_buffer_selection(mode, draw as u32) == 0 {
                return Err(DG_GL_ERROR_UNSUPPORTED);
            }
            unsafe {
                if draw {
                    (*state).draw_buffer = mode
                } else {
                    (*state).read_buffer = mode
                }
                select(api, state)?;
            }
        }
        FEnum_glHint => {
            if unsafe { crate::gl_validation::dreamgpu_gl_call_validate(function, args.as_ptr()) }
                != 0
            {
                return Err(DG_GL_ERROR_UNSUPPORTED);
            }
            call!(dg_glHint(word(0), word(1)));
        }
        FEnum_glActiveTexture => {
            let unit = word(0).wrapping_sub(GL_TEXTURE0);
            if unit >= unsafe { unit_limit(api, state)? } {
                unsafe { error(state, GL_INVALID_ENUM) };
                return Ok(());
            }
            call!(dg_glActiveTexture(word(0)));
            unsafe { switch_unit(state, unit) };
        }
        FEnum_glBindTexture => {
            let target = word(0);
            let (default, binding) = unsafe {
                match target {
                    GL_TEXTURE_1D => (
                        (*state).default_texture_1d,
                        addr_of_mut!((*state).bound_texture_1d),
                    ),
                    GL_TEXTURE_CUBE_MAP => (
                        (*state).default_texture_cube,
                        addr_of_mut!((*state).bound_texture_cube),
                    ),
                    GL_TEXTURE_3D => (
                        (*state).default_texture_3d,
                        addr_of_mut!((*state).bound_texture_3d),
                    ),
                    _ => (
                        (*state).default_texture,
                        addr_of_mut!((*state).bound_texture),
                    ),
                }
            };
            let result = unsafe {
                dreamgpu_texture_bind(
                    memory,
                    (*state).textures,
                    target,
                    word(1),
                    default,
                    binding,
                    serial,
                    addr_of_mut!((*state).guest_errors),
                )
            };
            if result != 0 {
                return Err(result);
            }
        }
        FEnum_glCopyTexImage2D
        | FEnum_glCopyTexSubImage2D
        | FEnum_glCopyTexImage1D
        | FEnum_glCopyTexSubImage1D
        | FEnum_glCopyTexSubImage3D => {
            let result =
                unsafe { copy.ok_or(DG_GL_ERROR_UNSUPPORTED)?(opaque, function, args.as_ptr()) };
            if result != 0 {
                return Err(result);
            }
        }
        FEnum_glTexParameteri | FEnum_glTexParameterf => {
            if crate::gl_validation::dreamgpu_gl_texture_params(word(0), word(1)) != 1 {
                return Err(DG_GL_ERROR_TEXTURE);
            }
            let texture = unsafe { bound(state, word(0)) };
            unsafe {
                dreamgpu_texture_wait(api, texture, serial);
            }
            if function == FEnum_glTexParameteri {
                call!(dg_glTexParameteri(word(0), word(1), word(2) as i32));
            } else {
                call!(dg_glTexParameterf(
                    word(0),
                    word(1),
                    f32::from_bits(word(2))
                ));
            }
            unsafe {
                dreamgpu_texture_written(api, texture, serial);
            }
        }
        FEnum_glTexEnvi | FEnum_glTexEnvf => {
            if crate::gl_validation::dreamgpu_gl_texture_env_params(word(0), word(1)) != 1 {
                return Err(DG_GL_ERROR_TEXTURE);
            }
            if function == FEnum_glTexEnvi {
                call!(dg_glTexEnvi(word(0), word(1), word(2) as i32));
            } else {
                call!(dg_glTexEnvf(word(0), word(1), f32::from_bits(word(2))));
            }
        }
        FEnum_glBegin => {
            if word(0) > GL_POLYGON {
                return Err(DG_GL_ERROR_CONTEXT);
            }
            unsafe {
                prepare_textures(api, state, serial)?;
                crate::texture::border::prepare(&*memory, state)?;
            }
        }
        FEnum_glEnd => unsafe { crate::texture::border::finish(&*memory, state) },
        FEnum_glBindProgramARB => {
            return unsafe { crate::program::bind(&*memory, state, word(0), word(1)) };
        }
        FEnum_glProgramLocalParameter4fARB => {
            let v = [2, 3, 4, 5].map(|i| f32::from_bits(word(i)));
            return unsafe { crate::program::local(&*memory, state, word(0), word(1), v) };
        }
        _ => return Err(DG_GL_ERROR_UNSUPPORTED),
    }
    Ok(())
}
/// # Safety
/// Current render context owns state. Args are an immutable command snapshot.
/// Copy callback can inspect state but cannot retain pointers/reenter. Rust uses
/// raw state access and holds no exclusive context reference across callbacks.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_context_resource(
    memory: *const Memory,
    state: *mut ContextState,
    serial: u64,
    function: u32,
    args: *const u8,
    bytes: u32,
    copy: Option<CopyTexture>,
    opaque: *mut c_void,
) -> u32 {
    if memory.is_null() || state.is_null() || args.is_null() {
        return DG_GL_ERROR_CONTEXT;
    }
    let words = crate::gl_validation::dreamgpu_gl_function_words(function);
    if words > 32 || words * 4 != bytes {
        return DG_GL_ERROR_BATCH;
    }
    let args = unsafe { core::slice::from_raw_parts(args, bytes as usize) };
    match unsafe { scalar(memory, state, serial, function, args, copy, opaque) } {
        Ok(()) => 0,
        Err(e) => e,
    }
}
#[cfg(test)]
mod tests;
