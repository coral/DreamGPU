use super::*;
use std::cell::RefCell;
#[derive(Default)]
struct State {
    order: [i32; 2],
    values: Vec<f64>,
    calls: u32,
    error: u32,
    fail: bool,
}
std::thread_local! {static S:RefCell<State>=RefCell::new(State{order:[2,3],..Default::default()});}
unsafe extern "C" fn integer(name: u32, p: *mut i32) {
    assert_eq!(name, GL_MAX_EVAL_ORDER);
    unsafe {
        *p = 32;
    }
}
unsafe extern "C" fn get_error() -> u32 {
    S.with(|s| core::mem::take(&mut s.borrow_mut().error))
}
unsafe extern "C" fn map2(
    target: u32,
    u1: f64,
    u2: f64,
    us: i32,
    uo: i32,
    v1: f64,
    v2: f64,
    vs: i32,
    vo: i32,
    p: *const f64,
) {
    assert_eq!(
        (target, u1, u2, us, uo, v1, v2, vs, vo),
        (GL_MAP2_VERTEX_3, -1., 2., 9, 2, 0.25, 0.75, 3, 3)
    );
    S.with(|s| {
        let mut s = s.borrow_mut();
        s.values = unsafe { core::slice::from_raw_parts(p, 18) }.to_vec();
        s.calls += 1;
    });
}
unsafe extern "C" fn get_i(_: u32, pname: u32, p: *mut i32) {
    assert_eq!(pname, GL_ORDER);
    S.with(|s| unsafe { core::ptr::copy_nonoverlapping(s.borrow().order.as_ptr(), p, 2) });
}
unsafe extern "C" fn get_d(_: u32, pname: u32, p: *mut f64) {
    assert_eq!(pname, GL_COEFF);
    S.with(|s| {
        let mut s = s.borrow_mut();
        s.calls += 1;
        unsafe { core::ptr::copy_nonoverlapping(s.values.as_ptr(), p, s.values.len()) };
        if s.fail {
            s.error = GL_INVALID_OPERATION;
        }
    });
}
fn api() -> DreamGpuGlApi {
    let mut a = unsafe { core::mem::zeroed::<DreamGpuGlApi>() };
    a.dg_glGetIntegerv = Some(integer);
    a.dg_glGetError = Some(get_error);
    a.dg_glMap2d = Some(map2);
    a.dg_glGetMapiv = Some(get_i);
    a.dg_glGetMapdv = Some(get_d);
    a
}
#[test]
fn complete_typed_control_grid_native_order_query_and_atomic_outputs() {
    let a = api();
    let args = [GL_MAP2_VERTEX_3, 2, 3, 0, 0, 0, 0, 0];
    let coeff: Vec<f64> = (0..18).map(|i| 16777217.25 + i as f64).collect();
    let mut data: Vec<u8> = [-1f64, 2., 0.25, 0.75]
        .into_iter()
        .flat_map(f64::to_le_bytes)
        .collect();
    data.extend(coeff.iter().flat_map(|x| x.to_le_bytes()));
    let mut errors = 0;
    assert_eq!(unsafe { limit(&a) }, Ok(8));
    unsafe {
        set(&a, &mut errors, FEnum_glMap2d, &args, &data).unwrap();
    }
    S.with(|s| assert_eq!(s.borrow().values, coeff));
    let mut output = [0xa5u8; 152];
    unsafe {
        get(
            &a,
            &mut errors,
            FEnum_glGetMapdv,
            GL_MAP2_VERTEX_3,
            GL_COEFF,
            18,
            output.as_mut_ptr(),
        )
        .unwrap();
    }
    assert_eq!(&output[..144], &data[32..]);
    assert_eq!(&output[144..], &[0xa5; 8]);
    output.fill(0xa5);
    assert_eq!(
        unsafe {
            get(
                &a,
                &mut errors,
                FEnum_glGetMapdv,
                GL_MAP2_VERTEX_3,
                GL_COEFF,
                15,
                output.as_mut_ptr(),
            )
        },
        Err(DG_GL_ERROR_LIMIT)
    );
    assert_eq!(output, [0xa5; 152]);
    S.with(|s| s.borrow_mut().order = [9, 3]);
    assert_eq!(
        unsafe {
            get(
                &a,
                &mut errors,
                FEnum_glGetMapdv,
                GL_MAP2_VERTEX_3,
                GL_COEFF,
                18,
                output.as_mut_ptr(),
            )
        },
        Err(DG_GL_ERROR_LIMIT)
    );
    assert_eq!(output, [0xa5; 152]);
    S.with(|s| {
        let mut s = s.borrow_mut();
        s.order = [2, 3];
        s.fail = true;
    });
    assert_eq!(
        unsafe {
            get(
                &a,
                &mut errors,
                FEnum_glGetMapdv,
                GL_MAP2_VERTEX_3,
                GL_COEFF,
                18,
                output.as_mut_ptr(),
            )
        },
        Err(DG_GL_ERROR_HOST)
    );
    assert_eq!(output, [0xa5; 152]);
    assert_ne!(errors, 0);
}
#[test]
fn malformed_map_and_mesh_are_bounded_before_native_calls() {
    let mut args = [GL_MAP2_VERTEX_4, 8, 8, 0, 0, 0, 0, 0];
    let mut p = vec![0u8; 2080];
    p[8..16].copy_from_slice(&1f64.to_le_bytes());
    p[24..32].copy_from_slice(&1f64.to_le_bytes());
    assert_eq!(validate(FEnum_glMap2d, &args, &p), 0);
    for n in [0, 1, 31, 2079] {
        assert_ne!(validate(FEnum_glMap2d, &args, &p[..n]), 0);
    }
    args[1] = u32::MAX;
    assert_ne!(validate(FEnum_glMap2d, &args, &p), 0);
    args[1] = 8;
    args[0] = GL_MAP1_VERTEX_4;
    assert_ne!(validate(FEnum_glMap2d, &args, &p), 0);
    assert!(!mesh(
        FEnum_glEvalMesh2,
        &[
            GL_POINT,
            i32::MIN as u32,
            i32::MAX as u32,
            i32::MIN as u32,
            i32::MAX as u32
        ]
    ));
    assert!(mesh(FEnum_glEvalMesh2, &[GL_FILL, 10, 9, 0, u32::MAX]));
    assert!(!mesh(FEnum_glEvalMesh1, &[GL_FILL, 0, 1, 0, 0]));
    assert!(mesh(FEnum_glEvalMesh2, &[GL_FILL, 0, 7, 0, 7]));
    assert!(query_count(GL_MAP2_VERTEX_4, GL_COEFF, 256));
    assert!(!query_count(GL_MAP2_VERTEX_4, GL_COEFF, 257));
    assert!(!query_count(GL_MAP1_VERTEX_3, GL_ORDER, 2));
}
std::thread_local! {static V:RefCell<Vec<(u32,i32,i32)>>=const{RefCell::new(Vec::new())};}
unsafe extern "C" fn begin(mode: u32) {
    V.with(|v| v.borrow_mut().push((mode, 0, 0)));
}
unsafe extern "C" fn end() {
    V.with(|v| v.borrow_mut().push((u32::MAX, 0, 0)));
}
unsafe extern "C" fn point1(i: i32) {
    V.with(|v| v.borrow_mut().push((1, i, 0)));
}
unsafe extern "C" fn point2(i: i32, j: i32) {
    V.with(|v| v.borrow_mut().push((2, i, j)));
}
#[test]
fn terminal_mesh_widens_inclusive_counters_and_preserves_primitive_order() {
    let mut api = api();
    api.dg_glBegin = Some(begin);
    api.dg_glEnd = Some(end);
    api.dg_glEvalPoint1 = Some(point1);
    api.dg_glEvalPoint2 = Some(point2);
    let m = i32::MAX;
    unsafe {
        terminal_mesh(
            &api,
            FEnum_glEvalMesh1,
            &[GL_POINT, m as u32, m as u32, 0, 0],
        )
        .unwrap();
    }
    assert_eq!(
        V.with(|v| core::mem::take(&mut *v.borrow_mut())),
        [(GL_POINTS, 0, 0), (1, m, 0), (u32::MAX, 0, 0)]
    );
    unsafe {
        terminal_mesh(
            &api,
            FEnum_glEvalMesh2,
            &[GL_FILL, (m - 1) as u32, m as u32, (m - 1) as u32, m as u32],
        )
        .unwrap();
    }
    assert_eq!(
        V.with(|v| core::mem::take(&mut *v.borrow_mut())),
        [
            (GL_QUAD_STRIP, 0, 0),
            (2, m - 1, m - 1),
            (2, m - 1, m),
            (2, m, m - 1),
            (2, m, m),
            (u32::MAX, 0, 0)
        ]
    );
    // An empty inner range must not make billions of native empty Begin/End pairs.
    assert!(mesh(
        FEnum_glEvalMesh2,
        &[GL_LINE, 1, 0, i32::MIN as u32, m as u32]
    ));
    unsafe {
        terminal_mesh(
            &api,
            FEnum_glEvalMesh2,
            &[GL_LINE, 1, 0, i32::MIN as u32, m as u32],
        )
        .unwrap();
    }
    assert!(V.with(|v| v.borrow().is_empty()));
}
