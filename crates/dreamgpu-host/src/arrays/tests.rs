use super::*;
use std::cell::RefCell;
thread_local! { static SEEN: RefCell<Vec<(u32, usize)>> = const { RefCell::new(Vec::new()) }; static ERROR: RefCell<u32> = const { RefCell::new(0) }; static ACTIVE: RefCell<u32> = const { RefCell::new(GL_TEXTURE0) }; }
fn note(op: u32, value: usize) {
    SEEN.with(|v| v.borrow_mut().push((op, value)));
}
unsafe extern "C" fn get(pname: u32, out: *mut f32) {
    note(14, pname as usize);
    let count = if pname == GL_CURRENT_NORMAL { 3 } else { 4 };
    for i in 0..count {
        unsafe {
            *out.add(i) = f32::from_bits(pname + i as u32);
        }
    }
}
unsafe extern "C" fn push(mask: u32) {
    note(1, mask as usize);
}
unsafe extern "C" fn pop() {
    note(2, 0);
}
unsafe extern "C" fn enable(cap: u32) {
    note(3, cap as usize);
}
unsafe extern "C" fn disable(cap: u32) {
    note(4, cap as usize);
}
unsafe extern "C" fn pointer(size: i32, kind: u32, stride: i32, data: *const core::ffi::c_void) {
    note(15, kind as usize);
    note(16, size as usize);
    note(5, stride as usize);
    note(6, data as usize);
}
unsafe extern "C" fn normal(kind: u32, stride: i32, data: *const core::ffi::c_void) {
    note(15, kind as usize);
    note(5, stride as usize);
    note(6, data as usize);
}
unsafe extern "C" fn draw_arrays(mode: u32, first: i32, count: i32) {
    note(7, mode as usize);
    note(8, first as usize);
    note(9, count as usize);
}
unsafe extern "C" fn draw_elements(mode: u32, count: i32, _: u32, data: *const core::ffi::c_void) {
    note(7, mode as usize);
    note(9, count as usize);
    note(10, data as usize);
}
unsafe extern "C" fn error() -> u32 {
    ERROR.with(|v| *v.borrow())
}
unsafe extern "C" fn restore(p: *const f32) {
    note(11, unsafe { *p }.to_bits() as usize);
}
unsafe extern "C" fn active(texture: u32) {
    note(17, texture as usize);
}
unsafe extern "C" fn client_active(texture: u32) {
    note(18, texture as usize);
}
unsafe extern "C" fn multi_restore(target: u32, p: *const f32) {
    note(19, target as usize);
    note(11, unsafe { *p }.to_bits() as usize);
}
unsafe extern "C" fn integer(pname: u32, out: *mut i32) {
    note(20, pname as usize);
    unsafe { *out = ACTIVE.with(|v| *v.borrow()) as i32 };
}
fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGetFloatv = Some(get);
    a.dg_glPushClientAttrib = Some(push);
    a.dg_glPopClientAttrib = Some(pop);
    a.dg_glEnableClientState = Some(enable);
    a.dg_glDisableClientState = Some(disable);
    a.dg_glVertexPointer = Some(pointer);
    a.dg_glColorPointer = Some(pointer);
    a.dg_glNormalPointer = Some(normal);
    a.dg_glTexCoordPointer = Some(pointer);
    a.dg_glSecondaryColorPointer = Some(pointer);
    a.dg_glDrawArrays = Some(draw_arrays);
    a.dg_glDrawElements = Some(draw_elements);
    a.dg_glGetError = Some(error);
    a.dg_glColor4fv = Some(restore);
    a.dg_glSecondaryColor3fv = Some(restore);
    a.dg_glNormal3fv = Some(restore);
    a.dg_glTexCoord4fv = Some(restore);
    a.dg_glActiveTexture = Some(active);
    a.dg_glClientActiveTexture = Some(client_active);
    a.dg_glMultiTexCoord4fv = Some(multi_restore);
    a.dg_glGetIntegerv = Some(integer);
    a
}
#[test]
fn multitexture_arrays_select_each_unit_and_restore_its_own_coordinates() {
    let api = api();
    let data = raw::tests::extended_packet();
    let mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_TEXCOORD | 0x200;
    // Unit 1 is active: unit 0's array must not save or restore unit 1's coordinates.
    ACTIVE.with(|v| *v.borrow_mut() = GL_TEXTURE0 + 1);
    SEEN.with(|v| v.borrow_mut().clear());
    ERROR.with(|v| *v.borrow_mut() = 0);
    let args = [GL_TRIANGLES, 0, 3, mask, 0, 0, 0, 0];
    assert_eq!(
        unsafe { draw(&api, FEnum_glDrawArraysEXT, &args, &data) },
        Ok(0)
    );
    let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
    ACTIVE.with(|v| *v.borrow_mut() = GL_TEXTURE0);
    let units = |op: u32| {
        seen.iter()
            .filter(|v| v.0 == op)
            .map(|v| v.1 as u32 - GL_TEXTURE0)
            .collect::<Vec<_>>()
    };
    // Saves switch to units 0 and 3, then return to the guest's unit 1.
    assert_eq!(units(17), [0, 3, 1]);
    assert_eq!(units(18), [0, 1, 2, 3, 4, 5, 6, 7]);
    assert_eq!(units(19), [0, 3]);
    let base = data.as_ptr() as usize;
    let pointers = seen
        .iter()
        .filter(|v| v.0 == 6)
        .map(|v| v.1 - base)
        .collect::<Vec<_>>();
    assert_eq!(pointers, [64, 88, 112]);
    assert_eq!(
        seen.iter()
            .filter(|v| v.0 == 3)
            .map(|v| v.1 as u32)
            .collect::<Vec<_>>(),
        [
            GL_VERTEX_ARRAY,
            GL_TEXTURE_COORD_ARRAY,
            GL_TEXTURE_COORD_ARRAY
        ]
    );
    // Disabled units 1, 2 and 4-7 are explicitly disabled under the push.
    assert_eq!(
        seen.iter()
            .filter(|v| v.0 == 4 && v.1 as u32 == GL_TEXTURE_COORD_ARRAY)
            .count(),
        6
    );
    // Restoration follows PopClientAttrib.
    assert!(
        seen.iter().position(|v| v.0 == 2).unwrap() < seen.iter().position(|v| v.0 == 19).unwrap()
    );
    // The seven-slot function cannot carry units 1-7.
    assert_eq!(
        unsafe { draw(&api, FEnum_glDrawArrays, &args, &data) },
        Err(1)
    );
    // A missing per-unit entry point fails before any client pointer is exposed.
    let mut partial = api;
    partial.dg_glClientActiveTexture = None;
    SEEN.with(|v| v.borrow_mut().clear());
    assert_eq!(
        unsafe { draw(&partial, FEnum_glDrawArraysEXT, &args, &data) },
        Err(3)
    );
    assert!(SEEN.with(|v| v.borrow().is_empty()));
}
#[test]
fn vertex_layout_indices_and_restore_on_gpu_error() {
    let api = api();
    for (attributes, stride) in [(15, 64), (31, 80)] {
        let mut data = vec![0; stride * 3 + 6];
        data[stride * 3..].copy_from_slice(&[0, 0, 1, 0, 2, 0]);
        let args = [GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 3, attributes, 0, 0, 0];
        ERROR.with(|v| *v.borrow_mut() = GL_INVALID_OPERATION);
        assert_eq!(
            unsafe { draw(&api, FEnum_glDrawElements, &args, &data) },
            Ok(GL_INVALID_OPERATION)
        );
        let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
        let base = data.as_ptr() as usize;
        let mut offsets = vec![base, base + 16, base + 32, base + 44];
        if stride == 80 {
            offsets.push(base + 64);
        }
        assert_eq!(
            seen.iter()
                .filter(|v| v.0 == 6)
                .map(|v| v.1)
                .collect::<Vec<_>>(),
            offsets
        );
        assert!(seen.contains(&(10, base + stride * 3)));
        assert_eq!(
            seen.iter()
                .filter(|v| v.0 == 5)
                .map(|v| v.1)
                .collect::<Vec<_>>(),
            vec![stride; offsets.len()]
        );
        let mut expected = vec![(11, GL_CURRENT_COLOR as usize)];
        if attributes & DG_GL_ARRAY_SECONDARY != 0 {
            expected.push((11, GL_CURRENT_SECONDARY_COLOR as usize));
        }
        expected.extend([
            (11, GL_CURRENT_NORMAL as usize),
            (11, GL_CURRENT_TEXTURE_COORDS as usize),
        ]);
        assert_eq!(
            seen.iter()
                .copied()
                .filter(|v| v.0 == 11)
                .collect::<Vec<_>>(),
            expected
        );
        assert!(
            seen.iter().position(|v| v.0 == 2).unwrap()
                < seen.iter().position(|v| v.0 == 11).unwrap()
        );
        let truncated = &data[..data.len() - 1];
        assert_eq!(
            unsafe { draw(&api, FEnum_glDrawElements, &args, truncated) },
            Err(1)
        );
        assert!(SEEN.with(|v| v.borrow().is_empty()));
    }
    ERROR.with(|v| *v.borrow_mut() = 0);
    let data = [0u8; 192];
    let args = [GL_TRIANGLES, 0, 3, 1, 0, 0, 0, 0];
    assert_eq!(
        unsafe { draw(&api, FEnum_glDrawArrays, &args, &data) },
        Ok(0)
    );
    assert!(SEEN.with(|v| v.borrow().contains(&(9, 3))));
}
unsafe extern "C" fn index_get(_: u32, p: *mut f64) {
    unsafe {
        *p = 16777217.25;
    }
}
unsafe extern "C" fn edge_get(_: u32, p: *mut u8) {
    unsafe {
        *p = 1;
    }
}
unsafe extern "C" fn index_restore(v: f64) {
    note(12, v.to_bits() as usize);
}
unsafe extern "C" fn edge_restore(v: u8) {
    note(13, v as usize);
}
unsafe extern "C" fn edge_pointer(stride: i32, p: *const core::ffi::c_void) {
    note(5, stride as usize);
    note(6, p as usize);
}
#[test]
fn extended_arrays_restore_exact_current_index_edge_and_pointer_state_on_error() {
    let mut api = api();
    api.dg_glGetDoublev = Some(index_get);
    api.dg_glGetBooleanv = Some(edge_get);
    api.dg_glIndexd = Some(index_restore);
    api.dg_glEdgeFlag = Some(edge_restore);
    api.dg_glIndexPointer = Some(normal);
    api.dg_glEdgeFlagPointer = Some(edge_pointer);
    let data = [0u8; 288];
    let args = [
        GL_TRIANGLES,
        0,
        3,
        DG_GL_ARRAY_POSITION | DG_GL_ARRAY_INDEX | DG_GL_ARRAY_EDGE,
        0,
        0,
        0,
        0,
    ];
    ERROR.with(|v| *v.borrow_mut() = GL_INVALID_OPERATION);
    assert_eq!(
        unsafe { draw(&api, FEnum_glDrawArrays, &args, &data) },
        Ok(GL_INVALID_OPERATION)
    );
    let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
    assert!(seen.contains(&(6, data.as_ptr() as usize + 80)));
    assert!(seen.contains(&(6, data.as_ptr() as usize + 88)));
    assert_eq!(
        &seen[seen.len() - 2..],
        &[(12, 16777217.25f64.to_bits() as usize), (13, 1)]
    );
    api.dg_glIndexPointer = None;
    assert_eq!(
        unsafe { draw(&api, FEnum_glDrawArrays, &args, &data) },
        Err(3)
    );
    assert!(SEEN.with(|v| v.borrow().is_empty()));
}

#[test]
fn sparse_game_arrays_do_not_query_restore_or_point_at_disabled_attributes() {
    for (mask, expected) in [
        (DG_GL_ARRAY_POSITION, vec![]),
        (
            DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR | DG_GL_ARRAY_TEXCOORD,
            vec![GL_CURRENT_COLOR, GL_CURRENT_TEXTURE_COORDS],
        ),
        (
            DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR | DG_GL_ARRAY_SECONDARY | DG_GL_ARRAY_TEXCOORD,
            vec![
                GL_CURRENT_COLOR,
                GL_CURRENT_SECONDARY_COLOR,
                GL_CURRENT_TEXTURE_COORDS,
            ],
        ),
    ] {
        SEEN.with(|v| v.borrow_mut().clear());
        ERROR.with(|v| *v.borrow_mut() = 0);
        let stride = if mask & DG_GL_ARRAY_SECONDARY != 0 {
            80
        } else {
            64
        };
        let data = vec![0; stride * 3];
        assert_eq!(
            unsafe {
                draw(
                    &api(),
                    FEnum_glDrawArrays,
                    &[GL_TRIANGLES, 0, 3, mask, 0, 0, 0, 0],
                    &data,
                )
            },
            Ok(0)
        );
        let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
        let queries = seen
            .iter()
            .filter(|v| v.0 == 14)
            .map(|v| v.1 as u32)
            .collect::<Vec<_>>();
        let restored = seen
            .iter()
            .filter(|v| v.0 == 11)
            .map(|v| v.1 as u32)
            .collect::<Vec<_>>();
        assert_eq!(queries, expected);
        assert_eq!(restored, expected);
        assert_eq!(seen.iter().filter(|v| v.0 == 6).count(), expected.len() + 1);
        assert_eq!(seen.iter().filter(|v| v.0 == 1).count(), 1);
        assert_eq!(seen.iter().filter(|v| v.0 == 2).count(), 1);
        assert_eq!(seen.iter().filter(|v| v.0 == 7).count(), 1);
    }
}

#[test]
fn compact_native_pointers_preserve_types_and_restore_on_draw_error() {
    let mut api = api();
    api.dg_glGetDoublev = Some(index_get);
    api.dg_glGetBooleanv = Some(edge_get);
    api.dg_glIndexd = Some(index_restore);
    api.dg_glEdgeFlag = Some(edge_restore);
    api.dg_glIndexPointer = Some(normal);
    api.dg_glEdgeFlagPointer = Some(edge_pointer);
    let data = raw::tests::packet();
    let args = [GL_TRIANGLES, 0, 3, DG_GL_ARRAY_RAW | 127, 0, 0, 0, 0];
    SEEN.with(|v| v.borrow_mut().clear());
    ERROR.with(|v| *v.borrow_mut() = GL_INVALID_OPERATION);
    assert_eq!(
        unsafe { draw(&api, FEnum_glDrawArrays, &args, &data) },
        Ok(GL_INVALID_OPERATION)
    );
    let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
    let offsets = seen
        .iter()
        .filter(|v| v.0 == 6)
        .map(|v| v.1 - data.as_ptr() as usize)
        .collect::<Vec<_>>();
    assert_eq!(offsets, [32, 80, 92, 112, 136, 152, 176]);
    assert!(seen.iter().filter(|v| v.0 == 5).all(|v| v.1 == 0));
    assert_eq!(
        seen.iter()
            .filter(|v| v.0 == 15)
            .map(|v| v.1 as u32)
            .collect::<Vec<_>>(),
        [
            GL_DOUBLE,
            GL_UNSIGNED_BYTE,
            GL_SHORT,
            GL_FLOAT,
            GL_BYTE,
            GL_DOUBLE
        ]
    );
    assert_eq!(
        &seen[seen.len() - 2..],
        &[(12, 16777217.25f64.to_bits() as usize), (13, 1)]
    );
    let mut bad = data.clone();
    bad[28] = 1;
    assert_eq!(
        unsafe { draw(&api, FEnum_glDrawArrays, &args, &bad) },
        Err(1)
    );
    assert!(SEEN.with(|v| v.borrow().is_empty()));
    api.dg_glColorPointer = None;
    assert_eq!(
        unsafe { draw(&api, FEnum_glDrawArrays, &args, &data) },
        Err(3)
    );
    assert!(SEEN.with(|v| v.borrow().is_empty()));
}
unsafe extern "C" fn attrib_pointer(
    index: u32,
    size: i32,
    kind: u32,
    normalized: u8,
    stride: i32,
    data: *const core::ffi::c_void,
) {
    note(21, index as usize);
    note(15, kind as usize);
    note(16, size as usize);
    note(22, normalized as usize);
    note(5, stride as usize);
    note(6, data as usize);
}
unsafe extern "C" fn attrib_enable(index: u32) {
    note(23, index as usize);
}
unsafe extern "C" fn attrib_disable(index: u32) {
    note(24, index as usize);
}
unsafe extern "C" fn attrib_get(index: u32, pname: u32, out: *mut f32) {
    assert_eq!(pname, GL_CURRENT_VERTEX_ATTRIB_ARB);
    note(25, index as usize);
    for i in 0..4 {
        unsafe { *out.add(i) = f32::from_bits(0x4000_0000 + index * 16 + i as u32) };
    }
}
unsafe extern "C" fn attrib_restore(index: u32, p: *const f32) {
    note(26, index as usize);
    note(11, unsafe { *p.add(3) }.to_bits() as usize);
}
fn attrib_api() -> DreamGpuGlApi {
    let mut a = api();
    a.dg_glVertexAttribPointerARB = Some(attrib_pointer);
    a.dg_glEnableVertexAttribArrayARB = Some(attrib_enable);
    a.dg_glDisableVertexAttribArrayARB = Some(attrib_disable);
    a.dg_glGetVertexAttribfvARB = Some(attrib_get);
    a.dg_glVertexAttrib4fvARB = Some(attrib_restore);
    a
}
#[test]
fn attribute_draws_bind_generic_arrays_for_the_draw_alone() {
    let api = attrib_api();
    // Generic 0 (FLOAT 2) provokes without a vertex array; unit 0 FLOAT 2
    // coordinates, generic 3 UNSIGNED_BYTE 4 normalized, generic 15 SHORT 1.
    let data = raw::tests::attribute_packet();
    let mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_TEXCOORD;
    let generic = 1 | 1 << 3 | 1 << 15;
    let args = [GL_TRIANGLES, 0, 3, mask, generic, 0, 0, 0];
    SEEN.with(|v| v.borrow_mut().clear());
    ERROR.with(|v| *v.borrow_mut() = 0);
    assert_eq!(
        unsafe { draw(&api, FEnum_glVertexAttribPointerARB, &args, &data) },
        Ok(0)
    );
    let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
    let ops = |op: u32| {
        seen.iter()
            .filter(|v| v.0 == op)
            .map(|v| v.1)
            .collect::<Vec<_>>()
    };
    // Current values of attributes 1-15 are saved and restored after the
    // pop; attribute zero has none.
    assert_eq!(ops(25), [3, 15]);
    assert_eq!(ops(26), [3, 15]);
    let at = |op: u32, value: usize| seen.iter().position(|v| *v == (op, value)).unwrap();
    assert!(at(25, 15) < at(1, GL_CLIENT_VERTEX_ARRAY_BIT as usize));
    assert!(at(2, 0) < at(26, 3));
    assert!(seen.contains(&(11, 0x4000_0000 + 3 * 16 + 3)));
    // No vertex array: generic attribute 0 is the position.
    assert!(seen.contains(&(4, GL_VERTEX_ARRAY as usize)));
    assert!(!seen.contains(&(3, GL_VERTEX_ARRAY as usize)));
    assert_eq!(ops(21), [0, 3, 15]);
    assert_eq!(ops(22), [0, 1, 0]);
    let base = data.as_ptr() as usize;
    assert_eq!(
        ops(6).iter().map(|p| p - base).collect::<Vec<_>>(),
        [128, 152, 176, 188]
    );
    // Enabled for the draw, disabled again before the pop.
    assert_eq!(ops(23), [0, 3, 15]);
    assert_eq!(ops(24), [0, 3, 15]);
    assert!(at(23, 15) < at(7, GL_TRIANGLES as usize));
    assert!(at(9, 3) < at(24, 0) && at(24, 15) < at(2, 0));
    // A native error is returned as the draw's GL error.
    ERROR.with(|v| *v.borrow_mut() = GL_INVALID_OPERATION);
    assert_eq!(
        unsafe { draw(&api, FEnum_glVertexAttribPointerARB, &args, &data) },
        Ok(GL_INVALID_OPERATION)
    );
    ERROR.with(|v| *v.borrow_mut() = 0);
    // Neither a position nor generic attribute 0: malformed.
    let without = [GL_TRIANGLES, 0, 3, mask, 1 << 3 | 1 << 15, 0, 0, 0];
    SEEN.with(|v| v.borrow_mut().clear());
    assert_eq!(
        unsafe { draw(&api, FEnum_glVertexAttribPointerARB, &without, &data) },
        Err(1)
    );
    // A missing generic entry point fails before any pointer is exposed.
    let mut partial = api;
    partial.dg_glGetVertexAttribfvARB = None;
    assert_eq!(
        unsafe { draw(&partial, FEnum_glVertexAttribPointerARB, &args, &data) },
        Err(3)
    );
    assert!(SEEN.with(|v| v.borrow().is_empty()));
    // The sixteen-slot function never carries generic attributes.
    assert_eq!(
        unsafe { draw(&api, FEnum_glDrawArraysEXT, &args, &data) },
        Err(1)
    );
}
unsafe extern "C" fn bind_buffer(target: u32, name: u32) {
    note(27, target as usize);
    note(28, name as usize);
}
/// The (target, name) of every bind and the (stride, pointer) of every
/// pointer call, in order, with draws.
fn buffer_calls(seen: &[(u32, usize)]) -> Vec<(u32, usize, usize)> {
    let mut out = Vec::new();
    for (i, &(op, value)) in seen.iter().enumerate() {
        match op {
            27 => out.push((27, value, seen[i + 1].1)),
            5 => out.push((5, value, seen[i + 1].1)),
            7 => out.push((7, value, seen[i + 1].1)),
            _ => {}
        }
    }
    out
}
#[test]
fn buffer_draws_bind_each_slot_point_at_offsets_and_unbind() {
    let mut api = attrib_api();
    api.dg_glBindBuffer = Some(bind_buffer);
    let data = raw::tests::buffer_packet(3, &[]);
    let base = data.as_ptr() as usize;
    let a = [
        GL_TRIANGLES,
        5,
        3,
        raw::tests::BUFFER_MASK,
        2,
        1,
        3,
        0,
        0,
        0,
    ];
    let layout = raw::layout_buffer(&a, &data, true).unwrap();
    let mut resolved = crate::buffer::Resolved {
        slots: [0; 32],
        element: 0,
    };
    resolved.slots[0] = 77;
    SEEN.with(|v| v.borrow_mut().clear());
    ERROR.with(|v| *v.borrow_mut() = 0);
    assert_eq!(
        unsafe { buffer_draw(&api, &a, &data, &layout, &resolved) },
        Ok(0)
    );
    let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
    // Position reads buffer 77 at offset 8 with its stride; the client colour
    // and generic sections hold vertices from 5, so their pointers start five
    // vertices before them; the draw starts at vertex 5; no binding outlives it.
    assert_eq!(
        buffer_calls(&seen),
        [
            (27, GL_ARRAY_BUFFER as usize, 77),
            (5, 20, 8),
            (27, GL_ARRAY_BUFFER as usize, 0),
            (5, 0, (base + 140).wrapping_sub(20)),
            (27, GL_ARRAY_BUFFER as usize, 0),
            (5, 0, (base + 152).wrapping_sub(20)),
            (7, GL_TRIANGLES as usize, 5),
            (27, GL_ARRAY_BUFFER as usize, 0),
            (27, GL_ELEMENT_ARRAY_BUFFER as usize, 0),
        ]
    );
    // An element buffer is bound for the draw, its offset passed as indices;
    // inline indices bind zero and point into the payload.
    let e = [
        GL_TRIANGLES,
        5,
        6,
        raw::tests::BUFFER_MASK,
        2,
        1,
        3,
        GL_UNSIGNED_INT,
        9,
        12,
    ];
    let layout = raw::layout_buffer(&e, &data, true).unwrap();
    resolved.element = 88;
    SEEN.with(|v| v.borrow_mut().clear());
    assert_eq!(
        unsafe { buffer_draw(&api, &e, &data, &layout, &resolved) },
        Ok(0)
    );
    let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
    let at = seen
        .iter()
        .position(|v| *v == (27, GL_ELEMENT_ARRAY_BUFFER as usize))
        .unwrap();
    assert_eq!(seen[at + 1], (28, 88));
    assert_eq!(
        &seen[at + 2..at + 5],
        [(7, GL_TRIANGLES as usize), (9, 6), (10, 12)]
    );
    let inline = raw::tests::buffer_packet(3, &[5, 7, 6, 5]);
    let i = [
        GL_TRIANGLE_STRIP,
        5,
        4,
        raw::tests::BUFFER_MASK,
        2,
        1,
        3,
        GL_UNSIGNED_SHORT,
        0,
        0,
    ];
    let layout = raw::layout_buffer(&i, &inline, true).unwrap();
    resolved.element = 0;
    SEEN.with(|v| v.borrow_mut().clear());
    assert_eq!(
        unsafe { buffer_draw(&api, &i, &inline, &layout, &resolved) },
        Ok(0)
    );
    let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
    let at = seen
        .iter()
        .position(|v| *v == (27, GL_ELEMENT_ARRAY_BUFFER as usize))
        .unwrap();
    assert_eq!(seen[at + 1], (28, 0));
    assert_eq!(seen[at + 4], (10, inline.as_ptr() as usize + 164));
    // A native error restores like the other draws; without BindBuffer the
    // draw exposes nothing.
    ERROR.with(|v| *v.borrow_mut() = GL_INVALID_OPERATION);
    assert_eq!(
        unsafe {
            buffer_draw(
                &api,
                &a,
                &data,
                &raw::layout_buffer(&a, &data, true).unwrap(),
                &resolved,
            )
        },
        Ok(GL_INVALID_OPERATION)
    );
    ERROR.with(|v| *v.borrow_mut() = 0);
    api.dg_glBindBuffer = None;
    SEEN.with(|v| v.borrow_mut().clear());
    assert_eq!(
        unsafe {
            buffer_draw(
                &api,
                &a,
                &data,
                &raw::layout_buffer(&a, &data, true).unwrap(),
                &resolved,
            )
        },
        Err(3)
    );
    assert!(SEEN.with(|v| v.borrow().is_empty()));
}
#[test]
fn bgra_buffer_slots_read_their_buffers_with_size_bgra() {
    let mut api = attrib_api();
    api.dg_glBindBuffer = Some(bind_buffer);
    let (a, data) = raw::tests::bgra_packet();
    let layout = raw::layout_buffer_draw(FEnum_glDrawRangeElementsEXT, &a, &data, true).unwrap();
    let mut resolved = crate::buffer::Resolved {
        slots: [0; 32],
        element: 0,
    };
    for (slot, native) in [(0, 70), (1, 71), (4, 71), (17, 72)] {
        resolved.slots[slot] = native;
    }
    SEEN.with(|v| v.borrow_mut().clear());
    ERROR.with(|v| *v.borrow_mut() = 0);
    assert_eq!(
        unsafe { buffer_draw(&api, &a, &data, &layout, &resolved) },
        Ok(0)
    );
    let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
    let ops = |op: u32| {
        seen.iter()
            .filter(|v| v.0 == op)
            .map(|v| v.1)
            .collect::<Vec<_>>()
    };
    // Position, colour, secondary colour, then generic 1 and 3: the BGRA
    // buffer slots pass size BGRA with UNSIGNED_BYTE; the RGBA client section
    // and the position keep their component counts.
    let bgra = GL_BGRA as usize;
    assert_eq!(ops(16), [3, bgra, bgra, bgra, 4]);
    let ubyte = GL_UNSIGNED_BYTE as usize;
    assert_eq!(ops(15), [GL_FLOAT as usize, ubyte, ubyte, ubyte, ubyte]);
    assert_eq!(ops(21), [1, 3]);
    assert_eq!(ops(22), [1, 1]);
    // Buffer slots point at their reference offsets with their strides; the
    // client section starts five vertices before its bytes.
    let base = data.as_ptr() as usize;
    assert_eq!(ops(6), [8, 12, 16, 0, (base + 176).wrapping_sub(20)]);
    assert_eq!(ops(5), [20, 20, 20, 0, 0]);
    let binds = buffer_calls(&seen)
        .into_iter()
        .filter(|c| c.0 == 27)
        .map(|c| c.2)
        .collect::<Vec<_>>();
    assert_eq!(binds, [70, 71, 71, 72, 0, 0, 0]);
    assert!(seen.contains(&(7, GL_TRIANGLES as usize)));
}
/// A Quake III-like indexed client draw as the frontend sends it with buffer
/// draws: no buffer slots, client sections of vertices `base`..`base + 299`
/// (FLOAT 3 positions, UNSIGNED_BYTE 4 colours, FLOAT 2 coordinates of units
/// 0 and 1) and 1500 inline UNSIGNED_INT indices, each `base` + 0..299.
fn ranged_packet(base: u32) -> ([u32; 10], Vec<u8>, Vec<u32>) {
    let vertices = 300u32;
    let indices: Vec<u32> = (0..1500u32).map(|i| base + (i * 7) % vertices).collect();
    let mut data = vec![0u8; 128];
    for (slot, descriptor) in [
        (0, GL_FLOAT | (3 << 16)),
        (1, GL_UNSIGNED_BYTE | (4 << 16)),
        (3, GL_FLOAT | (2 << 16)),
        (7, GL_FLOAT | (2 << 16)),
    ] {
        data[slot * 4..slot * 4 + 4].copy_from_slice(&u32::to_le_bytes(descriptor));
    }
    for (bytes, fill) in [(12, 1u8), (4, 2), (8, 3), (8, 4)] {
        data.extend((0..vertices * bytes).map(|v| (v as u8).wrapping_mul(fill)));
    }
    data.extend(indices.iter().flat_map(|i| i.to_le_bytes()));
    let a = [
        GL_TRIANGLES,
        base,
        1500,
        DG_GL_ARRAY_RAW
            | DG_GL_ARRAY_POSITION
            | DG_GL_ARRAY_COLOR
            | DG_GL_ARRAY_TEXCOORD
            | DG_GL_ARRAY_TEXCOORD1,
        0,
        0,
        vertices,
        GL_UNSIGNED_INT,
        0,
        0,
    ];
    (a, data, indices)
}
#[test]
fn client_only_buffer_draws_carry_each_array_once_and_bound_every_index() {
    let base = 100;
    let (a, data, _) = ranged_packet(base);
    // 300 vertices of 32 bytes and the indices, against 1500 expanded vertices
    // of the sixteen-slot DrawArraysEXT record.
    assert_eq!(data.len(), 128 + 300 * 32 + 1500 * 4);
    let expanded = 64 + 1500 * 32;
    assert!(data.len() * 3 < expanded);
    let layout = raw::layout_buffer_draw(FEnum_glDrawRangeElements, &a, &data, true).unwrap();
    assert_eq!(
        (
            layout.slots[0].offset,
            layout.slots[1].offset,
            layout.slots[3].offset,
            layout.slots[7].offset,
            layout.indices
        ),
        (128, 3728, 4928, 7328, 9728)
    );
    let word_bytes = |a: &[u32]| a.iter().flat_map(|w| w.to_le_bytes()).collect::<Vec<u8>>();
    let validate = |a: &[u32; 10], data: &[u8]| {
        let args = word_bytes(a);
        unsafe {
            crate::gl_validation::dreamgpu_gl_data_validate(
                FEnum_glDrawRangeElements,
                args.as_ptr(),
                data.as_ptr(),
                data.len() as u32,
            )
        }
    };
    assert_eq!(validate(&a, &data), 0);
    // Every inline index lies within the client vertices: one below or past
    // them fails the batch (the frontend sends only indices it scanned).
    for (at, value) in [(0, base - 1), (1499, base + 300), (700, u32::MAX)] {
        let mut bad = data.clone();
        let offset = 9728 + at * 4;
        bad[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        assert_eq!(validate(&a, &bad), DG_GL_ERROR_BATCH, "{at} {value}");
        assert!(raw::layout_buffer_draw(FEnum_glDrawRangeElements, &a, &bad, true).is_none());
    }
    // Without buffer slots nothing resolves against the context's buffers.
    let resolved =
        unsafe { crate::buffer::resolve(core::ptr::null(), &a, &layout, &data) }.unwrap();
    assert_eq!((resolved.slots, resolved.element), ([0; 32], 0));
    // Every slot reads its section through a pointer `base` vertices before
    // it with tight strides, no buffer is bound, and the indices are the
    // payload's.
    let mut api = attrib_api();
    api.dg_glBindBuffer = Some(bind_buffer);
    SEEN.with(|v| v.borrow_mut().clear());
    ERROR.with(|v| *v.borrow_mut() = 0);
    assert_eq!(
        unsafe { buffer_draw(&api, &a, &data, &layout, &resolved) },
        Ok(0)
    );
    let seen = SEEN.with(|v| core::mem::take(&mut *v.borrow_mut()));
    let p = data.as_ptr() as usize;
    let calls = buffer_calls(&seen);
    let pointers: Vec<_> = calls
        .iter()
        .filter(|c| c.0 == 5)
        .map(|c| (c.1, c.2))
        .collect();
    assert_eq!(
        pointers,
        [
            (0, (p + 128).wrapping_sub(100 * 12)),
            (0, (p + 3728).wrapping_sub(100 * 4)),
            (0, (p + 4928).wrapping_sub(100 * 8)),
            (0, (p + 7328).wrapping_sub(100 * 8)),
        ]
    );
    assert!(calls.iter().filter(|c| c.0 == 27).all(|c| c.2 == 0));
    let at = seen
        .iter()
        .position(|v| *v == (7, GL_TRIANGLES as usize))
        .unwrap();
    assert_eq!(&seen[at + 1..at + 3], [(9, 1500), (10, p + 9728)]);
}
