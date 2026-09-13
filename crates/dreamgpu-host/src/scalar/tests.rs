use super::*;
use std::cell::RefCell;
thread_local! { static OBSERVED: RefCell<Vec<u64>> = const { RefCell::new(Vec::new()) }; }
fn note(values: &[u64]) {
    OBSERVED.with(|v| v.borrow_mut().extend_from_slice(values));
}
fn take() -> Vec<u64> {
    OBSERVED.with(|v| core::mem::take(&mut *v.borrow_mut()))
}
unsafe extern "C" fn color(a: f32, b: f32, c: f32, d: f32) {
    note(&[
        a.to_bits() as u64,
        b.to_bits() as u64,
        c.to_bits() as u64,
        d.to_bits() as u64,
    ]);
}
unsafe extern "C" fn depth(a: f64) {
    note(&[a.to_bits()]);
}
unsafe extern "C" fn matrix(p: *const f32) {
    for i in 0..16 {
        note(&[unsafe { *p.add(i) }.to_bits() as u64]);
    }
}
unsafe extern "C" fn viewport(x: i32, y: i32, w: i32, h: i32) {
    note(&[x as u32 as u64, y as u32 as u64, w as u64, h as u64]);
}
unsafe extern "C" fn mask(a: u8, b: u8, c: u8, d: u8) {
    note(&[a as u64, b as u64, c as u64, d as u64]);
}
unsafe extern "C" fn begin(mode: u32) {
    note(&[100, mode as u64]);
}
unsafe extern "C" fn end() {
    note(&[101]);
}
unsafe extern "C" fn hook(p: *mut c_void, function: u32, _: *const u8) -> u32 {
    note(&[200, function as u64]);
    if p.is_null() {
        0
    } else {
        11
    }
}
fn run(api: &DreamGpuGlApi, state: &mut u32, function: u32, args: &[u32]) -> Result<(), u32> {
    let args: Vec<u8> = args.iter().flat_map(|v| v.to_le_bytes()).collect();
    unsafe { execute(api, state, hook, core::ptr::null_mut(), function, &args) }
}
#[test]
fn generated_gl_abi_preserves_bits_and_enforces_begin() {
    // Every field is Option<extern fn>; null is its specified C representation.
    let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    api.dg_glColor4f = Some(color);
    api.dg_glClearDepth = Some(depth);
    api.dg_glLoadMatrixf = Some(matrix);
    api.dg_glViewport = Some(viewport);
    api.dg_glColorMask = Some(mask);
    api.dg_glBegin = Some(begin);
    api.dg_glEnd = Some(end);
    let mut state = 0;
    let bits = [0x7fc12345, 0x80000000, 1, 0x3f800000];
    run(&api, &mut state, FEnum_glColor4f, &bits).unwrap();
    assert_eq!(take(), bits.map(u64::from));
    run(
        &api,
        &mut state,
        FEnum_glClearDepth,
        &[0x01234567, 0x7ff81234],
    )
    .unwrap();
    assert_eq!(take(), [0x7ff8123401234567]);
    let matrix: Vec<u32> = (0..16).map(|v| 0x3f800000 + v).collect();
    run(&api, &mut state, FEnum_glLoadMatrixf, &matrix).unwrap();
    assert_eq!(
        take(),
        matrix.into_iter().map(u64::from).collect::<Vec<_>>()
    );
    run(
        &api,
        &mut state,
        FEnum_glViewport,
        &[u32::MAX, u32::MAX - 1, 640, 480],
    )
    .unwrap();
    assert_eq!(take(), [u32::MAX as u64, (u32::MAX - 1) as u64, 640, 480]);
    run(&api, &mut state, FEnum_glColorMask, &[0, 1, u32::MAX, 128]).unwrap();
    assert_eq!(take(), [0, 1, 1, 1]);
    run(&api, &mut state, FEnum_glBegin, &[4]).unwrap();
    assert_eq!(state, 1);
    assert_eq!(take(), [200, FEnum_glBegin as u64, 100, 4]);
    assert_eq!(run(&api, &mut state, FEnum_glBegin, &[4]), Err(4));
    assert_eq!(
        run(&api, &mut state, FEnum_glViewport, &[0, 0, 1, 1]),
        Err(4)
    );
    assert!(take().is_empty());
    run(&api, &mut state, FEnum_glColor4f, &bits).unwrap();
    take();
    run(&api, &mut state, FEnum_glEnd, &[]).unwrap();
    assert_eq!(state, 0);
    assert_eq!(take(), [101]);
    assert_eq!(run(&api, &mut state, FEnum_glEnd, &[]), Err(4));
    assert_eq!(run(&api, &mut state, FEnum_glBegin, &[10]), Err(4));
    assert_eq!(run(&api, &mut state, FEnum_glColor4f, &[0, 0, 0]), Err(1));
    assert_eq!(run(&api, &mut state, u32::MAX, &[]), Err(3));
    assert!(take().is_empty());
    assert_eq!(
        unsafe {
            execute(
                &api,
                &mut state,
                hook,
                std::ptr::dangling_mut::<c_void>(),
                FEnum_glBegin,
                &4u32.to_le_bytes(),
            )
        },
        Err(11)
    );
    assert_eq!(state, 0);
    assert_eq!(take(), [200, FEnum_glBegin as u64]);
}
