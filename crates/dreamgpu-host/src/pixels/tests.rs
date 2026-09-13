use super::*;
use std::cell::RefCell;
#[derive(Clone, PartialEq, Debug)]
struct State {
    ints: [i32; 4],
    floats: [f32; 10],
    size: i32,
    limit: i32,
    calls: usize,
}
thread_local! {static STATE:RefCell<State>=const{RefCell::new(State{ints:[0;4],floats:[0.;10],size:1,limit:256,calls:0})};}
unsafe extern "C" fn geti(name: u32, out: *mut i32) {
    STATE.with(|s| {
        let s = s.borrow();
        unsafe {
            out.write(if name == GL_MAX_PIXEL_MAP_TABLE {
                s.limit
            } else if let Some(i) = INTEGER.iter().position(|v| *v == name) {
                s.ints[i]
            } else {
                s.size
            })
        }
    })
}
unsafe extern "C" fn getf(name: u32, out: *mut f32) {
    STATE.with(|s| unsafe {
        out.write(s.borrow().floats[FLOAT.iter().position(|v| *v == name).unwrap()])
    })
}
unsafe extern "C" fn seti(name: u32, value: i32) {
    STATE.with(|s| s.borrow_mut().ints[INTEGER.iter().position(|v| *v == name).unwrap()] = value)
}
unsafe extern "C" fn setf(name: u32, value: f32) {
    STATE.with(|s| s.borrow_mut().floats[FLOAT.iter().position(|v| *v == name).unwrap()] = value)
}
unsafe extern "C" fn get_map(_: u32, out: *mut u32) {
    STATE.with(|s| {
        let mut s = s.borrow_mut();
        s.calls += 1;
        for i in 0..s.size as usize {
            unsafe { out.add(i).write(u32::MAX - i as u32) }
        }
    })
}
unsafe extern "C" fn error() -> u32 {
    0
}
fn api() -> DreamGpuGlApi {
    let mut a = unsafe { core::mem::zeroed::<DreamGpuGlApi>() };
    a.dg_glGetIntegerv = Some(geti);
    a.dg_glGetFloatv = Some(getf);
    a.dg_glPixelTransferi = Some(seti);
    a.dg_glPixelTransferf = Some(setf);
    a.dg_glGetPixelMapuiv = Some(get_map);
    a.dg_glGetError = Some(error);
    a
}
#[test]
fn internal_transfer_guard_preserves_exact_state_without_attribute_stack() {
    let a = api();
    let original = State {
        ints: [1, 1, i32::MAX, i32::MIN],
        floats: [2., 3., 4., 5., 6., -0., -1., 0.25, -0.5, 9.],
        size: 1,
        limit: 256,
        calls: 0,
    };
    STATE.with(|s| *s.borrow_mut() = original.clone());
    {
        let _guard = unsafe { Neutral::new(&a) }.unwrap();
        STATE.with(|s| {
            let s = s.borrow();
            assert_eq!(s.ints, [0; 4]);
            assert_eq!(s.floats, [1., 1., 1., 1., 1., 0., 0., 0., 0., 0.]);
        });
    }
    STATE.with(|s| {
        assert_eq!(*s.borrow(), original);
        assert_eq!(s.borrow().floats[5].to_bits(), (-0f32).to_bits());
    });
    let mut incomplete = a;
    incomplete.dg_glPixelTransferf = None;
    assert!(unsafe { Neutral::new(&incomplete) }.is_err());
    STATE.with(|s| assert_eq!(*s.borrow(), original));
}
#[test]
fn map_bounds_and_native_size_mismatch_never_write_or_call_getter() {
    assert!(!map_count(u32::MAX, u32::MAX));
    assert!(!map_count(GL_PIXEL_MAP_I_TO_I, 3));
    assert!(map_count(GL_PIXEL_MAP_R_TO_R, 3));
    assert!(map_count(GL_PIXEL_MAP_I_TO_I, 256));
    assert!(!map_count(GL_PIXEL_MAP_R_TO_R, 257));
    let a = api();
    let mut errors = 0;
    let mut output = [0xccu8; 20];
    STATE.with(|s| {
        s.borrow_mut().size = 4;
        s.borrow_mut().calls = 0;
        s.borrow_mut().limit = 256;
    });
    assert_eq!(
        unsafe { get_map_checked(&a, &mut errors, 2, output.as_mut_ptr()) },
        Err(DG_GL_ERROR_LIMIT)
    );
    assert_eq!(output, [0xcc; 20]);
    STATE.with(|s| assert_eq!(s.borrow().calls, 0));
    unsafe { get_map_checked(&a, &mut errors, 4, output.as_mut_ptr()) }.unwrap();
    assert_eq!(&output[16..], &[0xcc; 4]);
    assert_eq!(&output[..4], &u32::MAX.to_le_bytes());
    STATE.with(|s| s.borrow_mut().limit = 32);
    assert_eq!(unsafe { limit(&a) }.unwrap(), 32);
    STATE.with(|s| s.borrow_mut().limit = 1024);
    assert_eq!(unsafe { limit(&a) }.unwrap(), 256);
}
unsafe fn get_map_checked(a: &DreamGpuGlApi, e: *mut u32, n: u32, p: *mut u8) -> Result<(), u32> {
    unsafe { super::get_map(a, e, FEnum_glGetPixelMapuiv, GL_PIXEL_MAP_I_TO_I, n, p) }
}
