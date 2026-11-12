use super::*;
use crate::state::{dreamgpu_context_state_init, dreamgpu_context_state_release};
use crate::texture::Texture;
use core::ffi::c_void;
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
    collections::{BTreeMap, VecDeque},
    mem::MaybeUninit,
};

#[derive(Default)]
struct Allocator {
    live: BTreeMap<usize, Layout>,
}
unsafe extern "C" fn allocate(p: *mut c_void, bytes: usize) -> *mut c_void {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    let layout = Layout::from_size_align(bytes.max(1), 8).unwrap();
    let p = unsafe { alloc(layout) };
    assert!(!p.is_null());
    // Deliberately not zero initialized; production initialization owns writes.
    unsafe { core::ptr::write_bytes(p, 0xaa, bytes) };
    a.live.insert(p as usize, layout);
    p.cast()
}
unsafe extern "C" fn free(p: *mut c_void, object: *mut c_void) {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    let layout = a
        .live
        .remove(&(object as usize))
        .expect("foreign/double free");
    unsafe { dealloc(object.cast(), layout) }
}
unsafe extern "C" fn forget(_: *mut c_void, _: *mut Texture) {}

/// A native buffer model: live names with contents and usage, the
/// ARRAY_BUFFER binding (every operation must leave it zero), an error queue.
#[derive(Default)]
struct Native {
    next: u32,
    live: BTreeMap<u32, (Vec<u8>, u32)>,
    bound: u32,
    errors: VecDeque<u32>,
    /// The next BufferData fails with GL_OUT_OF_MEMORY.
    fail_data: bool,
    calls: Vec<&'static str>,
}
std::thread_local! {static N: RefCell<Native> = RefCell::new(Native::default());}
fn native<R>(f: impl FnOnce(&mut Native) -> R) -> R {
    N.with(|n| f(&mut n.borrow_mut()))
}
unsafe extern "C" fn fake_get_error() -> u32 {
    native(|n| n.errors.pop_front().unwrap_or(0))
}
unsafe extern "C" fn fake_gen(count: i32, out: *mut u32) {
    assert_eq!(count, 1);
    native(|n| {
        n.next += 1;
        n.live.insert(n.next, (Vec::new(), 0));
        unsafe { *out = n.next };
    })
}
unsafe extern "C" fn fake_delete(count: i32, names: *const u32) {
    assert_eq!(count, 1);
    let name = unsafe { *names };
    native(|n| {
        assert!(
            n.live.remove(&name).is_some(),
            "native double delete {name}"
        );
        assert_eq!(n.bound, 0, "deleted while bound");
        n.calls.push("delete");
    })
}
unsafe extern "C" fn fake_bind(target: u32, name: u32) {
    assert_eq!(target, GL_ARRAY_BUFFER);
    native(|n| {
        assert!(
            name == 0 || n.live.contains_key(&name),
            "bind of a dead name"
        );
        n.bound = name;
    })
}
unsafe extern "C" fn fake_data(target: u32, size: isize, data: *const c_void, usage: u32) {
    assert_eq!(target, GL_ARRAY_BUFFER);
    native(|n| {
        assert_ne!(n.bound, 0);
        n.calls.push("data");
        if core::mem::take(&mut n.fail_data) {
            n.errors.push_back(GL_OUT_OF_MEMORY);
            return;
        }
        let bytes = if data.is_null() {
            vec![0xcc; size as usize]
        } else {
            unsafe { core::slice::from_raw_parts(data.cast::<u8>(), size as usize) }.to_vec()
        };
        let bound = n.bound;
        n.live.insert(bound, (bytes, usage));
    })
}
unsafe extern "C" fn fake_sub_data(target: u32, offset: isize, size: isize, data: *const c_void) {
    assert_eq!(target, GL_ARRAY_BUFFER);
    native(|n| {
        n.calls.push("subdata");
        let bound = n.bound;
        let store = &mut n.live.get_mut(&bound).expect("bound buffer").0;
        let bytes = unsafe { core::slice::from_raw_parts(data.cast::<u8>(), size as usize) };
        store[offset as usize..(offset + size) as usize].copy_from_slice(bytes);
    })
}
fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGetError = Some(fake_get_error);
    a.dg_glGenBuffers = Some(fake_gen);
    a.dg_glDeleteBuffers = Some(fake_delete);
    a.dg_glBindBuffer = Some(fake_bind);
    a.dg_glBufferData = Some(fake_data);
    a.dg_glBufferSubData = Some(fake_sub_data);
    a
}
struct Fixture {
    _api: Box<DreamGpuGlApi>,
    allocator: Box<Allocator>,
    _count: Box<u32>,
    bytes: Box<u64>,
    _images: Box<u64>,
    memory: Memory,
}
fn fixture() -> Fixture {
    native(|n| *n = Native::default());
    let api = Box::new(api());
    let mut allocator = Box::<Allocator>::default();
    let mut count = Box::new(0);
    let mut bytes = Box::new(0);
    let mut images = Box::new(0);
    let memory = Memory {
        api: &*api,
        bytes: &mut *bytes,
        image_bytes: &mut *images,
        count: &mut *count,
        opaque: (&mut *allocator as *mut Allocator).cast(),
        allocate,
        free,
        forget_read: forget,
    };
    Fixture {
        _api: api,
        allocator,
        _count: count,
        bytes,
        _images: images,
        memory,
    }
}
fn context(m: &Memory, shared: *mut Textures) -> Box<MaybeUninit<ContextState>> {
    let mut c = Box::<ContextState>::new_uninit();
    assert_eq!(
        unsafe { dreamgpu_context_state_init(m, c.as_mut_ptr(), shared) },
        0
    );
    c
}
fn release(m: &Memory, mut c: Box<MaybeUninit<ContextState>>) {
    unsafe { dreamgpu_context_state_release(m, c.as_mut_ptr()) };
}
fn errors(c: &mut MaybeUninit<ContextState>) -> u32 {
    unsafe { core::mem::take(&mut (*c.as_mut_ptr()).guest_errors) }
}
const INVALID_VALUE: u32 = 1 << 1;
const OUT_OF_MEMORY: u32 = 1 << 5;
fn put(m: &Memory, c: &mut MaybeUninit<ContextState>, name: u32, size: u32, data: &[u8]) {
    let a = [name, size, GL_STATIC_DRAW, 0, 0, 0, 0, 0];
    assert!(data_shape(&a, data));
    assert_eq!(unsafe { super::data(m, c.as_mut_ptr(), &a, data) }, Ok(()));
}
fn sub(m: &Memory, c: &mut MaybeUninit<ContextState>, name: u32, offset: u32, data: &[u8]) {
    let a = [name, offset, 0, 0, 0, 0, 0, 0];
    assert!(sub_data_shape(&a, data));
    assert_eq!(unsafe { sub_data(m, c.as_mut_ptr(), &a, data) }, Ok(()));
}
/// The host copy and the native contents of a buffer.
fn contents(c: &mut MaybeUninit<ContextState>, name: u32) -> Option<(Vec<u8>, Vec<u8>)> {
    let b = unsafe { find(c.as_ptr(), name) };
    if b.is_null() {
        return None;
    }
    let (host, native_name) = unsafe {
        (
            if (*b).size == 0 {
                Vec::new()
            } else {
                core::slice::from_raw_parts((*b).data, (*b).size as usize).to_vec()
            },
            (*b).native,
        )
    };
    Some((host, native(|n| n.live[&native_name].0.clone())))
}

#[test]
fn record_shapes_bound_names_sizes_usages_and_payloads() {
    for usage in [
        GL_STREAM_DRAW,
        GL_STREAM_READ,
        GL_STREAM_COPY,
        GL_STATIC_DRAW,
        GL_STATIC_READ,
        GL_STATIC_COPY,
        GL_DYNAMIC_DRAW,
        GL_DYNAMIC_READ,
        GL_DYNAMIC_COPY,
    ] {
        assert!(data_shape(&[1, 4, usage, 0, 0, 0, 0, 0], &[1, 2, 3, 4]));
        assert!(data_shape(&[1, 4, usage, 0, 0, 0, 0, 0], &[]));
    }
    // Name zero, an unknown hint, a size beyond the bound, a partial payload.
    assert!(!data_shape(&[0, 4, GL_STATIC_DRAW, 0, 0, 0, 0, 0], &[]));
    assert!(!data_shape(&[1, 4, GL_STATIC_DRAW + 3, 0, 0, 0, 0, 0], &[]));
    assert!(!data_shape(&[1, 4, GL_ARRAY_BUFFER, 0, 0, 0, 0, 0], &[]));
    assert!(data_shape(
        &[1, DG_GL_MAX_BUFFER_BYTES, GL_STATIC_DRAW, 0, 0, 0, 0, 0],
        &[]
    ));
    assert!(!data_shape(
        &[1, DG_GL_MAX_BUFFER_BYTES + 1, GL_STATIC_DRAW, 0, 0, 0, 0, 0],
        &[]
    ));
    assert!(!data_shape(
        &[1, 4, GL_STATIC_DRAW, 0, 0, 0, 0, 0],
        &[1, 2, 3]
    ));
    assert!(sub_data_shape(
        &[1, DG_GL_MAX_BUFFER_BYTES - 2, 0, 0, 0, 0, 0, 0],
        &[1, 2]
    ));
    assert!(!sub_data_shape(
        &[1, DG_GL_MAX_BUFFER_BYTES - 1, 0, 0, 0, 0, 0, 0],
        &[1, 2]
    ));
    assert!(!sub_data_shape(&[1, u32::MAX, 0, 0, 0, 0, 0, 0], &[1]));
    assert!(!sub_data_shape(&[0, 0, 0, 0, 0, 0, 0, 0], &[1]));
}

#[test]
fn data_creates_charges_replaces_and_mirrors_native_storage() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    let base = *f.bytes;
    // Zero-defined storage, then payload replacement of the same size reuses
    // the host copy; a different size reallocates and recharges.
    put(m, &mut c, 7, 8, &[]);
    assert_eq!(contents(&mut c, 7), Some((vec![0; 8], vec![0; 8])));
    let charged = *f.bytes - base;
    assert!(charged >= 8 + OBJECT_BYTES);
    put(m, &mut c, 7, 8, &[1, 2, 3, 4, 5, 6, 7, 8]);
    assert_eq!(*f.bytes - base, charged);
    let bytes = (1..=8).collect::<Vec<u8>>();
    assert_eq!(contents(&mut c, 7), Some((bytes.clone(), bytes)));
    put(m, &mut c, 7, 3, &[9, 8, 7]);
    assert_eq!(*f.bytes - base, charged - 5);
    assert_eq!(contents(&mut c, 7), Some((vec![9, 8, 7], vec![9, 8, 7])));
    // Empty storage keeps the object.
    put(m, &mut c, 7, 0, &[]);
    assert_eq!(contents(&mut c, 7), Some((Vec::new(), Vec::new())));
    // Usage reaches native GL; every native operation restored the binding.
    put(m, &mut c, 7, 2, &[5, 6]);
    let native_name = unsafe { (*find(c.as_ptr(), 7)).native };
    native(|n| {
        assert_eq!(n.live[&native_name].1, GL_STATIC_DRAW);
        assert_eq!(n.bound, 0);
    });
    assert_eq!(errors(&mut c), 0);
    release(m, c);
    // The last context released every object, its native buffer and charge.
    native(|n| assert!(n.live.is_empty()));
    assert_eq!(*f.bytes, 0);
    assert!(f.allocator.live.is_empty());
}

#[test]
fn subdata_stores_ranges_and_rejects_anything_beyond_as_invalid_value() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    put(m, &mut c, 3, 6, &[0, 1, 2, 3, 4, 5]);
    sub(m, &mut c, 3, 2, &[9, 9]);
    let expected = vec![0, 1, 9, 9, 4, 5];
    assert_eq!(
        contents(&mut c, 3),
        Some((expected.clone(), expected.clone()))
    );
    sub(m, &mut c, 3, 6, &[]);
    native(|n| n.calls.clear());
    for (name, offset, data) in [(3, 5, &[1u8, 2][..]), (3, 7, &[][..]), (4, 0, &[1][..])] {
        sub(m, &mut c, name, offset, data);
        assert_eq!(errors(&mut c), INVALID_VALUE, "{name} {offset}");
    }
    native(|n| assert!(n.calls.is_empty()));
    assert_eq!(contents(&mut c, 3), Some((expected.clone(), expected)));
    release(m, c);
}

#[test]
fn exhaustion_and_native_failure_leave_an_empty_object_with_out_of_memory() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    put(m, &mut c, 1, 4, &[1, 2, 3, 4]);
    // A store beyond the remaining budget.
    let used = *f.bytes;
    unsafe { *m.bytes = u64::from(DG_GL_MAX_TEXTURE_BYTES) - 2 };
    put(m, &mut c, 1, 16, &[]);
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    assert_eq!(contents(&mut c, 1), Some((Vec::new(), Vec::new())));
    unsafe { *m.bytes = used - 4 };
    // A native BufferData failure.
    native(|n| n.fail_data = true);
    put(m, &mut c, 1, 4, &[1, 2, 3, 4]);
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    assert_eq!(unsafe { (*find(c.as_ptr(), 1)).size }, 0);
    assert_eq!(*f.bytes, used - 4);
    // The table bound itself.
    let ns = unsafe { (*(*c.as_ptr()).textures).buffers };
    let saved = unsafe { (*ns).count };
    unsafe { (*ns).count = DG_GL_MAX_BUFFERS };
    put(m, &mut c, 2, 4, &[]);
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    unsafe { (*ns).count = saved };
    assert!(unsafe { find(c.as_ptr(), 2) }.is_null());
    release(m, c);
    assert_eq!(*f.bytes, 0);
    assert!(f.allocator.live.is_empty());
}

#[test]
fn deletion_and_sharing_follow_the_namespace() {
    let f = fixture();
    let m = &f.memory;
    let mut first = context(m, null_mut());
    let shared = unsafe { (*first.as_ptr()).textures };
    let mut second = context(m, shared);
    let mut other = context(m, null_mut());
    put(m, &mut first, 5, 2, &[1, 2]);
    // The sharing context reads and writes the same object; an unrelated
    // namespace does not see it.
    sub(m, &mut second, 5, 1, &[7]);
    assert_eq!(contents(&mut first, 5).unwrap().0, [1, 7]);
    assert!(unsafe { find(other.as_ptr(), 5) }.is_null());
    put(m, &mut other, 5, 1, &[3]);
    assert_eq!(contents(&mut first, 5).unwrap().0, [1, 7]);
    // Zero and unknown names are ignored; deletion frees native storage.
    let names = [0u32, 9, 5, 5].map(u32::to_le_bytes).concat();
    assert_eq!(unsafe { delete(m, second.as_mut_ptr(), &names) }, Ok(()));
    assert!(unsafe { find(first.as_ptr(), 5) }.is_null());
    native(|n| assert_eq!(n.calls.iter().filter(|c| **c == "delete").count(), 1));
    assert_eq!(errors(&mut second), 0);
    release(m, first);
    release(m, second);
    release(m, other);
    assert_eq!(*f.bytes, 0);
    assert!(f.allocator.live.is_empty());
}

/// A buffer draw record: position from buffer 1 (FLOAT 2, stride 16 at 8) and
/// colour from a client section (UNSIGNED_BYTE 4), `count` vertices or indices.
fn record(
    index: u32,
    element: u32,
    offset: u32,
    base: u32,
    indices: &[u16],
) -> ([u32; 10], Vec<u8>) {
    let vertices = if index == 0 {
        indices.len() as u32
    } else {
        indices.iter().max().map_or(0, |&m| u32::from(m) + 1) - base
    };
    let count = indices.len() as u32;
    let a = [
        GL_TRIANGLES,
        base,
        count,
        DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR,
        0,
        1,
        vertices,
        index,
        element,
        offset,
    ];
    let mut data = vec![0u8; 128];
    data[..4].copy_from_slice(&(GL_FLOAT | (2 << 16)).to_le_bytes());
    data[4..8].copy_from_slice(&(GL_UNSIGNED_BYTE | (4 << 16)).to_le_bytes());
    for word in [1u32, 8, 16] {
        data.extend_from_slice(&word.to_le_bytes());
    }
    data.extend((0..vertices * 4).map(|v| v as u8));
    if index != 0 && element == 0 {
        for i in indices {
            data.extend_from_slice(&i.to_le_bytes());
        }
    }
    data.resize((data.len() + 3) & !3, 0);
    (a, data)
}

#[test]
fn draws_resolve_natives_and_bound_every_buffer_read() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    // Buffer 1: four vertices at stride 16 from offset 8 need 8 + 3*16 + 8.
    put(m, &mut c, 1, 64, &[]);
    // Buffer 2: indices 0, 3, 1 at offset 2.
    let mut indices = vec![0u8; 2];
    for i in [0u16, 3, 1] {
        indices.extend_from_slice(&i.to_le_bytes());
    }
    put(m, &mut c, 2, 8, &indices);
    let natives = |c: &MaybeUninit<ContextState>, name| unsafe { (*find(c.as_ptr(), name)).native };
    let resolve = |c: &MaybeUninit<ContextState>, a: &[u32; 10], data: &[u8]| {
        let layout = crate::arrays::raw::layout_buffer(a, data, true).expect("admitted");
        unsafe { super::resolve(c.as_ptr(), a, &layout, data) }
    };
    // An array draw of vertices 0-3 fits; from vertex 1 the position slot is
    // unread (no error: the draw leaves its array disabled).
    let (a, data) = record(0, 0, 0, 0, &[0; 4]);
    let r = resolve(&c, &a, &data).unwrap();
    assert_eq!(
        (r.slots[0], r.slots[1], r.element, r.unread),
        (natives(&c, 1), 0, 0, 0)
    );
    let (a, data) = record(0, 0, 0, 1, &[0; 4]);
    assert_eq!(resolve(&c, &a, &data).map(|r| r.unread), Ok(1));
    // Inline indices: the largest index bounds the buffer read.
    let (a, data) = record(GL_UNSIGNED_SHORT, 0, 0, 0, &[0, 3, 2]);
    assert_eq!(resolve(&c, &a, &data).map(|r| r.unread), Ok(0));
    let (a, data) = record(GL_UNSIGNED_SHORT, 0, 0, 0, &[0, 4, 2]);
    assert_eq!(resolve(&c, &a, &data).map(|r| r.unread), Ok(1));
    // Element buffer indices (0, 3, 1) with `vertices` client vertices from `base`.
    let element = |offset: u32, vertices: u32, base: u32| {
        let (mut a, mut data) = record(GL_UNSIGNED_SHORT, 2, offset, 0, &[0, 0, 0]);
        a[1] = base;
        a[6] = vertices;
        data.truncate(128 + 12);
        data.extend((0..vertices * 4).map(|v| v as u8));
        data.resize((data.len() + 3) & !3, 0);
        (a, data)
    };
    let (a, data) = element(2, 4, 0);
    let r = resolve(&c, &a, &data).unwrap();
    assert_eq!((r.slots[0], r.element), (natives(&c, 1), natives(&c, 2)));
    // The element range must lie within its buffer; indices within the client
    // vertices; a missing buffer is an error.
    let (a, data) = element(4, 4, 0);
    assert_eq!(resolve(&c, &a, &data).err(), Some(GL_INVALID_OPERATION));
    for (vertices, base) in [(3, 0), (4, 1)] {
        let (a, data) = element(2, vertices, base);
        assert_eq!(
            resolve(&c, &a, &data).err(),
            Some(GL_INVALID_OPERATION),
            "{vertices} {base}"
        );
    }
    let (mut a, data) = element(2, 4, 0);
    a[8] = 9;
    assert_eq!(resolve(&c, &a, &data).err(), Some(GL_INVALID_OPERATION));
    let (a, mut data) = record(0, 0, 0, 0, &[0; 4]);
    data[128..132].copy_from_slice(&3u32.to_le_bytes());
    assert_eq!(resolve(&c, &a, &data).err(), Some(GL_INVALID_OPERATION));
    // A shrunken attribute buffer is checked against its current size.
    put(m, &mut c, 1, 63, &[]);
    let (a, data) = record(0, 0, 0, 0, &[0; 4]);
    assert_eq!(resolve(&c, &a, &data).map(|r| r.unread), Ok(1));
    assert_eq!(errors(&mut c), 0);
    release(m, c);
}

#[test]
fn index_bounds_only_rise_and_let_fitting_buffer_only_draws_skip_the_scan() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    let bounds = |c: &MaybeUninit<ContextState>, name| unsafe { (*find(c.as_ptr(), name)).bounds };
    // Bytes 00 01 02 03 05 00 ff 00: u8 max 255, u16 max 0x0302, u32 max 0x03020100.
    put(m, &mut c, 2, 8, &[0, 1, 2, 3, 5, 0, 255, 0]);
    assert_eq!(bounds(&c, 2), [255, 0x0302, 0x0302_0100]);
    // A subdata of one odd byte raises the values it overlaps; lower data never lowers.
    sub(m, &mut c, 2, 5, &[7]);
    assert_eq!(bounds(&c, 2), [255, 0x0705, 0x0302_0100]);
    sub(m, &mut c, 2, 0, &[0; 8]);
    assert_eq!(bounds(&c, 2), [255, 0x0705, 0x0302_0100]);
    put(m, &mut c, 2, 4, &[]);
    assert_eq!(bounds(&c, 2), [0; 3]);
    // Buffer 1: positions FLOAT 2 for vertices 0-3 (stride 8); buffer 3:
    // UNSIGNED_SHORT indices 0, 3, 1 then 9 (beyond) at offset 6.
    put(m, &mut c, 1, 32, &[]);
    let indices = [0u16, 3, 1, 9].map(u16::to_le_bytes).concat();
    put(m, &mut c, 3, 8, &indices);
    let mut data = vec![0u8; 128];
    data[..4].copy_from_slice(&(GL_FLOAT | (2 << 16)).to_le_bytes());
    for word in [1u32, 0, 8] {
        data.extend_from_slice(&word.to_le_bytes());
    }
    let resolve = |c: &MaybeUninit<ContextState>, count: u32, offset: u32| {
        let a = [
            GL_TRIANGLES,
            0,
            count,
            DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION,
            0,
            1,
            0,
            GL_UNSIGNED_SHORT,
            3,
            offset,
        ];
        let layout = crate::arrays::raw::layout_buffer(&a, &data, true).expect("admitted");
        unsafe { super::resolve(c.as_ptr(), &a, &layout, &data) }
    };
    // The bound (9) does not fit four vertices, so the first three indices
    // are scanned and fit; the fourth is beyond, leaving position unread.
    assert_eq!(resolve(&c, 3, 0).map(|r| r.unread), Ok(0));
    assert_eq!(resolve(&c, 1, 6).map(|r| r.unread), Ok(1));
    // With room for ten vertices the bound fits and no scan is needed.
    put(m, &mut c, 1, 80, &[]);
    assert_eq!(
        resolve(&c, 4, 0).map(|r| r.element).ok(),
        Some(unsafe { (*find(c.as_ptr(), 3)).native })
    );
    // The element range still has to lie within its buffer.
    assert_eq!(resolve(&c, 4, 2).err(), Some(GL_INVALID_OPERATION));
    release(m, c);
}

#[test]
fn bgra_buffer_slots_are_bounded_as_four_byte_elements() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    // Vertices 5-7: position FLOAT 3 at 8 + v * 20 in buffer 4; colour and
    // secondary colour BGRA at 12 and 16 + v * 20 in buffer 5; generic 1 BGRA
    // tight in buffer 6.
    put(m, &mut c, 4, 160, &[]);
    put(m, &mut c, 5, 160, &[]);
    put(m, &mut c, 6, 32, &[]);
    let (a, data) = crate::arrays::raw::tests::bgra_packet();
    let resolve = |c: &MaybeUninit<ContextState>| {
        let layout =
            crate::arrays::raw::layout_buffer_draw(FEnum_glDrawRangeElementsEXT, &a, &data, true)
                .expect("admitted");
        unsafe { super::resolve(c.as_ptr(), &a, &layout, &data) }
    };
    let natives = |c: &MaybeUninit<ContextState>, name| unsafe { (*find(c.as_ptr(), name)).native };
    let r = resolve(&c).unwrap();
    assert_eq!(
        [r.slots[0], r.slots[1], r.slots[4], r.slots[17]],
        [
            natives(&c, 4),
            natives(&c, 5),
            natives(&c, 5),
            natives(&c, 6)
        ]
    );
    // One byte short of the last BGRA element leaves that slot unread: in
    // buffer 5 the secondary colour (at 16) ends last, the colour fits.
    assert_eq!(resolve(&c).map(|r| r.unread), Ok(0));
    put(m, &mut c, 5, 159, &[]);
    assert_eq!(resolve(&c).map(|r| r.unread), Ok(1 << 4));
    put(m, &mut c, 5, 160, &[]);
    put(m, &mut c, 6, 31, &[]);
    assert_eq!(resolve(&c).map(|r| r.unread), Ok(1 << 17));
    put(m, &mut c, 6, 32, &[]);
    assert_eq!(resolve(&c).map(|r| r.unread), Ok(0));
    assert_eq!(errors(&mut c), 0);
    release(m, c);
}
