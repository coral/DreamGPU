// SPDX-License-Identifier: GPL-2.0-or-later
//! CGL's compatibility sampler ignores supplied image borders. A private
//! fragment-only program samples exact native texels while native vertex
//! processing, alpha/depth/stencil tests, blending and rasterization remain in GL.
//! No private sampler state survives a draw, and no guest shader API is exposed.
#![allow(non_upper_case_globals)]
use super::{
    names::{dreamgpu_texture_unref, Memory},
    Texture,
};
use crate::{gl_api::*, pixels, query, resource::OwnedBytes, state::ContextState};
use core::ptr::null_mut;

pub(crate) struct State {
    texture: *mut Texture,
    version: u64,
    atlas: u32,
    program: u32,
    charged: u64,
    levels: [[f32; 4]; 12],
    count: i32,
    width: i32,
    height: i32,
    format: i32,
    base: i32,
    old_program: i32,
    old_active: i32,
    old_binding: i32,
    active: bool,
}
impl State {
    const EMPTY: Self = Self {
        texture: null_mut(),
        version: 0,
        atlas: 0,
        program: 0,
        charged: 0,
        levels: [[0.; 4]; 12],
        count: 0,
        width: 0,
        height: 0,
        format: 0,
        base: 0,
        old_program: 0,
        old_active: 0,
        old_binding: 0,
        active: false,
    };
}
unsafe fn discard(m: &Memory, s: &mut State) {
    let a = unsafe { &*m.api };
    if s.atlas != 0 {
        unsafe {
            a.dg_glDeleteTextures.unwrap()(1, &s.atlas);
        }
        s.atlas = 0;
    }
    if s.charged != 0 {
        unsafe {
            *m.bytes -= s.charged;
        }
        s.charged = 0;
    }
    let texture = core::mem::replace(&mut s.texture, null_mut());
    if !texture.is_null() {
        unsafe {
            dreamgpu_texture_unref(m, texture);
        }
    }
}
pub(crate) unsafe fn release(m: &Memory, state: *mut State) {
    if state.is_null() {
        return;
    }
    let s = unsafe { &mut *state };
    unsafe {
        restore(&*m.api, s);
        discard(m, s);
    }
    if s.program != 0 {
        unsafe {
            (*m.api).dg_glDeleteProgram.unwrap()(s.program);
        }
    }
    unsafe {
        (m.free)(m.opaque, state.cast());
    }
}
unsafe fn restore(a: &DreamGpuGlApi, s: &mut State) {
    if !s.active {
        return;
    }
    unsafe {
        a.dg_glUseProgram.unwrap()(s.old_program as u32);
        a.dg_glActiveTexture.unwrap()(GL_TEXTURE1);
        a.dg_glBindTexture.unwrap()(GL_TEXTURE_2D, s.old_binding as u32);
        a.dg_glActiveTexture.unwrap()(s.old_active as u32);
    }
    s.active = false;
}
pub(crate) unsafe fn finish(m: &Memory, c: *mut ContextState) {
    let s = unsafe { (*c).border_sampler };
    if !s.is_null() {
        unsafe {
            restore(&*m.api, &mut *s);
        }
    }
}
unsafe fn integer(a: &DreamGpuGlApi, name: u32) -> i32 {
    let mut v = 0;
    unsafe {
        a.dg_glGetIntegerv.unwrap()(name, &mut v);
    }
    v
}
unsafe fn parameter(a: &DreamGpuGlApi, target: u32, name: u32) -> i32 {
    let mut v = 0;
    unsafe {
        a.dg_glGetTexParameteriv.unwrap()(target, name, &mut v);
    }
    v
}
unsafe fn level_parameter(a: &DreamGpuGlApi, target: u32, level: i32, name: u32) -> i32 {
    let mut v = 0;
    unsafe {
        a.dg_glGetTexLevelParameteriv.unwrap()(target, level, name, &mut v);
    }
    v
}
fn base_format(format: i32) -> i32 {
    (match format as u32 {
        1 | GL_LUMINANCE | GL_LUMINANCE4..=GL_LUMINANCE16 => GL_LUMINANCE,
        2 | GL_LUMINANCE_ALPHA | GL_LUMINANCE4_ALPHA4..=GL_LUMINANCE16_ALPHA16 => {
            GL_LUMINANCE_ALPHA
        }
        3 | GL_RGB | GL_R3_G3_B2 | GL_RGB4..=GL_RGB16 => GL_RGB,
        GL_ALPHA | GL_ALPHA4..=GL_ALPHA16 => GL_ALPHA,
        GL_INTENSITY | GL_INTENSITY4..=GL_INTENSITY16 => GL_INTENSITY,
        _ => GL_RGBA,
    }) as i32
}
unsafe fn compile(a: &DreamGpuGlApi) -> Result<u32, u32> {
    let source = include_bytes!("border.glsl");
    let shader = unsafe { a.dg_glCreateShader.ok_or(DG_GL_ERROR_UNSUPPORTED)?(GL_FRAGMENT_SHADER) };
    if shader == 0 {
        return Err(DG_GL_ERROR_HOST);
    }
    let mut status = 0;
    unsafe {
        let pointer = source.as_ptr().cast();
        let length = source.len() as i32;
        a.dg_glShaderSource.unwrap()(shader, 1, &pointer, &length);
        a.dg_glCompileShader.unwrap()(shader);
        a.dg_glGetShaderiv.unwrap()(shader, GL_COMPILE_STATUS, &mut status);
    }
    if status == 0 {
        unsafe {
            a.dg_glDeleteShader.unwrap()(shader);
        }
        return Err(DG_GL_ERROR_HOST);
    }
    let program = unsafe { a.dg_glCreateProgram.unwrap()() };
    if program != 0 {
        unsafe {
            a.dg_glAttachShader.unwrap()(program, shader);
            a.dg_glLinkProgram.unwrap()(program);
            a.dg_glGetProgramiv.unwrap()(program, GL_LINK_STATUS, &mut status);
        }
    }
    unsafe {
        a.dg_glDeleteShader.unwrap()(shader);
    }
    if program == 0 || status == 0 {
        if program != 0 {
            unsafe {
                a.dg_glDeleteProgram.unwrap()(program);
            }
        }
        return Err(DG_GL_ERROR_HOST);
    }
    Ok(program)
}
struct Stores<'a> {
    api: &'a DreamGpuGlApi,
    unpack: [i32; 5],
}
impl<'a> Stores<'a> {
    unsafe fn new(api: &'a DreamGpuGlApi) -> Self {
        let mut s = Self {
            api,
            unpack: [0; 5],
        };
        for (i, key) in [
            GL_UNPACK_ALIGNMENT,
            GL_UNPACK_ROW_LENGTH,
            GL_UNPACK_SKIP_ROWS,
            GL_UNPACK_SKIP_PIXELS,
            GL_UNPACK_SWAP_BYTES,
        ]
        .into_iter()
        .enumerate()
        {
            unsafe {
                api.dg_glGetIntegerv.unwrap()(key, &mut s.unpack[i]);
                api.dg_glPixelStorei.unwrap()(key, i32::from(i == 0));
            }
        }
        s
    }
}
impl Drop for Stores<'_> {
    fn drop(&mut self) {
        for (key, value) in [
            GL_UNPACK_ALIGNMENT,
            GL_UNPACK_ROW_LENGTH,
            GL_UNPACK_SKIP_ROWS,
            GL_UNPACK_SKIP_PIXELS,
            GL_UNPACK_SWAP_BYTES,
        ]
        .into_iter()
        .zip(self.unpack)
        {
            unsafe {
                self.api.dg_glPixelStorei.unwrap()(key, value);
            }
        }
    }
}
unsafe fn upload_atlas(
    m: &Memory,
    s: &mut State,
    texture: *mut Texture,
    target: u32,
    base: i32,
    count: i32,
    format: i32,
) -> Result<(), u32> {
    let a = unsafe { &*m.api };
    unsafe {
        discard(m, s);
    }
    let two = target == GL_TEXTURE_2D;
    let mut levels = [[0.; 4]; 12];
    let mut extent = [0, 0];
    let mut column_y = 0;
    let mut temporary_bytes = 0;
    for (i, out) in levels.iter_mut().enumerate().take(count as usize) {
        let level = base as usize + i;
        let w = unsafe { (*texture).widths[level] } as i32;
        let h = unsafe { (*texture).heights[level] } as i32;
        let x = if two && i != 0 {
            (unsafe { (*texture).widths[base as usize] }) as i32
        } else {
            0
        };
        let y = if two && i == 0 { 0 } else { column_y };
        *out = [
            (w - 2) as f32,
            if two { (h - 2) as f32 } else { 1. },
            y as f32,
            x as f32,
        ];
        if !two || i != 0 {
            column_y += h;
        }
        extent[0] = extent[0].max(x + w);
        extent[1] = extent[1].max(y + h);
        temporary_bytes = temporary_bytes.max(w as u64 * h as u64 * 8);
    }
    let maximum = unsafe { integer(a, GL_MAX_TEXTURE_SIZE) };
    if extent[0] > maximum || extent[1] > maximum {
        return Err(DG_GL_ERROR_LIMIT);
    }
    let charge = extent[0] as u64 * extent[1] as u64 * 8;
    let used = unsafe { *m.bytes };
    if charge + temporary_bytes > u64::from(DG_GL_MAX_TEXTURE_BYTES).saturating_sub(used) {
        return Err(DG_GL_ERROR_LIMIT);
    }
    let Some(pixels) = (unsafe { OwnedBytes::zeroed(m, temporary_bytes as usize) }) else {
        return Err(DG_GL_ERROR_HOST);
    };
    let _pack = unsafe { query::Pack::new(a)? };
    let _stores = unsafe { Stores::new(a) };
    let _transfer = unsafe { pixels::Neutral::new(a)? };
    let mut atlas = 0;
    unsafe {
        a.dg_glActiveTexture.unwrap()(GL_TEXTURE1);
        a.dg_glGenTextures.unwrap()(1, &mut atlas);
    }
    if atlas == 0 {
        return Err(DG_GL_ERROR_HOST);
    }
    // Charge the GPU allocation until exact context-owned deletion. The private
    // staging allocation is bounded by the same preflight peak-memory limit.
    s.atlas = atlas;
    s.charged = charge;
    unsafe {
        *m.bytes += charge;
    }
    unsafe {
        a.dg_glBindTexture.unwrap()(GL_TEXTURE_2D, atlas);
        for (key, value) in [
            (GL_TEXTURE_MIN_FILTER, GL_NEAREST),
            (GL_TEXTURE_MAG_FILTER, GL_NEAREST),
            (GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE),
            (GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE),
            (GL_TEXTURE_BASE_LEVEL, 0),
            (GL_TEXTURE_MAX_LEVEL, 0),
        ] {
            a.dg_glTexParameteri.unwrap()(GL_TEXTURE_2D, key, value as i32);
        }
        a.dg_glTexImage2D.unwrap()(
            GL_TEXTURE_2D,
            0,
            GL_RGBA16 as i32,
            extent[0],
            extent[1],
            0,
            GL_RGBA,
            GL_UNSIGNED_SHORT,
            core::ptr::null(),
        );
    }
    let mut error = unsafe { a.dg_glGetError.unwrap()() };
    if error == 0 {
        for (i, data) in levels.iter().enumerate().take(count as usize) {
            let level = base as usize + i;
            unsafe {
                a.dg_glActiveTexture.unwrap()(GL_TEXTURE0);
                a.dg_glGetTexImage.unwrap()(
                    target,
                    level as i32,
                    GL_RGBA,
                    GL_UNSIGNED_SHORT,
                    pixels.p.cast(),
                );
                error = a.dg_glGetError.unwrap()();
                if error != 0 {
                    break;
                }
                a.dg_glActiveTexture.unwrap()(GL_TEXTURE1);
                a.dg_glTexSubImage2D.unwrap()(
                    GL_TEXTURE_2D,
                    0,
                    data[3] as i32,
                    data[2] as i32,
                    (*texture).widths[level] as i32,
                    (*texture).heights[level] as i32,
                    GL_RGBA,
                    GL_UNSIGNED_SHORT,
                    pixels.p.cast(),
                );
                error = a.dg_glGetError.unwrap()();
            }
            if error != 0 {
                break;
            }
        }
    }
    if error != 0 {
        unsafe {
            discard(m, s);
        }
        return Err(DG_GL_ERROR_HOST);
    }
    s.levels = levels;
    s.width = extent[0];
    s.height = extent[1];
    s.count = count;
    s.base = base;
    s.format = base_format(format);
    s.texture = texture;
    s.version = unsafe { (*texture).version };
    unsafe {
        (*texture).refs += 1;
    }
    Ok(())
}
unsafe fn location(a: &DreamGpuGlApi, p: u32, name: &[u8]) -> i32 {
    unsafe { a.dg_glGetUniformLocation.unwrap()(p, name.as_ptr().cast()) }
}
unsafe fn set_i(a: &DreamGpuGlApi, p: u32, name: &[u8], v: i32) {
    unsafe {
        a.dg_glUniform1i.unwrap()(location(a, p, name), v);
    }
}
unsafe fn set_f(a: &DreamGpuGlApi, p: u32, name: &[u8], v: f32) {
    unsafe {
        a.dg_glUniform1f.unwrap()(location(a, p, name), v);
    }
}
unsafe fn uniforms(a: &DreamGpuGlApi, s: &State, target: u32, min: i32) -> Result<(), u32> {
    let p = s.program;
    unsafe {
        set_i(a, p, b"image\0", 1);
        set_i(a, p, b"is2D\0", i32::from(target == GL_TEXTURE_2D));
        set_i(a, p, b"levelCount\0", s.count);
        set_i(a, p, b"baseFormat\0", s.format);
        a.dg_glUniform2f.unwrap()(
            location(a, p, b"atlasSize\0"),
            s.width as f32,
            s.height as f32,
        );
        a.dg_glUniform4fv.unwrap()(
            location(a, p, b"levels\0"),
            s.count,
            s.levels.as_ptr().cast(),
        );
        set_i(a, p, b"minFilter\0", min);
        for (name, key) in [
            (b"magFilter\0".as_slice(), GL_TEXTURE_MAG_FILTER),
            (b"wrapS\0", GL_TEXTURE_WRAP_S),
            (b"wrapT\0", GL_TEXTURE_WRAP_T),
        ] {
            set_i(a, p, name, parameter(a, target, key));
        }
        for (name, key) in [
            (b"minLOD\0".as_slice(), GL_TEXTURE_MIN_LOD),
            (b"maxLOD\0", GL_TEXTURE_MAX_LOD),
        ] {
            let mut value = 0.;
            a.dg_glGetTexParameterfv.unwrap()(target, key, &mut value);
            set_f(a, p, name, value);
        }
        let mut bias = 0.;
        a.dg_glGetTexEnvfv.unwrap()(GL_TEXTURE_FILTER_CONTROL, GL_TEXTURE_LOD_BIAS, &mut bias);
        set_f(a, p, b"lodBias\0", bias);
        let mut color = [0.; 4];
        a.dg_glGetTexEnvfv.unwrap()(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color.as_mut_ptr());
        a.dg_glUniform4fv.unwrap()(location(a, p, b"envColor\0"), 1, color.as_ptr());
        a.dg_glGetFloatv.unwrap()(GL_FOG_COLOR, color.as_mut_ptr());
        a.dg_glUniform4fv.unwrap()(location(a, p, b"fogColor\0"), 1, color.as_ptr());
        for (name, key) in [
            (b"envMode\0".as_slice(), GL_TEXTURE_ENV_MODE),
            (b"combineRGB\0", GL_COMBINE_RGB),
            (b"combineAlpha\0", GL_COMBINE_ALPHA),
        ] {
            let mut value = 0;
            a.dg_glGetTexEnviv.unwrap()(GL_TEXTURE_ENV, key, &mut value);
            set_i(a, p, name, value);
        }
        for (name, key) in [
            (b"rgbScale\0".as_slice(), GL_RGB_SCALE),
            (b"alphaScale\0", GL_ALPHA_SCALE),
        ] {
            let mut value = 0.;
            a.dg_glGetTexEnvfv.unwrap()(GL_TEXTURE_ENV, key, &mut value);
            set_f(a, p, name, value);
        }
        for (name, key) in [
            (b"sourceRGB\0".as_slice(), GL_SOURCE0_RGB),
            (b"sourceAlpha\0", GL_SOURCE0_ALPHA),
            (b"operandRGB\0", GL_OPERAND0_RGB),
            (b"operandAlpha\0", GL_OPERAND0_ALPHA),
        ] {
            let mut v = [0; 3];
            for (i, value) in v.iter_mut().enumerate() {
                a.dg_glGetTexEnviv.unwrap()(GL_TEXTURE_ENV, key + i as u32, value);
            }
            a.dg_glUniform1iv.unwrap()(location(a, p, name), 3, v.as_ptr());
        }
        set_i(
            a,
            p,
            b"fogEnabled\0",
            i32::from(a.dg_glIsEnabled.unwrap()(GL_FOG) != 0),
        );
        set_i(a, p, b"fogMode\0", integer(a, GL_FOG_MODE));
        let secondary = a.dg_glIsEnabled.unwrap()(GL_COLOR_SUM) != 0
            || (a.dg_glIsEnabled.unwrap()(GL_LIGHTING) != 0
                && integer(a, GL_LIGHT_MODEL_COLOR_CONTROL) == GL_SEPARATE_SPECULAR_COLOR as i32);
        set_i(a, p, b"colorSumEnabled\0", i32::from(secondary));
        for (name, key) in [
            (b"fogDensity\0".as_slice(), GL_FOG_DENSITY),
            (b"fogStart\0", GL_FOG_START),
            (b"fogEnd\0", GL_FOG_END),
        ] {
            let mut v = 0.;
            a.dg_glGetFloatv.unwrap()(key, &mut v);
            set_f(a, p, name, v);
        }
        if a.dg_glGetError.unwrap()() != 0 {
            return Err(DG_GL_ERROR_HOST);
        }
    }
    Ok(())
}
/// Both immediate and array/evaluator draws enter here, after shared texture waits.
pub(crate) unsafe fn prepare(m: &Memory, c: *mut ContextState) -> Result<(), u32> {
    if !cfg!(target_os = "macos") {
        return Ok(());
    }
    let a = unsafe { &*m.api };
    let two = unsafe { (*c).bound_texture };
    let one = unsafe { (*c).bound_texture_1d };
    if two.is_null() || one.is_null() {
        return Ok(());
    }
    // Ordinary textures avoid every extra native state query.
    if unsafe { (*two).borders.iter().all(|b| *b == 0) && (*one).borders.iter().all(|b| *b == 0) } {
        let s = unsafe { (*c).border_sampler };
        if !s.is_null() {
            unsafe {
                discard(m, &mut *s);
            }
        }
        return Ok(());
    }
    unsafe {
        query::remember(a, core::ptr::addr_of_mut!((*c).guest_errors))?;
    }
    let texture = unsafe {
        if a.dg_glIsEnabled.unwrap()(GL_TEXTURE_2D) != 0 {
            two
        } else if a.dg_glIsEnabled.unwrap()(GL_TEXTURE_1D) != 0 {
            one
        } else {
            return Ok(());
        }
    };
    let target = unsafe { (*texture).target };
    let base = unsafe { parameter(a, target, GL_TEXTURE_BASE_LEVEL) };
    if !(0..12).contains(&base) || unsafe { (*texture).borders[base as usize] } != 1 {
        return Ok(());
    }
    if unsafe { integer(a, GL_RENDER_MODE) } != GL_RENDER as i32 {
        return Ok(());
    }
    let min = unsafe { parameter(a, target, GL_TEXTURE_MIN_FILTER) };
    let max = unsafe { parameter(a, target, GL_TEXTURE_MAX_LEVEL) }.min(11);
    let w = unsafe { (*texture).widths[base as usize] };
    let h = unsafe { (*texture).heights[base as usize] };
    if w <= 2 || (target == GL_TEXTURE_2D && h <= 2) {
        return Ok(());
    }
    let format = unsafe { level_parameter(a, target, base, GL_TEXTURE_INTERNAL_FORMAT) };
    let mip = min != GL_NEAREST as i32 && min != GL_LINEAR as i32;
    let mut count = 1;
    let mut iw = w - 2;
    let mut ih = if target == GL_TEXTURE_2D { h - 2 } else { 1 };
    if max < base {
        return Ok(());
    }
    if mip {
        while (iw > 1 || ih > 1) && base + count <= max {
            iw = (iw / 2).max(1);
            ih = (ih / 2).max(1);
            let l = (base + count) as usize;
            if unsafe {
                (*texture).borders[l] != 1
                    || (*texture).widths[l] != iw + 2
                    || (*texture).heights[l] != (if target == GL_TEXTURE_2D { ih + 2 } else { 1 })
            } || unsafe { level_parameter(a, target, l as i32, GL_TEXTURE_INTERNAL_FORMAT) }
                != format
            {
                return Ok(());
            }
            count += 1;
        }
    }
    let mut raw = unsafe { (*c).border_sampler };
    if raw.is_null() {
        raw = unsafe { (m.allocate)(m.opaque, core::mem::size_of::<State>()) }.cast();
        if raw.is_null() {
            return Err(DG_GL_ERROR_HOST);
        }
        unsafe {
            raw.write(State::EMPTY);
            (*c).border_sampler = raw;
        }
    }
    let s = unsafe { &mut *raw };
    if s.program == 0 {
        s.program = unsafe { compile(a)? };
    }
    unsafe {
        s.old_program = integer(a, GL_CURRENT_PROGRAM);
        s.old_active = integer(a, GL_ACTIVE_TEXTURE);
        a.dg_glActiveTexture.unwrap()(GL_TEXTURE1);
        s.old_binding = integer(a, GL_TEXTURE_BINDING_2D);
        a.dg_glActiveTexture.unwrap()(GL_TEXTURE0);
        s.active = true;
    }
    let result = (|| {
        if s.texture != texture
            || s.version != unsafe { (*texture).version }
            || s.base != base
            || s.count != count
        {
            unsafe {
                upload_atlas(m, s, texture, target, base, count, format)?;
            }
        }
        unsafe {
            a.dg_glActiveTexture.unwrap()(GL_TEXTURE1);
            a.dg_glBindTexture.unwrap()(GL_TEXTURE_2D, s.atlas);
            a.dg_glActiveTexture.unwrap()(GL_TEXTURE0);
            a.dg_glUseProgram.unwrap()(s.program);
            uniforms(a, s, target, min)?;
        }
        Ok(())
    })();
    if result.is_err() {
        unsafe {
            restore(a, s);
        }
    }
    result
}
