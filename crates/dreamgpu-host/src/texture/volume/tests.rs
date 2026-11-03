use super::*;
use std::cell::RefCell;

/// Native calls of one test: unpack state, images, subimage boxes, copies,
/// and errors to inject.
#[derive(Default)]
struct Mock {
    stores: [i32; 7],
    /// UNPACK_* values seen by each native image/subimage call.
    upload_stores: Vec<[i32; 7]>,
    images: Vec<([i32; 3], bool)>,
    subs: Vec<[i32; 6]>,
    copies: Vec<[i32; 7]>,
    size: [i32; 3],
    /// The native image is narrower than the request.
    shrink: bool,
    error: u32,
    image_error: u32,
    sub_error: u32,
    generate: i32,
    fences: u32,
}
thread_local! { static MOCK: RefCell<Mock> = RefCell::new(Mock::default()); }
fn with<R>(f: impl FnOnce(&mut Mock) -> R) -> R {
    MOCK.with(|m| f(&mut m.borrow_mut()))
}
fn unpack_index(key: u32) -> Option<usize> {
    UNPACK.iter().position(|&k| k == key)
}
unsafe extern "C" fn get_integer(key: u32, out: *mut i32) {
    let value = with(|m| unpack_index(key).map_or(0, |i| m.stores[i]));
    unsafe { *out = value };
}
unsafe extern "C" fn get_float(_: u32, out: *mut f32) {
    unsafe { *out = 0. };
}
unsafe extern "C" fn transfer_i(_: u32, _: i32) {}
unsafe extern "C" fn transfer_f(_: u32, _: f32) {}
unsafe extern "C" fn store(key: u32, value: i32) {
    with(|m| m.stores[unpack_index(key).unwrap()] = value);
}
unsafe extern "C" fn error() -> u32 {
    with(|m| core::mem::take(&mut m.error))
}
unsafe extern "C" fn parameter(target: u32, key: u32, out: *mut i32) {
    assert_eq!((target, key), (GL_TEXTURE_3D, GL_GENERATE_MIPMAP));
    unsafe { *out = with(|m| m.generate) };
}
unsafe extern "C" fn level(target: u32, _: i32, key: u32, out: *mut i32) {
    assert_eq!(target, GL_TEXTURE_3D);
    let size = with(|m| m.size);
    unsafe {
        *out = match key {
            GL_TEXTURE_WIDTH => size[0],
            GL_TEXTURE_HEIGHT => size[1],
            GL_TEXTURE_DEPTH => size[2],
            _ => panic!("level parameter {key:#x}"),
        }
    };
}
unsafe extern "C" fn image(
    target: u32,
    _: i32,
    _: i32,
    w: i32,
    h: i32,
    d: i32,
    border: i32,
    _: u32,
    _: u32,
    pixels: *const c_void,
) {
    assert_eq!(border, 0);
    with(|m| {
        m.upload_stores.push(m.stores);
        if target == GL_TEXTURE_3D {
            m.images.push(([w, h, d], pixels.is_null()));
        }
        m.error = m.image_error;
        if m.image_error == 0 && target == GL_TEXTURE_3D {
            m.size = if m.shrink { [w / 2, h, d] } else { [w, h, d] };
        }
    });
}
unsafe extern "C" fn sub(
    target: u32,
    _: i32,
    x: i32,
    y: i32,
    z: i32,
    w: i32,
    h: i32,
    d: i32,
    _: u32,
    _: u32,
    pixels: *const c_void,
) {
    assert_eq!(target, GL_TEXTURE_3D);
    assert!(!pixels.is_null());
    with(|m| {
        m.upload_stores.push(m.stores);
        m.subs.push([x, y, z, w, h, d]);
        m.error = m.sub_error;
    });
}
unsafe extern "C" fn copy_image(
    target: u32,
    _: i32,
    x: i32,
    y: i32,
    z: i32,
    sx: i32,
    sy: i32,
    w: i32,
    h: i32,
) {
    assert_eq!(target, GL_TEXTURE_3D);
    with(|m| m.copies.push([x, y, z, sx, sy, w, h]));
}
unsafe extern "C" fn fence(_: u32, _: u32) -> *mut __GLsync {
    with(|m| {
        m.fences += 1;
        m.fences as usize as *mut __GLsync
    })
}
unsafe extern "C" fn delete(_: *mut __GLsync) {}
unsafe extern "C" fn wait_sync(_: *mut __GLsync, _: u32, _: u64) {}
fn api() -> DreamGpuGlApi {
    let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    api.dg_glGetIntegerv = Some(get_integer);
    api.dg_glGetFloatv = Some(get_float);
    api.dg_glPixelTransferi = Some(transfer_i);
    api.dg_glPixelTransferf = Some(transfer_f);
    api.dg_glPixelStorei = Some(store);
    api.dg_glGetError = Some(error);
    api.dg_glGetTexParameteriv = Some(parameter);
    api.dg_glGetTexLevelParameteriv = Some(level);
    api.dg_glTexImage3D = Some(image);
    api.dg_glTexSubImage3D = Some(sub);
    api.dg_glCopyTexSubImage3D = Some(copy_image);
    api.dg_glFenceSync = Some(fence);
    api.dg_glDeleteSync = Some(delete);
    api.dg_glWaitSync = Some(wait_sync);
    api
}
const HEADER: usize = 16;
unsafe extern "C" fn allocate(_: *mut c_void, bytes: usize) -> *mut c_void {
    let layout = std::alloc::Layout::from_size_align(bytes + HEADER, HEADER).unwrap();
    unsafe {
        let p = std::alloc::alloc(layout);
        p.cast::<usize>().write(bytes);
        p.add(HEADER).cast()
    }
}
unsafe extern "C" fn free(_: *mut c_void, p: *mut c_void) {
    unsafe {
        let base = p.cast::<u8>().sub(HEADER);
        let bytes = base.cast::<usize>().read();
        let layout = std::alloc::Layout::from_size_align(bytes + HEADER, HEADER).unwrap();
        std::alloc::dealloc(base, layout);
    }
}
unsafe extern "C" fn forget(_: *mut c_void, _: *mut Texture) {}
fn memory(api: &DreamGpuGlApi, total: &mut u64, count: &mut u32) -> names::Memory {
    names::Memory {
        api,
        bytes: total,
        image_bytes: core::ptr::null_mut(),
        count,
        opaque: core::ptr::null_mut(),
        allocate,
        free,
        forget_read: forget,
    }
}
fn volume_texture() -> Texture {
    Texture {
        name: 9,
        target: GL_TEXTURE_3D,
        guest_name: 9,
        refs: 1,
        ..Texture::default()
    }
}
fn image3d(w: u32, h: u32, d: u32) -> [u32; 10] {
    [
        GL_TEXTURE_3D,
        0,
        GL_RGBA8,
        w,
        h,
        d,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        0,
    ]
}
fn sub3d(x: u32, y: u32, z: u32, w: u32, h: u32, d: u32) -> [u32; 10] {
    [
        GL_TEXTURE_3D,
        0,
        x,
        y,
        z,
        w,
        h,
        d,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ]
}
fn run(
    m: &names::Memory,
    t: &mut Texture,
    function: u32,
    a: [u32; 10],
    data: &[u8],
) -> (Result<(), u32>, u32) {
    let mut gl_error = 0;
    let result = unsafe { upload(m, t, m.bytes, 1, function, &a, data, &mut gl_error) };
    (result, gl_error)
}
fn rgba(w: u32, h: u32, d: u32) -> Vec<u8> {
    vec![0x5a; (w * h * d * 4) as usize]
}

#[test]
fn record_shapes_are_exact_and_bounded() {
    let image = |a: [u32; 10], data: &[u8]| validate(FEnum_glTexImage3D, &a, data);
    assert_eq!(image(image3d(4, 2, 3), &rgba(4, 2, 3)), 0);
    assert_eq!(image(image3d(4, 2, 3), &[]), 0);
    assert_eq!(image(image3d(4, 2, 3), &rgba(4, 2, 2)), DG_GL_ERROR_TEXTURE);
    assert_eq!(image(image3d(256, 256, 256), &[]), 0);
    for [w, h, d] in [[257, 1, 1], [1, 257, 1], [1, 1, 257]] {
        assert_eq!(image(image3d(w, h, d), &[]), DG_GL_ERROR_TEXTURE);
    }
    // Levels shrink the limit (to zero beyond level 8); level 11 still
    // admits an empty image.
    let mut a = image3d(128, 1, 1);
    a[1] = 1;
    assert_eq!(image(a, &[]), 0);
    a[3] = 129;
    assert_eq!(image(a, &[]), DG_GL_ERROR_TEXTURE);
    a[1] = 9;
    a[3] = 0;
    assert_eq!(image(a, &[]), DG_GL_ERROR_TEXTURE);
    a[1] = 11;
    a[3..6].copy_from_slice(&[0, 0, 0]);
    assert_eq!(image(a, &[]), 0);
    a[1] = 12;
    assert_eq!(image(a, &[]), DG_GL_ERROR_TEXTURE);
    // A border of one is a record (refused at execution); two is not.
    let mut a = image3d(3, 3, 3);
    a[6] = 1;
    assert_eq!(image(a, &rgba(3, 3, 3)), 0);
    a[3] = 1;
    assert_eq!(image(a, &[]), DG_GL_ERROR_TEXTURE);
    a[3] = 3;
    a[6] = 2;
    assert_eq!(image(a, &[]), DG_GL_ERROR_TEXTURE);
    // Uncompressed internal formats and the 2D wire types only.
    for (internal, format, kind, ok) in [
        (3, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, true),
        (GL_LUMINANCE8, GL_LUMINANCE, GL_FLOAT, true),
        (GL_RGBA16, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, true),
        (
            GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            false,
        ),
        (GL_COMPRESSED_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, false),
        (GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, false),
        (GL_RGBA8, GL_RGB, GL_UNSIGNED_SHORT_4_4_4_4, false),
        (5, GL_RGBA, GL_UNSIGNED_BYTE, false),
    ] {
        let a = [GL_TEXTURE_3D, 0, internal, 1, 1, 1, 0, format, kind, 0];
        assert_eq!(
            image(a, &[]) == 0,
            ok,
            "{internal:#x} {format:#x} {kind:#x}"
        );
    }
    // Proxies carry no payload and may exceed the limits.
    let mut proxy = image3d(4096, 4096, 4096);
    proxy[0] = GL_PROXY_TEXTURE_3D;
    assert_eq!(image(proxy, &[]), 0);
    proxy[3..6].copy_from_slice(&[1, 1, 1]);
    assert_eq!(image(proxy, &rgba(1, 1, 1)), DG_GL_ERROR_TEXTURE);
    proxy[6] = 1;
    assert_eq!(image(proxy, &[]), DG_GL_ERROR_TEXTURE);
    for target in [GL_TEXTURE_2D, GL_PROXY_TEXTURE_2D, GL_TEXTURE_CUBE_MAP] {
        let mut a = image3d(1, 1, 1);
        a[0] = target;
        assert_eq!(image(a, &[]), DG_GL_ERROR_TEXTURE);
    }
    // Subimages carry exactly their pixels, at least one texel of each extent;
    // offsets are execution rules.
    let subimage = |a: [u32; 10], data: &[u8]| validate(FEnum_glTexSubImage3D, &a, data);
    assert_eq!(
        subimage(sub3d(u32::MAX, 7, 300, 2, 3, 4), &rgba(2, 3, 4)),
        0
    );
    assert_eq!(subimage(sub3d(0, 0, 0, 2, 3, 4), &[]), DG_GL_ERROR_TEXTURE);
    assert_eq!(subimage(sub3d(0, 0, 0, 0, 3, 4), &[]), DG_GL_ERROR_TEXTURE);
    assert_eq!(
        subimage(sub3d(0, 0, 0, 1, 1, 257), &rgba(1, 1, 257)),
        DG_GL_ERROR_TEXTURE
    );
    let mut a = sub3d(0, 0, 0, 1, 1, 1);
    a[1] = 12;
    assert_eq!(subimage(a, &rgba(1, 1, 1)), DG_GL_ERROR_TEXTURE);
    // Copies: the 3D target, a level and at most the protocol extent.
    let copy = [GL_TEXTURE_3D, 3, 1, 2, 3, 4, 5, 256, 0, 0];
    assert_eq!(validate_copy(&copy), 0);
    for (index, value) in [(0, GL_TEXTURE_2D), (1, 12), (7, 257), (8, 257)] {
        let mut a = copy;
        a[index] = value;
        assert_eq!(validate_copy(&a), DG_GL_ERROR_TEXTURE, "{index}");
    }
}

#[test]
fn wire_entry_points_parse_nine_and_ten_words() {
    use crate::gl_validation::*;
    assert_eq!(
        dreamgpu_gl_function_words(FEnum_glTexImage3D),
        DG_GL_FUNCTION_INLINE_DATA | 9
    );
    assert_eq!(
        dreamgpu_gl_function_words(FEnum_glTexSubImage3D),
        DG_GL_FUNCTION_INLINE_DATA | 10
    );
    assert_eq!(dreamgpu_gl_function_words(FEnum_glCopyTexSubImage3D), 9);
    // The EXT aliases never travel; the frontend submits core numbers.
    assert_eq!(dreamgpu_gl_function_words(FEnum_glTexImage3DEXT), u32::MAX);
    let bytes = |words: &[u32]| {
        words
            .iter()
            .flat_map(|w| w.to_le_bytes())
            .collect::<Vec<_>>()
    };
    let pixels = [9u8; 6];
    let sub = bytes(&[GL_TEXTURE_3D, 0, 1, 1, 1, 1, 1, 2, GL_RGB, GL_UNSIGNED_BYTE]);
    assert_eq!(
        unsafe {
            dreamgpu_gl_data_validate(FEnum_glTexSubImage3D, sub.as_ptr(), pixels.as_ptr(), 6)
        },
        0
    );
    // The tenth word (type) decides the payload size.
    let wrong = bytes(&[GL_TEXTURE_3D, 0, 1, 1, 1, 1, 1, 2, GL_RGB, GL_FLOAT]);
    assert_eq!(
        unsafe {
            dreamgpu_gl_data_validate(FEnum_glTexSubImage3D, wrong.as_ptr(), pixels.as_ptr(), 6)
        },
        DG_GL_ERROR_TEXTURE
    );
    let copy = bytes(&[GL_TEXTURE_3D, 0, 0, 0, 0, 0, 0, 4, 257]);
    assert_eq!(
        unsafe { dreamgpu_gl_call_validate(FEnum_glCopyTexSubImage3D, copy.as_ptr()) },
        DG_GL_ERROR_TEXTURE
    );
    let copy = bytes(&[GL_TEXTURE_3D, 0, 0, 0, 0, 0, 0, 4, 4]);
    assert_eq!(
        unsafe { dreamgpu_gl_call_validate(FEnum_glCopyTexSubImage3D, copy.as_ptr()) },
        0
    );
    // Queries: the 3D binding and limit, TEXTURE_DEPTH, the 3D enable, the
    // parameters, and a first pixel anywhere in the largest level.
    let read = 4 << DG_GL_TEXTURE_READ_COUNT_SHIFT;
    let mut kind = 0;
    for (function, args, count) in [
        (FEnum_glGetIntegerv, [GL_TEXTURE_BINDING_3D, 0, 0], 1),
        (FEnum_glGetIntegerv, [GL_MAX_3D_TEXTURE_SIZE, 0, 0], 1),
        (FEnum_glIsEnabled, [GL_TEXTURE_3D, 0, 0], 1),
        (
            FEnum_glGetTexLevelParameteriv,
            [GL_PROXY_TEXTURE_3D, 3, GL_TEXTURE_DEPTH],
            1,
        ),
        (
            FEnum_glGetTexLevelParameteriv,
            [GL_TEXTURE_2D, 0, GL_TEXTURE_DEPTH],
            1,
        ),
        (
            FEnum_glGetTexParameteriv,
            [GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, 0],
            1,
        ),
        (
            FEnum_glGetTexParameteriv,
            [GL_TEXTURE_3D, GL_TEXTURE_RESIDENT, 0],
            1,
        ),
        (FEnum_glGetTexImage, [GL_TEXTURE_3D, read, (1 << 24) - 1], 4),
        (FEnum_glGetTexImage, [GL_TEXTURE_3D, read, 1 << 24], 0),
        (FEnum_glGetTexImage, [GL_TEXTURE_2D, read, 1 << 24], 0),
        (
            FEnum_glGetCompressedTexImage,
            [GL_TEXTURE_3D, 1 << 16, 0],
            0,
        ),
    ] {
        let args = bytes(&args);
        assert_eq!(
            unsafe { dreamgpu_gl_query_shape(function, args.as_ptr(), &mut kind) },
            count,
            "{function} {args:?}"
        );
    }
}

#[test]
fn images_charge_every_image_and_zero_define_in_bounded_slabs() {
    let api = api();
    let (mut total, mut count) = (0, 0);
    let m = memory(&api, &mut total, &mut count);
    let mut t = volume_texture();
    let saved = [8, 19, 3, 5, 1, 7, 2];
    with(|v| {
        *v = Mock {
            stores: saved,
            ..Mock::default()
        }
    });
    // A payload defines the image directly, under canonical unpack state.
    assert_eq!(
        run(
            &m,
            &mut t,
            FEnum_glTexImage3D,
            image3d(4, 2, 3),
            &rgba(4, 2, 3)
        ),
        (Ok(()), 0)
    );
    assert_eq!(unsafe { *m.bytes }, 4 * 2 * 3 * 4);
    assert_eq!(
        (
            t.widths[0],
            t.heights[0],
            t.depths[0],
            t.levels[0],
            t.undefined_levels
        ),
        (4, 2, 3, 96, 0)
    );
    with(|v| {
        assert_eq!(v.images, [([4, 2, 3], false)]);
        assert_eq!(v.upload_stores, [[1, 0, 0, 0, 0, 0, 0]]);
        assert!(v.subs.is_empty());
        assert_eq!(v.stores, saved);
    });
    // Without payload: 256x256x256 RGBA8 is 64 MiB, zero-defined four whole
    // images (one MiB) per subimage.
    assert_eq!(
        run(&m, &mut t, FEnum_glTexImage3D, image3d(256, 256, 256), &[]),
        (Ok(()), 0)
    );
    assert_eq!(unsafe { *m.bytes }, 64 << 20);
    assert_eq!(t.depths[0], 256);
    with(|v| {
        assert_eq!(v.images.last(), Some(&([256, 256, 256], true)));
        assert_eq!(v.subs.len(), 64);
        assert_eq!(v.subs[1], [0, 0, 4, 256, 256, 4]);
        assert!(v.upload_stores[1..]
            .iter()
            .all(|s| *s == [1, 0, 0, 0, 0, 0, 0]));
        assert_eq!(v.stores, saved);
        v.subs.clear();
    });
    // High-precision storage is charged at eight bytes per texel.
    let mut level1 = image3d(128, 128, 2);
    level1[1] = 1;
    level1[2] = GL_RGBA16;
    assert_eq!(
        run(&m, &mut t, FEnum_glTexImage3D, level1, &[]),
        (Ok(()), 0)
    );
    assert_eq!(t.levels[1], 128 * 128 * 2 * 8);
    assert_eq!(unsafe { *m.bytes }, (64 << 20) + 128 * 128 * 2 * 8);
    with(|v| assert_eq!(v.subs, [[0, 0, 0, 128, 128, 2]]));
    // Redefining a level replaces its charge; generated levels add a third.
    with(|v| v.generate = 1);
    assert_eq!(
        run(
            &m,
            &mut t,
            FEnum_glTexImage3D,
            image3d(3, 3, 3),
            &rgba(3, 3, 3)
        ),
        (Ok(()), 0)
    );
    assert_eq!(t.levels[0], 108 + 36);
    assert_eq!(unsafe { *m.bytes }, 144 + 128 * 128 * 2 * 8);
    with(|v| v.generate = 0);
    // The budget counts depth: two 128 MiB levels exceed 256 MiB with the
    // first one's 256 KiB, and the record fails without touching the level.
    let mut big = image3d(256, 256, 256);
    big[2] = GL_RGBA16;
    assert_eq!(run(&m, &mut t, FEnum_glTexImage3D, big, &[]), (Ok(()), 0));
    let mut other = volume_texture();
    let calls = with(|v| v.images.len());
    assert_eq!(
        run(&m, &mut other, FEnum_glTexImage3D, big, &[]),
        (Err(DG_GL_ERROR_LIMIT), 0)
    );
    assert_eq!(with(|v| v.images.len()), calls);
    assert_eq!(other.levels, [0; 12]);
    assert_eq!(unsafe { *m.bytes }, (128 << 20) + 128 * 128 * 2 * 8);
}

#[test]
fn failures_are_gl_errors_and_keep_or_mark_the_level() {
    let api = api();
    let (mut total, mut count) = (0, 0);
    let m = memory(&api, &mut total, &mut count);
    let mut t = volume_texture();
    with(|v| *v = Mock::default());
    assert_eq!(
        run(
            &m,
            &mut t,
            FEnum_glTexImage3D,
            image3d(2, 2, 2),
            &rgba(2, 2, 2)
        ),
        (Ok(()), 0)
    );
    let version = t.version;
    // A native image error defines nothing: the level keeps its image.
    with(|v| v.image_error = GL_INVALID_VALUE);
    assert_eq!(
        run(&m, &mut t, FEnum_glTexImage3D, image3d(4, 4, 4), &[]),
        (Ok(()), GL_INVALID_VALUE)
    );
    assert_eq!((t.depths[0], t.levels[0], t.version), (2, 32, version));
    with(|v| v.image_error = 0);
    // A border is refused before native GL.
    let mut border = image3d(4, 4, 4);
    border[6] = 1;
    let calls = with(|v| v.images.len());
    assert_eq!(
        run(&m, &mut t, FEnum_glTexImage3D, border, &[]),
        (Ok(()), GL_INVALID_OPERATION)
    );
    assert_eq!(with(|v| v.images.len()), calls);
    // Subimages outside the level never reach native GL.
    for a in [
        sub3d(1, 0, 0, 2, 1, 1),
        sub3d(0, 1, 0, 1, 2, 1),
        sub3d(0, 0, 1, 1, 1, 2),
        sub3d(u32::MAX, 0, 0, 1, 1, 1),
        sub3d(0, 0, u32::MAX, 1, 1, 1),
    ] {
        assert_eq!(
            run(
                &m,
                &mut t,
                FEnum_glTexSubImage3D,
                a,
                &rgba(a[5], a[6], a[7])
            ),
            (Ok(()), GL_INVALID_VALUE),
            "{a:?}"
        );
    }
    assert!(with(|v| v.subs.is_empty()));
    // A failed zero fill keeps the storage charged and the level undefined
    // until a whole-level subimage defines it.
    with(|v| v.sub_error = GL_OUT_OF_MEMORY);
    assert_eq!(
        run(&m, &mut t, FEnum_glTexImage3D, image3d(2, 2, 2), &[]),
        (Err(DG_GL_ERROR_TEXTURE), GL_OUT_OF_MEMORY)
    );
    assert_eq!((t.undefined_levels, t.levels[0]), (1, 32));
    with(|v| v.sub_error = 0);
    assert_eq!(
        run(
            &m,
            &mut t,
            FEnum_glTexSubImage3D,
            sub3d(0, 0, 1, 2, 2, 1),
            &rgba(2, 2, 1)
        ),
        (Ok(()), 0)
    );
    assert_eq!(t.undefined_levels, 1);
    assert_eq!(
        run(
            &m,
            &mut t,
            FEnum_glTexSubImage3D,
            sub3d(0, 0, 0, 2, 2, 2),
            &rgba(2, 2, 2)
        ),
        (Ok(()), 0)
    );
    assert_eq!(t.undefined_levels, 0);
    // A native subimage error (format mismatch, an undefined level) is the
    // guest's error and leaves the texture's version alone.
    let version = t.version;
    with(|v| v.sub_error = GL_INVALID_OPERATION);
    assert_eq!(
        run(
            &m,
            &mut t,
            FEnum_glTexSubImage3D,
            sub3d(0, 0, 0, 1, 1, 1),
            &rgba(1, 1, 1)
        ),
        (Ok(()), GL_INVALID_OPERATION)
    );
    assert_eq!(t.version, version);
    with(|v| v.sub_error = 0);
    // A native image smaller than requested is published as defined natively.
    with(|v| v.shrink = true);
    assert_eq!(
        run(
            &m,
            &mut t,
            FEnum_glTexImage3D,
            image3d(4, 2, 2),
            &rgba(4, 2, 2)
        ),
        (Ok(()), 0)
    );
    assert_eq!((t.widths[0], t.levels[0]), (2, 32));
    // Only a 3D texture owns 3D records.
    let mut flat = Texture::default();
    assert_eq!(
        run(
            &m,
            &mut flat,
            FEnum_glTexImage3D,
            image3d(1, 1, 1),
            &rgba(1, 1, 1)
        )
        .0,
        Err(DG_GL_ERROR_TEXTURE)
    );
}

#[test]
fn proxies_reject_beyond_the_virtual_limits_and_copies_check_the_level() {
    let api = api();
    with(|v| *v = Mock::default());
    let mut mask = 0b100;
    let mut a = image3d(64, 32, 16);
    a[0] = GL_PROXY_TEXTURE_3D;
    a[1] = 2;
    assert_eq!(unsafe { proxy(&api, &mut mask, &a) }, 0);
    assert_eq!(mask, 0);
    a[3] = 65;
    assert_eq!(unsafe { proxy(&api, &mut mask, &a) }, 0);
    assert_eq!(mask, 0b100);
    a[3] = 4;
    a[6] = 1;
    assert_eq!(unsafe { proxy(&api, &mut mask, &a) }, 0);
    assert_eq!(mask, 0b100);
    // A native proxy error is the guest's and leaves the level's state alone.
    a[6] = 0;
    mask = 0b100;
    with(|v| v.image_error = GL_INVALID_ENUM);
    assert_eq!(unsafe { proxy(&api, &mut mask, &a) }, GL_INVALID_ENUM);
    assert_eq!(mask, 0b100);
    with(|v| v.image_error = 0);

    let mut t = volume_texture();
    t.widths[1] = 4;
    t.heights[1] = 4;
    t.depths[1] = 2;
    let wide = |a: [u32; 9]| {
        let mut out = [0; 10];
        out[..9].copy_from_slice(&a);
        out
    };
    for (a, error) in [
        ([GL_TEXTURE_3D, 1, 2, 2, 1, 0, 0, 2, 2], 0),
        ([GL_TEXTURE_3D, 1, 3, 0, 0, 0, 0, 2, 1], GL_INVALID_VALUE),
        ([GL_TEXTURE_3D, 1, 0, 3, 0, 0, 0, 1, 2], GL_INVALID_VALUE),
        ([GL_TEXTURE_3D, 1, 0, 0, 2, 0, 0, 1, 1], GL_INVALID_VALUE),
        (
            [GL_TEXTURE_3D, 1, u32::MAX, 0, 0, 0, 0, 1, 1],
            GL_INVALID_VALUE,
        ),
    ] {
        let mut errors = 0;
        assert_eq!(
            unsafe { copy(&api, &mut t, &mut errors, 1, 1, &wide(a)) },
            0
        );
        assert_eq!(errors != 0, error != 0, "{a:?}");
    }
    with(|v| assert_eq!(v.copies, [[2, 2, 1, 0, 0, 2, 2]]));
    // No drawable, a 2D texture or a malformed record are record faults.
    let mut errors = 0;
    let ok = wide([GL_TEXTURE_3D, 1, 0, 0, 0, 0, 0, 1, 1]);
    assert_eq!(
        unsafe { copy(&api, &mut t, &mut errors, 1, 0, &ok) },
        DG_GL_ERROR_TEXTURE
    );
    let mut flat = Texture::default();
    assert_eq!(
        unsafe { copy(&api, &mut flat, &mut errors, 1, 1, &ok) },
        DG_GL_ERROR_TEXTURE
    );
    let mut bad = ok;
    bad[7] = 300;
    assert_eq!(
        unsafe { copy(&api, &mut t, &mut errors, 1, 1, &bad) },
        DG_GL_ERROR_TEXTURE
    );
    assert_eq!(errors, 0);
}
