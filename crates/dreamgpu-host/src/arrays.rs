// SPDX-License-Identifier: GPL-2.0-or-later
//! Fixed vertex-array execution with bounded payloads and full pointer restoration.
use crate::gl_api::*;
pub(crate) mod raw;

unsafe fn draw(
    api: &DreamGpuGlApi,
    function: u32,
    args: &[u32; 8],
    data: &[u8],
) -> Result<u32, u32> {
    let elements = function == FEnum_glDrawElements;
    // DrawArraysEXT is the compact layout with texture units 1-7 (slots 7-13).
    let units = function == FEnum_glDrawArraysEXT;
    if !elements && !units && function != FEnum_glDrawArrays {
        return Err(3);
    }
    let vertices = args[if elements { 3 } else { 2 }];
    let wire_attributes = args[if elements { 4 } else { 3 }];
    let compact = wire_attributes & DG_GL_ARRAY_RAW != 0;
    if units && !compact {
        return Err(1);
    }
    let raw: Option<[raw::Attribute; 16]> = if compact {
        if elements {
            return Err(1);
        }
        Some(if units {
            raw::layout_extended(wire_attributes, vertices, data).ok_or(1u32)?
        } else {
            let seven = raw::layout(wire_attributes, vertices, data).ok_or(1u32)?;
            let mut all = [raw::Attribute::default(); 16];
            all[..7].copy_from_slice(&seven);
            all
        })
    } else {
        None
    };
    let allowed = if units {
        DG_GL_ARRAY_EXT_MASK
    } else {
        DG_GL_ARRAY_MASK
    };
    let attributes = wire_attributes & !DG_GL_ARRAY_RAW;
    let extended = attributes & (DG_GL_ARRAY_INDEX | DG_GL_ARRAY_EDGE) != 0;
    let stride = if extended {
        DG_GL_VERTEX_EXTENDED_BYTES as u64
    } else if attributes & DG_GL_ARRAY_SECONDARY != 0 {
        DG_GL_VERTEX_SECONDARY_BYTES as u64
    } else {
        DG_GL_VERTEX_BYTES as u64
    };
    let index_size = if elements {
        match args[2] {
            GL_UNSIGNED_BYTE => 1,
            GL_UNSIGNED_SHORT => 2,
            GL_UNSIGNED_INT => 4,
            _ => return Err(1),
        }
    } else {
        0
    };
    let indices = if elements { args[1] } else { 0 };
    if args[0] > GL_POLYGON
        || vertices > DG_GL_MAX_VERTICES
        || indices > DG_GL_MAX_INDICES
        || attributes & !allowed != 0
        || attributes & DG_GL_ARRAY_POSITION == 0
        || (!elements && args[1] != 0)
        || (!compact
            && u64::from(vertices) * stride + u64::from(indices) * index_size != data.len() as u64)
    {
        return Err(1);
    }
    if vertices == 0 || (elements && indices == 0) {
        return Ok(0);
    }
    // Check every needed entry before exposing a client pointer to GL, so a
    // missing function cannot escape before PopClientAttrib restores ownership.
    if api.dg_glGetFloatv.is_none()
        || api.dg_glPushClientAttrib.is_none()
        || api.dg_glPopClientAttrib.is_none()
        || api.dg_glEnableClientState.is_none()
        || api.dg_glDisableClientState.is_none()
        || api.dg_glVertexPointer.is_none()
        || api.dg_glColorPointer.is_none()
        || api.dg_glNormalPointer.is_none()
        || api.dg_glTexCoordPointer.is_none()
        || api.dg_glSecondaryColorPointer.is_none()
        || api.dg_glDrawArrays.is_none()
        || api.dg_glDrawElements.is_none()
        || api.dg_glGetError.is_none()
        || api.dg_glColor4fv.is_none()
        || api.dg_glNormal3fv.is_none()
        || api.dg_glTexCoord4fv.is_none()
        || api.dg_glSecondaryColor3fv.is_none()
    {
        return Err(3);
    }
    // Texture coordinates of unit u: unit 0 is slot 3, units 1-7 are slots 7-13.
    let texcoord_units: u32 = (0..DG_GL_MAX_TEXTURE_UNITS)
        .filter(|&u| {
            attributes
                & if u == 0 {
                    DG_GL_ARRAY_TEXCOORD
                } else {
                    DG_GL_ARRAY_TEXCOORD1 << (u - 1)
                }
                != 0
        })
        .fold(0, |mask, u| mask | 1 << u);
    // Current coordinates and client arrays are per texture unit. Any
    // coordinate array needs explicit unit selection so an active unit other
    // than zero cannot receive unit 0's coordinates or lose its own state.
    if texcoord_units != 0
        && (api.dg_glActiveTexture.is_none()
            || api.dg_glClientActiveTexture.is_none()
            || api.dg_glMultiTexCoord4fv.is_none()
            || api.dg_glGetIntegerv.is_none())
    {
        return Err(3);
    }
    if extended
        && (api.dg_glGetDoublev.is_none()
            || api.dg_glGetBooleanv.is_none()
            || api.dg_glIndexd.is_none()
            || api.dg_glEdgeFlag.is_none()
            || api.dg_glIndexPointer.is_none()
            || api.dg_glEdgeFlagPointer.is_none())
    {
        return Err(3);
    }
    // Legacy layout and compact descriptors share native pointer/state lifetime.
    let attribute = |i: usize, size: i32, kind: u32, offset: usize| {
        if let Some(raw) = &raw {
            (raw[i].size as i32, raw[i].kind, 0, raw[i].offset)
        } else {
            (size, kind, stride as i32, offset)
        }
    };
    let mut index = 0.0;
    let mut edge = 0;
    let mut color = [0.0f32; 4];
    let mut normal = [0.0f32; 3];
    let mut texcoords = [[0.0f32; 4]; DG_GL_MAX_TEXTURE_UNITS as usize];
    let mut active = GL_TEXTURE0 as i32;
    let mut secondary = [0.0f32; 4];
    unsafe {
        // GL modifies current values only for enabled arrays. Preserve those
        // values; disabled packet attributes require no native query or restore.
        // Khronos glDrawArrays reference, "Attributes that aren't modified
        // remain well defined."
        if attributes & DG_GL_ARRAY_COLOR != 0 {
            api.dg_glGetFloatv.unwrap()(GL_CURRENT_COLOR, color.as_mut_ptr());
        }
        if attributes & DG_GL_ARRAY_SECONDARY != 0 {
            api.dg_glGetFloatv.unwrap()(GL_CURRENT_SECONDARY_COLOR, secondary.as_mut_ptr());
        }
        if attributes & DG_GL_ARRAY_NORMAL != 0 {
            api.dg_glGetFloatv.unwrap()(GL_CURRENT_NORMAL, normal.as_mut_ptr());
        }
        if texcoord_units != 0 {
            api.dg_glGetIntegerv.unwrap()(GL_ACTIVE_TEXTURE, &mut active);
            for (u, saved) in texcoords.iter_mut().enumerate() {
                if texcoord_units & 1 << u != 0 {
                    api.dg_glActiveTexture.unwrap()(GL_TEXTURE0 + u as u32);
                    api.dg_glGetFloatv.unwrap()(GL_CURRENT_TEXTURE_COORDS, saved.as_mut_ptr());
                }
            }
            api.dg_glActiveTexture.unwrap()(active as u32);
        }
        if attributes & DG_GL_ARRAY_INDEX != 0 {
            api.dg_glGetDoublev.unwrap()(GL_CURRENT_INDEX, &mut index);
        }
        if attributes & DG_GL_ARRAY_EDGE != 0 {
            api.dg_glGetBooleanv.unwrap()(GL_EDGE_FLAG, &mut edge);
        }
        api.dg_glPushClientAttrib.unwrap()(GL_CLIENT_VERTEX_ARRAY_BIT);
        if units {
            api.dg_glClientActiveTexture.unwrap()(GL_TEXTURE0);
        }
        api.dg_glEnableClientState.unwrap()(GL_VERTEX_ARRAY);
        let (size, kind, step, offset) = attribute(0, 4, GL_FLOAT, 0);
        api.dg_glVertexPointer.unwrap()(size, kind, step, data.as_ptr().add(offset).cast());
        for (bit, cap) in [
            (DG_GL_ARRAY_COLOR, GL_COLOR_ARRAY),
            (DG_GL_ARRAY_SECONDARY, GL_SECONDARY_COLOR_ARRAY),
            (DG_GL_ARRAY_NORMAL, GL_NORMAL_ARRAY),
            (DG_GL_ARRAY_TEXCOORD, GL_TEXTURE_COORD_ARRAY),
        ] {
            if attributes & bit != 0 {
                api.dg_glEnableClientState.unwrap()(cap);
            } else {
                api.dg_glDisableClientState.unwrap()(cap);
            }
        }
        if attributes & DG_GL_ARRAY_COLOR != 0 {
            let (size, kind, step, offset) = attribute(1, 4, GL_FLOAT, DG_GL_VERTEX_COLOR as usize);
            api.dg_glColorPointer.unwrap()(size, kind, step, data.as_ptr().add(offset).cast());
        }
        if attributes & DG_GL_ARRAY_NORMAL != 0 {
            let (_, kind, step, offset) = attribute(2, 3, GL_FLOAT, DG_GL_VERTEX_NORMAL as usize);
            api.dg_glNormalPointer.unwrap()(kind, step, data.as_ptr().add(offset).cast());
        }
        if attributes & DG_GL_ARRAY_TEXCOORD != 0 {
            let (size, kind, step, offset) =
                attribute(3, 4, GL_FLOAT, DG_GL_VERTEX_TEXCOORD as usize);
            api.dg_glTexCoordPointer.unwrap()(size, kind, step, data.as_ptr().add(offset).cast());
        }
        // PushClientAttrib saved CLIENT_ACTIVE_TEXTURE and every unit's array;
        // PopClientAttrib below restores both.
        if units {
            for u in 1..DG_GL_MAX_TEXTURE_UNITS as usize {
                api.dg_glClientActiveTexture.unwrap()(GL_TEXTURE0 + u as u32);
                if texcoord_units & 1 << u == 0 {
                    api.dg_glDisableClientState.unwrap()(GL_TEXTURE_COORD_ARRAY);
                    continue;
                }
                let a = raw.as_ref().expect("units are compact")[6 + u];
                api.dg_glEnableClientState.unwrap()(GL_TEXTURE_COORD_ARRAY);
                api.dg_glTexCoordPointer.unwrap()(
                    a.size as i32,
                    a.kind,
                    0,
                    data.as_ptr().add(a.offset).cast(),
                );
            }
        }
        if attributes & DG_GL_ARRAY_SECONDARY != 0 {
            let (size, kind, step, offset) =
                attribute(4, 3, GL_FLOAT, DG_GL_VERTEX_SECONDARY as usize);
            api.dg_glSecondaryColorPointer.unwrap()(
                size,
                kind,
                step,
                data.as_ptr().add(offset).cast(),
            );
        }
        if extended {
            for (bit, cap) in [
                (DG_GL_ARRAY_INDEX, GL_INDEX_ARRAY),
                (DG_GL_ARRAY_EDGE, GL_EDGE_FLAG_ARRAY),
            ] {
                if attributes & bit != 0 {
                    api.dg_glEnableClientState.unwrap()(cap);
                } else {
                    api.dg_glDisableClientState.unwrap()(cap);
                }
            }
            if attributes & DG_GL_ARRAY_INDEX != 0 {
                let (_, kind, step, offset) =
                    attribute(5, 1, GL_DOUBLE, DG_GL_VERTEX_INDEX as usize);
                api.dg_glIndexPointer.unwrap()(kind, step, data.as_ptr().add(offset).cast());
            }
            if attributes & DG_GL_ARRAY_EDGE != 0 {
                let (_, _, step, offset) =
                    attribute(6, 1, GL_UNSIGNED_BYTE, DG_GL_VERTEX_EDGE as usize);
                api.dg_glEdgeFlagPointer.unwrap()(step, data.as_ptr().add(offset).cast());
            }
        }
        if elements {
            api.dg_glDrawElements.unwrap()(
                args[0],
                indices as i32,
                args[2],
                data.as_ptr()
                    .add(vertices as usize * stride as usize)
                    .cast(),
            );
        } else {
            api.dg_glDrawArrays.unwrap()(args[0], 0, vertices as i32);
        }
        let error = api.dg_glGetError.unwrap()();
        api.dg_glPopClientAttrib.unwrap()();
        if attributes & DG_GL_ARRAY_COLOR != 0 {
            api.dg_glColor4fv.unwrap()(color.as_ptr());
        }
        if attributes & DG_GL_ARRAY_SECONDARY != 0 {
            api.dg_glSecondaryColor3fv.unwrap()(secondary.as_ptr());
        }
        if attributes & DG_GL_ARRAY_NORMAL != 0 {
            api.dg_glNormal3fv.unwrap()(normal.as_ptr());
        }
        for (u, saved) in texcoords.iter().enumerate() {
            if texcoord_units & 1 << u != 0 {
                api.dg_glMultiTexCoord4fv.unwrap()(GL_TEXTURE0 + u as u32, saved.as_ptr());
            }
        }
        if attributes & DG_GL_ARRAY_INDEX != 0 {
            api.dg_glIndexd.unwrap()(index);
        }
        if attributes & DG_GL_ARRAY_EDGE != 0 {
            api.dg_glEdgeFlag.unwrap()(edge);
        }
        Ok(error)
    }
}

/// # Safety
/// API belongs to the current native context. Args contain 8 host-endian words;
/// payload has passed full index/padding validation and is native-endian immutable
/// vertex data (QEMU adapts only on big-endian hosts). GL must consume client arrays
/// synchronously as specified. Client state is restored before this call returns.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_arrays(
    api: *const DreamGpuGlApi,
    function: u32,
    args: *const u32,
    data: *const u8,
    bytes: u32,
    gl_error: *mut u32,
) -> u32 {
    if api.is_null() || args.is_null() || data.is_null() || gl_error.is_null() {
        return 1;
    }
    unsafe {
        *gl_error = 0;
    }
    match unsafe {
        draw(
            &*api,
            function,
            &*args.cast::<[u32; 8]>(),
            core::slice::from_raw_parts(data, bytes as usize),
        )
    } {
        Ok(error) => {
            unsafe {
                *gl_error = error;
            }
            if error == 0 {
                0
            } else {
                6
            }
        }
        Err(error) => error,
    }
}

#[cfg(test)]
mod tests;
