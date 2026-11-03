use super::*;
use crate::state::{dreamgpu_context_state_init, dreamgpu_context_state_release};
use crate::texture::Texture;
use core::ffi::c_void;
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
    collections::{BTreeMap, BTreeSet, VecDeque},
    mem::MaybeUninit,
};

#[derive(Default)]
struct Allocator {
    live: BTreeMap<usize, Layout>,
    calls: usize,
    fail: usize,
}
unsafe extern "C" fn allocate(p: *mut c_void, bytes: usize) -> *mut c_void {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    a.calls += 1;
    if a.calls == a.fail {
        return null_mut();
    }
    let layout = Layout::from_size_align(bytes.max(1), 8).unwrap();
    let p = unsafe { alloc(layout) };
    assert!(!p.is_null());
    // Deliberately not zero initialized; production initialization must own writes.
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

/// A native ARB program model: names, per-name strings and locals, one
/// binding, an error queue and the error position/string.
#[derive(Default)]
struct Native {
    next: u32,
    live: BTreeSet<u32>,
    deleted: Vec<u32>,
    bound: u32,
    strings: BTreeMap<u32, Vec<u8>>,
    loads: Vec<(u32, Vec<u8>)>,
    locals: BTreeMap<(u32, u32), [f32; 4]>,
    errors: VecDeque<u32>,
    position: i32,
    message: Vec<u8>,
    units: i32,
    coords: i32,
    images: i32,
    gen_fails: bool,
}
std::thread_local! {static N: RefCell<Native> = RefCell::new(Native {
    position: -1, message: b"\0".to_vec(), units: 8, coords: 8, images: 32, ..Native::default()
});}
fn native<R>(f: impl FnOnce(&mut Native) -> R) -> R {
    N.with(|n| f(&mut n.borrow_mut()))
}
unsafe extern "C" fn fake_get_error() -> u32 {
    native(|n| n.errors.pop_front().unwrap_or(0))
}
unsafe extern "C" fn fake_gen(count: i32, out: *mut u32) {
    assert_eq!(count, 1);
    native(|n| {
        if n.gen_fails {
            unsafe { *out = 0 };
            return;
        }
        n.next += 1;
        n.live.insert(n.next);
        unsafe { *out = n.next };
    })
}
unsafe extern "C" fn fake_delete(count: i32, names: *const u32) {
    assert_eq!(count, 1);
    let name = unsafe { *names };
    native(|n| {
        assert!(n.live.remove(&name), "native double delete {name}");
        n.deleted.push(name);
        if n.bound == name {
            n.bound = 0;
        }
    })
}
unsafe extern "C" fn fake_bind(target: u32, name: u32) {
    assert_eq!(target, GL_FRAGMENT_PROGRAM_ARB);
    native(|n| {
        assert!(
            name == 0 || n.live.contains(&name),
            "bind of a dead name {name}"
        );
        n.bound = name;
    })
}
/// Accepts "!!ARBfp1.0 ... END" without '@'; anything else fails at the first '@'.
unsafe extern "C" fn fake_load(target: u32, format: u32, length: i32, string: *const c_void) {
    assert_eq!(
        (target, format),
        (GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB)
    );
    let bytes =
        unsafe { core::slice::from_raw_parts(string.cast::<u8>(), length as usize) }.to_vec();
    native(|n| {
        n.loads.push((n.bound, bytes.clone()));
        if bytes.starts_with(b"!!ARBfp1.0") && bytes.ends_with(b"END") && !bytes.contains(&b'@') {
            n.position = -1;
            n.message = b"\0".to_vec();
            n.strings.insert(n.bound, bytes);
        } else {
            n.position = bytes.iter().position(|&b| b == b'@').unwrap_or(bytes.len()) as i32;
            n.message = b"line 1:\nsyntax error \xff\0".to_vec();
            n.errors.push_back(GL_INVALID_OPERATION);
        }
    })
}
unsafe extern "C" fn fake_local(target: u32, index: u32, x: f32, y: f32, z: f32, w: f32) {
    assert_eq!(target, GL_FRAGMENT_PROGRAM_ARB);
    native(|n| {
        let bound = n.bound;
        n.locals.insert((bound, index), [x, y, z, w]);
    })
}
unsafe extern "C" fn fake_get_integer(pname: u32, out: *mut i32) {
    native(|n| unsafe {
        *out = match pname {
            GL_MAX_TEXTURE_UNITS => n.units,
            GL_MAX_TEXTURE_COORDS_ARB => n.coords,
            GL_MAX_TEXTURE_IMAGE_UNITS_ARB => n.images,
            GL_PROGRAM_ERROR_POSITION_ARB => n.position,
            other => panic!("unexpected integer {other:#x}"),
        }
    })
}
unsafe extern "C" fn fake_get_program(target: u32, pname: u32, out: *mut i32) {
    assert_eq!(target, GL_FRAGMENT_PROGRAM_ARB);
    native(|n| unsafe {
        *out = match pname {
            GL_PROGRAM_LENGTH_ARB => n.strings.get(&n.bound).map_or(0, Vec::len) as i32,
            _ => 7,
        }
    })
}
unsafe extern "C" fn fake_get_program_string(target: u32, pname: u32, out: *mut c_void) {
    assert_eq!(
        (target, pname),
        (GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_STRING_ARB)
    );
    native(|n| match n.strings.get(&n.bound) {
        Some(s) => unsafe { core::ptr::copy_nonoverlapping(s.as_ptr(), out.cast(), s.len()) },
        // Like Mesa, an empty string writes one terminator.
        None => unsafe { *out.cast::<u8>() = 0 },
    })
}
unsafe extern "C" fn fake_get_string(name: u32) -> *const u8 {
    assert_eq!(name, GL_PROGRAM_ERROR_STRING_ARB);
    native(|n| n.message.as_ptr())
}

fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGetError = Some(fake_get_error);
    a.dg_glGenProgramsARB = Some(fake_gen);
    a.dg_glDeleteProgramsARB = Some(fake_delete);
    a.dg_glBindProgramARB = Some(fake_bind);
    a.dg_glProgramStringARB = Some(fake_load);
    a.dg_glProgramLocalParameter4fARB = Some(fake_local);
    a.dg_glGetIntegerv = Some(fake_get_integer);
    a.dg_glGetProgramivARB = Some(fake_get_program);
    a.dg_glGetProgramStringARB = Some(fake_get_program_string);
    a.dg_glGetString = Some(fake_get_string);
    a
}
struct Fixture {
    _api: Box<DreamGpuGlApi>,
    allocator: Box<Allocator>,
    count: Box<u32>,
    bytes: Box<u64>,
    images: Box<u64>,
    memory: Memory,
}
fn fixture() -> Fixture {
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
        count,
        bytes,
        images,
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
fn take_errors(c: *mut ContextState) -> u32 {
    unsafe { core::mem::take(&mut (*c).guest_errors) }
}
const OOM: u32 = 1 << (GL_OUT_OF_MEMORY - GL_INVALID_ENUM);
const INVALID_OPERATION: u32 = 1 << (GL_INVALID_OPERATION - GL_INVALID_ENUM);
const FP: u32 = GL_FRAGMENT_PROGRAM_ARB;
fn args(length: u32) -> [u32; 8] {
    [FP, GL_PROGRAM_FORMAT_ASCII_ARB, length, 0, 0, 0, 0, 0]
}
fn load_string(m: &Memory, c: *mut ContextState, s: &[u8]) {
    unsafe { string(m, c, &args(s.len() as u32), s) }.unwrap();
}
fn reset_native() {
    native(|n| {
        *n = Native {
            position: -1,
            message: b"\0".to_vec(),
            units: 8,
            coords: 8,
            images: 32,
            ..Native::default()
        }
    });
}

#[test]
fn names_objects_and_default_bindings_follow_the_arb_object_model() {
    reset_native();
    let mut f = fixture();
    let m = &f.memory;
    let mut boxed = context(m, null_mut());
    let c = boxed.as_mut_ptr();
    // The default binding is native program zero and allocates nothing.
    unsafe { bind(m, c, FP, 0) }.unwrap();
    assert!(unsafe { (*(*c).textures).programs }.is_null());
    assert_eq!(native(|n| n.bound), 0);
    // Generated names are reserved, not objects; distinct and nonzero.
    let mut out = [0u8; 12];
    unsafe { generate(m, c, 3, out.as_mut_ptr()) };
    let names: Vec<u32> = out
        .as_chunks::<4>()
        .0
        .iter()
        .map(|w| u32::from_le_bytes(*w))
        .collect();
    assert_eq!(names, [1, 2, 3]);
    assert!(names.iter().all(|&n| !unsafe { is_program(c, n) }));
    assert!(native(|n| n.live.is_empty()));
    // First bind creates the object; rebinding reuses it.
    unsafe { bind(m, c, FP, 2) }.unwrap();
    let native_two = native(|n| n.bound);
    assert_ne!(native_two, 0);
    assert!(unsafe { is_program(c, 2) } && !unsafe { is_program(c, 1) });
    assert_eq!(unsafe { binding(c) }, 2);
    unsafe { bind(m, c, FP, 0) }.unwrap();
    unsafe { bind(m, c, FP, 2) }.unwrap();
    assert_eq!(native(|n| (n.bound, n.live.len())), (native_two, 1));
    // A manual (never generated) name is a valid object too.
    unsafe { bind(m, c, FP, 77) }.unwrap();
    assert!(unsafe { is_program(c, 77) });
    // The next generated name skips every used one.
    unsafe { generate(m, c, 1, out.as_mut_ptr()) };
    assert_eq!(u32::from_le_bytes(out[..4].try_into().unwrap()), 4);
    // A wrong-target object is a GL error, never a record fault.
    let ns = unsafe { (*(*c).textures).programs };
    let p = unsafe { (*ns).entries[locate(ns, 77).unwrap()].program };
    unsafe { (*p).target = GL_VERTEX_PROGRAM_ARB };
    assert_eq!(unsafe { bind(m, c, FP, 77) }, Ok(()));
    assert_eq!(take_errors(c), INVALID_OPERATION);
    assert_eq!(unsafe { binding(c) }, 77);
    unsafe { (*p).target = FP };
    // Deletion: reserved and unused names vanish, zero is ignored.
    let delete_names =
        |list: &[u32]| -> Vec<u8> { list.iter().flat_map(|n| n.to_le_bytes()).collect() };
    unsafe { delete(m, c, &delete_names(&[0, 1, 3, 4, 999])) }.unwrap();
    assert!(!unsafe { is_program(c, 1) });
    assert_eq!(unsafe { (*ns).count }, 2);
    // Deleting the bound object reverts this context to the default.
    unsafe { delete(m, c, &delete_names(&[77])) }.unwrap();
    assert_eq!((unsafe { binding(c) }, native(|n| n.bound)), (0, 0));
    assert!(unsafe { (*c).program }.is_null());
    assert_eq!(native(|n| n.deleted.len()), 1);
    release(m, boxed);
    assert_eq!(
        native(|n| n.live.len()),
        0,
        "namespace release deletes every object"
    );
    assert!(f.allocator.live.is_empty());
    assert_eq!((*f.bytes, *f.count, *f.images), (0, 0, 0));
    f.allocator.fail = 0;
}

#[test]
fn shared_namespaces_keep_deleted_programs_bound_in_other_contexts() {
    reset_native();
    let f = fixture();
    let m = &f.memory;
    let mut first = context(m, null_mut());
    let a = first.as_mut_ptr();
    let mut second = context(m, unsafe { (*a).textures });
    let b = second.as_mut_ptr();
    unsafe { bind(m, a, FP, 5) }.unwrap();
    let five = native(|n| n.bound);
    load_string(m, a, b"!!ARBfp1.0 MOV result.color, fragment.color; END");
    // The second context sees the shared object by name.
    assert!(unsafe { is_program(b, 5) });
    unsafe { bind(m, b, FP, 5) }.unwrap();
    assert_eq!(native(|n| n.bound), five);
    // Deleted in the first context: the name is gone for both, but the second
    // context's binding (and charge) survive until it rebinds.
    unsafe { delete(m, a, &5u32.to_le_bytes()) }.unwrap();
    assert!(!unsafe { is_program(a, 5) } && !unsafe { is_program(b, 5) });
    assert_eq!(unsafe { binding(b) }, 5);
    let p = unsafe { (*b).program };
    assert_eq!(unsafe { ((*p).refs, (*p).deleted) }, (1, 1));
    let charged = *f.bytes;
    assert!(charged > OBJECT_BYTES);
    unsafe { bind(m, b, FP, 0) }.unwrap();
    assert!(*f.bytes < charged);
    release(m, second);
    release(m, first);
    assert_eq!(*f.bytes, 0);
    assert!(f.allocator.live.is_empty());
    // The program once; the namespace's private validation object once.
    assert_eq!(
        native(|n| n.deleted.iter().filter(|&&d| d == five).count()),
        1
    );
    assert_eq!(native(|n| (n.deleted.len(), n.live.len())), (2, 0));
}

#[test]
fn loads_validate_in_a_private_object_and_never_disturb_the_bound_program() {
    reset_native();
    let f = fixture();
    let m = &f.memory;
    let mut boxed = context(m, null_mut());
    let c = boxed.as_mut_ptr();
    unsafe { bind(m, c, FP, 9) }.unwrap();
    let nine = native(|n| n.bound);
    let good = b"!!ARBfp1.0 TEX result.color, fragment.texcoord, texture[7], 2D; END";
    load_string(m, c, good);
    // Parsed first in the private object, then in the bound one.
    let ns = unsafe { (*(*c).textures).programs };
    let scratch = unsafe { (*ns).scratch };
    assert_ne!(scratch, 0);
    assert_eq!(
        native(|n| n.loads.iter().map(|(name, _)| *name).collect::<Vec<_>>()),
        [scratch, nine]
    );
    assert_eq!(
        native(|n| (n.bound, n.strings[&nine].clone())),
        (nine, good.to_vec())
    );
    assert_eq!(take_errors(c), 0);
    assert_eq!(unsafe { (*(*c).program).length }, good.len() as u32);
    // A native parse error leaves the bound program untouched and rebound.
    native(|n| n.loads.clear());
    load_string(m, c, b"!!ARBfp1.0 @YZ END");
    assert_eq!(take_errors(c), INVALID_OPERATION);
    assert_eq!(native(|n| (n.loads.len(), n.bound)), (1, nine));
    assert_eq!(native(|n| n.strings[&nine].clone()), good.to_vec());
    assert_eq!(unsafe { error_position(&*m.api, c) }, Ok(11));
    // One printable string ending at its terminator, as the guest drivers'
    // STRING validation (guest/nt/include/dg-gl-result.h) requires.
    let mut text = [0xccu8; 64];
    assert_eq!(
        unsafe { error_string(&*m.api, c, text.as_mut_ptr(), 64) },
        Ok(23)
    );
    assert_eq!(&text[..23], b"line 1: syntax error ?\0");
    assert!(text[23..].iter().all(|&b| b == 0xcc));
    // Truncation keeps the terminator inside the capacity.
    assert_eq!(
        unsafe { error_string(&*m.api, c, text.as_mut_ptr(), 5) },
        Ok(5)
    );
    assert_eq!(&text[..5], b"line\0");
    // Host-detected failures never reach native GL and override its state.
    for (bytes, kind, at) in [
        (b"!!ARBfp1.0 \x80 END".to_vec(), ERROR_ASCII, 11),
        (b"!!ARBfp1.0 \0 END".to_vec(), ERROR_ASCII, 11),
        (
            b"!!ARBfp1.0 TEX r, f, texture [ 8 ], 2D; END".to_vec(),
            ERROR_UNITS,
            31,
        ),
        (
            b"!!ARBfp1.0 MOV r, fragment.texcoord[9]; END".to_vec(),
            ERROR_UNITS,
            36,
        ),
    ] {
        native(|n| n.loads.clear());
        load_string(m, c, &bytes);
        assert_eq!(take_errors(c), INVALID_OPERATION);
        assert_eq!(native(|n| n.loads.len()), 0);
        assert_eq!(
            unsafe { ((*c).program_error_kind, error_position(&*m.api, c)) },
            (kind, Ok(at))
        );
        let n = unsafe { error_string(&*m.api, c, text.as_mut_ptr(), 64) }.unwrap() as usize;
        assert!(text.starts_with(b"DreamGPU: ") && (2..=64).contains(&n) && text[n - 1] == 0);
        assert!(text[..n - 1].iter().all(|b| (32..=126).contains(b)));
    }
    // An untransferred string fails at its length.
    unsafe { string(m, c, &args(70000), &[]) }.unwrap();
    assert_eq!(take_errors(c), INVALID_OPERATION);
    assert_eq!(unsafe { error_position(&*m.api, c) }, Ok(70000));
    // The next native load returns position/string ownership to native GL.
    load_string(m, c, good);
    assert_eq!(
        unsafe { ((*c).program_error_kind, error_position(&*m.api, c)) },
        (0, Ok(-1))
    );
    // No message is the empty string: its terminator alone.
    assert_eq!(
        unsafe { error_string(&*m.api, c, text.as_mut_ptr(), 64) },
        Ok(1)
    );
    assert_eq!(text[0], 0);
    // Reading the string back in word windows, zero padded.
    let mut out = [0xccu8; 16];
    unsafe { read_string(m, FP, 4, 1, out.as_mut_ptr()) }.unwrap();
    assert_eq!(&out, &good[4..20]);
    assert_eq!(good.len(), 67);
    unsafe { read_string(m, FP, 4, 16, out.as_mut_ptr()) }.unwrap();
    assert_eq!(&out[..3], &good[64..]);
    assert!(out[3..].iter().all(|&b| b == 0));
    unsafe { read_string(m, FP, 4, 17, out.as_mut_ptr()) }.unwrap();
    assert_eq!(out, [0; 16]);
    release(m, boxed);
    assert_eq!(*f.bytes, 0);
    assert!(f.allocator.live.is_empty());
}

#[test]
fn the_default_object_is_private_to_its_share_group_and_materialized_once() {
    reset_native();
    let f = fixture();
    let m = &f.memory;
    let mut one = context(m, null_mut());
    let a = one.as_mut_ptr();
    let mut two = context(m, null_mut());
    let b = two.as_mut_ptr();
    // Loading with name zero bound creates a private default object.
    load_string(m, a, b"!!ARBfp1.0 END");
    let default = native(|n| n.bound);
    assert_ne!(default, 0);
    assert_eq!(unsafe { binding(a) }, 0);
    unsafe { local(m, a, FP, 3, [1., 2., 3., 4.]) }.unwrap();
    assert_eq!(native(|n| n.locals[&(default, 3)]), [1., 2., 3., 4.]);
    unsafe { bind(m, a, FP, 0) }.unwrap();
    assert_eq!(native(|n| n.bound), default);
    // Another share group's default is separate.
    unsafe { local(m, b, FP, 3, [5., 6., 7., 8.]) }.unwrap();
    let other = native(|n| n.bound);
    assert!(other != default && other != 0);
    assert_eq!(native(|n| n.locals[&(default, 3)]), [1., 2., 3., 4.]);
    release(m, two);
    release(m, one);
    assert_eq!((*f.bytes, native(|n| n.live.len())), (0, 0));
    assert!(f.allocator.live.is_empty());
}

#[test]
fn exhausted_storage_is_out_of_memory_and_charges_balance() {
    reset_native();
    let mut f = fixture();
    let mut boxed = context(&f.memory, null_mut());
    let c = boxed.as_mut_ptr();
    // The namespace table charge does not fit.
    *f.bytes = u64::from(DG_GL_MAX_TEXTURE_BYTES) - 10;
    unsafe { bind(&f.memory, c, FP, 1) }.unwrap();
    assert_eq!(take_errors(c), OOM);
    assert!(unsafe { (*(*c).textures).programs }.is_null());
    let mut out = [0xccu8; 8];
    unsafe { generate(&f.memory, c, 2, out.as_mut_ptr()) };
    assert_eq!((take_errors(c), out), (OOM, [0; 8]));
    *f.bytes = 0;
    // A failed native name, then a failed allocation: no object, no charge.
    native(|n| n.gen_fails = true);
    unsafe { bind(&f.memory, c, FP, 1) }.unwrap();
    assert_eq!(take_errors(c), OOM);
    native(|n| n.gen_fails = false);
    let table = *f.bytes;
    f.allocator.fail = f.allocator.calls + 1;
    unsafe { bind(&f.memory, c, FP, 1) }.unwrap();
    assert_eq!((take_errors(c), *f.bytes), (OOM, table));
    assert!(!unsafe { is_program(c, 1) });
    // A string beyond the remaining budget is refused before native GL.
    unsafe { bind(&f.memory, c, FP, 1) }.unwrap();
    let before = *f.bytes;
    *f.bytes = u64::from(DG_GL_MAX_TEXTURE_BYTES) - 4;
    native(|n| n.loads.clear());
    load_string(&f.memory, c, b"!!ARBfp1.0 END");
    assert_eq!((take_errors(c), native(|n| n.loads.len())), (OOM, 0));
    *f.bytes = before;
    // Replacing a string recharges the difference; a failed load restores it.
    let longer = b"!!ARBfp1.0 MOV result.color, fragment.color; END";
    load_string(&f.memory, c, longer);
    assert_eq!(*f.bytes, before + longer.len() as u64);
    load_string(
        &f.memory,
        c,
        b"!!ARBfp1.0 @ END and a much longer failing string",
    );
    assert_eq!(
        (take_errors(c), *f.bytes),
        (INVALID_OPERATION, before + longer.len() as u64)
    );
    load_string(&f.memory, c, b"!!ARBfp1.0 END");
    assert_eq!(*f.bytes, before + 14);
    // The namespace's name limit.
    let ns = unsafe { (*(*c).textures).programs };
    unsafe { (*ns).count = NAMES };
    unsafe { bind(&f.memory, c, FP, 2) }.unwrap();
    assert_eq!(take_errors(c), OOM);
    unsafe { generate(&f.memory, c, 1, out.as_mut_ptr()) };
    assert_eq!(take_errors(c), OOM);
    unsafe { (*ns).count = 1 };
    release(&f.memory, boxed);
    assert_eq!(*f.bytes, 0);
    assert!(f.allocator.live.is_empty());
}

#[test]
fn texture_limits_are_clamped_to_the_logical_units() {
    reset_native();
    let f = fixture();
    let mut boxed = context(&f.memory, null_mut());
    let c = boxed.as_mut_ptr();
    let a = unsafe { &*f.memory.api };
    assert_eq!(unsafe { limits(a, c) }, Ok((8, 8)));
    native(|n| (n.units, n.coords, n.images) = (16, 4, 6));
    unsafe { (*c).unit_limit = 0 };
    assert_eq!(unsafe { limits(a, c) }, Ok((4, 6)));
    native(|n| (n.units, n.coords, n.images) = (2, 0, 32));
    unsafe { (*c).unit_limit = 0 };
    assert_eq!(unsafe { limits(a, c) }, Ok((1, 2)));
    release(&f.memory, boxed);
}

#[test]
fn unit_scanning_finds_only_real_references_beyond_the_limits() {
    let scan = |s: &[u8]| unit_beyond(s, 8, 8);
    assert_eq!(scan(b"TEX r, f, texture[7], 2D;"), None);
    assert_eq!(scan(b"TEX r, f, texture[8], 2D;"), Some(18));
    assert_eq!(scan(b"TEX r, f, texture, 2D;"), None);
    assert_eq!(scan(b"TEX r,f,texture  #c\n [ 12 ],2D;"), Some(23));
    assert_eq!(scan(b"# texture[9]\nTEX r, f, texture[1], 2D;"), None);
    assert_eq!(scan(b"TEX r, f, mytexture[9], texture_9[9];"), None);
    assert_eq!(scan(b"PARAM m[] = { state.matrix.texture[7] };"), None);
    assert_eq!(
        scan(b"PARAM m[] = { state.matrix . texture[8] };"),
        Some(37)
    );
    assert_eq!(scan(b"ATTRIB t = fragment.texcoord[8];"), Some(29));
    assert_eq!(scan(b"ATTRIB t = fragment.texcoord;"), None);
    assert_eq!(
        scan(b"MOV r, 1.5e2; texcoord[9]"),
        None,
        "a bare texcoord is not a set"
    );
    assert_eq!(scan(b"TEX r, f, texture[99999999999999], 2D;"), Some(18));
    assert_eq!(unit_beyond(b"texture[3] x.texcoord[2]", 3, 4), None);
    assert_eq!(unit_beyond(b"texture[3] x.texcoord[2]", 2, 4), Some(22));
    assert_eq!(unit_beyond(b"texture[3] x.texcoord[2]", 3, 3), Some(8));
    assert_eq!(scan(b"texture["), None);
    assert_eq!(non_ascii(b"abc\x7f"), None);
    assert_eq!(non_ascii(b"ab\0c"), Some(2));
    assert_eq!(non_ascii(b"abc\xc3"), Some(3));
}

#[test]
fn record_shapes_follow_the_protocol() {
    use crate::gl_validation::*;
    let validate = |f: u32, a: &[u32], data: &[u8]| {
        let mut words = [0u8; 32];
        for (i, w) in a.iter().enumerate() {
            words[i * 4..i * 4 + 4].copy_from_slice(&w.to_le_bytes());
        }
        unsafe { dreamgpu_gl_data_validate(f, words.as_ptr(), data.as_ptr(), data.len() as u32) }
    };
    let call = |f: u32, a: &[u32]| {
        let words: Vec<u8> = a.iter().flat_map(|w| w.to_le_bytes()).collect();
        unsafe { dreamgpu_gl_call_validate(f, words.as_ptr()) }
    };
    let query = |f: u32, a: [u32; 3]| {
        let words: Vec<u8> = a.iter().flat_map(|w| w.to_le_bytes()).collect();
        let mut kind = 0;
        let count = unsafe { dreamgpu_gl_query_shape(f, words.as_ptr(), &mut kind) };
        (
            unsafe { dreamgpu_gl_query_validate(f, words.as_ptr()) },
            count,
            kind,
        )
    };
    assert_eq!(dreamgpu_gl_function_words(FEnum_glBindProgramARB), 2);
    assert_eq!(
        dreamgpu_gl_function_words(FEnum_glProgramEnvParameter4fARB),
        6
    );
    assert_eq!(
        dreamgpu_gl_function_words(FEnum_glProgramLocalParameter4fARB),
        6
    );
    assert_eq!(
        dreamgpu_gl_function_words(FEnum_glProgramStringARB),
        DG_GL_FUNCTION_INLINE_DATA | 3
    );
    assert_eq!(
        dreamgpu_gl_function_words(FEnum_glDeleteProgramsARB),
        DG_GL_FUNCTION_INLINE_DATA | 1
    );
    assert_eq!(
        dreamgpu_gl_function_words(FEnum_glGetProgramStringARB),
        DG_GL_FUNCTION_QUERY | 3
    );
    // Double, vector and vertex-program entry points never reach the host.
    for f in [
        FEnum_glProgramEnvParameter4dARB,
        FEnum_glProgramEnvParameter4fvARB,
        FEnum_glProgramLocalParameter4dvARB,
        FEnum_glGetProgramEnvParameterdvARB,
    ] {
        assert_eq!(dreamgpu_gl_function_words(f), u32::MAX);
    }
    for f in [
        FEnum_glBindProgramARB,
        FEnum_glProgramEnvParameter4fARB,
        FEnum_glProgramLocalParameter4fARB,
    ] {
        assert_eq!(call(f, &[FP, 1, 0, 0, 0, 0]), 0);
        assert_eq!(
            call(f, &[GL_VERTEX_PROGRAM_ARB, 1, 0, 0, 0, 0]),
            DG_GL_ERROR_UNSUPPORTED
        );
    }
    let fmt = GL_PROGRAM_FORMAT_ASCII_ARB;
    assert_eq!(validate(FEnum_glProgramStringARB, &[FP, fmt, 3], b"abc"), 0);
    assert_eq!(validate(FEnum_glProgramStringARB, &[FP, fmt, 0], b""), 0);
    // An untransferred nonzero length; the host reports it as a GL error.
    assert_eq!(
        validate(FEnum_glProgramStringARB, &[FP, fmt, 70000], b""),
        0
    );
    assert_eq!(
        validate(FEnum_glProgramStringARB, &[FP, fmt, 4], b"abc"),
        DG_GL_ERROR_BATCH
    );
    assert_eq!(
        validate(FEnum_glProgramStringARB, &[FP, 0, 3], b"abc"),
        DG_GL_ERROR_BATCH
    );
    assert_eq!(
        validate(
            FEnum_glProgramStringARB,
            &[GL_VERTEX_PROGRAM_ARB, fmt, 3],
            b"abc"
        ),
        DG_GL_ERROR_BATCH
    );
    assert_eq!(
        validate(FEnum_glProgramStringARB, &[FP, fmt, u32::MAX], b""),
        DG_GL_ERROR_BATCH
    );
    let long = vec![b' '; DG_GL_MAX_PROGRAM_BYTES as usize + 1];
    assert_eq!(
        validate(
            FEnum_glProgramStringARB,
            &[FP, fmt, long.len() as u32],
            &long
        ),
        DG_GL_ERROR_BATCH
    );
    assert_eq!(
        validate(
            FEnum_glProgramStringARB,
            &[FP, fmt, long.len() as u32 - 1],
            &long[1..]
        ),
        0
    );
    assert_eq!(validate(FEnum_glDeleteProgramsARB, &[2], &[0; 8]), 0);
    assert_eq!(
        validate(FEnum_glDeleteProgramsARB, &[2], &[0; 4]),
        DG_GL_ERROR_BATCH
    );
    assert_eq!(
        validate(
            FEnum_glDeleteProgramsARB,
            &[DG_GL_MAX_PROGRAMS + 1],
            &vec![0; 16388]
        ),
        DG_GL_ERROR_BATCH
    );
    assert_eq!(
        query(FEnum_glGenProgramsARB, [128, 0, 0]),
        (0, 128, DG_GL_RESULT_INT)
    );
    assert_eq!(
        query(FEnum_glGenProgramsARB, [129, 0, 0]).0,
        DG_GL_ERROR_UNSUPPORTED
    );
    assert_eq!(
        query(FEnum_glGenProgramsARB, [0, 0, 0]).0,
        DG_GL_ERROR_UNSUPPORTED
    );
    assert_eq!(
        query(FEnum_glIsProgramARB, [5, 0, 0]),
        (0, 1, DG_GL_RESULT_BOOL)
    );
    assert_eq!(query(FEnum_glIsProgramARB, [5, 1, 0]).0, DG_GL_ERROR_BATCH);
    assert_eq!(
        query(
            FEnum_glGetProgramivARB,
            [FP, GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB, 0]
        ),
        (0, 1, DG_GL_RESULT_INT)
    );
    for pname in [
        GL_PROGRAM_ADDRESS_REGISTERS_ARB,
        GL_MAX_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB,
        GL_PROGRAM_STRING_ARB,
    ] {
        assert_eq!(
            query(FEnum_glGetProgramivARB, [FP, pname, 0]).0,
            DG_GL_ERROR_UNSUPPORTED
        );
    }
    assert_eq!(
        query(
            FEnum_glGetProgramivARB,
            [GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_LENGTH_ARB, 0]
        )
        .0,
        DG_GL_ERROR_UNSUPPORTED
    );
    assert_eq!(
        query(FEnum_glGetProgramLocalParameterfvARB, [FP, 4000, 0]),
        (0, 4, DG_GL_RESULT_FLOAT)
    );
    assert_eq!(
        query(FEnum_glGetProgramStringARB, [FP, 16384, 16320]),
        (0, 16384, DG_GL_RESULT_INT)
    );
    assert_eq!(
        query(FEnum_glGetProgramStringARB, [FP, 16385, 0]).0,
        DG_GL_ERROR_UNSUPPORTED
    );
    assert_eq!(
        query(FEnum_glGetProgramStringARB, [FP, 1, 16321]).0,
        DG_GL_ERROR_UNSUPPORTED
    );
    assert_eq!(
        query(FEnum_glGetString, [GL_PROGRAM_ERROR_STRING_ARB, 0, 0]),
        (0, DG_GL_MAX_RESULT_BYTES, DG_GL_RESULT_STRING)
    );
    for pname in [
        GL_PROGRAM_ERROR_POSITION_ARB,
        GL_MAX_TEXTURE_COORDS_ARB,
        GL_MAX_TEXTURE_IMAGE_UNITS_ARB,
        GL_CURRENT_MATRIX_STACK_DEPTH_ARB,
        GL_MAX_PROGRAM_MATRICES_ARB,
        GL_MAX_PROGRAM_MATRIX_STACK_DEPTH_ARB,
        GL_FRAGMENT_PROGRAM_ARB,
    ] {
        assert_eq!(
            query(FEnum_glGetIntegerv, [pname, 0, 0]),
            (0, 1, DG_GL_RESULT_INT)
        );
    }
    assert_eq!(
        query(FEnum_glGetFloatv, [GL_CURRENT_MATRIX_ARB, 0, 0]),
        (0, 16, DG_GL_RESULT_FLOAT)
    );
    assert_eq!(
        query(FEnum_glIsEnabled, [GL_FRAGMENT_PROGRAM_ARB, 0, 0]),
        (0, 1, DG_GL_RESULT_BOOL)
    );
}
