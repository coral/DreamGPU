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
    assert_eq!(take(), [101, 200, FEnum_glEnd as u64]);
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

unsafe extern "C" fn raster(x: f64, y: f64, z: f64, w: f64) {
    note(&[x.to_bits(), y.to_bits(), z.to_bits(), w.to_bits()]);
}
#[test]
fn raster_double_wire_bounds_and_begin_rejection() {
    let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    api.dg_glRasterPos4d = Some(raster);
    let bits = [
        1.0000000000000002f64.to_bits(),
        (-0.0f64).to_bits(),
        0,
        2f64.to_bits(),
    ];
    let args: Vec<u32> = bits
        .iter()
        .flat_map(|b| [*b as u32, (*b >> 32) as u32])
        .collect();
    let mut state = 0;
    for n in 0..8 {
        assert_eq!(
            run(&api, &mut state, FEnum_glRasterPos4d, &args[..n]),
            Err(1)
        );
        assert!(take().is_empty());
    }
    run(&api, &mut state, FEnum_glRasterPos4d, &args).unwrap();
    assert_eq!(take(), bits);
    state = 1;
    assert_eq!(run(&api, &mut state, FEnum_glRasterPos4d, &args), Err(4));
    assert_eq!(state, 1);
    assert!(take().is_empty());
}

unsafe extern "C" fn copy_pixels(x: i32, y: i32, w: i32, h: i32, kind: u32) {
    note(&[
        x as u32 as u64,
        y as u32 as u64,
        w as u64,
        h as u64,
        kind as u64,
    ]);
}
#[test]
fn copy_pixels_signed_coordinates_bounds_and_begin_guard() {
    let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    api.dg_glCopyPixels = Some(copy_pixels);
    let mut state = 0;
    let args = [u32::MAX, 0x8000_0000, 2, 3, GL_COLOR];
    for n in 0..5 {
        assert_eq!(
            run(&api, &mut state, FEnum_glCopyPixels, &args[..n]),
            Err(1)
        );
        assert!(take().is_empty());
    }
    run(&api, &mut state, FEnum_glCopyPixels, &args).unwrap();
    assert_eq!(take(), args.map(u64::from));
    state = 1;
    assert_eq!(run(&api, &mut state, FEnum_glCopyPixels, &args), Err(4));
    assert_eq!(state, 1);
    assert!(take().is_empty());
}
unsafe extern "C" fn edge(v: u8) {
    note(&[v as u64]);
}
#[test]
fn index_double_and_edge_flag_are_legal_inside_begin_without_float_conversion() {
    let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    api.dg_glIndexd = Some(depth);
    api.dg_glEdgeFlag = Some(edge);
    let mut state = 1;
    let value = 16777217.25f64.to_bits();
    assert_eq!(
        run(
            &api,
            &mut state,
            FEnum_glIndexd,
            &[value as u32, (value >> 32) as u32]
        ),
        Ok(())
    );
    assert_eq!(take(), [value]);
    assert_eq!(run(&api, &mut state, FEnum_glEdgeFlag, &[7]), Ok(()));
    assert_eq!(take(), [1]);
    assert_eq!(run(&api, &mut state, FEnum_glIndexd, &[1]), Err(1));
    assert_eq!(
        run(
            &api,
            &mut state,
            FEnum_glAccum,
            &[GL_RETURN, 1f32.to_bits()]
        ),
        Err(4)
    );
    assert!(take().is_empty());
}
unsafe extern "C" fn mesh1(mode: u32, first: i32, last: i32) {
    note(&[mode as u64, first as u32 as u64, last as u32 as u64]);
}
#[test]
fn evaluator_mesh_observes_begin_texture_admission_before_native_draw() {
    let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    api.dg_glEvalMesh1 = Some(mesh1);
    let mut state = 0;
    let args: Vec<u8> = [GL_LINE, 0, 3]
        .into_iter()
        .flat_map(u32::to_le_bytes)
        .collect();
    assert_eq!(
        unsafe {
            execute(
                &api,
                &mut state,
                hook,
                core::ptr::dangling_mut(),
                FEnum_glEvalMesh1,
                &args,
            )
        },
        Err(11)
    );
    assert_eq!(take(), [200, FEnum_glBegin as u64]);
    assert_eq!(
        run(&api, &mut state, FEnum_glEvalMesh1, &[GL_LINE, 0, 3]),
        Ok(())
    );
    assert_eq!(
        take(),
        [
            200,
            FEnum_glBegin as u64,
            GL_LINE as u64,
            0,
            3,
            200,
            FEnum_glEnd as u64
        ]
    );
    assert_eq!(state, 0);
}
