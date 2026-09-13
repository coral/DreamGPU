// SPDX-License-Identifier: GPL-2.0-or-later
use super::*;
use crate::texture::{names::Entry, Texture};
use std::cell::RefCell;
#[derive(Default)]
struct Calls {
    names: Vec<u32>,
    priorities: Vec<(u32, u32)>,
    all: bool,
}
thread_local! {static C:RefCell<Calls>=RefCell::new(Calls::default());}
unsafe extern "C" fn error() -> u32 {
    0
}
unsafe extern "C" fn resident_native(n: i32, names: *const u32, out: *mut u8) -> u8 {
    assert_eq!(n, 3);
    let names = unsafe { core::slice::from_raw_parts(names, 3) };
    C.with(|c| {
        let mut c = c.borrow_mut();
        c.names.extend_from_slice(names);
        if c.all {
            1
        } else {
            unsafe {
                out.write(1);
                out.add(1).write(0);
                out.add(2).write(1);
            }
            0
        }
    })
}
unsafe extern "C" fn priority_native(n: i32, name: *const u32, value: *const f32) {
    assert_eq!(n, 1);
    C.with(|c| {
        c.borrow_mut()
            .priorities
            .push(unsafe { (*name, (*value).to_bits()) })
    });
}
fn setup() -> DreamGpuGlApi {
    C.with(|c| *c.borrow_mut() = Calls::default());
    let mut a = unsafe { core::mem::zeroed::<DreamGpuGlApi>() };
    a.dg_glGetError = Some(error);
    a.dg_glAreTexturesResident = Some(resident_native);
    a.dg_glPrioritizeTextures = Some(priority_native);
    a
}
fn insert(ns: &mut Namespace, name: u32, t: &mut Texture) {
    ns.entries[(name.wrapping_mul(0x9e3779b1) as usize) & 8191] = Entry { name, texture: t };
}
#[test]
fn native_residency_translates_names_and_preserves_false_per_object_results() {
    let a = setup();
    let mut ns = Box::new(Namespace::default());
    let mut t = Texture {
        name: 701,
        ..Texture::default()
    };
    let mut u = Texture {
        name: 809,
        ..Texture::default()
    };
    insert(&mut ns, 7, &mut t);
    insert(&mut ns, 9, &mut u);
    let mut e = 0;
    assert_eq!(
        unsafe { resident(&a, &mut *ns, &mut e, [7, 9, 7]) },
        Ok([1, 0, 1, 0, 1])
    );
    C.with(|c| {
        let mut c = c.borrow_mut();
        assert_eq!(c.names, [701, 809, 701]);
        c.all = true;
    });
    assert_eq!(
        unsafe { resident(&a, &mut *ns, &mut e, [9, 7, 9]) },
        Ok([1, 1, 1, 1, 1])
    );
    C.with(|c| c.borrow_mut().names.clear());
    for names in [[7, 0, 9], [7, 8, 9]] {
        assert_eq!(unsafe { resident(&a, &mut *ns, &mut e, names) }, Ok([0; 5]));
    }
    C.with(|c| assert!(c.borrow().names.is_empty()));
    assert_eq!(e, 1 << (GL_INVALID_VALUE - GL_INVALID_ENUM));
}
#[test]
fn priorities_ignore_nonobjects_preserve_float_bits_and_do_not_rebind() {
    let a = setup();
    let mut ns = Box::new(Namespace::default());
    let mut t = Texture {
        name: 1234,
        ..Texture::default()
    };
    insert(&mut ns, 77, &mut t);
    let mut e = 0;
    let data = [0, 0x7fc12345, 88, 1, 77, (-2f32).to_bits(), 77, 0x7fc12345]
        .into_iter()
        .flat_map(u32::to_le_bytes)
        .collect::<Vec<_>>();
    assert_eq!(unsafe { prioritize(&a, &mut *ns, &mut e, &data) }, Ok(()));
    C.with(|c| {
        assert_eq!(
            c.borrow().priorities,
            [(1234, (-2f32).to_bits()), (1234, 0x7fc12345)]
        )
    });
    assert_eq!((e, t.name, t.version), (0, 1234, 0));
}
#[test]
fn priority_payload_and_residency_shape_are_bounded_before_dispatch() {
    use crate::gl_validation::*;
    let a = [1u32.to_le_bytes(), 0u32.to_le_bytes(), 0u32.to_le_bytes()].concat();
    let bytes = [0u8; 8];
    assert_eq!(
        unsafe {
            dreamgpu_gl_data_validate(FEnum_glPrioritizeTextures, a.as_ptr(), bytes.as_ptr(), 7)
        },
        DG_GL_ERROR_BATCH
    );
    assert_eq!(
        unsafe {
            dreamgpu_gl_data_validate(FEnum_glPrioritizeTextures, a.as_ptr(), bytes.as_ptr(), 8)
        },
        0
    );
    let mut kind = 0;
    assert_eq!(
        unsafe { dreamgpu_gl_query_shape(FEnum_glAreTexturesResident, a.as_ptr(), &mut kind) },
        5
    );
    assert_eq!(kind, DG_GL_RESULT_BOOL);
    assert_eq!(
        unsafe { dreamgpu_gl_query_result_bytes(FEnum_glAreTexturesResident, a.as_ptr()) },
        5
    );
}

#[test]
fn copy_one_dimensional_full_formats_and_border_arithmetic_are_validated() {
    use crate::gl_validation::*;
    let formats = [
        GL_ALPHA,
        GL_LUMINANCE,
        GL_LUMINANCE_ALPHA,
        GL_INTENSITY,
        GL_RGB,
        GL_RGBA,
        GL_R3_G3_B2,
    ]
    .into_iter()
    .chain(0x803b..=0x8048)
    .chain(0x804a..=0x804d)
    .chain(0x804f..=0x805b);
    for f in formats {
        let wire = [GL_TEXTURE_1D, 0, f, 0, 0, 6, 1]
            .into_iter()
            .flat_map(u32::to_le_bytes)
            .collect::<Vec<_>>();
        assert_eq!(
            unsafe { dreamgpu_gl_call_validate(FEnum_glCopyTexImage1D, wire.as_ptr()) },
            0,
            "format{f:x}"
        );
    }
    for (f, l, w, b) in [
        (1, 0, 6, 1),
        (0x804e, 0, 6, 1),
        (GL_RGBA, 12, 6, 1),
        (GL_RGBA, 0, 1, 1),
        (GL_RGBA, 0, 6, 2),
        (GL_RGBA, 0, u32::MAX, 1),
    ] {
        let wire = [GL_TEXTURE_1D, l, f, 0, 0, w, b]
            .into_iter()
            .flat_map(u32::to_le_bytes)
            .collect::<Vec<_>>();
        assert_eq!(
            unsafe { dreamgpu_gl_call_validate(FEnum_glCopyTexImage1D, wire.as_ptr()) },
            DG_GL_ERROR_TEXTURE
        );
    }
}
