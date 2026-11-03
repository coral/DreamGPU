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
    // The attribute draw (ARB_vertex_program) adds generic attributes 0-15
    // (slots 16-31) to the DrawArraysEXT slots, with their mask in args[4].
    let attribs = function == FEnum_glVertexAttribPointerARB;
    // DrawArraysEXT is the compact layout with texture units 1-7 (slots 7-13).
    let units = function == FEnum_glDrawArraysEXT || attribs;
    if !elements && !units && function != FEnum_glDrawArrays {
        return Err(3);
    }
    let vertices = args[if elements { 3 } else { 2 }];
    let wire_attributes = args[if elements { 4 } else { 3 }];
    let generic = if attribs { args[4] } else { 0 };
    let compact = wire_attributes & DG_GL_ARRAY_RAW != 0;
    if units && !compact {
        return Err(1);
    }
    let raw: Option<[raw::Attribute; 32]> = if compact {
        if elements {
            return Err(1);
        }
        let mut all = [raw::Attribute::default(); 32];
        if attribs {
            all = raw::layout_attribs(wire_attributes, generic, vertices, data).ok_or(1u32)?;
        } else if units {
            let sixteen = raw::layout_extended(wire_attributes, vertices, data).ok_or(1u32)?;
            all[..16].copy_from_slice(&sixteen);
        } else {
            let seven = raw::layout(wire_attributes, vertices, data).ok_or(1u32)?;
            all[..7].copy_from_slice(&seven);
        }
        Some(all)
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
    // Generic attribute 0 is the position: it provokes drawing without one.
    let position = attributes & DG_GL_ARRAY_POSITION != 0;
    if args[0] > GL_POLYGON
        || vertices > DG_GL_MAX_VERTICES
        || indices > DG_GL_MAX_INDICES
        || attributes & !allowed != 0
        || (!position && generic & 1 == 0)
        || (!elements && args[1] != 0)
        || (!compact
            && u64::from(vertices) * stride + u64::from(indices) * index_size != data.len() as u64)
    {
        return Err(1);
    }
    if vertices == 0 || (elements && indices == 0) {
        return Ok(0);
    }
    unsafe {
        execute(
            api,
            &Spec {
                mode: args[0],
                attributes,
                generic,
                units,
                raw,
                stride,
                primitive: if elements {
                    Primitive::Elements {
                        count: indices as i32,
                        kind: args[2],
                        offset: vertices as usize * stride as usize,
                    }
                } else {
                    Primitive::Arrays {
                        first: 0,
                        count: vertices as i32,
                    }
                },
                buffers: None,
            },
            data,
        )
    }
}

/// The buffer draw (ARB_vertex_buffer_object): `a` and `layout` are an
/// admitted record with a nonzero count, `resolved` its bounded native
/// buffers. Returns the native GL error like the other array draws.
///
/// # Safety
/// API belongs to the current native context, which binds no buffer; payload
/// is native-endian.
pub(crate) unsafe fn buffer_draw(
    api: &DreamGpuGlApi,
    a: &[u32; 10],
    data: &[u8],
    layout: &raw::BufferLayout,
    resolved: &crate::buffer::Resolved,
) -> Result<u32, u32> {
    let [mode, base, count, mask, generic, _, _, kind, element, element_offset] = *a;
    unsafe {
        execute(
            api,
            &Spec {
                mode,
                attributes: mask & !DG_GL_ARRAY_RAW,
                generic,
                units: true,
                raw: Some(layout.slots),
                stride: 0,
                primitive: if layout.index_size == 0 {
                    Primitive::Arrays {
                        first: base as i32,
                        count: count as i32,
                    }
                } else {
                    Primitive::Elements {
                        count: count as i32,
                        kind,
                        offset: if element != 0 {
                            element_offset as usize
                        } else {
                            layout.indices
                        },
                    }
                },
                buffers: Some(Buffers {
                    references: &layout.references,
                    natives: &resolved.slots,
                    element: resolved.element,
                    base,
                }),
            },
            data,
        )
    }
}

/// Native buffers of a buffer draw, its references and its client base.
struct Buffers<'a> {
    references: &'a [raw::Reference; 32],
    natives: &'a [u32; 32],
    element: u32,
    base: u32,
}
enum Primitive {
    Arrays {
        first: i32,
        count: i32,
    },
    /// Indices at `offset`: into the payload, or into the element buffer.
    Elements {
        count: i32,
        kind: u32,
        offset: usize,
    },
}
/// One validated array draw: enabled conventional slots (without ARRAY_RAW),
/// generic attributes, whether units 1-7 and the fog coordinate belong to the
/// record (DrawArraysEXT and later), the compact layout or the legacy stride,
/// and buffer sources of a buffer draw.
struct Spec<'a> {
    mode: u32,
    attributes: u32,
    generic: u32,
    units: bool,
    raw: Option<[raw::Attribute; 32]>,
    stride: u64,
    primitive: Primitive,
    buffers: Option<Buffers<'a>>,
}

unsafe fn execute(api: &DreamGpuGlApi, spec: &Spec, data: &[u8]) -> Result<u32, u32> {
    let Spec {
        mode,
        attributes,
        generic,
        units,
        stride,
        ..
    } = *spec;
    let raw = spec.raw;
    let extended = attributes & (DG_GL_ARRAY_INDEX | DG_GL_ARRAY_EDGE) != 0;
    let position = attributes & DG_GL_ARRAY_POSITION != 0;
    if spec.buffers.is_some() && api.dg_glBindBuffer.is_none() {
        return Err(3);
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
    let fog = attributes & DG_GL_ARRAY_FOG_COORD != 0;
    if fog && (api.dg_glFogCoordPointer.is_none() || api.dg_glFogCoordf.is_none()) {
        return Err(3);
    }
    if generic != 0
        && (api.dg_glVertexAttribPointerARB.is_none()
            || api.dg_glEnableVertexAttribArrayARB.is_none()
            || api.dg_glDisableVertexAttribArrayARB.is_none()
            || api.dg_glGetVertexAttribfvARB.is_none()
            || api.dg_glVertexAttrib4fvARB.is_none())
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
    // A buffer draw's slot reads its native buffer at the reference offset,
    // or its client section, which holds vertices from the record's base.
    let source = |i: usize, size: i32, kind: u32, offset: usize| {
        let Some(raw) = &raw else {
            return (
                size,
                kind,
                stride as i32,
                unsafe { data.as_ptr().add(offset) }.cast::<core::ffi::c_void>(),
            );
        };
        let a = raw[i];
        let (step, pointer) = match &spec.buffers {
            Some(b) if b.natives[i] != 0 => {
                let r = b.references[i];
                unsafe { api.dg_glBindBuffer.unwrap()(GL_ARRAY_BUFFER, b.natives[i]) };
                (r.stride as i32, r.offset as usize as *const u8)
            }
            Some(b) => {
                unsafe { api.dg_glBindBuffer.unwrap()(GL_ARRAY_BUFFER, 0) };
                let first = b.base as usize * a.size as usize * a.unit;
                (
                    0,
                    unsafe { data.as_ptr().add(a.offset) }.wrapping_sub(first),
                )
            }
            None => (0, unsafe { data.as_ptr().add(a.offset) }),
        };
        (
            a.size as i32,
            a.kind,
            step,
            pointer.cast::<core::ffi::c_void>(),
        )
    };
    let mut index = 0.0;
    let mut edge = 0;
    let mut color = [0.0f32; 4];
    let mut normal = [0.0f32; 3];
    let mut texcoords = [[0.0f32; 4]; DG_GL_MAX_TEXTURE_UNITS as usize];
    let mut active = GL_TEXTURE0 as i32;
    let mut secondary = [0.0f32; 4];
    let mut fog_coordinate = 0.0f32;
    let mut currents = [[0.0f32; 4]; DG_GL_MAX_VERTEX_ATTRIBS as usize];
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
        if fog {
            api.dg_glGetFloatv.unwrap()(GL_CURRENT_FOG_COORDINATE, &mut fog_coordinate);
        }
        if attributes & DG_GL_ARRAY_INDEX != 0 {
            api.dg_glGetDoublev.unwrap()(GL_CURRENT_INDEX, &mut index);
        }
        if attributes & DG_GL_ARRAY_EDGE != 0 {
            api.dg_glGetBooleanv.unwrap()(GL_EDGE_FLAG, &mut edge);
        }
        // Generic attribute zero has no current value (ARB_vertex_program).
        for (n, saved) in currents.iter_mut().enumerate().skip(1) {
            if generic & 1 << n != 0 {
                api.dg_glGetVertexAttribfvARB.unwrap()(
                    n as u32,
                    GL_CURRENT_VERTEX_ATTRIB_ARB,
                    saved.as_mut_ptr(),
                );
            }
        }
        api.dg_glPushClientAttrib.unwrap()(GL_CLIENT_VERTEX_ARRAY_BIT);
        if units {
            api.dg_glClientActiveTexture.unwrap()(GL_TEXTURE0);
        }
        if position {
            api.dg_glEnableClientState.unwrap()(GL_VERTEX_ARRAY);
            let (size, kind, step, pointer) = source(0, 4, GL_FLOAT, 0);
            api.dg_glVertexPointer.unwrap()(size, kind, step, pointer);
        } else {
            api.dg_glDisableClientState.unwrap()(GL_VERTEX_ARRAY);
        }
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
            let (size, kind, step, pointer) = source(1, 4, GL_FLOAT, DG_GL_VERTEX_COLOR as usize);
            api.dg_glColorPointer.unwrap()(size, kind, step, pointer);
        }
        if attributes & DG_GL_ARRAY_NORMAL != 0 {
            let (_, kind, step, pointer) = source(2, 3, GL_FLOAT, DG_GL_VERTEX_NORMAL as usize);
            api.dg_glNormalPointer.unwrap()(kind, step, pointer);
        }
        if attributes & DG_GL_ARRAY_TEXCOORD != 0 {
            let (size, kind, step, pointer) =
                source(3, 4, GL_FLOAT, DG_GL_VERTEX_TEXCOORD as usize);
            api.dg_glTexCoordPointer.unwrap()(size, kind, step, pointer);
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
                let (size, kind, step, pointer) = source(6 + u, 0, 0, 0);
                api.dg_glEnableClientState.unwrap()(GL_TEXTURE_COORD_ARRAY);
                api.dg_glTexCoordPointer.unwrap()(size, kind, step, pointer);
            }
        }
        // CLIENT_VERTEX_ARRAY_BIT covers the fog coordinate array (GL 1.4);
        // only DrawArraysEXT carries it, so other draws leave it untouched.
        if units {
            if fog {
                let (_, kind, step, pointer) = source(14, 0, 0, 0);
                api.dg_glEnableClientState.unwrap()(GL_FOG_COORDINATE_ARRAY);
                api.dg_glFogCoordPointer.unwrap()(kind, step, pointer);
            } else {
                api.dg_glDisableClientState.unwrap()(GL_FOG_COORDINATE_ARRAY);
            }
        }
        if attributes & DG_GL_ARRAY_SECONDARY != 0 {
            let (size, kind, step, pointer) =
                source(4, 3, GL_FLOAT, DG_GL_VERTEX_SECONDARY as usize);
            api.dg_glSecondaryColorPointer.unwrap()(size, kind, step, pointer);
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
                let (_, kind, step, pointer) = source(5, 1, GL_DOUBLE, DG_GL_VERTEX_INDEX as usize);
                api.dg_glIndexPointer.unwrap()(kind, step, pointer);
            }
            if attributes & DG_GL_ARRAY_EDGE != 0 {
                let (_, _, step, pointer) =
                    source(6, 1, GL_UNSIGNED_BYTE, DG_GL_VERTEX_EDGE as usize);
                api.dg_glEdgeFlagPointer.unwrap()(step, pointer);
            }
        }
        // Generic arrays are enabled only for this draw: native GL never holds
        // one between records, so none needs disabling here.
        for n in 0..DG_GL_MAX_VERTEX_ATTRIBS as usize {
            if generic & 1 << n == 0 {
                continue;
            }
            let normalized = raw.as_ref().expect("attributes are compact")[16 + n].normalized;
            let (size, kind, step, pointer) = source(16 + n, 0, 0, 0);
            api.dg_glVertexAttribPointerARB.unwrap()(
                n as u32,
                size,
                kind,
                u8::from(normalized),
                step,
                pointer,
            );
            api.dg_glEnableVertexAttribArrayARB.unwrap()(n as u32);
        }
        match spec.primitive {
            Primitive::Elements {
                count,
                kind,
                offset,
            } => {
                let indices = match &spec.buffers {
                    Some(b) => {
                        api.dg_glBindBuffer.unwrap()(GL_ELEMENT_ARRAY_BUFFER, b.element);
                        if b.element != 0 {
                            offset as *const u8
                        } else {
                            data.as_ptr().add(offset)
                        }
                    }
                    None => data.as_ptr().add(offset),
                };
                api.dg_glDrawElements.unwrap()(mode, count, kind, indices.cast());
            }
            Primitive::Arrays { first, count } => {
                api.dg_glDrawArrays.unwrap()(mode, first, count);
            }
        }
        let error = api.dg_glGetError.unwrap()();
        // CLIENT_VERTEX_ARRAY_BIT covers generic arrays too; disable them
        // explicitly so no native array outlives the draw on any provider.
        for n in 0..DG_GL_MAX_VERTEX_ATTRIBS {
            if generic & 1 << n != 0 {
                api.dg_glDisableVertexAttribArrayARB.unwrap()(n);
            }
        }
        api.dg_glPopClientAttrib.unwrap()();
        // Native contexts hold no buffer binding between records.
        if spec.buffers.is_some() {
            api.dg_glBindBuffer.unwrap()(GL_ARRAY_BUFFER, 0);
            api.dg_glBindBuffer.unwrap()(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
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
        if fog {
            api.dg_glFogCoordf.unwrap()(fog_coordinate);
        }
        if attributes & DG_GL_ARRAY_INDEX != 0 {
            api.dg_glIndexd.unwrap()(index);
        }
        if attributes & DG_GL_ARRAY_EDGE != 0 {
            api.dg_glEdgeFlag.unwrap()(edge);
        }
        for (n, saved) in currents.iter().enumerate().skip(1) {
            if generic & 1 << n != 0 {
                api.dg_glVertexAttrib4fvARB.unwrap()(n as u32, saved.as_ptr());
            }
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
