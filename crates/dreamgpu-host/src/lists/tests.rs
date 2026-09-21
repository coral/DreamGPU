// SPDX-License-Identifier: GPL-2.0-or-later
use super::*;
use std::{
    alloc::{alloc as heap, dealloc, Layout},
    vec::Vec,
};
#[derive(Default)]
struct Harness {
    fail: bool,
    allocations: usize,
    calls: Vec<(u32, Vec<u8>, Vec<u8>)>,
    begin: u32,
}
unsafe extern "C" fn allocate(o: *mut c_void, n: usize) -> *mut c_void {
    let h = unsafe { &mut *o.cast::<Harness>() };
    if h.fail {
        return null_mut();
    }
    let p = unsafe { heap(Layout::from_size_align(n + 16, 16).unwrap()) };
    if !p.is_null() {
        unsafe {
            p.cast::<usize>().write(n);
        };
        h.allocations += 1;
    }
    if p.is_null() {
        null_mut()
    } else {
        unsafe { p.add(16).cast() }
    }
}
unsafe extern "C" fn free_heap(o: *mut c_void, p: *mut c_void) {
    let h = unsafe { &mut *o.cast::<Harness>() };
    let p = unsafe { p.cast::<u8>().sub(16) };
    unsafe {
        dealloc(
            p,
            Layout::from_size_align(p.cast::<usize>().read() + 16, 16).unwrap(),
        );
    }
    h.allocations -= 1;
}
unsafe extern "C" fn forget(_: *mut c_void, _: *mut crate::texture::Texture) {}
unsafe extern "C" fn integer(_: u32, out: *mut i32) {
    unsafe {
        *out = 10;
    }
}
unsafe extern "C" fn execute(
    o: *mut c_void,
    _: u32,
    f: u32,
    a: *const u8,
    d: *const u8,
    n: u32,
) -> u32 {
    let h = unsafe { &mut *o.cast::<Harness>() };
    let words = crate::gl_validation::dreamgpu_gl_function_words(f) & !DG_GL_FUNCTION_KIND_MASK;
    h.calls.push((
        f,
        unsafe { core::slice::from_raw_parts(a, words as usize * 4) }.to_vec(),
        if n == 0 {
            vec![]
        } else {
            unsafe { core::slice::from_raw_parts(d, n as usize) }.to_vec()
        },
    ));
    if f == FEnum_glBegin {
        h.begin = 1;
    } else if f == FEnum_glEnd {
        h.begin = 0;
    }
    0
}
struct Fixture {
    h: Box<Harness>,
    api: Box<DreamGpuGlApi>,
    credit: Box<u64>,
    textures: Box<Textures>,
    c: Box<ContextState>,
}
impl Fixture {
    fn new() -> Self {
        let mut api: Box<DreamGpuGlApi> = Box::new(unsafe { core::mem::zeroed() });
        api.dg_glGetIntegerv = Some(integer);
        let mut textures = Box::<Textures>::default();
        let mut c: Box<ContextState> = Box::new(unsafe { core::mem::zeroed() });
        c.textures = &mut *textures;
        Self {
            h: Box::default(),
            api,
            credit: Box::new(0),
            textures,
            c,
        }
    }
    fn memory(&mut self) -> Memory {
        Memory {
            api: &*self.api,
            bytes: null_mut(),
            image_bytes: &mut *self.credit,
            count: null_mut(),
            opaque: (&mut *self.h as *mut Harness).cast(),
            allocate,
            free: free_heap,
            forget_read: forget,
        }
    }
    fn query(&mut self, f: u32, a: [u32; 3]) -> Vec<u32> {
        let m = self.memory();
        let mut out = [0u8; 8];
        unsafe {
            query(&m, &mut *self.c, f, a, out.as_mut_ptr()).unwrap();
        }
        if f == FEnum_glIsList {
            return vec![u32::from(out[0])];
        }
        out[..query_shape(f, a) as usize * 4]
            .as_chunks::<4>()
            .0
            .iter()
            .map(|b| u32::from_le_bytes(*b))
            .collect()
    }
    fn command(&mut self, f: u32, args: &[u32], data: &[u8]) {
        let kind = u32::from(
            crate::gl_validation::dreamgpu_gl_function_words(f) & DG_GL_FUNCTION_KIND_MASK != 0,
        );
        let a: Vec<u8> = args.iter().flat_map(|v| v.to_le_bytes()).collect();
        let m = self.memory();
        let begin = &mut self.h.begin as *mut u32;
        assert_eq!(
            unsafe {
                dreamgpu_list_dispatch(
                    &m,
                    &mut *self.c,
                    begin,
                    execute,
                    m.opaque,
                    kind,
                    f,
                    a.as_ptr(),
                    args.len() as u32,
                    data.as_ptr(),
                    data.len() as u32,
                )
            },
            0
        );
    }
    fn start(&mut self, n: u32, mode: u32) {
        assert_eq!(self.query(FEnum_glNewList, [n, mode, 0]), [0]);
    }
    fn end(&mut self) {
        assert_eq!(self.query(FEnum_glEndList, [0; 3]), [0]);
    }
    fn call(&mut self, n: u32) {
        self.command(FEnum_glCallList, &[n], &[]);
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        let m = self.memory();
        unsafe {
            release(&m, self.c.lists);
            namespace_release(&m, self.textures.lists);
        };
        assert_eq!(*self.credit, 0);
        assert_eq!(self.h.allocations, 0);
    }
}
#[test]
fn immutable_compile_execute_and_immediate_commands() {
    let mut f = Fixture::new();
    f.start(1, GL_COMPILE);
    let mut data = [1u8; 128];
    f.command(FEnum_glPolygonStipple, &[], &data);
    data.fill(9);
    f.command(FEnum_glFlush, &[], &[]);
    assert_eq!(f.h.calls.len(), 1);
    f.end();
    f.call(1);
    assert_eq!(f.h.calls[1].2, [1u8; 128]);
    f.start(2, GL_COMPILE_AND_EXECUTE);
    f.command(FEnum_glClear, &[GL_COLOR_BUFFER_BIT], &[]);
    assert_eq!(f.h.calls.len(), 3);
    f.end();
    f.call(2);
    assert_eq!(f.h.calls.len(), 4);
}
#[test]
fn failed_replacement_preserves_old_and_credit() {
    let mut f = Fixture::new();
    f.start(7, GL_COMPILE);
    f.command(FEnum_glListBase, &[123], &[]);
    f.end();
    let credit = *f.credit;
    f.start(7, GL_COMPILE);
    f.h.fail = true;
    f.command(FEnum_glListBase, &[456], &[]);
    f.h.fail = false;
    assert_eq!(f.query(FEnum_glEndList, [0; 3]), [GL_OUT_OF_MEMORY]);
    assert_eq!(*f.credit, credit);
    f.call(7);
    assert_eq!(word(&f.h.calls[0].1, 0), 123);
    f.h.fail = true;
    assert_eq!(
        f.query(FEnum_glGenLists, [4096, 0, 0]),
        [GL_OUT_OF_MEMORY, 0]
    );
    f.h.fail = false;
}
#[test]
fn namespaces_reservation_collision_deletion_and_wrap() {
    let mut f = Fixture::new();
    assert_eq!(f.query(FEnum_glGenLists, [3, 0, 0]), [0, 1]);
    for n in 1..=3 {
        assert_eq!(f.query(FEnum_glIsList, [n, 0, 0]), [1]);
    }
    for n in [8193, 16385] {
        f.start(n, GL_COMPILE);
        f.command(FEnum_glListBase, &[n], &[]);
        f.end();
    }
    f.query(FEnum_glDeleteLists, [1, 1, 0]);
    for n in [8193, 16385] {
        f.call(n);
    }
    assert_eq!(f.h.calls.len(), 2);
    unsafe {
        (*f.textures.lists).next = u32::MAX;
    }
    assert_eq!(f.query(FEnum_glGenLists, [1, 0, 0]), [0, u32::MAX]);
    let mut other: ContextState = unsafe { core::mem::zeroed() };
    other.textures = &mut *f.textures;
    let m = f.memory();
    let mut out = [0u8; 4];
    unsafe {
        query(
            &m,
            &mut other,
            FEnum_glIsList,
            [8193, 0, 0],
            out.as_mut_ptr(),
        )
        .unwrap();
    }
    assert_eq!(out[0], 1);
    f.query(FEnum_glDeleteLists, [0, i32::MAX as u32, 0]);
    assert_eq!(f.query(FEnum_glIsList, [8193, 0, 0]), [0]);
}
#[test]
fn nested_late_binding_base_and_bounded_recursion() {
    let mut f = Fixture::new();
    f.start(1, GL_COMPILE);
    f.call(11);
    f.end();
    f.start(11, GL_COMPILE);
    f.command(FEnum_glListBase, &[7], &[]);
    f.end();
    f.call(1);
    f.start(11, GL_COMPILE);
    f.command(FEnum_glListBase, &[8], &[]);
    f.end();
    f.call(1);
    assert_eq!(word(&f.h.calls[0].1, 0), 7);
    assert_eq!(word(&f.h.calls[1].1, 0), 8);
    f.command(FEnum_glCallLists, &[1], &1u32.to_le_bytes());
    assert_eq!(f.h.calls.len(), 3);
    f.start(22, GL_COMPILE);
    f.call(22);
    f.command(FEnum_glListBase, &[22], &[]);
    f.end();
    f.call(22);
    assert_eq!(f.h.calls.len(), 3 + NESTING as usize);
}
#[test]
fn errors_are_deferred_to_replay_and_compiler_structure_is_retained() {
    let mut f = Fixture::new();
    f.start(1, GL_COMPILE);
    f.command(DG_GL_RECORD_ERROR, &[GL_INVALID_ENUM], &[]);
    assert_eq!(f.c.guest_errors, 0);
    f.command(FEnum_glBegin, &[GL_POINTS], &[]);
    f.command(DG_GL_RECORD_ERROR, &[GL_INVALID_OPERATION], &[]);
    assert_eq!(f.query(FEnum_glEndList, [0; 3]), [GL_INVALID_OPERATION]);
    f.command(FEnum_glEnd, &[], &[]);
    f.end();
    f.call(1);
    assert_eq!(
        f.h.calls.iter().map(|v| v.0).collect::<Vec<_>>(),
        [FEnum_glBegin, FEnum_glEnd]
    );
    assert_eq!(f.h.begin, 0);
}
#[test]
fn unfinished_image_discards_new_definition_without_execution() {
    let mut f = Fixture::new();
    f.start(1, GL_COMPILE);
    f.command(
        FEnum_glDrawPixels,
        &[1, 1, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0, 1, 1],
        &[1, 2],
    );
    assert_eq!(f.query(FEnum_glEndList, [0; 3]), [GL_INVALID_OPERATION]);
    assert!(f.h.calls.is_empty());
    assert_eq!(f.query(FEnum_glIsList, [1, 0, 0]), [0]);
    // Context teardown with an open compiler releases its unpublished nodes.
    f.start(2, GL_COMPILE);
    f.command(FEnum_glColor3f, &[0, 0, 0], &[]);
}

#[test]
fn proxy_texture_definitions_execute_immediately_and_are_not_replayed() {
    let mut f = Fixture::new();
    f.start(1, GL_COMPILE);
    f.command(
        FEnum_glTexImage2D,
        &[
            GL_PROXY_TEXTURE_2D,
            0,
            GL_RGBA8,
            4,
            4,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
        ],
        &[],
    );
    assert_eq!(f.h.calls.len(), 1);
    f.end();
    f.call(1);
    assert_eq!(f.h.calls.len(), 1);
}
