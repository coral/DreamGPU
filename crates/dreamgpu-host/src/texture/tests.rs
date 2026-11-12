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
    generate: i32,
    generate_error: u32,
    /// Native targets of 2D images/subimages, zero fills and parameter queries.
    targets: Vec<u32>,
    zero_targets: Vec<u32>,
    parameter_targets: Vec<u32>,
    /// Native internal formats of images and proxies.
    internals: Vec<i32>,
    /// NUL-terminated GL_VERSION and GL_EXTENSIONS answers, and how often
    /// either was asked.
    version: &'static [u8],
    extensions: &'static [u8],
    strings: u32,
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
    internal: i32,
    width: i32,
    border: i32,
    _: u32,
    _: u32,
    pixels: *const c_void,
) {
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.internals.push(internal);
        v.width = width - if v.strip_border { 2 * border } else { 0 };
        v.border = if v.strip_border { 0 } else { border };
    });
    image(1, pixels);
}
unsafe extern "C" fn image2(
    target: u32,
    _: i32,
    internal: i32,
    width: i32,
    height: i32,
    border: i32,
    _: u32,
    _: u32,
    pixels: *const c_void,
) {
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.targets.push(target);
        v.internals.push(internal);
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
    target: u32,
    _: i32,
    _: i32,
    _: i32,
    _: i32,
    _: i32,
    _: u32,
    _: u32,
    pixels: *const c_void,
) {
    MOCK.with(|v| v.borrow_mut().targets.push(target));
    image(4, pixels);
}
unsafe extern "C" fn zero(
    _: *mut c_void,
    _: *mut Texture,
    target: u32,
    _: u32,
    _: u32,
    _: u32,
) -> u32 {
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.zeros += 1;
        v.zero_targets.push(target);
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
unsafe extern "C" fn parameter(target: u32, key: u32, out: *mut i32) {
    assert_eq!(key, GL_GENERATE_MIPMAP);
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.parameter_targets.push(target);
        unsafe { *out = v.generate };
        v.error = v.generate_error;
    });
}
unsafe extern "C" fn string(name: u32) -> *const u8 {
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.strings += 1;
        match name {
            GL_VERSION => v.version.as_ptr(),
            GL_EXTENSIONS => v.extensions.as_ptr(),
            _ => core::ptr::null(),
        }
    })
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
    api.dg_glGetString = Some(string);
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

#[test]
fn generated_mip_chains_are_charged_with_their_defining_image() {
    let mut api = api();
    let mut t = Texture::default();
    let mut total = 0;
    MOCK.with(|v| *v.borrow_mut() = Mock::default());
    let image = |w: u32, h: u32| {
        [
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            w,
            h,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
        ]
    };
    // Without the native query the charge is exactly the defined level.
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexImage2D,
            image(4, 4),
            64
        ),
        (Ok(()), 0)
    );
    assert_eq!((total, t.levels[0]), (64, 64));
    // GENERATE_MIPMAP set: a third more, rounded up, replacing the old charge.
    api.dg_glGetTexParameteriv = Some(parameter);
    MOCK.with(|v| v.borrow_mut().generate = 1);
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexImage2D,
            image(4, 4),
            64
        ),
        (Ok(()), 0)
    );
    assert_eq!((total, t.levels[0]), (86, 86));
    assert_eq!(t.widths[0], 4);
    // Subimages never change the charge.
    let tile = [GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE];
    assert_eq!(
        run(&api, &mut t, &mut total, FEnum_glTexSubImage2D, tile, 16),
        (Ok(()), 0)
    );
    assert_eq!(total, 86);
    // A native without the parameter reports an error: consumed, never
    // charged and never surfaced as the upload's own error.
    MOCK.with(|v| {
        let mut v = v.borrow_mut();
        v.generate = 0;
        v.generate_error = GL_INVALID_ENUM;
    });
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexImage2D,
            image(4, 4),
            64
        ),
        (Ok(()), 0)
    );
    assert_eq!((total, t.levels[0]), (64, 64));
    MOCK.with(|v| v.borrow_mut().generate_error = 0);
    // The chain charge alone can exhaust the budget before native allocation.
    MOCK.with(|v| v.borrow_mut().generate = 1);
    // Replacing the 64-byte level fits on its own; its 22-byte chain does not.
    total = u64::from(DG_GL_MAX_TEXTURE_BYTES) - 10;
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexImage2D,
            image(4, 4),
            64
        )
        .0,
        Err(9)
    );
}

#[test]
fn cube_faces_track_levels_charges_bounds_and_undefined_images_separately() {
    let mut api = api();
    api.dg_glGetTexParameteriv = Some(parameter);
    MOCK.with(|v| *v.borrow_mut() = Mock::default());
    let mut faces = [Face::EMPTY; FACES];
    let mut t = Texture {
        target: GL_TEXTURE_CUBE_MAP,
        faces: &mut faces,
        ..Texture::default()
    };
    let mut total = 0;
    let face = |n: u32| GL_TEXTURE_CUBE_MAP_POSITIVE_X + n;
    let image = |target: u32, side: u32| {
        [
            target,
            0,
            GL_RGBA8,
            side,
            side,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
        ]
    };
    // Two faces of different sizes: each is charged and tracked on its own,
    // and the texture's arrays summarize them.
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexImage2D,
            image(face(0), 4),
            64
        ),
        (Ok(()), 0)
    );
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexImage2D,
            image(face(3), 8),
            256
        ),
        (Ok(()), 0)
    );
    assert_eq!(total, 320);
    assert_eq!((faces[0].widths[0], faces[0].levels[0]), (4, 64));
    assert_eq!((faces[3].widths[0], faces[3].heights[0]), (8, 8));
    assert_eq!((t.levels[0], t.widths[0], t.undefined_levels), (320, 0, 0));
    // Mipmap generation is a parameter of the cube map, never of a face.
    assert_eq!(
        MOCK.with(|v| v.borrow().parameter_targets.clone()),
        [GL_TEXTURE_CUBE_MAP; 2]
    );
    // Redefining one face replaces only that face's charge.
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexImage2D,
            image(face(0), 2),
            16
        ),
        (Ok(()), 0)
    );
    assert_eq!(
        (total, t.levels[0], faces[0].levels[0], faces[3].levels[0]),
        (272, 272, 16, 256)
    );
    // Subimage bounds use the face's own level: inside -Y, outside +X.
    let tile = |target: u32| [target, 0, 2, 2, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE];
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexSubImage2D,
            tile(face(0)),
            16
        ),
        (Ok(()), GL_INVALID_VALUE)
    );
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexSubImage2D,
            tile(face(3)),
            16
        ),
        (Ok(()), 0)
    );
    // A failed zero fill poisons that face's level, and the summary, until a
    // complete subimage of the same face defines it.
    MOCK.with(|v| v.borrow_mut().zero_error = GL_OUT_OF_MEMORY);
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexImage2D,
            image(face(4), 2),
            0
        ),
        (Err(11), GL_OUT_OF_MEMORY)
    );
    MOCK.with(|v| v.borrow_mut().zero_error = 0);
    assert_eq!((faces[4].undefined_levels, t.undefined_levels), (1, 1));
    assert_eq!(faces[0].undefined_levels | faces[3].undefined_levels, 0);
    let full = |target: u32| [target, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE];
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexSubImage2D,
            full(face(0)),
            16
        ),
        (Ok(()), 0)
    );
    assert_eq!(t.undefined_levels, 1);
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexSubImage2D,
            full(face(4)),
            16
        ),
        (Ok(()), 0)
    );
    assert_eq!((faces[4].undefined_levels, t.undefined_levels), (0, 0));
    assert_eq!(total, 288);
    // Native calls and the zero fill name the face, never TEXTURE_2D.
    MOCK.with(|v| {
        let v = v.borrow();
        assert!(v.targets.iter().all(|&target| cube_face(target).is_some()));
        assert_eq!(v.zero_targets, [face(4)]);
    });
    // A face record never reaches a 2D texture, nor a 2D record a cube map.
    let calls = MOCK.with(|v| v.borrow().uploads.len());
    assert_eq!(
        run(
            &api,
            &mut t,
            &mut total,
            FEnum_glTexImage2D,
            image(GL_TEXTURE_2D, 2),
            16
        )
        .0,
        Err(DG_GL_ERROR_TEXTURE)
    );
    let mut flat = Texture::default();
    assert_eq!(
        run(
            &api,
            &mut flat,
            &mut total,
            FEnum_glTexImage2D,
            image(face(1), 2),
            16
        )
        .0,
        Err(DG_GL_ERROR_TEXTURE)
    );
    assert_eq!(MOCK.with(|v| v.borrow().uploads.len()), calls);
    assert_eq!(total, 288);
}

#[test]
fn texture_and_face_layouts_match_the_c_mirror() {
    let pointer = core::mem::size_of::<usize>();
    assert_eq!(
        core::mem::offset_of!(Texture, faces),
        core::mem::offset_of!(Texture, levels) + 12 * 8
    );
    assert_eq!(core::mem::size_of::<Texture>() % 8, 0);
    assert_eq!(
        core::mem::offset_of!(Texture, depths),
        core::mem::offset_of!(Texture, faces) + pointer
    );
    assert_eq!(
        core::mem::size_of::<Texture>(),
        (core::mem::offset_of!(Texture, depths) + 12 * 4).next_multiple_of(8)
    );
    assert_eq!(core::mem::offset_of!(Face, undefined_levels), 3 * 12 * 4);
    assert_eq!(core::mem::offset_of!(Face, levels), 3 * 12 * 4 + 8);
    assert_eq!(core::mem::size_of::<Face>(), 3 * 12 * 4 + 8 + 12 * 8);
    assert_eq!(
        binding_target(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z),
        GL_TEXTURE_CUBE_MAP
    );
    assert_eq!(
        binding_target(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z + 1),
        GL_PROXY_TEXTURE_CUBE_MAP
    );
    assert_eq!(cube_face(GL_TEXTURE_CUBE_MAP), None);
    assert_eq!(cube_face(GL_TEXTURE_CUBE_MAP_POSITIVE_Y), Some(2));
}

#[test]
fn rgb565_images_use_native_rgb565_or_exact_rgb8_storage() {
    for (version, extensions, native) in [
        (
            &b"4.6 (Compatibility Profile) Mesa 26.2.6\0"[..],
            &b"\0"[..],
            DG_GL_RGB565,
        ),
        (b"4.1 Metal - 89.3\0", b"\0", DG_GL_RGB565),
        (
            b"2.1 Metal - 89.3\0",
            b"GL_ARB_vertex_program GL_ARB_ES2_compatibility\0",
            DG_GL_RGB565,
        ),
        (b"2.1 Metal - 89.3\0", b"GL_ARB_vertex_program\0", GL_RGB8),
        (
            b"3.3 Mesa\0",
            b"GL_ARB_ES2_compatibility_x GL_x_GL_ARB_ES2_compatibility\0",
            GL_RGB8,
        ),
    ] {
        MOCK.with(|v| {
            let mut v = v.borrow_mut();
            *v = Mock::default();
            v.version = version;
            v.extensions = extensions;
        });
        let api = api();
        let mut t = Texture {
            target: GL_TEXTURE_2D,
            ..Texture::default()
        };
        let mut total = 0;
        let (rgb, short) = (GL_RGB, GL_UNSIGNED_SHORT_5_6_5);
        let image = [GL_TEXTURE_2D, 0, DG_GL_RGB565, 4, 4, 0, rgb, short];
        assert_eq!(
            run(&api, &mut t, &mut total, FEnum_glTexImage2D, image, 32),
            (Ok(()), 0)
        );
        // Proxies judge the same native storage.
        let square = [GL_PROXY_TEXTURE_2D, 0, DG_GL_RGB565, 4, 4, 0, rgb, short];
        assert_eq!(unsafe { proxy(&api, &square) }, 0);
        let line = [GL_PROXY_TEXTURE_1D, 0, DG_GL_RGB565, 4, 1, 0, rgb, short];
        assert_eq!(unsafe { proxy(&api, &line) }, 0);
        assert_eq!(
            MOCK.with(|v| v.borrow().internals.clone()),
            [native as i32; 3],
            "{:?}",
            core::str::from_utf8(version)
        );
        // Every other format is native GL's, without asking its strings.
        MOCK.with(|v| {
            let mut v = v.borrow_mut();
            v.internals.clear();
            v.strings = 0;
        });
        let other = [GL_TEXTURE_2D, 0, GL_RGB5, 4, 4, 0, rgb, short];
        assert_eq!(
            run(&api, &mut t, &mut total, FEnum_glTexImage2D, other, 32),
            (Ok(()), 0)
        );
        assert_eq!(unsafe { native_internal_format(&api, GL_RGB8) }, GL_RGB8);
        assert_eq!(
            MOCK.with(|v| v.borrow().internals.clone()),
            [GL_RGB5 as i32]
        );
        assert_eq!(MOCK.with(|v| v.borrow().strings), 0);
    }
    // Without the string query the host keeps RGB8 storage.
    let mut api = api();
    api.dg_glGetString = None;
    assert_eq!(
        unsafe { native_internal_format(&api, DG_GL_RGB565) },
        GL_RGB8
    );
    for (version, rgb565) in [
        (&b"4.1"[..], true),
        (b"4.10.2 vendor", true),
        (b"10.0", true),
        (b"5", true),
        (b"4.0.30319", false),
        (b"4", false),
        (b"3.3 (Core Profile)", false),
        (b"2.1 ATI-4.14", false),
        (b"", false),
        (b"OpenGL 4.6", false),
        (b"99999999999999999999.1", false),
    ] {
        assert_eq!(version_has_rgb565(version), rgb565, "{version:?}");
    }
}
