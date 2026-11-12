// SPDX-License-Identifier: GPL-2.0-or-later
//! Directional lights without spot cones.
//!
//! GL computes a light's spot factor from its cone whenever SPOT_CUTOFF is
//! not 180, also for a light at infinity (POSITION w = 0), and Mesa follows
//! that formula. Direct3D ignores spot parameters for directional lights, and
//! games of the era relied on their drivers doing the same. Hitman 2's OpenGL
//! renderer (RenderOpenGL.dll, written for NVIDIA) sets spot cones through its
//! full light path, then reuses the same lights as directional ones and
//! changes only their colours and positions: under Mesa the stale cones zero
//! every light, and lit characters draw black.
//!
//! For one draw (an array draw, or Begin to End), every enabled directional
//! light with a cone runs natively with SPOT_CUTOFF 180, then gets the guest's
//! value back. Queries, attribute stacks and display lists therefore keep the
//! guest's value. Lighting off, an enabled ARB vertex program (which may read
//! the cone through `state.light[n].spot.direction`) and positional lights
//! keep native behaviour. Only SPOT_CUTOFF changes; it is not transformed by
//! the modelview matrix and the calls raise no GL error.
#![allow(non_upper_case_globals)]
use crate::gl_api::*;

/// GL_MAX_LIGHTS of the guest contract (LIGHT0..LIGHT7).
pub(crate) const LIGHTS: usize = 8;

/// Lights held at SPOT_CUTOFF 180 (a mask) and their guest cutoffs. A
/// context keeps one from Begin to End (include/dreamgpu-host.h mirrors it).
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Held {
    pub lights: u32,
    pub cutoffs: [f32; LIGHTS],
}
impl Held {
    pub(crate) const EMPTY: Self = Self {
        lights: 0,
        cutoffs: [0.; LIGHTS],
    };
}

/// Holds every enabled directional light's cone open for the next draw.
///
/// # Safety
/// `api` belongs to the current native context, outside Begin/End.
pub(crate) unsafe fn hold(api: &DreamGpuGlApi) -> Held {
    let mut held = Held::EMPTY;
    let (Some(enabled), Some(get), Some(set)) =
        (api.dg_glIsEnabled, api.dg_glGetLightfv, api.dg_glLightf)
    else {
        return held;
    };
    if unsafe { enabled(GL_LIGHTING) } == 0 {
        return held;
    }
    let mut program = None;
    for (i, cutoff) in held.cutoffs.iter_mut().enumerate() {
        let light = GL_LIGHT0 + i as u32;
        if unsafe { enabled(light) } == 0 {
            continue;
        }
        let mut position = [0f32; 4];
        unsafe { get(light, GL_POSITION, position.as_mut_ptr()) };
        if position[3] != 0. {
            continue;
        }
        let mut value = 180f32;
        unsafe { get(light, GL_SPOT_CUTOFF, &mut value) };
        if value == 180. {
            continue;
        }
        // Asked once, and only when a cone would change.
        if *program.get_or_insert_with(|| unsafe { enabled(GL_VERTEX_PROGRAM_ARB) } != 0) {
            break;
        }
        unsafe { set(light, GL_SPOT_CUTOFF, 180.) };
        *cutoff = value;
        held.lights |= 1 << i;
    }
    held
}

/// Gives each held light its guest cutoff back.
///
/// # Safety
/// As for [`hold`], with the same context.
pub(crate) unsafe fn release(api: &DreamGpuGlApi, held: &Held) {
    let Some(set) = api.dg_glLightf else {
        return;
    };
    for (i, cutoff) in held.cutoffs.iter().enumerate() {
        if held.lights & (1 << i) != 0 {
            unsafe { set(GL_LIGHT0 + i as u32, GL_SPOT_CUTOFF, *cutoff) };
        }
    }
}

#[cfg(test)]
mod tests;
