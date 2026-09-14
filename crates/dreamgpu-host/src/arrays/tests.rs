use super::*;
use std::cell::RefCell;
thread_local! { static SEEN: RefCell<Vec<(u32, usize)>> = const { RefCell::new(Vec::new()) }; static ERROR: RefCell<u32> = const { RefCell::new(0) }; }
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
    a
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
