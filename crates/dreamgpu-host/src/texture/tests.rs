use super::*;
use std::cell::RefCell;
#[derive(Default)]
struct Mock {
    stores: [i32; 5],
    uploads: Vec<(u32, bool)>,
    error: u32,
    zero_error: u32,
    zeros: u32,
    waits: u32,
    fences: u32,
    deletes: u32,
    width: i32,
    height: i32,
    border: i32,
    strip_border: bool,
    query_error: u32,
    offsets: Vec<i32>,
}
thread_local! { static MOCK: RefCell<Mock> = RefCell::new(Mock::default()); }
fn index(key: u32) -> usize {
    [
        GL_UNPACK_ALIGNMENT,
        GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_PIXELS,
        GL_UNPACK_SWAP_BYTES,
    ]
    .iter()
    .position(|&k| k == key)
    .unwrap()
}
unsafe extern "C" fn get(key: u32, out: *mut i32) {
    unsafe {
        *out = MOCK.with(|v| v.borrow().stores[index(key)]);
    }
}
unsafe extern "C" fn store(key: u32, val: i32) {
    MOCK.with(|v| v.borrow_mut().stores[index(key)] = val);
}
unsafe extern "C" fn error() -> u32 {
    MOCK.with(|v| core::mem::take(&mut v.borrow_mut().error))
}
fn image(kind: u32, pixels: *const c_void) {
    MOCK.with(|v| v.borrow_mut().uploads.push((kind, pixels.is_null())));
}
unsafe extern "C" fn image1(
    _: u32,
    _: i32,
    _: i32,
    width: i32,
    border: i32,
    _: u32,
    _: u32,
    pixels: *const c_void,
) {
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.width = width - if v.strip_border { 2 * border } else { 0 };
        v.border = if v.strip_border { 0 } else { border };
    });
    image(1, pixels);
}
unsafe extern "C" fn image2(
    _: u32,
    _: i32,
    _: i32,
    width: i32,
    height: i32,
    border: i32,
    _: u32,
    _: u32,
    pixels: *const c_void,
) {
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.width = width;
        v.height = height;
        v.border = border;
    });
    image(2, pixels);
}
unsafe extern "C" fn sub1(
    _: u32,
    _: i32,
    offset: i32,
    _: i32,
    _: u32,
    _: u32,
    pixels: *const c_void,
) {
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.offsets.push(offset);
        if v.width == 0 {
            v.error = GL_INVALID_OPERATION;
        }
    });
    image(3, pixels);
}
unsafe extern "C" fn sub2(
    _: u32,
    _: i32,
    _: i32,
    _: i32,
    _: i32,
    _: i32,
    _: u32,
    _: u32,
    pixels: *const c_void,
) {
    image(4, pixels);
}
unsafe extern "C" fn zero(_: *mut c_void, _: *mut Texture, _: u32, _: u32, _: u32) -> u32 {
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.zeros += 1;
        v.zero_error
    })
}
unsafe extern "C" fn fence(_: u32, _: u32) -> *mut __GLsync {
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.fences += 1;
        v.fences as usize as *mut __GLsync
    })
}
unsafe extern "C" fn delete(_: *mut __GLsync) {
    MOCK.with(|v| v.borrow_mut().deletes += 1);
}
unsafe extern "C" fn wait_sync(_: *mut __GLsync, _: u32, _: u64) {
    MOCK.with(|v| v.borrow_mut().waits += 1);
}
unsafe extern "C" fn level(_: u32, _: i32, key: u32, out: *mut i32) {
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        unsafe {
            *out = if key == GL_TEXTURE_WIDTH {
                v.width
            } else if key == GL_TEXTURE_HEIGHT {
                v.height
            } else {
                v.border
            };
        }
        v.error = v.query_error;
    });
}
fn api() -> DreamGpuGlApi {
    let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    api.dg_glGetIntegerv = Some(get);
    api.dg_glGetTexLevelParameteriv = Some(level);
    api.dg_glPixelStorei = Some(store);
    api.dg_glGetError = Some(error);
    api.dg_glTexImage1D = Some(image1);
    api.dg_glTexImage2D = Some(image2);
    api.dg_glTexSubImage1D = Some(sub1);
    api.dg_glTexSubImage2D = Some(sub2);
    api.dg_glWaitSync = Some(wait_sync);
    api.dg_glDeleteSync = Some(delete);
    api.dg_glFenceSync = Some(fence);
    api
}
fn run(
    api: &DreamGpuGlApi,
    t: &mut Texture,
    total: &mut u64,
    function: u32,
    args: [u32; 8],
    bytes: u32,
) -> (Result<(), u32>, u32) {
    let mut error = 0;
    let data = [0x12u8; 256];
    let result = unsafe {
        upload(
            api,
            t,
            total,
            1,
            function,
            &args,
            data.as_ptr(),
            bytes,
            zero,
            core::ptr::null_mut(),
            &mut error,
        )
    };
    (result, error)
}
#[test]
fn allocation_accounting_zero_failure_recovery_and_pixelstore() {
    let api = api();
    let mut t = Texture::default();
    let mut total = 0;
    let saved = [8, 19, 3, 5, 1];
    MOCK.with(|v| {
        *v.borrow_mut() = Mock {
            stores: saved,
            ..Mock::default()
        }
    });
    let image = [
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        4,
        4,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage2D, image, 64),
        (Ok(()), 0)
    );
    assert_eq!(total, 64);
    assert_eq!(
        (t.widths[0], t.heights[0], t.levels[0], t.undefined_levels),
        (4, 4, 64, 0)
    );
    assert_eq!(MOCK.with(|v| v.borrow().stores), saved);
    MOCK.with(|v| v.borrow_mut().zero_error = GL_OUT_OF_MEMORY);
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage2D, image, 0),
        (Err(11), GL_OUT_OF_MEMORY)
    );
    assert_eq!(total, 64);
    assert_eq!(t.undefined_levels, 1);
    assert_eq!(t.version, 2);
    assert_eq!(MOCK.with(|v| v.borrow().stores), saved);
    let tile = [GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE];
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexSubImage2D, tile, 64),
        (Ok(()), 0)
    );
    assert_eq!(t.undefined_levels, 0);
    assert_eq!(t.version, 3);
    MOCK.with(|v| v.borrow_mut().error = GL_OUT_OF_MEMORY);
    let bigger = [
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        8,
        8,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage2D, bigger, 256),
        (Ok(()), GL_OUT_OF_MEMORY)
    );
    assert_eq!(total, 64);
    assert_eq!(t.widths[0], 4);
    assert_eq!(t.version, 3);
    assert_eq!(MOCK.with(|v| v.borrow().stores), saved);
    let mut bad = tile;
    bad[2] = 1;
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexSubImage2D, bad, 64),
        (Ok(()), GL_INVALID_VALUE)
    );
    total = u64::from(DG_GL_MAX_TEXTURE_BYTES);
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage2D, bigger, 256).0,
        Err(9)
    );
}
#[test]
fn same_context_and_foreign_version_fence_ownership() {
    let api = api();
    let mut t = Texture::default();
    MOCK.with(|v| *v.borrow_mut() = Mock::default());
    unsafe {
        written(&api, &mut t, 1);
        for _ in 0..1000 {
            wait(&api, &mut t, 1);
        }
        wait(&api, &mut t, 2);
        wait(&api, &mut t, 2);
        wait(&api, &mut t, 3);
        wait(&api, &mut t, 3);
    }
    assert_eq!(MOCK.with(|v| v.borrow().waits), 2);
    unsafe {
        written(&api, &mut t, 1);
        wait(&api, &mut t, 2);
    }
    assert_eq!(
        MOCK.with(|v| (v.borrow().waits, v.borrow().fences, v.borrow().deletes)),
        (3, 2, 1)
    );
    assert_eq!((t.writer_serial, t.waiter_serial, t.version), (1, 2, 2));
    unsafe {
        wait(&api, &mut t, 3);
        wait(&api, &mut t, 2); // Conservative waiter eviction.
        written(&api, &mut t, 2);
        wait(&api, &mut t, 2);
        wait(&api, &mut t, 1);
        wait(&api, &mut t, 4); // New identity despite recycled allocation address.
        written(&api, &mut t, 4);
        wait(&api, &mut t, 4);
    }
    assert_eq!(
        MOCK.with(|v| (v.borrow().waits, v.borrow().fences, v.borrow().deletes)),
        (7, 4, 3)
    );
    assert_eq!((t.writer_serial, t.waiter_serial, t.version), (4, 0, 4));
}

#[test]
fn border_upload_native_dimensions_signed_bounds_and_full_precision_budget() {
    let api = api();
    let mut t = Texture {
        target: GL_TEXTURE_1D,
        ..Texture::default()
    };
    let mut total = 0;
    MOCK.with(|v| *v.borrow_mut() = Mock::default());
    let image = [
        GL_TEXTURE_1D,
        0,
        GL_RGBA16,
        6,
        1,
        1,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage1D, image, 24),
        (Ok(()), 0)
    );
    assert_eq!((t.widths[0], total), (6, 48));
    assert_eq!(MOCK.with(|v| (v.borrow().width, v.borrow().border)), (6, 1));
    let mut sub = [
        GL_TEXTURE_1D,
        0,
        (-1i32) as u32,
        0,
        6,
        1,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexSubImage1D, sub, 24),
        (Ok(()), 0)
    );
    assert_eq!(MOCK.with(|v| v.borrow().offsets.clone()), [-1]);
    sub[2] = 4;
    sub[4] = 2;
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexSubImage1D, sub, 8),
        (Ok(()), GL_INVALID_VALUE)
    );
    sub[2] = (-2i32) as u32;
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexSubImage1D, sub, 8),
        (Ok(()), GL_INVALID_VALUE)
    );
    assert_eq!(MOCK.with(|v| v.borrow().offsets.len()), 1);
    MOCK.with(|v| v.borrow_mut().zero_error = GL_OUT_OF_MEMORY);
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage1D, image, 0),
        (Err(11), GL_OUT_OF_MEMORY)
    );
    assert_eq!(t.undefined_levels, 1);
    sub[2] = (-1i32) as u32;
    sub[4] = 6;
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexSubImage1D, sub, 24),
        (Ok(()), 0)
    );
    assert_eq!(t.undefined_levels, 0);
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.strip_border = true;
        v.zero_error = 0;
    });
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage1D, image, 24),
        (Ok(()), 0)
    );
    assert_eq!((t.widths[0], total), (4, 32)); // actual native image, never requested fake width
    MOCK.with(|v| v.borrow_mut().query_error = GL_INVALID_OPERATION);
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage1D, image, 24),
        (Err(11), GL_INVALID_OPERATION)
    );
    assert_eq!((t.widths[0], total, t.undefined_levels), (0, 48, 1));
    total = u64::from(DG_GL_MAX_TEXTURE_BYTES);
    let mut larger = image;
    larger[3] = 10;
    let calls = MOCK.with(|v| v.borrow().uploads.len());
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage1D, larger, 40).0,
        Err(9)
    );
    assert_eq!(MOCK.with(|v| v.borrow().uploads.len()), calls);
}

#[test]
fn ordinary_borderless_1d_retains_its_previous_storage_admission() {
    let api = api();
    let mut t = Texture {
        target: GL_TEXTURE_1D,
        ..Texture::default()
    };
    let mut total = u64::from(DG_GL_MAX_TEXTURE_BYTES) - 16;
    MOCK.with(|v| *v.borrow_mut() = Mock::default());
    let image = [
        GL_TEXTURE_1D,
        0,
        GL_RGBA8,
        4,
        1,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage1D, image, 16),
        (Ok(()), 0)
    );
    assert_eq!(t.levels[0], 16);
    assert_eq!(total, u64::from(DG_GL_MAX_TEXTURE_BYTES));
}

#[test]
fn undefined_native_array_uses_its_actual_error_without_poisoning_later_uploads() {
    let api = api();
    let mut t = Texture {
        target: GL_TEXTURE_1D,
        ..Texture::default()
    };
    let mut total = 0;
    MOCK.with(|v| *v.borrow_mut() = Mock::default());
    let sub = [GL_TEXTURE_1D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE];
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexSubImage1D, sub, 4),
        (Ok(()), GL_INVALID_OPERATION)
    );
    assert_eq!((t.version, total), (0, 0));
    assert_eq!(MOCK.with(|v| v.borrow().offsets.len()), 1); // actual native error, not width shadow
    let image = [
        GL_TEXTURE_1D,
        0,
        GL_RGBA8,
        4,
        1,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage1D, image, 16),
        (Ok(()), 0)
    );
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexSubImage1D, sub, 4),
        (Ok(()), 0)
    );
    assert_eq!((t.version, total), (2, 16));
}

#[test]
fn empty_redefinition_releases_full_precision_storage_without_zero_fill() {
    let api = api();
    MOCK.with(|v| *v.borrow_mut() = Mock::default());
    let mut t = Texture::default();
    let mut total = 0;
    let mut a = [
        GL_TEXTURE_2D,
        0,
        GL_RGBA16,
        4,
        4,
        0,
        GL_RGBA,
        GL_UNSIGNED_SHORT,
    ];
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexImage2D, a, 128),
        (Ok(()), 0)
    );
    assert_eq!(total, 128);
    assert_eq!(t.levels[0], 128);
    for (w, h) in [(0, 4), (4, 0), (0, 0)] {
        a[3] = w;
        a[4] = h;
        assert_eq!(
            run(&api, &mut t, &mut total, FEnum_glTexImage2D, a, 0),
            (Ok(()), 0)
        );
        assert_eq!(
            (
                total,
                t.levels[0],
                t.widths[0],
                t.heights[0],
                t.undefined_levels
            ),
            (0, 0, w, h, 0)
        );
    }
    assert_eq!(MOCK.with(|v| v.borrow().zeros), 0);
}
