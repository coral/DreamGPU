use super::*;
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
    collections::{BTreeMap, VecDeque},
};
#[derive(Default)]
struct Native {
    next: u32,
    textures: Vec<u32>,
    framebuffers: Vec<u32>,
    renderbuffers: Vec<u32>,
    deleted: Vec<u32>,
    errors: VecDeque<u32>,
    fail_storage: bool,
    fail_gen: u32,
    complete: bool,
    pack: [i32; 5],
    read_fb: i32,
    draw_fb: i32,
    color: [f32; 4],
    mask: [u8; 4],
    scissor: u8,
    clear_count: u32,
    tiles: Vec<(u32, u32)>,
    reads: u32,
    fail_read: bool,
    copies: Vec<(i32, i32, i32, i32)>,
    fail_copy: bool,
}
std::thread_local! {static N:RefCell<Native>=RefCell::new(Native::default());}
unsafe extern "C" fn gen_textures(n: i32, out: *mut u32) {
    N.with(|v| {
        let mut v = v.borrow_mut();
        for i in 0..n {
            v.next += 1;
            let x = if v.next == v.fail_gen { 0 } else { v.next };
            unsafe { *out.add(i as usize) = x };
            if x != 0 {
                v.textures.push(x);
            }
        }
    })
}
unsafe extern "C" fn gen_fb(n: i32, out: *mut u32) {
    assert_eq!(n, 1);
    N.with(|v| {
        let mut v = v.borrow_mut();
        v.next += 1;
        let x = if v.next == v.fail_gen { 0 } else { v.next };
        unsafe { *out = x };
        if x != 0 {
            v.framebuffers.push(x);
        }
    })
}
unsafe extern "C" fn gen_rb(n: i32, out: *mut u32) {
    assert_eq!(n, 1);
    N.with(|v| {
        let mut v = v.borrow_mut();
        v.next += 1;
        let x = if v.next == v.fail_gen { 0 } else { v.next };
        unsafe { *out = x };
        if x != 0 {
            v.renderbuffers.push(x);
        }
    })
}
unsafe extern "C" fn delete(n: i32, p: *const u32) {
    N.with(|v| {
        for i in 0..n {
            v.borrow_mut().deleted.push(unsafe { *p.add(i as usize) });
        }
    })
}
unsafe extern "C" fn bind(_: u32, _: u32) {}
unsafe extern "C" fn bind_fb(target: u32, name: u32) {
    N.with(|v| {
        let mut v = v.borrow_mut();
        if target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER {
            v.read_fb = name as i32;
        }
        if target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER {
            v.draw_fb = name as i32;
        }
    })
}
unsafe extern "C" fn param(_: u32, _: u32, _: i32) {}
unsafe extern "C" fn image(
    _: u32,
    _: i32,
    _: i32,
    _: i32,
    _: i32,
    _: i32,
    _: u32,
    _: u32,
    _: *const core::ffi::c_void,
) {
}
unsafe extern "C" fn storage(_: u32, _: u32, _: i32, _: i32) {
    N.with(|v| {
        let mut v = v.borrow_mut();
        if v.fail_storage {
            v.errors.push_back(GL_OUT_OF_MEMORY);
        }
    })
}
unsafe extern "C" fn attach(_: u32, _: u32, _: u32, _: u32, _: i32) {}
unsafe extern "C" fn attach_rb(_: u32, _: u32, _: u32, _: u32) {}
unsafe extern "C" fn check(_: u32) -> u32 {
    N.with(|v| {
        if v.borrow().complete {
            GL_FRAMEBUFFER_COMPLETE
        } else {
            GL_FRAMEBUFFER_UNSUPPORTED
        }
    })
}
unsafe extern "C" fn error() -> u32 {
    N.with(|v| v.borrow_mut().errors.pop_front().unwrap_or(0))
}
unsafe extern "C" fn color(r: f32, g: f32, b: f32, a: f32) {
    N.with(|v| v.borrow_mut().color = [r, g, b, a]);
}
unsafe extern "C" fn depth(_: f64) {}
unsafe extern "C" fn stencil(_: i32) {}
unsafe extern "C" fn draws(_: i32, _: *const u32) {}
unsafe extern "C" fn clear(_: u32) {
    N.with(|v| v.borrow_mut().clear_count += 1)
}
unsafe extern "C" fn fence(_: u32, _: u32) -> *mut __GLsync {
    2usize as *mut __GLsync
}
unsafe extern "C" fn delete_sync(_: *mut __GLsync) {}
unsafe extern "C" fn wait(_: *mut __GLsync, _: u32, _: u64) {}
unsafe extern "C" fn flush() {}
unsafe extern "C" fn enabled(cap: u32) -> u8 {
    assert_eq!(cap, GL_SCISSOR_TEST);
    N.with(|v| v.borrow().scissor)
}
unsafe extern "C" fn enable(_: u32) {
    N.with(|v| v.borrow_mut().scissor = 1)
}
unsafe extern "C" fn disable(_: u32) {
    N.with(|v| v.borrow_mut().scissor = 0)
}
unsafe extern "C" fn mask(r: u8, g: u8, b: u8, a: u8) {
    N.with(|v| v.borrow_mut().mask = [r, g, b, a])
}
const PACK: [u32; 5] = [
    GL_PACK_ALIGNMENT,
    GL_PACK_ROW_LENGTH,
    GL_PACK_SKIP_ROWS,
    GL_PACK_SKIP_PIXELS,
    GL_PACK_SWAP_BYTES,
];
unsafe extern "C" fn geti(name: u32, out: *mut i32) {
    N.with(|v| {
        let v = v.borrow();
        unsafe {
            *out = match name {
                GL_MAP_COLOR | GL_MAP_STENCIL | GL_INDEX_SHIFT | GL_INDEX_OFFSET => 0,
                GL_READ_FRAMEBUFFER_BINDING => v.read_fb,
                GL_DRAW_FRAMEBUFFER_BINDING => v.draw_fb,
                _ => v.pack[PACK.iter().position(|x| *x == name).unwrap()],
            };
        }
    })
}
unsafe extern "C" fn seti(name: u32, value: i32) {
    N.with(|v| v.borrow_mut().pack[PACK.iter().position(|x| *x == name).unwrap()] = value)
}
unsafe extern "C" fn getf(name: u32, out: *mut f32) {
    if name == GL_COLOR_CLEAR_VALUE {
        N.with(|v| unsafe { core::ptr::copy_nonoverlapping(v.borrow().color.as_ptr(), out, 4) });
    } else {
        unsafe {
            out.write(
                if matches!(
                    name,
                    GL_RED_SCALE | GL_GREEN_SCALE | GL_BLUE_SCALE | GL_ALPHA_SCALE | GL_DEPTH_SCALE
                ) {
                    1.
                } else {
                    0.
                },
            )
        };
    }
}
unsafe extern "C" fn getb(_: u32, out: *mut u8) {
    N.with(|v| unsafe { core::ptr::copy_nonoverlapping(v.borrow().mask.as_ptr(), out, 4) })
}
unsafe extern "C" fn subimage(
    _: u32,
    _: i32,
    _: i32,
    y: i32,
    w: i32,
    h: i32,
    _: u32,
    _: u32,
    p: *const core::ffi::c_void,
) {
    assert!(w > 0 && h > 0);
    let n = w as usize * h as usize * 4;
    assert!(n <= 65536);
    assert!(unsafe { core::slice::from_raw_parts(p.cast::<u8>(), n) }
        .iter()
        .all(|x| *x == 0));
    N.with(|v| v.borrow_mut().tiles.push((y as u32, h as u32)));
}
unsafe extern "C" fn read_image(_: u32, _: i32, _: u32, _: u32, p: *mut core::ffi::c_void) {
    N.with(|v| {
        let mut v = v.borrow_mut();
        v.reads += 1;
        assert_eq!(v.pack, [1, 0, 0, 0, 0]);
        if v.fail_read {
            v.errors.push_back(GL_INVALID_OPERATION);
        } else {
            for i in 0..32 {
                unsafe {
                    p.cast::<u8>().add(i).write(i as u8);
                }
            }
        }
    })
}
fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGenTextures = Some(gen_textures);
    a.dg_glDeleteTextures = Some(delete);
    a.dg_glBindTexture = Some(bind);
    a.dg_glTexParameteri = Some(param);
    a.dg_glTexImage2D = Some(image);
    a.dg_glGenFramebuffers = Some(gen_fb);
    a.dg_glDeleteFramebuffers = Some(delete);
    a.dg_glBindFramebuffer = Some(bind_fb);
    a.dg_glCheckFramebufferStatus = Some(check);
    a.dg_glFramebufferTexture2D = Some(attach);
    a.dg_glFramebufferRenderbuffer = Some(attach_rb);
    a.dg_glGenRenderbuffers = Some(gen_rb);
    a.dg_glDeleteRenderbuffers = Some(delete);
    a.dg_glBindRenderbuffer = Some(bind);
    a.dg_glRenderbufferStorage = Some(storage);
    a.dg_glGetError = Some(error);
    a.dg_glClearColor = Some(color);
    a.dg_glClearDepth = Some(depth);
    a.dg_glClearStencil = Some(stencil);
    a.dg_glDrawBuffers = Some(draws);
    a.dg_glClear = Some(clear);
    a.dg_glFenceSync = Some(fence);
    a.dg_glDeleteSync = Some(delete_sync);
    a.dg_glWaitSync = Some(wait);
    a.dg_glFlush = Some(flush);
    a.dg_glIsEnabled = Some(enabled);
    a.dg_glEnable = Some(enable);
    a.dg_glDisable = Some(disable);
    a.dg_glColorMask = Some(mask);
    a.dg_glGetIntegerv = Some(geti);
    a.dg_glGetFloatv = Some(getf);
    a.dg_glGetBooleanv = Some(getb);
    a.dg_glTexSubImage2D = Some(subimage);
    a.dg_glPixelStorei = Some(seti);
    a.dg_glGetTexImage = Some(read_image);
    a.dg_glPixelTransferi = Some(pixel_transfer_i);
    a.dg_glPixelTransferf = Some(pixel_transfer_f);
    a
}
#[derive(Default)]
struct Allocator {
    live: BTreeMap<usize, Layout>,
    fail: bool,
    max: usize,
}
unsafe extern "C" fn allocate(p: *mut core::ffi::c_void, n: usize) -> *mut core::ffi::c_void {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    if a.fail {
        return null_mut();
    }
    let l = Layout::from_size_align(n, 8).unwrap();
    let p = unsafe { alloc(l) };
    assert!(!p.is_null());
    unsafe { core::ptr::write_bytes(p, 0xa5, n) };
    a.live.insert(p as usize, l);
    a.max = a.max.max(n);
    p.cast()
}
unsafe extern "C" fn free(p: *mut core::ffi::c_void, object: *mut core::ffi::c_void) {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    let l = a.live.remove(&(object as usize)).expect("double free");
    unsafe { dealloc(object.cast(), l) }
}
unsafe extern "C" fn forget(_: *mut core::ffi::c_void, _: *mut Texture) {}
fn memory(a: &DreamGpuGlApi, alloc: &mut Allocator) -> Memory {
    Memory {
        api: a,
        bytes: null_mut(),
        image_bytes: core::ptr::null_mut(),
        count: null_mut(),
        opaque: (alloc as *mut Allocator).cast(),
        allocate,
        free,
        forget_read: forget,
    }
}
#[test]
fn drawable_failure_releases_exact_resources_and_success_initializes_both_buffers() {
    let a = api();
    for stage in 0..3 {
        N.with(|v| {
            *v.borrow_mut() = Native {
                fail_storage: stage == 0,
                complete: stage == 2,
                ..Native::default()
            }
        });
        let mut d = Drawable::EMPTY;
        let result = unsafe { dreamgpu_drawable_init(&a, &mut d, 16, 8) };
        assert_eq!(result, if stage == 2 { 0 } else { DG_GL_ERROR_HOST });
        if stage == 2 {
            N.with(|v| assert_eq!(v.borrow().clear_count, 1));
            unsafe { dreamgpu_drawable_release(&a, &mut d) };
        }
        assert_eq!(d.color, 0);
        assert!(d.last_write.is_null());
        N.with(|v| {
            let v = v.borrow();
            let mut generated = v
                .textures
                .iter()
                .chain(&v.framebuffers)
                .chain(&v.renderbuffers)
                .copied()
                .collect::<Vec<_>>();
            let mut deleted = v.deleted.clone();
            generated.sort();
            deleted.sort();
            assert_eq!(generated, deleted);
        });
    }
}
#[test]
fn zero_allocation_restores_native_state_and_bounds_fallback_tiles() {
    let a = api();
    let mut alloc = Allocator::default();
    let m = memory(&a, &mut alloc);
    for complete in [false, true] {
        N.with(|v| {
            *v.borrow_mut() = Native {
                complete,
                read_fb: 11,
                draw_fb: 12,
                color: [0.1, 0.2, 0.3, 0.4],
                mask: [1, 0, 1, 0],
                scissor: 1,
                ..Native::default()
            }
        });
        let t = Texture {
            name: 7,
            ..Texture::default()
        };
        assert_eq!(unsafe { dreamgpu_texture_zero(&m, &t, 0, 2048, 19) }, 0);
        N.with(|v| {
            let v = v.borrow();
            assert_eq!((v.read_fb, v.draw_fb), (11, 12));
            assert_eq!(v.color, [0.1, 0.2, 0.3, 0.4]);
            assert_eq!(v.mask, [1, 0, 1, 0]);
            assert_eq!(v.scissor, 1);
            assert_eq!(
                v.tiles,
                if complete {
                    vec![]
                } else {
                    vec![(0, 8), (8, 8), (16, 3)]
                }
            );
        });
        assert!(alloc.live.is_empty());
        assert!(alloc.max <= 65536);
    }
    let t = Texture {
        name: 7,
        deleted: 1,
        ..Texture::default()
    };
    N.with(|v| *v.borrow_mut() = Native::default());
    alloc.fail = true;
    assert_eq!(
        unsafe { dreamgpu_texture_zero(&m, &t, 0, 2048, 19) },
        GL_OUT_OF_MEMORY
    );
    N.with(|v| assert!(v.borrow().framebuffers.is_empty()));
}
#[test]
fn read_cache_owns_exact_bytes_invalidates_version_and_restores_pack_on_failure() {
    let a = api();
    let mut alloc = Allocator::default();
    let m = memory(&a, &mut alloc);
    let mut cache = ReadCache::EMPTY;
    let mut t = Texture::default();
    t.widths[0] = 4;
    t.heights[0] = 2;
    let mut errors = 0;
    N.with(|v| {
        *v.borrow_mut() = Native {
            pack: [8, 21, 2, 3, 1],
            ..Native::default()
        }
    });
    let mut out = [0xaa; 18];
    assert_eq!(
        unsafe {
            dreamgpu_texture_read(
                &m,
                &mut cache,
                &mut t,
                &mut errors,
                1,
                GL_TEXTURE_2D,
                0,
                0,
                16,
                out.as_mut_ptr().add(1),
            )
        },
        0
    );
    assert_eq!(&out[1..17], &(0..16u8).collect::<Vec<_>>());
    assert_eq!((out[0], out[17]), (0xaa, 0xaa));
    let mut tail = [0xaa; 26];
    assert_eq!(
        unsafe {
            dreamgpu_texture_read(
                &m,
                &mut cache,
                &mut t,
                &mut errors,
                1,
                GL_TEXTURE_2D,
                0,
                4,
                24,
                tail.as_mut_ptr().add(1),
            )
        },
        0
    );
    assert_eq!(&tail[1..17], &(16..32u8).collect::<Vec<_>>());
    assert_eq!(&tail[17..25], &[0; 8]);
    assert!(alloc.live.is_empty());
    N.with(|v| {
        let v = v.borrow();
        assert_eq!(v.reads, 1);
        assert_eq!(v.pack, [8, 21, 2, 3, 1]);
    });
    assert_eq!(
        unsafe {
            dreamgpu_texture_read(
                &m,
                &mut cache,
                &mut t,
                &mut errors,
                1,
                GL_TEXTURE_2D,
                0,
                0,
                16,
                out.as_mut_ptr().add(1),
            )
        },
        0
    );
    t.version += 1;
    N.with(|v| v.borrow_mut().fail_read = true);
    assert_eq!(
        unsafe {
            dreamgpu_texture_read(
                &m,
                &mut cache,
                &mut t,
                &mut errors,
                1,
                GL_TEXTURE_2D,
                0,
                4,
                16,
                out.as_mut_ptr().add(1),
            )
        },
        DG_GL_ERROR_HOST
    );
    assert!(alloc.live.is_empty());
    assert!(cache.texture.is_null());
    assert_ne!(errors, 0);
    N.with(|v| assert_eq!(v.borrow().pack, [8, 21, 2, 3, 1]));
}

unsafe extern "C" fn copy_image(_: u32, _: i32, _: u32, x: i32, y: i32, w: i32, h: i32, _: i32) {
    N.with(|v| {
        let mut v = v.borrow_mut();
        v.copies.push((x, y, w, h));
        if v.fail_copy {
            v.errors.push_back(GL_OUT_OF_MEMORY);
        }
    })
}
#[test]
fn texture_copy_preserves_signed_source_and_commits_accounting_only_on_success() {
    let mut a = api();
    a.dg_glCopyTexImage2D = Some(copy_image);
    N.with(|v| *v.borrow_mut() = Native::default());
    let mut t = Texture::default();
    let mut total = 0;
    let mut errors = 0;
    let args = [
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        (-2i32) as u32,
        (-3i32) as u32,
        4,
        2,
        0,
    ];
    let wire = args
        .iter()
        .flat_map(|w| w.to_le_bytes())
        .collect::<Vec<_>>();
    assert_eq!(
        unsafe {
            dreamgpu_texture_copy(
                &a,
                &mut t,
                &mut total,
                &mut errors,
                1,
                0,
                FEnum_glCopyTexImage2D,
                wire.as_ptr(),
            )
        },
        DG_GL_ERROR_TEXTURE
    );
    N.with(|v| assert!(v.borrow().copies.is_empty()));
    assert_eq!(
        unsafe {
            dreamgpu_texture_copy(
                &a,
                &mut t,
                &mut total,
                &mut errors,
                1,
                1,
                FEnum_glCopyTexImage2D,
                wire.as_ptr(),
            )
        },
        0
    );
    assert_eq!(
        (total, t.levels[0], t.widths[0], t.heights[0]),
        (32, 32, 4, 2)
    );
    N.with(|v| {
        let mut v = v.borrow_mut();
        assert_eq!(v.copies, [(-2, -3, 4, 2)]);
        v.fail_copy = true;
    });
    let version = t.version;
    assert_eq!(
        unsafe {
            dreamgpu_texture_copy(
                &a,
                &mut t,
                &mut total,
                &mut errors,
                1,
                1,
                FEnum_glCopyTexImage2D,
                wire.as_ptr(),
            )
        },
        0 // Native API failure is a GL error, not a fatal transport error.
    );
    assert_eq!((total, t.levels[0], t.version), (32, 32, version));
    assert_ne!(errors, 0);
}

#[test]
fn zero_native_names_never_clear_default_framebuffer_or_publish_drawable() {
    let a = api();
    for fail_gen in 1..=4 {
        N.with(|v| {
            *v.borrow_mut() = Native {
                fail_gen,
                complete: true,
                ..Native::default()
            }
        });
        let mut d = Drawable::EMPTY;
        assert_eq!(
            unsafe { dreamgpu_drawable_init(&a, &mut d, 16, 8) },
            DG_GL_ERROR_HOST
        );
        N.with(|v| {
            let v = v.borrow();
            assert_eq!(v.clear_count, 0);
            let mut generated = v
                .textures
                .iter()
                .chain(&v.framebuffers)
                .chain(&v.renderbuffers)
                .copied()
                .collect::<Vec<_>>();
            let mut deleted = v.deleted.clone();
            generated.sort();
            deleted.sort();
            assert_eq!(generated, deleted);
        });
        assert_eq!((d.color, d.front, d.depth), (0, 0, 0));
    }
}

unsafe extern "C" fn pixel_transfer_i(_: u32, _: i32) {}
unsafe extern "C" fn pixel_transfer_f(_: u32, _: f32) {}

unsafe extern "C" fn copy_image_1d(t: u32, l: i32, f: u32, x: i32, y: i32, w: i32, b: i32) {
    assert_eq!(b, 1);
    unsafe { copy_image(t, l, f, x, y, w, 1, b) };
}
unsafe extern "C" fn copy_sub_1d(_: u32, _: i32, offset: i32, x: i32, y: i32, w: i32) {
    N.with(|v| v.borrow_mut().copies.push((offset, x, y, w)));
}
unsafe extern "C" fn border_1d(t: u32, _: i32, p: u32, out: *mut i32) {
    assert_eq!(t, GL_TEXTURE_1D);
    unsafe {
        out.write(match p {
            GL_TEXTURE_BORDER => 1,
            GL_TEXTURE_WIDTH => 6,
            _ => panic!("unexpected level query"),
        })
    };
}

#[test]
fn one_dimensional_copy_preserves_border_signed_offsets_and_failed_allocation() {
    let mut a = api();
    a.dg_glCopyTexImage1D = Some(copy_image_1d);
    a.dg_glCopyTexSubImage1D = Some(copy_sub_1d);
    a.dg_glGetTexLevelParameteriv = Some(border_1d);
    N.with(|v| *v.borrow_mut() = Native::default());
    let mut t = Texture {
        target: GL_TEXTURE_1D,
        ..Texture::default()
    };
    let mut total = 0;
    let mut errors = 0;
    let args = [GL_TEXTURE_1D, 0, GL_RGBA8, (-2i32) as u32, 3, 6, 1]
        .into_iter()
        .flat_map(u32::to_le_bytes)
        .collect::<Vec<_>>();
    assert_eq!(
        unsafe {
            dreamgpu_texture_copy(
                &a,
                &mut t,
                &mut total,
                &mut errors,
                1,
                1,
                FEnum_glCopyTexImage1D,
                args.as_ptr(),
            )
        },
        0
    );
    assert_eq!((total, t.widths[0], t.heights[0]), (48, 6, 1));
    N.with(|v| assert_eq!(v.borrow().copies, [(-2, 3, 6, 1)]));
    let sub = [GL_TEXTURE_1D, 0, (-1i32) as u32, 2, 3, 6]
        .into_iter()
        .flat_map(u32::to_le_bytes)
        .collect::<Vec<_>>();
    assert_eq!(
        unsafe {
            dreamgpu_texture_copy(
                &a,
                &mut t,
                &mut total,
                &mut errors,
                1,
                1,
                FEnum_glCopyTexSubImage1D,
                sub.as_ptr(),
            )
        },
        0
    );
    N.with(|v| assert_eq!(v.borrow().copies.last(), Some(&(-1, 2, 3, 6))));
    let mut bad = sub.clone();
    bad[8..12].copy_from_slice(&(-2i32).to_le_bytes());
    let version = t.version;
    assert_eq!(
        unsafe {
            dreamgpu_texture_copy(
                &a,
                &mut t,
                &mut total,
                &mut errors,
                1,
                1,
                FEnum_glCopyTexSubImage1D,
                bad.as_ptr(),
            )
        },
        0
    );
    assert_eq!(t.version, version);
    assert_eq!(errors, 1 << (GL_INVALID_VALUE - GL_INVALID_ENUM));
    N.with(|v| assert_eq!(v.borrow().copies.len(), 2));
    N.with(|v| v.borrow_mut().fail_copy = true);
    assert_eq!(
        unsafe {
            dreamgpu_texture_copy(
                &a,
                &mut t,
                &mut total,
                &mut errors,
                1,
                1,
                FEnum_glCopyTexImage1D,
                args.as_ptr(),
            )
        },
        0
    );
    assert_eq!((total, t.widths[0], t.version), (48, 6, version));
    assert_ne!(errors, 0);
}

unsafe extern "C" fn stripped_border_1d(_: u32, _: i32, p: u32, out: *mut i32) {
    unsafe {
        out.write(match p {
            GL_TEXTURE_WIDTH => 4,
            GL_TEXTURE_BORDER => 0,
            _ => panic!("unexpected native query"),
        })
    };
}
#[test]
fn one_dimensional_metadata_tracks_native_border_stripping_without_fabricating_storage() {
    let mut a = api();
    a.dg_glCopyTexImage1D = Some(copy_image_1d);
    a.dg_glGetTexLevelParameteriv = Some(stripped_border_1d);
    N.with(|v| *v.borrow_mut() = Native::default());
    let mut t = Texture {
        target: GL_TEXTURE_1D,
        ..Texture::default()
    };
    let mut total = 0;
    let mut errors = 0;
    let wire = [GL_TEXTURE_1D, 0, GL_RGBA16, 0, 0, 6, 1]
        .into_iter()
        .flat_map(u32::to_le_bytes)
        .collect::<Vec<_>>();
    assert_eq!(
        unsafe {
            dreamgpu_texture_copy(
                &a,
                &mut t,
                &mut total,
                &mut errors,
                1,
                1,
                FEnum_glCopyTexImage1D,
                wire.as_ptr(),
            )
        },
        0
    );
    assert_eq!(
        (total, t.levels[0], t.widths[0], t.heights[0]),
        (32, 32, 4, 1)
    );
}

unsafe extern "C" fn zero_sub_1d(
    target: u32,
    level: i32,
    offset: i32,
    width: i32,
    format: u32,
    kind: u32,
    data: *const core::ffi::c_void,
) {
    assert_eq!(
        (target, level, offset, width, format, kind),
        (GL_TEXTURE_1D, 0, -1, 2050, GL_RGBA, GL_UNSIGNED_BYTE)
    );
    assert!(
        unsafe { core::slice::from_raw_parts(data.cast::<u8>(), width as usize * 4) }
            .iter()
            .all(|&v| v == 0)
    );
    N.with(|v| v.borrow_mut().tiles.push((offset as u32, width as u32)));
}
#[test]
fn one_dimensional_zero_covers_both_borders_with_bounded_temporary_storage() {
    let mut a = api();
    a.dg_glGetTexLevelParameteriv = Some(border_1d);
    a.dg_glTexSubImage1D = Some(zero_sub_1d);
    let mut alloc = Allocator::default();
    let m = memory(&a, &mut alloc);
    let t = Texture {
        name: 7,
        target: GL_TEXTURE_1D,
        ..Texture::default()
    };
    N.with(|v| *v.borrow_mut() = Native::default());
    assert_eq!(unsafe { dreamgpu_texture_zero(&m, &t, 0, 2050, 1) }, 0);
    assert_eq!(N.with(|v| v.borrow().tiles.clone()), [(u32::MAX, 2050)]);
    assert!(alloc.live.is_empty());
    assert_eq!(alloc.max, 8200);
    alloc.fail = true;
    assert_eq!(
        unsafe { dreamgpu_texture_zero(&m, &t, 0, 2050, 1) },
        GL_OUT_OF_MEMORY
    );
    assert_eq!(N.with(|v| v.borrow().tiles.len()), 1);
}

unsafe extern "C" fn read_precise_image(
    _: u32,
    level: i32,
    format: u32,
    ty: u32,
    p: *mut core::ffi::c_void,
) {
    assert_eq!(level, 0);
    assert_eq!(format, GL_RGBA);
    N.with(|v| {
        let mut v = v.borrow_mut();
        v.reads += 1;
        assert_eq!(v.pack, [1, 0, 0, 0, 0]);
    });
    match ty {
        GL_FLOAT => {
            for i in 0..32 {
                let f = 0.12345f32 + i as f32 / 100.0;
                unsafe {
                    core::ptr::copy_nonoverlapping(
                        f.to_le_bytes().as_ptr(),
                        p.cast::<u8>().add(i * 4),
                        4,
                    );
                }
            }
        }
        GL_UNSIGNED_BYTE => unsafe { core::ptr::write_bytes(p.cast::<u8>(), 0x7b, 32) },
        _ => panic!("unexpected texture read type"),
    }
}
#[test]
fn float_texture_cache_keeps_precision_and_separates_representation() {
    let mut a = api();
    a.dg_glGetTexImage = Some(read_precise_image);
    let mut alloc = Allocator::default();
    let m = memory(&a, &mut alloc);
    let mut cache = ReadCache::EMPTY;
    let mut t = Texture::default();
    t.widths[0] = 4;
    t.heights[0] = 2;
    let mut errors = 0;
    let mut out = [0xaa; 130];
    N.with(|v| {
        *v.borrow_mut() = Native {
            pack: [1, 0, 0, 0, 0],
            ..Native::default()
        }
    });
    for (level, first, capacity) in [
        (DG_GL_TEXTURE_READ_FLOAT, 0, 32),
        (0, 1, 4),
        (DG_GL_TEXTURE_READ_FLOAT, 1, 112),
    ] {
        assert_eq!(
            unsafe {
                dreamgpu_texture_read(
                    &m,
                    &mut cache,
                    &mut t,
                    &mut errors,
                    1,
                    GL_TEXTURE_2D,
                    level,
                    first,
                    capacity,
                    out.as_mut_ptr().add(1),
                )
            },
            0
        );
        if level != 0 {
            assert_eq!(
                f32::from_le_bytes(out[1..5].try_into().unwrap()),
                0.12345 + first as f32 * 4.0 / 100.0
            );
        } else {
            assert_eq!(&out[1..5], &[0x7b; 4]);
        }
        assert_eq!((out[0], out[129]), (0xaa, 0xaa));
    }
    N.with(|v| assert_eq!(v.borrow().reads, 3));
    assert!(alloc.live.is_empty());
    assert!(cache.pixels.is_null());
    assert_eq!(errors, 0);
}

#[test]
fn float_texture_read_rejects_four_gib_cache_before_allocation() {
    let a = api();
    let mut alloc = Allocator::default();
    let m = memory(&a, &mut alloc);
    let mut cache = ReadCache::EMPTY;
    let mut t = Texture::default();
    // Future adapter limits must not make the u32 cache length wrap at 4GiB.
    t.widths[0] = 16384;
    t.heights[0] = 16384;
    let mut errors = 0;
    let mut out = [0xab; 16];
    assert_eq!(
        unsafe {
            dreamgpu_texture_read(
                &m,
                &mut cache,
                &mut t,
                &mut errors,
                1,
                GL_TEXTURE_2D,
                DG_GL_TEXTURE_READ_FLOAT,
                0,
                16,
                out.as_mut_ptr(),
            )
        },
        DG_GL_ERROR_LIMIT
    );
    assert_eq!(out, [0xab; 16]);
    assert_eq!(alloc.max, 0);
    assert!(alloc.live.is_empty());
    assert!(cache.pixels.is_null());
    N.with(|v| assert_eq!(v.borrow().reads, 0));
}
