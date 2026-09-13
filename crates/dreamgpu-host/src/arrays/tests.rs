use super::*;
use std::cell::RefCell;
thread_local! { static SEEN: RefCell<Vec<(u32, usize)>> = const { RefCell::new(Vec::new()) }; static ERROR: RefCell<u32> = const { RefCell::new(0) }; }
fn note(op: u32, value: usize) {
    SEEN.with(|v| v.borrow_mut().push((op, value)));
}
unsafe extern "C" fn get(pname: u32, out: *mut f32) {
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
unsafe extern "C" fn pointer(_: i32, _: u32, stride: i32, data: *const core::ffi::c_void) {
    note(5, stride as usize);
    note(6, data as usize);
}
unsafe extern "C" fn normal(_: u32, stride: i32, data: *const core::ffi::c_void) {
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
        let restored = &seen[seen.len() - 5..];
        assert_eq!(
            restored,
            [
                (2, 0),
                (11, GL_CURRENT_COLOR as usize),
                (11, GL_CURRENT_SECONDARY_COLOR as usize),
                (11, GL_CURRENT_NORMAL as usize),
                (11, GL_CURRENT_TEXTURE_COORDS as usize)
            ]
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
