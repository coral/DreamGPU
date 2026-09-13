use super::*;
use std::alloc::{alloc, dealloc, Layout};
#[derive(Default)]
struct Check {
    live: usize,
    fail_alloc: bool,
    stage: u32,
    creates: u32,
    destroys: u32,
    owner: *mut NativeImage,
}
unsafe extern "C" fn allocate(p: *mut c_void, size: usize) -> *mut c_void {
    let c = unsafe { &mut *p.cast::<Check>() };
    if c.fail_alloc {
        return null_mut();
    }
    assert_eq!(size, core::mem::size_of::<NativeImage>());
    let p = unsafe { alloc(Layout::new::<NativeImage>()) };
    assert!(!p.is_null());
    unsafe { core::ptr::write_bytes(p, 0xa5, size) };
    c.live += 1;
    p.cast()
}
unsafe extern "C" fn free(p: *mut c_void, image: *mut c_void) {
    let c = unsafe { &mut *p.cast::<Check>() };
    assert_eq!(c.live, 1);
    c.live -= 1;
    unsafe { dealloc(image.cast(), Layout::new::<NativeImage>()) }
}
unsafe extern "C" fn forget(_: *mut c_void, _: *mut crate::texture::Texture) {}
unsafe extern "C" fn create(p: *mut c_void, image: *mut NativeImage) -> u32 {
    let c = unsafe { &mut *p.cast::<Check>() };
    c.creates += 1;
    c.owner = image;
    unsafe {
        assert_eq!(
            (
                (*image).width,
                (*image).height,
                (*image).fd,
                (*image).fence_fd
            ),
            (640, 480, -1, -1)
        );
        assert!((*image).surface.is_null());
        assert!((*image).native_image.is_null());
        assert!((*image).context.is_null());
        if c.stage >= 1 {
            (*image).surface = 7usize as *mut c_void;
        }
        if c.stage >= 2 {
            (*image).fd = 42;
            (*image).bo = 8usize as *mut c_void;
        }
        if c.stage >= 3 {
            (*image).native_image = 9usize as *mut c_void;
        }
    }
    if c.stage == 4 {
        0
    } else {
        DG_GL_ERROR_HOST
    }
}
unsafe extern "C" fn destroy(p: *mut c_void, image: *mut NativeImage) {
    let c = unsafe { &mut *p.cast::<Check>() };
    c.destroys += 1;
    assert_ne!(image, c.owner);
    unsafe {
        assert!((*c.owner).surface.is_null());
        assert_eq!((*c.owner).fd, -1);
        assert_eq!((*image).surface as usize, if c.stage >= 1 { 7 } else { 0 });
        assert_eq!((*image).fd, if c.stage >= 2 { 42 } else { -1 });
        assert_eq!(
            (*image).native_image as usize,
            if c.stage >= 3 { 9 } else { 0 }
        );
    }
}
#[test]
fn native_image_partial_allocation_rolls_back_each_stage_and_clears_owner_before_destroy() {
    for stage in 0..=4 {
        let mut c = Check {
            stage,
            ..Check::default()
        };
        let m = Memory {
            api: core::ptr::null(),
            bytes: null_mut(),
            image_bytes: core::ptr::null_mut(),
            count: null_mut(),
            opaque: (&mut c as *mut Check).cast(),
            allocate,
            free,
            forget_read: forget,
        };
        let mut error = 99;
        let image = unsafe {
            dreamgpu_image_new(
                &m,
                640,
                480,
                create,
                destroy,
                (&mut c as *mut Check).cast(),
                &mut error,
            )
        };
        if stage == 4 {
            assert_eq!(error, 0);
            assert!(!image.is_null());
            assert_eq!(c.live, 1);
            unsafe { dreamgpu_image_free(&m, image, destroy, (&mut c as *mut Check).cast()) };
        } else {
            assert_eq!(error, DG_GL_ERROR_HOST);
            assert!(image.is_null());
        }
        assert_eq!((c.live, c.creates, c.destroys), (0, 1, 1));
    }
}
#[test]
fn native_image_limits_and_allocation_failure_do_not_run_platform_create() {
    let mut c = Check {
        fail_alloc: true,
        ..Check::default()
    };
    let m = Memory {
        api: core::ptr::null(),
        bytes: null_mut(),
        image_bytes: core::ptr::null_mut(),
        count: null_mut(),
        opaque: (&mut c as *mut Check).cast(),
        allocate,
        free,
        forget_read: forget,
    };
    let mut error = 99;
    assert!(unsafe {
        dreamgpu_image_new(
            &m,
            u32::MAX,
            480,
            create,
            destroy,
            (&mut c as *mut Check).cast(),
            &mut error,
        )
    }
    .is_null());
    assert_eq!(error, DG_GL_ERROR_LIMIT);
    assert!(unsafe {
        dreamgpu_image_new(
            &m,
            640,
            480,
            create,
            destroy,
            (&mut c as *mut Check).cast(),
            &mut error,
        )
    }
    .is_null());
    assert_eq!(error, DG_GL_ERROR_HOST);
    assert_eq!((c.live, c.creates, c.destroys), (0, 0, 0));
}
