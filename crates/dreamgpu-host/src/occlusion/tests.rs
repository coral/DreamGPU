use super::*;
use crate::query::{dreamgpu_gl_query, QueryState};
use crate::state::{
    dreamgpu_context_resource, dreamgpu_context_state_init, dreamgpu_context_state_release,
};
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
    collections::BTreeMap,
    mem::MaybeUninit,
};

#[derive(Default)]
struct Allocator {
    live: BTreeMap<usize, Layout>,
}
unsafe extern "C" fn allocate(p: *mut core::ffi::c_void, bytes: usize) -> *mut core::ffi::c_void {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    let layout = Layout::from_size_align(bytes.max(1), 8).unwrap();
    let p = unsafe { alloc(layout) };
    assert!(!p.is_null());
    // Deliberately not zero initialized; production initialization owns writes.
    unsafe { core::ptr::write_bytes(p, 0xaa, bytes) };
    a.live.insert(p as usize, layout);
    p.cast()
}
unsafe extern "C" fn free(p: *mut core::ffi::c_void, object: *mut core::ffi::c_void) {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    let layout = a
        .live
        .remove(&(object as usize))
        .expect("foreign/double free");
    unsafe { dealloc(object.cast(), layout) }
}
unsafe extern "C" fn forget(_: *mut core::ffi::c_void, _: *mut crate::texture::Texture) {}

/// One native query: the result it reports and the availability polls left
/// before it completes.
#[derive(Clone, Copy, Default)]
struct NativeQuery {
    result: u32,
    pending: u32,
    begun: bool,
}
/// A native query and sync model with exact lifetimes, in order completion
/// and counted flushes, waits and finishes.
#[derive(Default)]
struct Native {
    next: u32,
    queries: BTreeMap<u32, NativeQuery>,
    active: u32,
    /// What the next ended native query will report, and its polls.
    next_result: u32,
    next_pending: u32,
    syncs: BTreeMap<usize, u32>,
    next_sync: usize,
    flushes: u32,
    finishes: u32,
    /// (flags, timeout) of every ClientWaitSync.
    waits: Vec<(u32, u64)>,
    counter_bits: i32,
    fail_gen: bool,
    fail_sync: bool,
}
std::thread_local! {static N: RefCell<Native> = RefCell::new(Native::default());}
fn native<R>(f: impl FnOnce(&mut Native) -> R) -> R {
    N.with(|n| f(&mut n.borrow_mut()))
}
unsafe extern "C" fn gen_queries(count: i32, out: *mut u32) {
    assert_eq!(count, 1);
    native(|n| {
        if n.fail_gen {
            return;
        }
        n.next += 1;
        n.queries.insert(n.next, NativeQuery::default());
        unsafe { *out = n.next };
    })
}
unsafe extern "C" fn delete_queries(count: i32, names: *const u32) {
    assert_eq!(count, 1);
    let name = unsafe { *names };
    native(|n| {
        assert!(
            n.queries.remove(&name).is_some(),
            "native query double delete"
        );
        if n.active == name {
            n.active = 0;
        }
    })
}
unsafe extern "C" fn begin_query(target: u32, name: u32) {
    assert_eq!(target, GL_SAMPLES_PASSED);
    native(|n| {
        assert_eq!(n.active, 0, "native query already active");
        let q = n.queries.get_mut(&name).expect("native query exists");
        q.begun = true;
        n.active = name;
    })
}
unsafe extern "C" fn end_query(target: u32) {
    assert_eq!(target, GL_SAMPLES_PASSED);
    native(|n| {
        assert_ne!(n.active, 0, "native query not active");
        let (result, pending) = (n.next_result, n.next_pending);
        let q = n.queries.get_mut(&n.active).unwrap();
        q.result = result;
        q.pending = pending;
        n.active = 0;
    })
}
unsafe extern "C" fn get_queryiv(target: u32, pname: u32, out: *mut i32) {
    assert_eq!((target, pname), (GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS));
    native(|n| unsafe { *out = n.counter_bits })
}
unsafe extern "C" fn get_query_object(name: u32, pname: u32, out: *mut u32) {
    native(|n| {
        assert_ne!(n.active, name, "native read of an active query");
        let q = n.queries.get_mut(&name).expect("native query exists");
        assert!(q.begun);
        unsafe {
            match pname {
                GL_QUERY_RESULT_AVAILABLE => {
                    if q.pending == 0 {
                        *out = 1;
                    } else {
                        q.pending -= 1;
                        *out = 0;
                    }
                }
                GL_QUERY_RESULT => {
                    // A wait completes this query and every earlier one.
                    q.pending = 0;
                    *out = q.result;
                }
                _ => panic!("native query pname {pname:#x}"),
            }
        }
    })
}
unsafe extern "C" fn flush() {
    native(|n| n.flushes += 1)
}
unsafe extern "C" fn finish_all() {
    native(|n| {
        n.finishes += 1;
        for polls in n.syncs.values_mut() {
            *polls = 0;
        }
    })
}
unsafe extern "C" fn fence_sync(condition: u32, flags: u32) -> *mut __GLsync {
    assert_eq!((condition, flags), (GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
    native(|n| {
        if n.fail_sync {
            return null_mut();
        }
        n.next_sync += 16;
        n.syncs.insert(n.next_sync, n.next_pending);
        n.next_sync as *mut __GLsync
    })
}
unsafe extern "C" fn client_wait_sync(sync: *mut __GLsync, flags: u32, timeout: u64) -> u32 {
    native(|n| {
        n.waits.push((flags, timeout));
        let polls = n.syncs.get_mut(&(sync as usize)).expect("live native sync");
        if *polls == 0 {
            GL_ALREADY_SIGNALED
        } else if timeout != 0 {
            *polls -= 1;
            if *polls == 0 {
                GL_CONDITION_SATISFIED
            } else {
                GL_TIMEOUT_EXPIRED
            }
        } else {
            *polls -= 1;
            GL_TIMEOUT_EXPIRED
        }
    })
}
unsafe extern "C" fn delete_sync(sync: *mut __GLsync) {
    native(|n| {
        assert!(
            n.syncs.remove(&(sync as usize)).is_some(),
            "native sync double delete"
        )
    })
}
unsafe extern "C" fn get_error() -> u32 {
    0
}

fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGenQueries = Some(gen_queries);
    a.dg_glDeleteQueries = Some(delete_queries);
    a.dg_glBeginQuery = Some(begin_query);
    a.dg_glEndQuery = Some(end_query);
    a.dg_glGetQueryiv = Some(get_queryiv);
    a.dg_glGetQueryObjectuiv = Some(get_query_object);
    a.dg_glFenceSync = Some(fence_sync);
    a.dg_glClientWaitSync = Some(client_wait_sync);
    a.dg_glDeleteSync = Some(delete_sync);
    a.dg_glFlush = Some(flush);
    a.dg_glFinish = Some(finish_all);
    a.dg_glGetError = Some(get_error);
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
    native(|n| {
        *n = Native {
            counter_bits: 64,
            ..Native::default()
        }
    });
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
type Context = Box<MaybeUninit<ContextState>>;
fn context(m: &Memory) -> Context {
    let mut c = Box::<ContextState>::new_uninit();
    assert_eq!(
        unsafe { dreamgpu_context_state_init(m, c.as_mut_ptr(), null_mut()) },
        0
    );
    c
}
fn s(c: &mut Context) -> *mut ContextState {
    c.as_mut_ptr()
}
fn release(m: &Memory, mut c: Context) {
    unsafe { dreamgpu_context_state_release(m, c.as_mut_ptr()) };
}
fn errors(c: &mut Context) -> u32 {
    unsafe { core::mem::take(&mut (*s(c)).guest_errors) }
}
const INVALID_OPERATION: u32 = 1 << 2;
const OUT_OF_MEMORY: u32 = 1 << 5;

/// A CALL record, admitted by the batch validator, through the context's
/// resource dispatch.
fn call(m: &Memory, c: &mut Context, function: u32, args: &[u32]) -> u32 {
    let data: Vec<u8> = args.iter().flat_map(|v| v.to_le_bytes()).collect();
    assert_eq!(
        crate::gl_validation::dreamgpu_gl_function_words(function) as usize,
        args.len()
    );
    assert_eq!(
        unsafe { crate::gl_validation::dreamgpu_gl_call_validate(function, data.as_ptr()) },
        0
    );
    unsafe {
        dreamgpu_context_resource(
            m,
            s(c),
            1,
            function,
            data.as_ptr(),
            data.len() as u32,
            None,
            null_mut(),
        )
    }
}
fn begin(m: &Memory, c: &mut Context, id: u32) {
    assert_eq!(call(m, c, FEnum_glBeginQuery, &[GL_SAMPLES_PASSED, id]), 0);
}
fn end(m: &Memory, c: &mut Context) {
    assert_eq!(call(m, c, FEnum_glEndQuery, &[GL_SAMPLES_PASSED]), 0);
}
fn set_fence(m: &Memory, c: &mut Context, fence: u32) {
    assert_eq!(
        call(m, c, FEnum_glSetFenceNV, &[fence, ALL_COMPLETED_NV]),
        0
    );
}
fn delete(m: &Memory, c: &mut Context, function: u32, names: &[u32]) {
    let args = (names.len() as u32).to_le_bytes();
    let payload: Vec<u8> = names.iter().flat_map(|v| v.to_le_bytes()).collect();
    assert_eq!(
        unsafe {
            crate::gl_validation::dreamgpu_gl_data_validate(
                function,
                args.as_ptr(),
                payload.as_ptr(),
                payload.len() as u32,
            )
        },
        0
    );
    assert_eq!(
        unsafe {
            crate::data::dreamgpu_gl_data(
                m,
                s(c),
                0,
                1,
                function,
                args.as_ptr(),
                payload.as_ptr(),
                payload.len() as u32,
            )
        },
        0
    );
}
/// A QUERY record, admitted by the batch validator, into exactly the
/// capacity the device allocates: (kind, words or bytes).
fn query(m: &Memory, c: &mut Context, function: u32, args: [u32; 3]) -> (u32, Vec<u32>) {
    let raw: Vec<u8> = args.iter().flat_map(|v| v.to_le_bytes()).collect();
    assert_eq!(
        unsafe { crate::gl_validation::dreamgpu_gl_query_validate(function, raw.as_ptr()) },
        0
    );
    let capacity =
        unsafe { crate::gl_validation::dreamgpu_gl_query_result_bytes(function, raw.as_ptr()) };
    assert!(capacity > 0 && capacity <= DG_GL_MAX_RESULT_BYTES);
    let state = QueryState {
        in_begin: 0,
        has_drawable: 1,
        width: 8,
        height: 8,
        draw_buffer: GL_BACK,
        read_buffer: GL_BACK,
        binding_1d: 0,
        binding_2d: 0,
        attrib_depth: 0,
        textures: unsafe { (*s(c)).textures },
        capture: null_mut(),
        memory: m,
        context: s(c),
    };
    let mut result = vec![0xccu8; capacity as usize];
    let (mut bytes, mut kind) = (0, 0);
    let e = unsafe {
        dreamgpu_gl_query(
            m.api,
            &state,
            addr_of_mut!((*s(c)).guest_errors),
            function,
            raw.as_ptr(),
            result.as_mut_ptr(),
            capacity,
            None,
            null_mut(),
            &mut bytes,
            &mut kind,
        )
    };
    assert_eq!(e, 0);
    assert!(bytes > 0 && bytes <= capacity);
    // The guest drivers' result validation (dg-gl-result.h) accepts it.
    if kind == DG_GL_RESULT_BOOL {
        assert!(result[..bytes as usize].iter().all(|&b| b <= 1));
        return (
            kind,
            result[..bytes as usize]
                .iter()
                .map(|&b| u32::from(b))
                .collect(),
        );
    }
    assert_eq!((kind, bytes % 4), (DG_GL_RESULT_INT, 0));
    let words = result[..bytes as usize]
        .as_chunks::<4>()
        .0
        .iter()
        .map(|w| u32::from_le_bytes(*w))
        .collect();
    (kind, words)
}
fn gen(m: &Memory, c: &mut Context, function: u32, count: u32) -> Vec<u32> {
    query(m, c, function, [count, 0, 0]).1
}
fn is(m: &Memory, c: &mut Context, function: u32, name: u32) -> bool {
    query(m, c, function, [name, 0, 0]) == (DG_GL_RESULT_BOOL, vec![1])
}
fn object(m: &Memory, c: &mut Context, id: u32, pname: u32, reports: u32) -> Vec<u32> {
    query(m, c, FEnum_glGetQueryObjectuiv, [id, pname, reports]).1
}
fn current(m: &Memory, c: &mut Context) -> u32 {
    query(
        m,
        c,
        FEnum_glGetQueryiv,
        [GL_SAMPLES_PASSED, GL_CURRENT_QUERY, 0],
    )
    .1[0]
}
/// End a query that will report `result` after `pending` polls.
fn end_with(m: &Memory, c: &mut Context, result: u32, pending: u32) {
    native(|n| {
        n.next_result = result;
        n.next_pending = pending;
    });
    end(m, c);
}
fn flushes() -> u32 {
    native(|n| n.flushes)
}
const PENDING: u32 = DG_GL_QUERY_PENDING;
const AVAILABLE: u32 = DG_GL_QUERY_AVAILABLE;

#[test]
fn record_shapes_admit_exactly_the_protocol() {
    use crate::gl_validation::{
        dreamgpu_gl_call_validate, dreamgpu_gl_data_validate, dreamgpu_gl_function_words,
        dreamgpu_gl_query_result_bytes, dreamgpu_gl_query_validate,
    };
    for (function, words) in [
        (FEnum_glBeginQuery, 2),
        (FEnum_glEndQuery, 1),
        (FEnum_glSetFenceNV, 2),
        (FEnum_glDeleteQueries, DG_GL_FUNCTION_INLINE_DATA | 1),
        (FEnum_glDeleteFencesNV, DG_GL_FUNCTION_INLINE_DATA | 1),
        (FEnum_glGenQueries, DG_GL_FUNCTION_QUERY | 1),
        (FEnum_glIsQuery, DG_GL_FUNCTION_QUERY | 1),
        (FEnum_glGetQueryiv, DG_GL_FUNCTION_QUERY | 2),
        (FEnum_glGetQueryObjectuiv, DG_GL_FUNCTION_QUERY | 3),
        (FEnum_glGenFencesNV, DG_GL_FUNCTION_QUERY | 1),
        (FEnum_glIsFenceNV, DG_GL_FUNCTION_QUERY | 1),
        (FEnum_glTestFenceNV, DG_GL_FUNCTION_QUERY | 1),
        (FEnum_glFinishFenceNV, DG_GL_FUNCTION_QUERY | 1),
        (FEnum_glGetFenceivNV, DG_GL_FUNCTION_QUERY | 2),
    ] {
        assert_eq!(dreamgpu_gl_function_words(function), words, "{function}");
    }
    // The ARB and other aliases are not records: the frontend sends core numbers.
    for function in [
        FEnum_glBeginQueryARB,
        FEnum_glGetQueryObjectivARB,
        FEnum_glGetQueryObjectiv,
        FEnum_glGenOcclusionQueriesNV,
        FEnum_glGenFencesAPPLE,
        FEnum_glFenceSync,
    ] {
        assert_eq!(dreamgpu_gl_function_words(function), u32::MAX, "{function}");
    }
    let words = |w: &[u32]| -> Vec<u8> { w.iter().flat_map(|v| v.to_le_bytes()).collect() };
    let call = |f, w: &[u32]| unsafe { dreamgpu_gl_call_validate(f, words(w).as_ptr()) };
    assert_eq!(call(FEnum_glBeginQuery, &[GL_SAMPLES_PASSED, 0]), 0);
    assert_ne!(call(FEnum_glBeginQuery, &[GL_SAMPLES_PASSED + 1, 1]), 0);
    assert_ne!(call(FEnum_glBeginQuery, &[0x88bf /* TIME_ELAPSED */, 1]), 0);
    assert_eq!(call(FEnum_glEndQuery, &[GL_SAMPLES_PASSED]), 0);
    assert_ne!(
        call(FEnum_glEndQuery, &[0x8c2f /* ANY_SAMPLES_PASSED */]),
        0
    );
    assert_eq!(call(FEnum_glSetFenceNV, &[0, ALL_COMPLETED_NV]), 0);
    assert_ne!(call(FEnum_glSetFenceNV, &[1, FENCE_STATUS_NV]), 0);
    let data = |f, count: u32, names: &[u32]| unsafe {
        let payload = words(names);
        dreamgpu_gl_data_validate(
            f,
            count.to_le_bytes().as_ptr(),
            payload.as_ptr(),
            payload.len() as u32,
        )
    };
    for f in [FEnum_glDeleteQueries, FEnum_glDeleteFencesNV] {
        assert_eq!(data(f, 2, &[1, 0]), 0);
        assert_eq!(data(f, 0, &[]), 0);
        assert_ne!(data(f, 3, &[1, 2]), 0);
        assert_ne!(data(f, 1, &[1, 2]), 0);
        assert_eq!(
            data(f, DG_GL_MAX_QUERIES, &vec![7; DG_GL_MAX_QUERIES as usize]),
            0
        );
        assert_ne!(
            data(
                f,
                DG_GL_MAX_QUERIES + 1,
                &vec![7; DG_GL_MAX_QUERIES as usize + 1]
            ),
            0
        );
    }
    let shape = |f, a: [u32; 3]| unsafe {
        let raw = words(&a);
        (
            dreamgpu_gl_query_validate(f, raw.as_ptr()),
            dreamgpu_gl_query_result_bytes(f, raw.as_ptr()),
        )
    };
    for f in [FEnum_glGenQueries, FEnum_glGenFencesNV] {
        assert_eq!(shape(f, [1, 0, 0]), (0, 4));
        assert_eq!(shape(f, [DG_GL_QUERY_NAME_BATCH, 0, 0]), (0, 512));
        assert_ne!(shape(f, [0, 0, 0]).0, 0);
        assert_ne!(shape(f, [DG_GL_QUERY_NAME_BATCH + 1, 0, 0]).0, 0);
        assert_ne!(shape(f, [1, 1, 0]).0, 0);
    }
    for f in [FEnum_glIsQuery, FEnum_glIsFenceNV] {
        assert_eq!(shape(f, [9, 0, 0]), (0, 1));
        assert_ne!(shape(f, [9, 0, 1]).0, 0);
    }
    for f in [FEnum_glTestFenceNV, FEnum_glFinishFenceNV] {
        assert_eq!(shape(f, [9, 0, 0]), (0, 8));
        assert_ne!(shape(f, [9, 1, 0]).0, 0);
    }
    assert_eq!(
        shape(FEnum_glGetQueryiv, [GL_SAMPLES_PASSED, GL_CURRENT_QUERY, 0]),
        (0, 4)
    );
    assert_eq!(
        shape(
            FEnum_glGetQueryiv,
            [GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, 0]
        ),
        (0, 4)
    );
    assert_ne!(
        shape(FEnum_glGetQueryiv, [GL_SAMPLES_PASSED, GL_QUERY_RESULT, 0]).0,
        0
    );
    assert_ne!(
        shape(FEnum_glGetQueryiv, [0x88bf, GL_CURRENT_QUERY, 0]).0,
        0
    );
    // GetQueryObjectuiv: the state and result, then up to 63 report pairs.
    for pname in [GL_QUERY_RESULT, GL_QUERY_RESULT_AVAILABLE] {
        assert_eq!(shape(FEnum_glGetQueryObjectuiv, [1, pname, 0]), (0, 8));
        assert_eq!(
            shape(FEnum_glGetQueryObjectuiv, [1, pname, DG_GL_QUERY_REPORTS]),
            (0, 512)
        );
        assert_ne!(
            shape(
                FEnum_glGetQueryObjectuiv,
                [1, pname, DG_GL_QUERY_REPORTS + 1]
            )
            .0,
            0
        );
    }
    assert_ne!(
        shape(FEnum_glGetQueryObjectuiv, [1, GL_CURRENT_QUERY, 0]).0,
        0
    );
    assert_eq!(shape(FEnum_glGetFenceivNV, [1, FENCE_STATUS_NV, 0]), (0, 8));
    assert_eq!(
        shape(FEnum_glGetFenceivNV, [1, FENCE_CONDITION_NV, 0]),
        (0, 8)
    );
    assert_ne!(shape(FEnum_glGetFenceivNV, [1, ALL_COMPLETED_NV, 0]).0, 0);
    assert!(MAX_ANSWER_WORDS * 4 == DG_GL_MAX_RESULT_BYTES as usize);
}

#[test]
fn names_objects_and_the_active_query_follow_arb_occlusion_query() {
    let f = fixture();
    let m = &f.memory;
    let base = *f.bytes;
    let mut c = context(m);
    assert_eq!(current(m, &mut c), 0);
    // QUERY_COUNTER_BITS: the native counter, at most the 32 bits results carry.
    assert_eq!(
        query(
            m,
            &mut c,
            FEnum_glGetQueryiv,
            [GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, 0]
        )
        .1,
        [32]
    );
    native(|n| n.counter_bits = 24);
    assert_eq!(
        query(
            m,
            &mut c,
            FEnum_glGetQueryiv,
            [GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, 0]
        )
        .1,
        [24]
    );
    // Generated names are distinct, nonzero and not objects until begun.
    let names = gen(m, &mut c, FEnum_glGenQueries, 3);
    assert_eq!(names, [1, 2, 3]);
    assert!(names.iter().all(|&n| !is(m, &mut c, FEnum_glIsQuery, n)));
    assert!(!is(m, &mut c, FEnum_glIsQuery, 0));
    assert_eq!(native(|n| n.queries.len()), 0);
    begin(m, &mut c, 2);
    assert!(is(m, &mut c, FEnum_glIsQuery, 2));
    assert_eq!(current(m, &mut c), 2);
    assert_eq!(errors(&mut c), 0);
    // Nesting: another BeginQuery, of any name, fails and leaves 2 active.
    begin(m, &mut c, 3);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    begin(m, &mut c, 2);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(current(m, &mut c), 2);
    assert!(!is(m, &mut c, FEnum_glIsQuery, 3));
    // Gen and Delete fail while a query is active, changing nothing.
    assert_eq!(gen(m, &mut c, FEnum_glGenQueries, 2), [0, 0]);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    delete(m, &mut c, FEnum_glDeleteQueries, &[2, 1]);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert!(is(m, &mut c, FEnum_glIsQuery, 2));
    // The active query has no result yet: a single zero word.
    assert_eq!(object(m, &mut c, 2, GL_QUERY_RESULT_AVAILABLE, 4), [0]);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(object(m, &mut c, 2, GL_QUERY_RESULT, 0), [0]);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    end_with(m, &mut c, 48, 0);
    assert_eq!(current(m, &mut c), 0);
    assert_eq!(object(m, &mut c, 2, GL_QUERY_RESULT, 0), [AVAILABLE, 48]);
    assert_eq!(errors(&mut c), 0);
    // EndQuery without an active query, and BeginQuery of zero.
    end(m, &mut c);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    begin(m, &mut c, 0);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(current(m, &mut c), 0);
    // Names that are not objects (reserved, unknown) have no result.
    for id in [1, 3, 99, 0] {
        assert_eq!(object(m, &mut c, id, GL_QUERY_RESULT_AVAILABLE, 0), [0]);
        assert_eq!(errors(&mut c), INVALID_OPERATION);
    }
    // Any nonzero name becomes an object at BeginQuery; Gen skips it.
    begin(m, &mut c, 4);
    end_with(m, &mut c, 0, 0);
    assert!(is(m, &mut c, FEnum_glIsQuery, 4));
    assert_eq!(gen(m, &mut c, FEnum_glGenQueries, 2), [5, 6]);
    assert_eq!(
        object(m, &mut c, 4, GL_QUERY_RESULT_AVAILABLE, 0),
        [AVAILABLE, 0]
    );
    // Re-beginning a query restarts its native counter.
    begin(m, &mut c, 2);
    end_with(m, &mut c, 7, 0);
    assert_eq!(
        object(m, &mut c, 2, GL_QUERY_RESULT_AVAILABLE, 0),
        [AVAILABLE, 7]
    );
    assert_eq!(native(|n| n.queries.len()), 2);
    assert_eq!(errors(&mut c), 0);
    // Deletion: objects and reserved names alike; zero and unknown ignored.
    delete(m, &mut c, FEnum_glDeleteQueries, &[0, 2, 1, 77]);
    assert_eq!(errors(&mut c), 0);
    assert!(!is(m, &mut c, FEnum_glIsQuery, 2));
    assert_eq!(object(m, &mut c, 2, GL_QUERY_RESULT, 0), [0]);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(native(|n| n.queries.len()), 1);
    // A deleted name is unused again.
    assert_eq!(gen(m, &mut c, FEnum_glGenQueries, 1), [7]);
    assert!(*f.bytes > base);
    release(m, c);
    assert_eq!(
        native(|n| n.queries.len()),
        0,
        "release deletes every native query"
    );
    assert_eq!(*f.bytes, base, "release returns every charge");
    assert!(
        f.allocator.live.is_empty(),
        "release frees every allocation"
    );
}

#[test]
fn availability_flushes_and_reports_each_end_once_in_order() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m);
    // Three queries ending with 10, 20 and 30 samples, each pending for two
    // native polls; native queries complete in order.
    for (id, result) in [(1, 10), (2, 20), (3, 30)] {
        begin(m, &mut c, id);
        end_with(m, &mut c, result, 2);
    }
    // Pending: the state, a zero result, a native flush, and no reports while
    // the oldest is pending.
    let before = flushes();
    assert_eq!(
        object(m, &mut c, 3, GL_QUERY_RESULT_AVAILABLE, DG_GL_QUERY_REPORTS),
        [PENDING, 0]
    );
    assert_eq!(flushes(), before + 1);
    // Query 1 completes; asking about 3 reports 1 as a pair.
    native(|n| n.queries.get_mut(&1).unwrap().pending = 0);
    assert_eq!(
        object(m, &mut c, 3, GL_QUERY_RESULT_AVAILABLE, DG_GL_QUERY_REPORTS),
        [PENDING, 0, 1, 10]
    );
    // QUERY_RESULT waits; the earlier query 2 is then complete too and is
    // reported, 1 was reported already.
    native(|n| n.queries.get_mut(&2).unwrap().pending = 0);
    let flushed = flushes();
    assert_eq!(
        object(m, &mut c, 3, GL_QUERY_RESULT, DG_GL_QUERY_REPORTS),
        [AVAILABLE, 30, 2, 20]
    );
    assert_eq!(flushes(), flushed, "an available answer does not flush");
    // Reported queries are still answered when queried themselves.
    assert_eq!(
        object(m, &mut c, 1, GL_QUERY_RESULT_AVAILABLE, DG_GL_QUERY_REPORTS),
        [AVAILABLE, 10]
    );
    assert_eq!(object(m, &mut c, 1, GL_QUERY_RESULT, 0), [AVAILABLE, 10]);
    // At most the requested reports, oldest first.
    for id in 4..=8 {
        begin(m, &mut c, id);
        end_with(m, &mut c, id * 100, 0);
    }
    assert_eq!(
        object(m, &mut c, 8, GL_QUERY_RESULT_AVAILABLE, 2),
        [AVAILABLE, 800, 4, 400, 5, 500]
    );
    assert_eq!(
        object(m, &mut c, 8, GL_QUERY_RESULT_AVAILABLE, 0),
        [AVAILABLE, 800]
    );
    assert_eq!(
        object(m, &mut c, 8, GL_QUERY_RESULT_AVAILABLE, 9),
        [AVAILABLE, 800, 6, 600, 7, 700]
    );
    // BeginQuery retires an unreported end, and so does deletion.
    begin(m, &mut c, 9);
    end_with(m, &mut c, 1, 0);
    begin(m, &mut c, 10);
    end_with(m, &mut c, 2, 0);
    begin(m, &mut c, 9);
    end_with(m, &mut c, 3, 0);
    delete(m, &mut c, FEnum_glDeleteQueries, &[10]);
    begin(m, &mut c, 11);
    end_with(m, &mut c, 4, 0);
    assert_eq!(
        object(m, &mut c, 11, GL_QUERY_RESULT_AVAILABLE, 9),
        [AVAILABLE, 4, 9, 3]
    );
    // Results saturate natively; the host passes 2^32-1 through.
    begin(m, &mut c, 12);
    end_with(m, &mut c, u32::MAX, 0);
    assert_eq!(
        object(m, &mut c, 12, GL_QUERY_RESULT, 0),
        [AVAILABLE, u32::MAX]
    );
    // A full queue forgets its oldest entry; queried directly it still answers.
    for id in 100..100 + ENDED as u32 + 3 {
        begin(m, &mut c, id);
        end_with(m, &mut c, id, 0);
    }
    let reported = object(
        m,
        &mut c,
        99 + ENDED as u32 + 3,
        GL_QUERY_RESULT_AVAILABLE,
        DG_GL_QUERY_REPORTS,
    );
    assert_eq!(reported.len(), 2 + 2 * DG_GL_QUERY_REPORTS as usize);
    assert_eq!(&reported[2..4], [103, 103]);
    assert_eq!(
        object(m, &mut c, 100, GL_QUERY_RESULT_AVAILABLE, 0),
        [AVAILABLE, 100]
    );
    // The queried id is never its own report: found pending, it stops the
    // reports even when it completes between the two native polls.
    begin(m, &mut c, 20);
    end_with(m, &mut c, 5, 1);
    assert_eq!(
        object(
            m,
            &mut c,
            20,
            GL_QUERY_RESULT_AVAILABLE,
            DG_GL_QUERY_REPORTS
        ),
        [PENDING, 0]
    );
    assert_eq!(
        object(m, &mut c, 20, GL_QUERY_RESULT_AVAILABLE, 0),
        [AVAILABLE, 5]
    );
    assert_eq!(errors(&mut c), 0);
    release(m, c);
    assert_eq!(native(|n| n.queries.len()), 0);
}

#[test]
fn queries_belong_to_one_context_and_survive_switches() {
    let f = fixture();
    let m = &f.memory;
    let mut one = context(m);
    let mut two = context(m);
    // Each native context has its own active query: switching contexts
    // stashes the model's.
    let switch = || native(|n| core::mem::take(&mut n.active));
    begin(m, &mut one, 5);
    let one_active = switch();
    // The other context has its own (inactive) query state and names.
    assert_eq!(current(m, &mut two), 0);
    assert!(!is(m, &mut two, FEnum_glIsQuery, 5));
    begin(m, &mut two, 5);
    end_with(m, &mut two, 9, 0);
    assert_eq!(errors(&mut two), 0);
    // Context one's query is still active and pending results survive.
    native(|n| n.active = one_active);
    assert_eq!(current(m, &mut one), 5);
    end_with(m, &mut one, 3, 0);
    assert_eq!(object(m, &mut one, 5, GL_QUERY_RESULT, 0), [AVAILABLE, 3]);
    assert_eq!(object(m, &mut two, 5, GL_QUERY_RESULT, 0), [AVAILABLE, 9]);
    assert_eq!(gen(m, &mut one, FEnum_glGenQueries, 1), [1]);
    assert_eq!(gen(m, &mut two, FEnum_glGenQueries, 1), [1]);
    // Fences are per context as well.
    set_fence(m, &mut one, 3);
    assert!(is(m, &mut one, FEnum_glIsFenceNV, 3));
    assert!(!is(m, &mut two, FEnum_glIsFenceNV, 3));
    release(m, two);
    assert_eq!(native(|n| (n.queries.len(), n.syncs.len())), (1, 1));
    // Releasing a context with an active query ends and deletes it.
    begin(m, &mut one, 6);
    release(m, one);
    assert_eq!(
        native(|n| (n.queries.len(), n.syncs.len(), n.active)),
        (0, 0, 0)
    );
}

#[test]
fn name_tables_and_objects_are_bounded_and_charged() {
    let f = fixture();
    let m = &f.memory;
    let base = *f.bytes;
    let mut c = context(m);
    // Names count against DG_GL_MAX_QUERIES per kind; failures reserve none.
    for batch in 0..DG_GL_MAX_QUERIES / DG_GL_QUERY_NAME_BATCH {
        let names = gen(m, &mut c, FEnum_glGenQueries, DG_GL_QUERY_NAME_BATCH);
        assert_eq!(names[0], batch * DG_GL_QUERY_NAME_BATCH + 1);
    }
    assert_eq!(errors(&mut c), 0);
    assert_eq!(gen(m, &mut c, FEnum_glGenQueries, 1), [0]);
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    begin(m, &mut c, 99_999);
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    assert_eq!(current(m, &mut c), 0);
    // A reserved name still becomes an object.
    begin(m, &mut c, 17);
    end_with(m, &mut c, 1, 0);
    assert_eq!(errors(&mut c), 0);
    // The fence table is separate.
    assert_eq!(gen(m, &mut c, FEnum_glGenFencesNV, 2), [1, 2]);
    delete(
        m,
        &mut c,
        FEnum_glDeleteQueries,
        &(1..=DG_GL_MAX_QUERIES).collect::<Vec<_>>(),
    );
    assert_eq!(native(|n| n.queries.len()), 0);
    // Exhausting the resource budget: the object is not created and the name
    // stays unused.
    let tables = *f.bytes;
    unsafe { *m.bytes = u64::from(DG_GL_MAX_TEXTURE_BYTES) - OBJECT_BYTES + 1 };
    begin(m, &mut c, 40);
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    set_fence(m, &mut c, 40);
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    unsafe { *m.bytes = tables };
    assert!(!is(m, &mut c, FEnum_glIsQuery, 40));
    assert!(!is(m, &mut c, FEnum_glIsFenceNV, 40));
    // A native allocation failure is OUT_OF_MEMORY too, with nothing charged.
    native(|n| n.fail_gen = true);
    begin(m, &mut c, 41);
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    assert_eq!(*f.bytes, tables);
    native(|n| n.fail_gen = false);
    begin(m, &mut c, 41);
    end(m, &mut c);
    assert_eq!(*f.bytes, tables + OBJECT_BYTES);
    release(m, c);
    assert_eq!(*f.bytes, base);
    // A context state that cannot be allocated is OUT_OF_MEMORY, not a fault.
    let mut c = context(m);
    unsafe { *m.bytes = u64::from(DG_GL_MAX_TEXTURE_BYTES) };
    begin(m, &mut c, 1);
    assert_eq!(gen(m, &mut c, FEnum_glGenFencesNV, 1), [0]);
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    assert!(unsafe { (*s(&mut c)).queries }.is_null());
    unsafe { *m.bytes = base };
    release(m, c);
}

#[test]
fn fences_follow_nv_fence() {
    let f = fixture();
    let m = &f.memory;
    let base = *f.bytes;
    let mut c = context(m);
    let fence = |c: &mut Context, function, name| query(m, c, function, [name, 0, 0]).1;
    // Not a fence: INVALID_OPERATION and a single zero word.
    for function in [FEnum_glTestFenceNV, FEnum_glFinishFenceNV] {
        assert_eq!(fence(&mut c, function, 4), [0]);
        assert_eq!(errors(&mut c), INVALID_OPERATION);
    }
    assert_eq!(
        query(m, &mut c, FEnum_glGetFenceivNV, [4, FENCE_STATUS_NV, 0]).1,
        [0]
    );
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    set_fence(m, &mut c, 0);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    // Generated names are not fences until set; any nonzero name can be set.
    assert_eq!(gen(m, &mut c, FEnum_glGenFencesNV, 2), [1, 2]);
    assert!(!is(m, &mut c, FEnum_glIsFenceNV, 1));
    native(|n| n.next_pending = 2);
    set_fence(m, &mut c, 9);
    assert!(is(m, &mut c, FEnum_glIsFenceNV, 9));
    assert_eq!(errors(&mut c), 0);
    // Pending: FALSE after a flushing zero-timeout wait, then TRUE, which
    // deletes the native sync and stays TRUE without native calls.
    assert_eq!(fence(&mut c, FEnum_glTestFenceNV, 9), [1, 0]);
    assert_eq!(
        native(|n| n.waits.clone()),
        [(GL_SYNC_FLUSH_COMMANDS_BIT, 0)]
    );
    assert_eq!(
        query(m, &mut c, FEnum_glGetFenceivNV, [9, FENCE_STATUS_NV, 0]).1,
        [1, 0]
    );
    assert_eq!(fence(&mut c, FEnum_glTestFenceNV, 9), [1, 1]);
    assert_eq!(native(|n| n.syncs.len()), 0);
    let waits = native(|n| n.waits.len());
    assert_eq!(fence(&mut c, FEnum_glTestFenceNV, 9), [1, 1]);
    assert_eq!(
        query(m, &mut c, FEnum_glGetFenceivNV, [9, FENCE_STATUS_NV, 0]).1,
        [1, 1]
    );
    assert_eq!(native(|n| n.waits.len()), waits);
    assert_eq!(
        query(m, &mut c, FEnum_glGetFenceivNV, [9, FENCE_CONDITION_NV, 0]).1,
        [1, ALL_COMPLETED_NV]
    );
    // SetFenceNV again resets the status; FinishFenceNV waits in bounded,
    // flushing native waits until the fence completes.
    native(|n| n.next_pending = 3);
    set_fence(m, &mut c, 9);
    native(|n| n.waits.clear());
    assert_eq!(fence(&mut c, FEnum_glFinishFenceNV, 9), [1, 1]);
    assert_eq!(
        native(|n| n.waits.clone()),
        [(GL_SYNC_FLUSH_COMMANDS_BIT, WAIT_NS); 3]
    );
    assert_eq!(fence(&mut c, FEnum_glTestFenceNV, 9), [1, 1]);
    assert_eq!(native(|n| n.syncs.len()), 0);
    // Without a native sync object the fence completes by finishing natively.
    native(|n| n.fail_sync = true);
    set_fence(m, &mut c, 1);
    assert_eq!(native(|n| n.finishes), 1);
    assert_eq!(fence(&mut c, FEnum_glTestFenceNV, 1), [1, 1]);
    native(|n| {
        n.fail_sync = false;
        n.next_pending = 5;
    });
    // Deletion while pending deletes the native sync; unknown names ignored.
    set_fence(m, &mut c, 2);
    assert_eq!(native(|n| n.syncs.len()), 1);
    delete(m, &mut c, FEnum_glDeleteFencesNV, &[2, 0, 1234]);
    assert_eq!(native(|n| n.syncs.len()), 0);
    assert!(!is(m, &mut c, FEnum_glIsFenceNV, 2));
    assert_eq!(fence(&mut c, FEnum_glTestFenceNV, 2), [0]);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    // Fence deletion is allowed while a query is active.
    begin(m, &mut c, 1);
    delete(m, &mut c, FEnum_glDeleteFencesNV, &[9]);
    assert_eq!(errors(&mut c), 0);
    assert!(!is(m, &mut c, FEnum_glIsFenceNV, 9));
    end(m, &mut c);
    // Release deletes pending native syncs.
    set_fence(m, &mut c, 3);
    release(m, c);
    assert_eq!(native(|n| (n.syncs.len(), n.queries.len())), (0, 0));
    assert_eq!(*f.bytes, base);
}

#[test]
fn native_entry_points_are_checked_before_anything_changes() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m);
    let mut incomplete = api();
    incomplete.dg_glClientWaitSync = None;
    let mut broken = unsafe { core::ptr::read(m) };
    broken.api = &incomplete;
    assert_eq!(
        call(&broken, &mut c, FEnum_glBeginQuery, &[GL_SAMPLES_PASSED, 1]),
        DG_GL_ERROR_UNSUPPORTED
    );
    assert!(unsafe { (*s(&mut c)).queries }.is_null());
    release(m, c);
}
