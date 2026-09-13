use super::*;

#[derive(Default)]
struct Mock {
    calls: Vec<&'static str>,
    fail_alloc: bool,
    begun: bool,
    make_error: u32,
    share: usize,
}
unsafe fn mock<'a>(p: *mut c_void) -> &'a mut Mock {
    unsafe { &mut *p.cast() }
}
unsafe extern "C" fn new_context(p: *mut c_void, share: *mut c_void) -> *mut c_void {
    let m = unsafe { mock(p) };
    m.calls.push("new_context");
    m.share = share as usize;
    if m.fail_alloc {
        core::ptr::null_mut()
    } else {
        std::ptr::dangling_mut::<c_void>()
    }
}
unsafe extern "C" fn new_drawable(p: *mut c_void, _: u32, _: u32) -> *mut c_void {
    let m = unsafe { mock(p) };
    m.calls.push("new_drawable");
    if m.fail_alloc {
        core::ptr::null_mut()
    } else {
        2usize as *mut c_void
    }
}
unsafe extern "C" fn free_context(p: *mut c_void, _: u32) {
    unsafe { mock(p) }.calls.push("free_context");
}
unsafe extern "C" fn free_drawable(p: *mut c_void, _: u32) {
    unsafe { mock(p) }.calls.push("free_drawable");
}
unsafe extern "C" fn close_begin(p: *mut c_void) {
    unsafe { mock(p) }.calls.push("close_begin");
}
unsafe extern "C" fn in_begin(p: *mut c_void, _: u32) -> u32 {
    u32::from(unsafe { mock(p) }.begun)
}
unsafe extern "C" fn make_current(p: *mut c_void, _: u32, _: u32) -> u32 {
    let m = unsafe { mock(p) };
    m.calls.push("make_current");
    m.make_error
}
unsafe extern "C" fn call(p: *mut c_void, _: u32, _: u32, _: *const u8) -> u32 {
    unsafe { mock(p) }.calls.push("call");
    0
}
unsafe extern "C" fn data(
    p: *mut c_void,
    _: u32,
    _: u32,
    args: *const u8,
    data: *const u8,
    n: u32,
) -> u32 {
    assert_eq!(unsafe { *args }, 42);
    assert_eq!(unsafe { *data }, 77);
    assert_eq!(n, 4);
    unsafe { mock(p) }.calls.push("data");
    0
}
unsafe extern "C" fn words(_: *mut c_void, _: u32) -> u32 {
    0x80000002
}
unsafe extern "C" fn query(p: *mut c_void, _: u32, _: u32, _: *const u8) -> u32 {
    unsafe { mock(p) }.calls.push("query");
    0
}
unsafe extern "C" fn present(p: *mut c_void, _: u32, _: u32, _: u32) -> u32 {
    unsafe { mock(p) }.calls.push("present");
    0
}
unsafe extern "C" fn desktop(p: *mut c_void, _: *const u8) -> u32 {
    unsafe { mock(p) }.calls.push("desktop");
    0
}
fn platform(m: &mut Mock) -> Platform {
    Platform {
        opaque: (m as *mut Mock).cast(),
        context_new: new_context,
        drawable_new: new_drawable,
        context_free: free_context,
        drawable_free: free_drawable,
        close_begin,
        in_begin,
        make_current,
        call,
        data,
        words,
        query,
        present,
        desktop,
    }
}
fn record(op: u32, client: u32, context: u32, drawable: u32, flags: u32, args: &[u32]) -> Vec<u8> {
    [
        op,
        32 + args.len() as u32 * 4,
        client,
        context,
        drawable,
        flags,
        0,
        1,
    ]
    .into_iter()
    .chain(args.iter().copied())
    .flat_map(u32::to_le_bytes)
    .collect()
}
fn run(s: &mut Resources, p: &Platform, r: Vec<u8>) -> Result<(), u32> {
    unsafe { execute(s, p, &r, 640, 480) }
}

#[test]
fn process_scoped_resources_and_render_dispatch() {
    let mut s = Resources::default();
    let mut m = Mock::default();
    let p = platform(&mut m);
    run(&mut s, &p, record(1, 1, 10, 0, 0, &[0])).unwrap();
    assert_eq!(run(&mut s, &p, record(1, 2, 11, 0, 0, &[10])), Err(CONTEXT));
    run(&mut s, &p, record(1, 1, 11, 0, 0, &[10])).unwrap();
    assert_eq!(m.share, 1);
    run(&mut s, &p, record(1, 2, 10, 0, 0, &[0])).unwrap();
    assert_eq!(run(&mut s, &p, record(1, 1, 10, 0, 0, &[0])), Err(CONTEXT));
    run(&mut s, &p, record(3, 1, 0, 20, 0, &[640, 480])).unwrap();
    assert_eq!(run(&mut s, &p, record(5, 2, 10, 20, 0, &[])), Err(CONTEXT));
    run(&mut s, &p, record(5, 1, 10, 20, 0, &[])).unwrap();
    run(&mut s, &p, record(6, 1, 10, 20, 0, &[123, 1])).unwrap();
    run(&mut s, &p, record(10, 1, 10, 20, 0, &[123, 4, 42, 0, 77])).unwrap();
    run(&mut s, &p, record(11, 1, 10, 20, 0, &[123, 1])).unwrap();
    run(&mut s, &p, record(7, 1, 10, 20, 17, &[640, 480])).unwrap();
    assert_eq!(
        run(&mut s, &p, record(7, 1, 10, 20, 16, &[639, 480])),
        Err(DRAWABLE)
    );
    m.begun = true;
    assert_eq!(run(&mut s, &p, record(7, 1, 10, 20, 0, &[])), Err(CONTEXT));
    run(&mut s, &p, record(6, 1, 10, 20, 0, &[123])).unwrap();
    m.begun = false;
    run(&mut s, &p, record(8, 1, 0, 0, 0, &[])).unwrap();
    assert!(unsafe { context(&mut s, 1, 10) }.is_none());
    assert!(unsafe { context(&mut s, 2, 10) }.is_some());
    assert!(unsafe { drawable(&mut s, 1, 20) }.is_none());
    assert_eq!(m.calls.iter().filter(|&&v| v == "free_context").count(), 2);
    assert_eq!(m.calls.iter().filter(|&&v| v == "free_drawable").count(), 1);
    assert!(
        m.calls.contains(&"data") && m.calls.contains(&"query") && m.calls.contains(&"present")
    );
}

#[test]
fn resource_allocation_failure_limits_and_reuse() {
    let mut s = Resources::default();
    let mut m = Mock::default();
    let p = platform(&mut m);
    m.fail_alloc = true;
    assert_eq!(run(&mut s, &p, record(1, 1, 1, 0, 0, &[0])), Err(HOST));
    assert_eq!(
        run(&mut s, &p, record(3, 1, 0, 1, 0, &[640, 480])),
        Err(HOST)
    );
    assert_eq!(s.next_epoch, 0);
    assert!(s.contexts[0].native.is_null());
    m.fail_alloc = false;
    run(&mut s, &p, record(3, 1, 0, 1, 0, &[4096, 4096])).unwrap();
    assert_eq!(
        run(&mut s, &p, record(3, 1, 0, 2, 0, &[4096, 4096])),
        Err(LIMIT)
    );
    run(&mut s, &p, record(4, 1, 0, 1, 0, &[])).unwrap();
    run(&mut s, &p, record(3, 1, 0, 2, 0, &[4096, 4096])).unwrap();
    assert_eq!(s.next_epoch, 2);
    for id in 1..=32 {
        run(&mut s, &p, record(1, 1, id, 0, 0, &[0])).unwrap();
    }
    assert_eq!(run(&mut s, &p, record(1, 1, 33, 0, 0, &[0])), Err(LIMIT));
    run(&mut s, &p, record(2, 1, 1, 0, 0, &[])).unwrap();
    run(&mut s, &p, record(1, 1, 33, 0, 0, &[0])).unwrap();
    run(&mut s, &p, record(8, 0, 0, 0, 0, &[])).unwrap();
    assert!(s.contexts.iter().all(|c| c.native.is_null()));
    assert!(s.drawables.iter().all(|d| d.native.is_null()));
    s.next_epoch = u64::MAX;
    assert_eq!(run(&mut s, &p, record(3, 1, 0, 1, 0, &[1, 1])), Err(LIMIT));
}

#[test]
fn failed_binding_and_truncated_data_do_not_dispatch() {
    let mut s = Resources::default();
    let mut m = Mock::default();
    let p = platform(&mut m);
    run(&mut s, &p, record(1, 1, 1, 0, 0, &[0])).unwrap();
    run(&mut s, &p, record(3, 1, 0, 1, 0, &[640, 480])).unwrap();
    m.make_error = HOST;
    assert_eq!(run(&mut s, &p, record(5, 1, 1, 1, 0, &[])), Err(HOST));
    assert_eq!(s.contexts[0].drawable, 0);
    m.make_error = 0;
    run(&mut s, &p, record(5, 1, 1, 1, 0, &[])).unwrap();
    assert_eq!(
        run(&mut s, &p, record(10, 1, 1, 1, 0, &[123, 4, 42])),
        Err(BATCH)
    );
    assert!(!m.calls.contains(&"data"));
    assert_eq!(
        unsafe { execute(&mut s, &p, &[0; 31], 640, 480) },
        Err(BATCH)
    );
}
