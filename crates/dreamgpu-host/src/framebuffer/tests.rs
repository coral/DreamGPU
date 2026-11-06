use super::*;
use crate::query::{dreamgpu_gl_query, QueryState};
use crate::state::{
    dreamgpu_context_resource, dreamgpu_context_state_init, dreamgpu_context_state_release,
};
use crate::texture::names::Namespace as Textures;
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
    collections::{BTreeMap, BTreeSet, VecDeque},
    mem::MaybeUninit,
};

#[derive(Default)]
struct Allocator {
    live: BTreeMap<usize, Layout>,
}
unsafe extern "C" fn allocate(p: *mut c_void, bytes: usize) -> *mut c_void {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    let layout = Layout::from_size_align(bytes.max(1), 8).unwrap();
    let p = unsafe { alloc(layout) };
    assert!(!p.is_null());
    // Deliberately not zero initialized; production initialization owns writes.
    unsafe { core::ptr::write_bytes(p, 0xaa, bytes) };
    a.live.insert(p as usize, layout);
    p.cast()
}
unsafe extern "C" fn free(p: *mut c_void, object: *mut c_void) {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    let layout = a
        .live
        .remove(&(object as usize))
        .expect("foreign/double free");
    unsafe { dealloc(object.cast(), layout) }
}
unsafe extern "C" fn forget(_: *mut c_void, _: *mut Texture) {}

/// One native attachment: (kind, object, level, layer).
type Native3 = (u32, u32, i32, i32);
/// A native framebuffer/renderbuffer/texture model with exact bindings.
#[derive(Default)]
struct Native {
    next: u32,
    framebuffers: BTreeMap<u32, BTreeMap<u32, Native3>>,
    renderbuffers: BTreeMap<u32, (u32, i32, i32)>,
    textures: BTreeSet<u32>,
    texture_binding: u32,
    draw: u32,
    read: u32,
    renderbuffer: u32,
    draw_buffers: BTreeMap<u32, Vec<u32>>,
    read_buffers: BTreeMap<u32, u32>,
    errors: VecDeque<u32>,
    /// The status the native framebuffer reports for objects.
    status: u32,
    /// The next RenderbufferStorage fails with this error.
    fail_storage: u32,
    /// Live fences and the waits issued.
    fences: BTreeSet<usize>,
    next_fence: usize,
    waits: u32,
    /// Clears: (draw framebuffer, mask, clear colour/depth/stencil).
    clears: Vec<(u32, u32)>,
    reads: Vec<(i32, i32, i32, i32, i32)>,
    pack_row_length: i32,
    blits: Vec<[i32; 8]>,
    generated: Vec<u32>,
    level_compressed: i32,
    scissor: u8,
    clear_color: [f32; 4],
    clear_depth: f64,
    color_mask: [u8; 4],
}
std::thread_local! {static N: RefCell<Native> = RefCell::new(Native::default());}
fn native<R>(f: impl FnOnce(&mut Native) -> R) -> R {
    N.with(|n| f(&mut n.borrow_mut()))
}
fn reset() {
    native(|n| {
        *n = Native {
            status: GL_FRAMEBUFFER_COMPLETE,
            color_mask: [1; 4],
            clear_color: [0.25; 4],
            ..Native::default()
        }
    });
}
unsafe extern "C" fn get_error() -> u32 {
    native(|n| n.errors.pop_front().unwrap_or(0))
}
unsafe extern "C" fn gen_framebuffers(count: i32, out: *mut u32) {
    assert_eq!(count, 1);
    native(|n| {
        n.next += 1;
        n.framebuffers.insert(n.next, BTreeMap::new());
        n.draw_buffers.insert(n.next, vec![GL_COLOR_ATTACHMENT0]);
        n.read_buffers.insert(n.next, GL_COLOR_ATTACHMENT0);
        unsafe { *out = n.next };
    })
}
unsafe extern "C" fn delete_framebuffers(count: i32, names: *const u32) {
    assert_eq!(count, 1);
    let name = unsafe { *names };
    native(|n| {
        assert!(
            n.framebuffers.remove(&name).is_some(),
            "native double delete"
        );
        if n.draw == name {
            n.draw = 0;
        }
        if n.read == name {
            n.read = 0;
        }
    })
}
unsafe extern "C" fn bind_framebuffer(target: u32, name: u32) {
    native(|n| {
        assert!(
            name == 0 || name >= 1000 || n.framebuffers.contains_key(&name),
            "bind of a dead framebuffer {name}"
        );
        if target != GL_READ_FRAMEBUFFER {
            n.draw = name;
        }
        if target != GL_DRAW_FRAMEBUFFER {
            n.read = name;
        }
    })
}
fn bound_native(n: &Native, target: u32) -> u32 {
    if target == GL_READ_FRAMEBUFFER {
        n.read
    } else {
        n.draw
    }
}
fn attach(target: u32, point: u32, value: Native3) {
    native(|n| {
        let fb = bound_native(n, target);
        let table = n.framebuffers.get_mut(&fb).expect("attach to an object");
        if value.1 == 0 {
            table.remove(&point);
        } else {
            table.insert(point, value);
        }
    })
}
unsafe extern "C" fn texture_1d(target: u32, point: u32, _: u32, texture: u32, level: i32) {
    attach(target, point, (GL_TEXTURE_1D, texture, level, 0));
}
unsafe extern "C" fn texture_2d(target: u32, point: u32, textarget: u32, texture: u32, level: i32) {
    attach(target, point, (textarget, texture, level, 0));
}
unsafe extern "C" fn texture_3d(
    target: u32,
    point: u32,
    _: u32,
    texture: u32,
    level: i32,
    layer: i32,
) {
    attach(target, point, (GL_TEXTURE_3D, texture, level, layer));
}
unsafe extern "C" fn attach_rb(target: u32, point: u32, _: u32, rb: u32) {
    attach(target, point, (GL_RENDERBUFFER, rb, 0, 0));
}
unsafe extern "C" fn status(target: u32) -> u32 {
    native(|n| {
        if bound_native(n, target) >= 1000 {
            GL_FRAMEBUFFER_COMPLETE
        } else {
            n.status
        }
    })
}
unsafe extern "C" fn gen_renderbuffers(count: i32, out: *mut u32) {
    assert_eq!(count, 1);
    native(|n| {
        n.next += 1;
        n.renderbuffers.insert(n.next, (0, 0, 0));
        unsafe { *out = n.next };
    })
}
unsafe extern "C" fn delete_renderbuffers(count: i32, names: *const u32) {
    assert_eq!(count, 1);
    let name = unsafe { *names };
    native(|n| {
        assert!(
            n.renderbuffers.remove(&name).is_some(),
            "native renderbuffer double delete"
        );
        if n.renderbuffer == name {
            n.renderbuffer = 0;
        }
        // Native deletion detaches from the bound framebuffers only.
        for fb in [n.draw, n.read] {
            if let Some(table) = n.framebuffers.get_mut(&fb) {
                table.retain(|_, a| !(a.0 == GL_RENDERBUFFER && a.1 == name));
            }
        }
    })
}
unsafe extern "C" fn bind_renderbuffer(_: u32, name: u32) {
    native(|n| {
        assert!(name == 0 || n.renderbuffers.contains_key(&name));
        n.renderbuffer = name;
    })
}
unsafe extern "C" fn renderbuffer_storage(_: u32, format: u32, w: i32, h: i32) {
    native(|n| {
        if n.fail_storage != 0 {
            let e = core::mem::take(&mut n.fail_storage);
            n.errors.push_back(e);
            return;
        }
        let rb = n.renderbuffer;
        n.renderbuffers.insert(rb, (format, w, h));
    })
}
unsafe extern "C" fn renderbuffer_parameter(_: u32, pname: u32, out: *mut i32) {
    native(|n| {
        let (format, ..) = n.renderbuffers[&n.renderbuffer];
        unsafe {
            *out = match pname {
                GL_RENDERBUFFER_DEPTH_SIZE if format == GL_DEPTH24_STENCIL8 => 24,
                GL_RENDERBUFFER_STENCIL_SIZE if format == GL_DEPTH24_STENCIL8 => 8,
                GL_RENDERBUFFER_RED_SIZE if format == GL_RGBA8 => 8,
                _ => 0,
            }
        };
    })
}
unsafe extern "C" fn blit(
    a: i32,
    b: i32,
    c: i32,
    d: i32,
    e: i32,
    f: i32,
    g: i32,
    h: i32,
    _: u32,
    _: u32,
) {
    native(|n| n.blits.push([a, b, c, d, e, f, g, h]))
}
unsafe extern "C" fn generate(target: u32) {
    native(|n| n.generated.push(target))
}
unsafe extern "C" fn integer(pname: u32, out: *mut i32) {
    native(|n| unsafe {
        *out = match pname {
            GL_MAX_RENDERBUFFER_SIZE => 8192,
            GL_MAX_COLOR_ATTACHMENTS => 8,
            GL_READ_FRAMEBUFFER_BINDING => n.read as i32,
            GL_DRAW_FRAMEBUFFER_BINDING => n.draw as i32,
            GL_STENCIL_WRITEMASK => 0x0f,
            GL_STENCIL_CLEAR_VALUE => 3,
            GL_PACK_ROW_LENGTH => n.pack_row_length,
            GL_PACK_ALIGNMENT => 4,
            _ => 0,
        }
    })
}
unsafe extern "C" fn float(pname: u32, out: *mut f32) {
    native(|n| unsafe {
        if pname == GL_COLOR_CLEAR_VALUE {
            core::ptr::copy_nonoverlapping(n.clear_color.as_ptr(), out, 4);
        } else {
            *out = 0.5;
        }
    })
}
unsafe extern "C" fn double(_: u32, out: *mut f64) {
    unsafe { *out = 0.375 };
}
unsafe extern "C" fn boolean(pname: u32, out: *mut u8) {
    native(|n| unsafe {
        if pname == GL_COLOR_WRITEMASK {
            core::ptr::copy_nonoverlapping(n.color_mask.as_ptr(), out, 4);
        } else {
            *out = 0;
        }
    })
}
unsafe extern "C" fn enabled(cap: u32) -> u8 {
    native(|n| u8::from(cap == GL_SCISSOR_TEST && n.scissor != 0))
}
unsafe extern "C" fn enable(cap: u32) {
    native(|n| {
        if cap == GL_SCISSOR_TEST {
            n.scissor = 1
        }
    })
}
unsafe extern "C" fn disable(cap: u32) {
    native(|n| {
        if cap == GL_SCISSOR_TEST {
            n.scissor = 0
        }
    })
}
unsafe extern "C" fn clear(mask: u32) {
    native(|n| {
        assert_eq!(n.scissor, 0, "initialization clears are unscissored");
        assert_eq!(n.color_mask, [1; 4]);
        let fb = n.draw;
        n.clears.push((fb, mask))
    })
}
unsafe extern "C" fn clear_color(r: f32, g: f32, b: f32, a: f32) {
    native(|n| n.clear_color = [r, g, b, a])
}
unsafe extern "C" fn clear_depth(value: f64) {
    native(|n| n.clear_depth = value)
}
unsafe extern "C" fn clear_stencil(_: i32) {}
unsafe extern "C" fn color_mask(r: u8, g: u8, b: u8, a: u8) {
    native(|n| n.color_mask = [r, g, b, a])
}
unsafe extern "C" fn depth_mask(_: u8) {}
unsafe extern "C" fn stencil_mask(_: u32) {}
unsafe extern "C" fn copy_pixels(_: i32, _: i32, _: i32, _: i32, _: u32) {
    native(|n| n.blits.push([0; 8]))
}
unsafe extern "C" fn tex_parameter(_: u32, pname: u32, out: *mut i32) {
    unsafe {
        *out = if pname == GL_TEXTURE_MAX_LEVEL {
            1000
        } else {
            0
        }
    };
}
unsafe extern "C" fn level_parameter(_: u32, _: i32, pname: u32, out: *mut i32) {
    native(|n| unsafe {
        *out = if pname == GL_TEXTURE_COMPRESSED {
            n.level_compressed
        } else {
            0
        }
    })
}
unsafe extern "C" fn fence_sync(_: u32, _: u32) -> *mut __GLsync {
    native(|n| {
        n.next_fence += 1;
        n.fences.insert(n.next_fence);
        n.next_fence as *mut __GLsync
    })
}
unsafe extern "C" fn delete_sync(sync: *mut __GLsync) {
    native(|n| assert!(n.fences.remove(&(sync as usize)), "dead fence"))
}
unsafe extern "C" fn wait_sync(sync: *mut __GLsync, _: u32, _: u64) {
    native(|n| {
        assert!(n.fences.contains(&(sync as usize)));
        n.waits += 1;
    })
}
unsafe extern "C" fn draw_buffer(mode: u32) {
    native(|n| {
        let fb = n.draw;
        n.draw_buffers.insert(fb, vec![mode]);
    })
}
unsafe extern "C" fn draw_buffers(count: i32, modes: *const u32) {
    native(|n| {
        let fb = n.draw;
        let modes = unsafe { core::slice::from_raw_parts(modes, count as usize) }.to_vec();
        n.draw_buffers.insert(fb, modes);
    })
}
unsafe extern "C" fn read_buffer(mode: u32) {
    native(|n| {
        let fb = n.read;
        n.read_buffers.insert(fb, mode);
    })
}
unsafe extern "C" fn gen_textures(_: i32, out: *mut u32) {
    native(|n| {
        n.next += 1;
        n.textures.insert(n.next);
        unsafe { *out = n.next };
    })
}
unsafe extern "C" fn bind_texture(_: u32, name: u32) {
    native(|n| n.texture_binding = name)
}
unsafe extern "C" fn delete_textures(_: i32, names: *const u32) {
    let name = unsafe { *names };
    native(|n| {
        assert!(n.textures.remove(&name), "native texture double delete");
        for fb in [n.draw, n.read] {
            if let Some(table) = n.framebuffers.get_mut(&fb) {
                table.retain(|_, a| a.0 == GL_RENDERBUFFER || a.1 != name);
            }
        }
    })
}
unsafe extern "C" fn native_read(x: i32, y: i32, w: i32, h: i32, _: u32, _: u32, out: *mut c_void) {
    native(|n| {
        let stride = if n.pack_row_length != 0 {
            n.pack_row_length
        } else {
            w
        };
        n.reads.push((x, y, w, h, stride));
        for row in 0..h {
            for column in 0..w {
                let value = (((x + column) << 16) | (y + row)) as u32;
                unsafe {
                    out.cast::<u32>()
                        .add((row * stride + column) as usize)
                        .write_unaligned(value)
                };
            }
        }
    })
}
unsafe extern "C" fn pixel_store(pname: u32, value: i32) {
    native(|n| {
        if pname == GL_PACK_ROW_LENGTH {
            n.pack_row_length = value
        }
    })
}
unsafe extern "C" fn push_attrib(_: u32) {}
unsafe extern "C" fn pop_attrib() {}
fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGetError = Some(get_error);
    a.dg_glGenFramebuffers = Some(gen_framebuffers);
    a.dg_glDeleteFramebuffers = Some(delete_framebuffers);
    a.dg_glBindFramebuffer = Some(bind_framebuffer);
    a.dg_glFramebufferTexture1D = Some(texture_1d);
    a.dg_glFramebufferTexture2D = Some(texture_2d);
    a.dg_glFramebufferTexture3D = Some(texture_3d);
    a.dg_glFramebufferRenderbuffer = Some(attach_rb);
    a.dg_glCheckFramebufferStatus = Some(status);
    a.dg_glGenRenderbuffers = Some(gen_renderbuffers);
    a.dg_glDeleteRenderbuffers = Some(delete_renderbuffers);
    a.dg_glBindRenderbuffer = Some(bind_renderbuffer);
    a.dg_glRenderbufferStorage = Some(renderbuffer_storage);
    a.dg_glGetRenderbufferParameteriv = Some(renderbuffer_parameter);
    a.dg_glBlitFramebuffer = Some(blit);
    a.dg_glGenerateMipmap = Some(generate);
    a.dg_glGetIntegerv = Some(integer);
    a.dg_glGetFloatv = Some(float);
    a.dg_glGetBooleanv = Some(boolean);
    a.dg_glGetDoublev = Some(double);
    a.dg_glIsEnabled = Some(enabled);
    a.dg_glEnable = Some(enable);
    a.dg_glDisable = Some(disable);
    a.dg_glClear = Some(clear);
    a.dg_glClearColor = Some(clear_color);
    a.dg_glClearDepth = Some(clear_depth);
    a.dg_glClearStencil = Some(clear_stencil);
    a.dg_glColorMask = Some(color_mask);
    a.dg_glDepthMask = Some(depth_mask);
    a.dg_glStencilMask = Some(stencil_mask);
    a.dg_glCopyPixels = Some(copy_pixels);
    a.dg_glGetTexParameteriv = Some(tex_parameter);
    a.dg_glGetTexLevelParameteriv = Some(level_parameter);
    a.dg_glFenceSync = Some(fence_sync);
    a.dg_glDeleteSync = Some(delete_sync);
    a.dg_glWaitSync = Some(wait_sync);
    a.dg_glDrawBuffer = Some(draw_buffer);
    a.dg_glDrawBuffers = Some(draw_buffers);
    a.dg_glReadBuffer = Some(read_buffer);
    a.dg_glGenTextures = Some(gen_textures);
    a.dg_glBindTexture = Some(bind_texture);
    a.dg_glDeleteTextures = Some(delete_textures);
    a.dg_glReadPixels = Some(native_read);
    a.dg_glPixelStorei = Some(pixel_store);
    a.dg_glPushAttrib = Some(push_attrib);
    a.dg_glPopAttrib = Some(pop_attrib);
    a
}
struct Fixture {
    _api: Box<DreamGpuGlApi>,
    allocator: Box<Allocator>,
    _count: Box<u32>,
    bytes: Box<u64>,
    _images: Box<u64>,
    memory: Memory,
}
fn fixture() -> Fixture {
    reset();
    let api = Box::new(api());
    let mut allocator = Box::<Allocator>::default();
    let mut count = Box::new(0);
    let mut bytes = Box::new(0);
    let mut images = Box::new(0);
    let memory = Memory {
        api: &*api,
        bytes: &mut *bytes,
        image_bytes: &mut *images,
        count: &mut *count,
        opaque: (&mut *allocator as *mut Allocator).cast(),
        allocate,
        free,
        forget_read: forget,
    };
    Fixture {
        _api: api,
        allocator,
        _count: count,
        bytes,
        _images: images,
        memory,
    }
}
/// The window's native framebuffer in these tests.
const WINDOW: u32 = 1000;
type Context = Box<MaybeUninit<ContextState>>;
fn context(m: &Memory, shared: *mut Textures) -> Context {
    let mut c = Box::<ContextState>::new_uninit();
    assert_eq!(
        unsafe { dreamgpu_context_state_init(m, c.as_mut_ptr(), shared) },
        0
    );
    unsafe { (*c.as_mut_ptr()).window_framebuffer = WINDOW };
    native(|n| {
        n.draw = WINDOW;
        n.read = WINDOW;
    });
    c
}
fn s(c: &mut Context) -> *mut ContextState {
    c.as_mut_ptr()
}
fn release(m: &Memory, mut c: Context) {
    unsafe { dreamgpu_context_state_release(m, c.as_mut_ptr()) };
}
fn errors(c: &mut Context) -> u32 {
    unsafe { core::mem::take(&mut (*s(c)).guest_errors) }
}
const INVALID_ENUM: u32 = 1;
const INVALID_VALUE: u32 = 1 << 1;
const INVALID_OPERATION: u32 = 1 << 2;
const OUT_OF_MEMORY: u32 = 1 << 5;
const INVALID_FRAMEBUFFER_OPERATION: u32 = 1 << 6;
/// A CALL record through the context's resource dispatch.
fn call_on(m: &Memory, c: &mut Context, serial: u64, function: u32, args: &[u32]) -> u32 {
    let data: Vec<u8> = args.iter().flat_map(|v| v.to_le_bytes()).collect();
    let words = crate::gl_validation::dreamgpu_gl_function_words(function);
    assert_eq!(words as usize, args.len(), "record words of {function}");
    let mut wide = [0u32; 10];
    wide[..args.len()].copy_from_slice(args);
    if unsafe { crate::gl_validation::dreamgpu_gl_call_validate(function, data.as_ptr()) } != 0 {
        return DG_GL_ERROR_UNSUPPORTED;
    }
    unsafe {
        dreamgpu_context_resource(
            m,
            s(c),
            serial,
            function,
            data.as_ptr(),
            data.len() as u32,
            None,
            null_mut(),
        )
    }
}
fn call(m: &Memory, c: &mut Context, function: u32, args: &[u32]) -> u32 {
    call_on(m, c, 1, function, args)
}
fn data(m: &Memory, c: &mut Context, function: u32, names: &[u32]) -> u32 {
    let args = (names.len() as u32).to_le_bytes();
    let payload: Vec<u8> = names.iter().flat_map(|v| v.to_le_bytes()).collect();
    assert_eq!(
        unsafe {
            crate::gl_validation::dreamgpu_gl_data_validate(
                function,
                args.as_ptr(),
                payload.as_ptr(),
                payload.len() as u32,
            )
        },
        0
    );
    unsafe {
        crate::data::dreamgpu_gl_data(
            m,
            s(c),
            0,
            1,
            function,
            args.as_ptr(),
            payload.as_ptr(),
            payload.len() as u32,
        )
    }
}
/// A one-value QUERY record (status and parameter queries, Get*v).
fn query(m: &Memory, c: &mut Context, function: u32, args: [u32; 3]) -> Result<u32, u32> {
    let raw: Vec<u8> = args.iter().flat_map(|v| v.to_le_bytes()).collect();
    let state = QueryState {
        in_begin: 0,
        has_drawable: 1,
        width: 64,
        height: 32,
        draw_buffer: unsafe { (*s(c)).draw_buffer },
        read_buffer: unsafe { (*s(c)).read_buffer },
        binding_1d: 0,
        binding_2d: 0,
        attrib_depth: 0,
        textures: unsafe { (*s(c)).textures },
        capture: null_mut(),
        memory: m,
        context: s(c),
    };
    let mut result = [0u8; 4];
    let (mut bytes, mut kind) = (0, 0);
    let e = unsafe {
        dreamgpu_gl_query(
            m.api,
            &state,
            addr_of_mut!((*s(c)).guest_errors),
            function,
            raw.as_ptr(),
            result.as_mut_ptr(),
            4,
            None,
            null_mut(),
            &mut bytes,
            &mut kind,
        )
    };
    if e != 0 {
        return Err(e);
    }
    assert_eq!((bytes, kind), (4, DG_GL_RESULT_INT));
    Ok(u32::from_le_bytes(result))
}
fn get(m: &Memory, c: &mut Context, pname: u32) -> u32 {
    query(m, c, FEnum_glGetIntegerv, [pname, 0, 0]).unwrap()
}
/// A texture of `target` named `name`, with a defined `w`x`h` level 0
/// charged at four bytes per texel.
fn texture(m: &Memory, c: &mut Context, target: u32, name: u32, w: u32, h: u32) -> *mut Texture {
    assert_eq!(call(m, c, FEnum_glBindTexture, &[target, name]), 0);
    let t = unsafe { dreamgpu_texture_lookup((*s(c)).textures, name) };
    assert!(!t.is_null());
    let faces = if target == GL_TEXTURE_CUBE_MAP { 6 } else { 1 };
    unsafe {
        for face in 0..faces {
            let image = if target == GL_TEXTURE_CUBE_MAP {
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face
            } else {
                target
            };
            let mut f = image_levels(t, image);
            f.widths[0] = w;
            f.heights[0] = h;
            f.levels[0] = u64::from(w * h * 4);
            *m.bytes += f.levels[0];
            set_image_levels(t, image, &f);
        }
        if target == GL_TEXTURE_3D {
            (*t).depths[0] = 4;
        }
    }
    t
}
fn native_attachments(fb: u32) -> BTreeMap<u32, Native3> {
    native(|n| n.framebuffers[&fb].clone())
}
fn object(c: &mut Context, name: u32) -> *mut Framebuffer {
    unsafe { Framebuffers::find((*s(c)).framebuffers, name) }
}

#[test]
fn record_shapes_words_and_query_shapes() {
    use crate::gl_validation::dreamgpu_gl_function_words as words;
    assert_eq!(words(FEnum_glBindFramebuffer), 2);
    assert_eq!(words(FEnum_glBindRenderbuffer), 2);
    assert_eq!(words(FEnum_glRenderbufferStorage), 4);
    assert_eq!(words(FEnum_glFramebufferTexture1D), 5);
    assert_eq!(words(FEnum_glFramebufferTexture2D), 5);
    assert_eq!(words(FEnum_glFramebufferTexture3D), 6);
    assert_eq!(words(FEnum_glFramebufferRenderbuffer), 4);
    assert_eq!(words(FEnum_glGenerateMipmap), 1);
    assert_eq!(words(FEnum_glBlitFramebuffer), 10);
    assert_eq!(
        words(FEnum_glDeleteFramebuffers),
        DG_GL_FUNCTION_INLINE_DATA | 1
    );
    assert_eq!(
        words(FEnum_glDeleteRenderbuffers),
        DG_GL_FUNCTION_INLINE_DATA | 1
    );
    assert_eq!(
        words(FEnum_glCheckFramebufferStatus),
        DG_GL_FUNCTION_QUERY | 1
    );
    assert_eq!(
        words(FEnum_glGetRenderbufferParameteriv),
        DG_GL_FUNCTION_QUERY | 2
    );
    assert_eq!(
        words(FEnum_glGetFramebufferAttachmentParameteriv),
        DG_GL_FUNCTION_QUERY | 3
    );
    let shape = |f, a: &[u32]| {
        let mut w = [0; 10];
        w[..a.len()].copy_from_slice(a);
        call_shape(f, &w)
    };
    for target in [GL_FRAMEBUFFER, GL_DRAW_FRAMEBUFFER, GL_READ_FRAMEBUFFER] {
        assert!(shape(FEnum_glBindFramebuffer, &[target, 7]));
    }
    assert!(!shape(FEnum_glBindFramebuffer, &[GL_RENDERBUFFER, 7]));
    assert!(shape(FEnum_glBindRenderbuffer, &[GL_RENDERBUFFER, 0]));
    assert!(!shape(FEnum_glBindRenderbuffer, &[GL_FRAMEBUFFER, 1]));
    for format in [
        GL_RGBA8,
        GL_RGB5,
        GL_RGBA16,
        GL_DEPTH_COMPONENT24,
        GL_STENCIL_INDEX8,
        GL_DEPTH24_STENCIL8,
        GL_DEPTH_STENCIL,
    ] {
        assert!(shape(
            FEnum_glRenderbufferStorage,
            &[GL_RENDERBUFFER, format, 16, 16]
        ));
    }
    // Alpha, luminance and intensity are not colour-renderable in EXT.
    for format in [GL_ALPHA8, GL_LUMINANCE8, GL_INTENSITY8, GL_RG32F, 0] {
        assert!(!shape(
            FEnum_glRenderbufferStorage,
            &[GL_RENDERBUFFER, format, 16, 16]
        ));
    }
    assert!(shape(
        FEnum_glRenderbufferStorage,
        &[GL_RENDERBUFFER, GL_RGBA8, 4096, 4096]
    ));
    assert!(!shape(
        FEnum_glRenderbufferStorage,
        &[GL_RENDERBUFFER, GL_RGBA8, 4097, 1]
    ));
    let t2 = |a: &[u32]| shape(FEnum_glFramebufferTexture2D, a);
    assert!(t2(&[
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT3,
        GL_TEXTURE_2D,
        1,
        11
    ]));
    assert!(t2(&[
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
        1,
        0
    ]));
    assert!(!t2(&[
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0 + 4,
        GL_TEXTURE_2D,
        1,
        0
    ]));
    assert!(!t2(&[
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        1,
        12
    ]));
    assert!(!t2(&[
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_1D,
        1,
        0
    ]));
    // ARB_texture_rectangle: a rectangle attaches through FramebufferTexture2D
    // (a nonzero level is an execution-time INVALID_VALUE).
    assert!(t2(&[
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_RECTANGLE_ARB,
        1,
        0
    ]));
    assert!(t2(&[
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_RECTANGLE_ARB,
        1,
        1
    ]));
    assert!(!t2(&[
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_PROXY_TEXTURE_RECTANGLE_ARB,
        1,
        0
    ]));
    assert!(!t2(&[GL_FRAMEBUFFER, 0x821a, GL_TEXTURE_2D, 1, 0])); // DEPTH_STENCIL_ATTACHMENT
    assert!(shape(
        FEnum_glFramebufferTexture1D,
        &[
            GL_READ_FRAMEBUFFER,
            GL_STENCIL_ATTACHMENT,
            GL_TEXTURE_1D,
            0,
            0
        ]
    ));
    assert!(shape(
        FEnum_glFramebufferTexture3D,
        &[
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_3D,
            1,
            0,
            255
        ]
    ));
    assert!(!shape(
        FEnum_glFramebufferTexture3D,
        &[
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_3D,
            1,
            0,
            256
        ]
    ));
    assert!(shape(
        FEnum_glFramebufferRenderbuffer,
        &[GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 3]
    ));
    assert!(!shape(
        FEnum_glFramebufferRenderbuffer,
        &[GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 3]
    ));
    for target in [
        GL_TEXTURE_1D,
        GL_TEXTURE_2D,
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_3D,
    ] {
        assert!(shape(FEnum_glGenerateMipmap, &[target]));
    }
    assert!(!shape(FEnum_glGenerateMipmap, &[GL_PROXY_TEXTURE_2D]));
    let blit = |mask, filter| {
        shape(
            FEnum_glBlitFramebuffer,
            &[0, 0, 1, 1, 0, 0, 1, 1, mask, filter],
        )
    };
    assert!(blit(GL_COLOR_BUFFER_BIT, GL_LINEAR));
    assert!(blit(
        GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
        GL_NEAREST
    ));
    assert!(!blit(GL_DEPTH_BUFFER_BIT, GL_LINEAR));
    assert!(!blit(GL_ACCUM_BUFFER_BIT, GL_NEAREST));
    assert!(!blit(GL_COLOR_BUFFER_BIT, GL_LINEAR_MIPMAP_LINEAR));
    // Queries.
    let q = |f, a: [u32; 3]| {
        let raw: Vec<u8> = a.iter().flat_map(|v| v.to_le_bytes()).collect();
        unsafe { crate::gl_validation::dreamgpu_gl_query_result_bytes(f, raw.as_ptr()) }
    };
    assert_eq!(
        q(FEnum_glCheckFramebufferStatus, [GL_READ_FRAMEBUFFER, 0, 0]),
        4
    );
    assert_eq!(
        q(FEnum_glCheckFramebufferStatus, [GL_RENDERBUFFER, 0, 0]),
        0
    );
    assert_eq!(
        q(
            FEnum_glGetFramebufferAttachmentParameteriv,
            [
                GL_FRAMEBUFFER,
                GL_DEPTH_ATTACHMENT,
                GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT
            ]
        ),
        4
    );
    assert_eq!(
        q(
            FEnum_glGetFramebufferAttachmentParameteriv,
            [GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 0x8212] // COLOR_ENCODING (GL 3)
        ),
        0
    );
    assert_eq!(
        q(
            FEnum_glGetRenderbufferParameteriv,
            [GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, 0]
        ),
        4
    );
    assert_eq!(
        q(
            FEnum_glGetRenderbufferParameteriv,
            [GL_RENDERBUFFER, 0x8cab, 0]
        ), // SAMPLES
        0
    );
    for pname in [
        GL_FRAMEBUFFER_BINDING,
        GL_READ_FRAMEBUFFER_BINDING,
        GL_RENDERBUFFER_BINDING,
        GL_MAX_COLOR_ATTACHMENTS,
        GL_MAX_RENDERBUFFER_SIZE,
    ] {
        assert_eq!(q(FEnum_glGetIntegerv, [pname, 0, 0]), 4);
    }
    // Draw and read buffer names: the attachments, ReadBuffer(NONE).
    use crate::gl_validation::dreamgpu_gl_buffer_selection as selection;
    assert_eq!(selection(GL_COLOR_ATTACHMENT3, 1), 1);
    assert_eq!(selection(GL_COLOR_ATTACHMENT3, 0), 1);
    assert_eq!(selection(GL_COLOR_ATTACHMENT0 + 4, 1), 0);
    assert_eq!(selection(GL_NONE, 0), 1);
    assert_eq!(selection(GL_FRONT_AND_BACK, 0), 0);
    // Deletion counts.
    let count = |f, n: u32, bytes: usize| unsafe {
        crate::gl_validation::dreamgpu_gl_data_validate(
            f,
            n.to_le_bytes().as_ptr(),
            vec![0u8; bytes].as_ptr(),
            bytes as u32,
        )
    };
    assert_eq!(count(FEnum_glDeleteFramebuffers, 2, 8), 0);
    assert_ne!(count(FEnum_glDeleteFramebuffers, 2, 4), 0);
    assert_ne!(
        count(FEnum_glDeleteFramebuffers, DG_GL_MAX_FRAMEBUFFERS + 1, 0),
        0
    );
    assert_eq!(count(FEnum_glDeleteRenderbuffers, 1, 4), 0);
    assert_ne!(
        count(FEnum_glDeleteRenderbuffers, DG_GL_MAX_RENDERBUFFERS + 1, 0),
        0
    );
}

#[test]
fn depth_texture_records_are_two_dimensional_and_borderless() {
    let image = |target: u32, internal: u32, border: u32, format: u32, kind: u32, bytes: usize| {
        let a = [target, 0, internal, 4, 4, border, format, kind];
        let raw: Vec<u8> = a.iter().flat_map(|v| v.to_le_bytes()).collect();
        let payload = vec![0u8; bytes];
        unsafe {
            crate::gl_validation::dreamgpu_gl_data_validate(
                FEnum_glTexImage2D,
                raw.as_ptr(),
                payload.as_ptr(),
                bytes as u32,
            )
        }
    };
    for internal in [
        GL_DEPTH_COMPONENT,
        GL_DEPTH_COMPONENT16,
        GL_DEPTH_COMPONENT24,
        GL_DEPTH_COMPONENT32,
        GL_DEPTH_STENCIL,
        GL_DEPTH24_STENCIL8,
    ] {
        assert_eq!(
            image(GL_TEXTURE_2D, internal, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 64),
            0
        );
        assert_eq!(
            image(GL_TEXTURE_2D, internal, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0),
            0
        );
        assert_eq!(
            image(
                GL_PROXY_TEXTURE_2D,
                internal,
                0,
                GL_DEPTH_COMPONENT,
                GL_FLOAT,
                0
            ),
            0
        );
        assert_ne!(
            image(GL_TEXTURE_2D, internal, 1, GL_DEPTH_COMPONENT, GL_FLOAT, 0),
            0
        );
        assert_ne!(
            image(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                internal,
                0,
                GL_DEPTH_COMPONENT,
                GL_FLOAT,
                0
            ),
            0
        );
    }
    assert_eq!(
        image(
            GL_TEXTURE_2D,
            GL_DEPTH24_STENCIL8,
            0,
            GL_DEPTH_STENCIL,
            GL_UNSIGNED_INT_24_8,
            64
        ),
        0
    );
    assert_ne!(
        image(
            GL_TEXTURE_2D,
            GL_DEPTH24_STENCIL8,
            0,
            GL_DEPTH_STENCIL,
            GL_UNSIGNED_INT,
            64
        ),
        0
    );
    assert_eq!(
        image(
            GL_TEXTURE_2D,
            GL_DEPTH_COMPONENT16,
            0,
            GL_DEPTH_COMPONENT,
            GL_UNSIGNED_SHORT,
            32
        ),
        0
    );
    // Depth pixels on a cube face or with a colour format of a colour target.
    assert_ne!(
        image(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X,
            GL_RGBA8,
            0,
            GL_DEPTH_COMPONENT,
            GL_FLOAT,
            64
        ),
        0
    );
    let copy = |internal: u32, target: u32, border: u32| {
        let a = [target, 0, internal, 0, 0, 4, 4, border];
        let raw: Vec<u8> = a.iter().flat_map(|v| v.to_le_bytes()).collect();
        unsafe {
            crate::gl_validation::dreamgpu_gl_call_validate(FEnum_glCopyTexImage2D, raw.as_ptr())
        }
    };
    assert_eq!(copy(GL_DEPTH_COMPONENT, GL_TEXTURE_2D, 0), 0);
    assert_eq!(copy(GL_DEPTH24_STENCIL8, GL_TEXTURE_2D, 0), 0);
    assert_ne!(copy(GL_DEPTH_COMPONENT, GL_TEXTURE_2D, 1), 0);
    assert_ne!(
        copy(GL_DEPTH_COMPONENT, GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0),
        0
    );
    assert_eq!(
        crate::pixel_image::byte_count(
            FEnum_glDrawPixels,
            3,
            2,
            GL_DEPTH_STENCIL,
            GL_UNSIGNED_INT_24_8
        ),
        Some(24)
    );
    assert_eq!(
        crate::pixel_image::byte_count(FEnum_glDrawPixels, 3, 2, GL_DEPTH_STENCIL, GL_FLOAT),
        None
    );
}

#[test]
fn objects_bind_per_context_and_binding_zero_restores_the_window() {
    let f = fixture();
    let m = &f.memory;
    let mut a = context(m, null_mut());
    let shared = unsafe { (*s(&mut a)).textures };
    let mut b = context(m, shared);
    let base = *f.bytes;
    assert_eq!(
        call(m, &mut a, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 5]),
        0
    );
    let fb = object(&mut a, 5);
    assert!(!fb.is_null());
    let native_fb = unsafe { (*fb).native };
    assert_eq!(native(|n| (n.draw, n.read)), (native_fb, native_fb));
    assert!(*f.bytes > base);
    assert_eq!(get(m, &mut a, GL_FRAMEBUFFER_BINDING), 5);
    assert_eq!(get(m, &mut a, GL_READ_FRAMEBUFFER_BINDING), 5);
    assert_eq!(get(m, &mut a, GL_DRAW_BUFFER), GL_COLOR_ATTACHMENT0);
    assert_eq!(get(m, &mut a, GL_READ_BUFFER), GL_COLOR_ATTACHMENT0);
    assert_eq!(get(m, &mut a, GL_DOUBLEBUFFER), 0);
    // Framebuffers are not shared: the same name is another object in b.
    assert_eq!(
        call(m, &mut b, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 5]),
        0
    );
    assert!(!object(&mut b, 5).is_null());
    assert_ne!(object(&mut b, 5), fb);
    // Separate draw and read bindings; zero is the window's native framebuffer
    // (a is current again: the platform bound the drawable, then a's objects).
    native(|n| {
        n.draw = WINDOW;
        n.read = WINDOW;
    });
    unsafe { rebind(&*m.api, s(&mut a)) };
    assert_eq!(
        call(
            m,
            &mut a,
            FEnum_glBindFramebuffer,
            &[GL_READ_FRAMEBUFFER, 0]
        ),
        0
    );
    assert_eq!(native(|n| (n.draw, n.read)), (native_fb, WINDOW));
    assert_eq!(get(m, &mut a, GL_READ_FRAMEBUFFER_BINDING), 0);
    assert_eq!(get(m, &mut a, GL_READ_BUFFER), GL_BACK);
    assert_eq!(
        call(
            m,
            &mut a,
            FEnum_glBindFramebuffer,
            &[GL_DRAW_FRAMEBUFFER, 0]
        ),
        0
    );
    assert_eq!(native(|n| (n.draw, n.read)), (WINDOW, WINDOW));
    assert_eq!(get(m, &mut a, GL_DOUBLEBUFFER), 1);
    // The platform's drawable rebinding restores the object bindings.
    assert_eq!(
        call(
            m,
            &mut a,
            FEnum_glBindFramebuffer,
            &[GL_DRAW_FRAMEBUFFER, 5]
        ),
        0
    );
    native(|n| {
        n.draw = WINDOW;
        n.read = WINDOW;
    });
    unsafe { rebind(&*m.api, s(&mut a)) };
    assert_eq!(native(|n| (n.draw, n.read)), (native_fb, WINDOW));
    // Deleting a bound framebuffer rebinds the window; unknown names are ignored.
    assert_eq!(data(m, &mut a, FEnum_glDeleteFramebuffers, &[0, 77, 5]), 0);
    assert!(object(&mut a, 5).is_null());
    assert_eq!(native(|n| n.draw), WINDOW);
    assert_eq!(get(m, &mut a, GL_FRAMEBUFFER_BINDING), 0);
    assert!(native(|n| !n.framebuffers.contains_key(&native_fb)));
    assert_eq!(errors(&mut a), 0);
    release(m, b);
    release(m, a);
    assert_eq!(*f.bytes, 0);
    assert!(f.allocator.live.is_empty());
    assert!(native(
        |n| n.framebuffers.is_empty() && n.renderbuffers.is_empty()
    ));
}

#[test]
fn draw_and_read_buffers_belong_to_the_bound_framebuffer() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    // The window rejects attachments and ReadBuffer(NONE); objects reject window buffers.
    assert_eq!(
        call(m, &mut c, FEnum_glDrawBuffer, &[GL_COLOR_ATTACHMENT0]),
        0
    );
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(call(m, &mut c, FEnum_glReadBuffer, &[GL_NONE]), 0);
    assert_eq!(errors(&mut c), INVALID_ENUM);
    assert_eq!(call(m, &mut c, FEnum_glDrawBuffer, &[GL_FRONT]), 0);
    assert_eq!(
        native(|n| n.draw_buffers[&WINDOW].clone()),
        vec![GL_COLOR_ATTACHMENT1]
    );
    assert_eq!(
        call(m, &mut c, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 2]),
        0
    );
    let native_fb = unsafe { (*object(&mut c, 2)).native };
    assert_eq!(call(m, &mut c, FEnum_glDrawBuffer, &[GL_BACK]), 0);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(
        call(m, &mut c, FEnum_glDrawBuffer, &[GL_COLOR_ATTACHMENT2]),
        0
    );
    assert_eq!(call(m, &mut c, FEnum_glReadBuffer, &[GL_NONE]), 0);
    assert_eq!(errors(&mut c), 0);
    assert_eq!(
        native(|n| n.draw_buffers[&native_fb].clone()),
        vec![GL_COLOR_ATTACHMENT2]
    );
    assert_eq!(native(|n| n.read_buffers[&native_fb]), GL_NONE);
    assert_eq!(get(m, &mut c, GL_DRAW_BUFFER), GL_COLOR_ATTACHMENT2);
    assert_eq!(get(m, &mut c, GL_READ_BUFFER), GL_NONE);
    // The window keeps its own buffers.
    unsafe { assert_eq!((*s(&mut c)).draw_buffer, GL_FRONT) };
    // PushAttrib with the object bound, PopAttrib with the window: the saved
    // attachment is not restored into the window.
    assert_eq!(
        call(
            m,
            &mut c,
            FEnum_glPushAttrib,
            &[GL_COLOR_BUFFER_BIT | GL_PIXEL_MODE_BIT]
        ),
        0
    );
    assert_eq!(
        call(m, &mut c, FEnum_glDrawBuffer, &[GL_COLOR_ATTACHMENT1]),
        0
    );
    assert_eq!(
        call(m, &mut c, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 0]),
        0
    );
    assert_eq!(call(m, &mut c, FEnum_glDrawBuffer, &[GL_BACK]), 0);
    assert_eq!(call(m, &mut c, FEnum_glPopAttrib, &[]), 0);
    unsafe { assert_eq!((*s(&mut c)).draw_buffer, GL_BACK) };
    assert_eq!(
        native(|n| n.draw_buffers[&WINDOW].clone()),
        vec![GL_COLOR_ATTACHMENT0]
    );
    unsafe { assert_eq!((*object(&mut c, 2)).draw_buffer, GL_COLOR_ATTACHMENT1) };
    // With the same framebuffer bound at both ends, the value is restored.
    assert_eq!(
        call(m, &mut c, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 2]),
        0
    );
    assert_eq!(
        call(m, &mut c, FEnum_glPushAttrib, &[GL_COLOR_BUFFER_BIT]),
        0
    );
    assert_eq!(
        call(m, &mut c, FEnum_glDrawBuffer, &[GL_COLOR_ATTACHMENT3]),
        0
    );
    assert_eq!(call(m, &mut c, FEnum_glPopAttrib, &[]), 0);
    assert_eq!(get(m, &mut c, GL_DRAW_BUFFER), GL_COLOR_ATTACHMENT1);
    assert_eq!(
        native(|n| n.draw_buffers[&native_fb].clone()),
        vec![GL_COLOR_ATTACHMENT1]
    );
    assert_eq!(errors(&mut c), 0);
    release(m, c);
    assert_eq!(*f.bytes, 0);
}

#[test]
fn texture_attachments_check_state_as_gl_errors_and_keep_their_images() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    let t2 = texture(m, &mut c, GL_TEXTURE_2D, 10, 64, 32);
    let cube = texture(m, &mut c, GL_TEXTURE_CUBE_MAP, 11, 16, 16);
    let volume = texture(m, &mut c, GL_TEXTURE_3D, 12, 8, 8);
    let one = texture(m, &mut c, GL_TEXTURE_1D, 13, 32, 1);
    let attach2 = |c: &mut Context, point, textarget, name, level| {
        call(
            m,
            c,
            FEnum_glFramebufferTexture2D,
            &[GL_FRAMEBUFFER, point, textarget, name, level],
        )
    };
    // The window takes no attachments.
    assert_eq!(
        attach2(&mut c, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 10, 0),
        0
    );
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(
        call(m, &mut c, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 3]),
        0
    );
    let fb = object(&mut c, 3);
    let native_fb = unsafe { (*fb).native };
    // Unknown names, other targets and levels beyond the target's limit.
    assert_eq!(
        attach2(&mut c, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 99, 0),
        0
    );
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(
        attach2(&mut c, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 11, 0),
        0
    );
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(
        attach2(
            &mut c,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X,
            10,
            0
        ),
        0
    );
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(
        call(
            m,
            &mut c,
            FEnum_glFramebufferTexture3D,
            &[
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT1,
                GL_TEXTURE_3D,
                12,
                9,
                0
            ]
        ),
        0
    );
    assert_eq!(errors(&mut c), INVALID_VALUE);
    assert!(native_attachments(native_fb).is_empty());
    let refs = unsafe { (*t2).refs };
    assert_eq!(
        attach2(&mut c, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 10, 0),
        0
    );
    assert_eq!(
        attach2(
            &mut c,
            GL_COLOR_ATTACHMENT1,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
            11,
            0
        ),
        0
    );
    assert_eq!(
        call(
            m,
            &mut c,
            FEnum_glFramebufferTexture3D,
            &[
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT2,
                GL_TEXTURE_3D,
                12,
                0,
                3
            ]
        ),
        0
    );
    assert_eq!(
        call(
            m,
            &mut c,
            FEnum_glFramebufferTexture1D,
            &[GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_1D, 13, 0]
        ),
        0
    );
    assert_eq!(errors(&mut c), 0);
    assert_eq!(unsafe { (*t2).refs }, refs + 1);
    let natives = native_attachments(native_fb);
    unsafe {
        assert_eq!(
            natives[&GL_COLOR_ATTACHMENT0],
            (GL_TEXTURE_2D, (*t2).name, 0, 0)
        );
        assert_eq!(
            natives[&GL_COLOR_ATTACHMENT1],
            (GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, (*cube).name, 0, 0)
        );
        assert_eq!(
            natives[&GL_COLOR_ATTACHMENT2],
            (GL_TEXTURE_3D, (*volume).name, 0, 3)
        );
        assert_eq!(
            natives[&GL_COLOR_ATTACHMENT3],
            (GL_TEXTURE_1D, (*one).name, 0, 0)
        );
    }
    let param = |c: &mut Context, point, pname| {
        query(
            m,
            c,
            FEnum_glGetFramebufferAttachmentParameteriv,
            [GL_FRAMEBUFFER, point, pname],
        )
        .unwrap()
    };
    assert_eq!(
        param(
            &mut c,
            GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE
        ),
        TEXTURE
    );
    assert_eq!(
        param(
            &mut c,
            GL_COLOR_ATTACHMENT1,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME
        ),
        11
    );
    assert_eq!(
        param(
            &mut c,
            GL_COLOR_ATTACHMENT1,
            GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE
        ),
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
    );
    assert_eq!(
        param(
            &mut c,
            GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE
        ),
        0
    );
    assert_eq!(
        param(
            &mut c,
            GL_COLOR_ATTACHMENT2,
            GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT
        ),
        3
    );
    assert_eq!(
        param(
            &mut c,
            GL_DEPTH_ATTACHMENT,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE
        ),
        GL_NONE
    );
    assert_eq!(errors(&mut c), 0);
    assert_eq!(
        param(
            &mut c,
            GL_DEPTH_ATTACHMENT,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME
        ),
        0
    );
    assert_eq!(errors(&mut c), INVALID_ENUM);
    // Reads clip to the smallest extent: 8 wide (the volume), 1 high (1D).
    let mut result = [0xffu8; 16 * 4];
    unsafe {
        read_pixels(
            &*m.api,
            s(&mut c),
            [6, 0, 4, 4],
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            4,
            result.as_mut_ptr(),
        )
        .unwrap()
    };
    assert_eq!(native(|n| n.reads.clone()), vec![(6, 0, 2, 1, 4)]);
    let word = |i: usize| u32::from_le_bytes(result[i * 4..i * 4 + 4].try_into().unwrap());
    assert_eq!(
        (word(0), word(1), word(2), word(4)),
        (6 << 16, 7 << 16, 0, 0)
    );
    assert_eq!(native(|n| n.pack_row_length), 0, "pack state restored");
    // Deleting a texture detaches it from the bound framebuffer; one that is
    // attached to an unbound framebuffer stays alive (deleted) until then.
    assert_eq!(
        call(m, &mut c, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 4]),
        0
    );
    assert_eq!(
        attach2(&mut c, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 10, 0),
        0
    );
    let fb4 = unsafe { (*object(&mut c, 4)).native };
    let native_t2 = unsafe { (*t2).name };
    let names = 10u32.to_le_bytes();
    assert_eq!(
        unsafe {
            crate::data::dreamgpu_gl_data(
                m,
                s(&mut c),
                0,
                1,
                FEnum_glDeleteTextures,
                1u32.to_le_bytes().as_ptr(),
                names.as_ptr(),
                4,
            )
        },
        0
    );
    assert!(native_attachments(fb4).is_empty());
    unsafe {
        assert!((*object(&mut c, 4)).attachments[DEPTH].texture.is_null());
        assert_eq!((*fb).attachments[0].texture, t2);
        assert_eq!((*t2).deleted, 1);
        assert_eq!((*t2).refs, 1);
    }
    assert!(native(|n| !n.textures.contains(&native_t2)));
    assert_eq!(param_on_3(m, &mut c), 10, "deleted images keep their name");
    // Deleting the framebuffer releases the last reference.
    assert_eq!(data(m, &mut c, FEnum_glDeleteFramebuffers, &[3, 4]), 0);
    assert_eq!(errors(&mut c), 0);
    release(m, c);
    assert!(f.allocator.live.is_empty());
}
/// OBJECT_NAME of framebuffer 3's first colour attachment.
fn param_on_3(m: &Memory, c: &mut Context) -> u32 {
    assert_eq!(
        call(m, c, FEnum_glBindFramebuffer, &[GL_READ_FRAMEBUFFER, 3]),
        0
    );
    let name = query(
        m,
        c,
        FEnum_glGetFramebufferAttachmentParameteriv,
        [
            GL_READ_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
        ],
    )
    .unwrap();
    assert_eq!(
        call(m, c, FEnum_glBindFramebuffer, &[GL_READ_FRAMEBUFFER, 4]),
        0
    );
    name
}

#[test]
fn renderbuffers_are_shared_charged_zeroed_and_outlive_deletion_while_attached() {
    let f = fixture();
    let m = &f.memory;
    let mut a = context(m, null_mut());
    let shared = unsafe { (*s(&mut a)).textures };
    let mut b = context(m, shared);
    // Storage without a bound renderbuffer.
    assert_eq!(
        call(
            m,
            &mut a,
            FEnum_glRenderbufferStorage,
            &[GL_RENDERBUFFER, GL_RGBA8, 8, 8]
        ),
        0
    );
    assert_eq!(errors(&mut a), INVALID_OPERATION);
    assert_eq!(
        query(
            m,
            &mut a,
            FEnum_glGetRenderbufferParameteriv,
            [GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, 0]
        ),
        Ok(0)
    );
    assert_eq!(errors(&mut a), INVALID_OPERATION);
    assert_eq!(
        call(m, &mut a, FEnum_glBindRenderbuffer, &[GL_RENDERBUFFER, 9]),
        0
    );
    assert_eq!(get(m, &mut a, GL_RENDERBUFFER_BINDING), 9);
    assert_eq!(
        query(
            m,
            &mut a,
            FEnum_glGetRenderbufferParameteriv,
            [GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, 0]
        ),
        Ok(GL_RGBA)
    );
    let before = *f.bytes;
    assert_eq!(
        call(
            m,
            &mut a,
            FEnum_glRenderbufferStorage,
            &[GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 64, 32]
        ),
        0
    );
    assert_eq!(errors(&mut a), 0);
    assert_eq!(*f.bytes - before, 64 * 32 * 4);
    // Zero-initialized through a private framebuffer, the guest's state kept.
    assert_eq!(
        native(|n| n.clears.clone()).last().map(|c| c.1),
        Some(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)
    );
    assert_eq!(
        native(|n| (n.draw, n.read, n.clear_color)),
        (WINDOW, WINDOW, [0.25; 4])
    );
    for (pname, value) in [
        (GL_RENDERBUFFER_WIDTH, 64),
        (GL_RENDERBUFFER_HEIGHT, 32),
        (GL_RENDERBUFFER_INTERNAL_FORMAT, GL_DEPTH24_STENCIL8),
        (GL_RENDERBUFFER_DEPTH_SIZE, 24),
        (GL_RENDERBUFFER_STENCIL_SIZE, 8),
    ] {
        assert_eq!(
            query(
                m,
                &mut a,
                FEnum_glGetRenderbufferParameteriv,
                [GL_RENDERBUFFER, pname, 0]
            ),
            Ok(value)
        );
    }
    // Beyond the native limit (clamped to the protocol's), and a native failure.
    assert_eq!(
        call(
            m,
            &mut a,
            FEnum_glRenderbufferStorage,
            &[GL_RENDERBUFFER, GL_RGBA8, 4096, 4096]
        ),
        0
    );
    assert_eq!(errors(&mut a), 0);
    assert_eq!(*f.bytes - before, 4096 * 4096 * 4);
    native(|n| n.fail_storage = GL_OUT_OF_MEMORY);
    assert_eq!(
        call(
            m,
            &mut a,
            FEnum_glRenderbufferStorage,
            &[GL_RENDERBUFFER, GL_RGBA8, 16, 16]
        ),
        0
    );
    assert_eq!(errors(&mut a), OUT_OF_MEMORY);
    assert_eq!(
        *f.bytes - before,
        4096 * 4096 * 4,
        "native failure keeps the storage"
    );
    // Budget exhaustion empties the storage.
    let used = *f.bytes;
    *unsafe { &mut *m.bytes } = u64::from(DG_GL_MAX_TEXTURE_BYTES) - 64;
    assert_eq!(
        call(
            m,
            &mut a,
            FEnum_glRenderbufferStorage,
            &[GL_RENDERBUFFER, GL_RGBA16, 4096, 4096]
        ),
        0
    );
    assert_eq!(errors(&mut a), OUT_OF_MEMORY);
    let rb = unsafe { (*s(&mut a)).renderbuffer };
    unsafe {
        assert_eq!(((*rb).width, (*rb).height, (*rb).bytes), (0, 0, 0));
        // The real use without the emptied storage.
        *m.bytes = used - 4096 * 4096 * 4;
    }
    assert_eq!(
        call(
            m,
            &mut a,
            FEnum_glRenderbufferStorage,
            &[GL_RENDERBUFFER, GL_RGBA8, 16, 16]
        ),
        0
    );
    assert_eq!(errors(&mut a), 0);
    // The other context of the share group sees the same object.
    assert_eq!(
        call(m, &mut b, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 1]),
        0
    );
    assert_eq!(
        call(
            m,
            &mut b,
            FEnum_glFramebufferRenderbuffer,
            &[GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, 9]
        ),
        0
    );
    assert_eq!(errors(&mut b), 0);
    let fb_b = unsafe { (*object(&mut b, 1)).native };
    assert_eq!(
        native_attachments(fb_b)[&GL_COLOR_ATTACHMENT0],
        (GL_RENDERBUFFER, unsafe { (*rb).native }, 0, 0)
    );
    assert_eq!(
        call(
            m,
            &mut b,
            FEnum_glFramebufferRenderbuffer,
            &[GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 8]
        ),
        0
    );
    assert_eq!(errors(&mut b), INVALID_OPERATION, "8 is not an object");
    // Also attach it in a's bound framebuffer, then delete it from a: it is
    // unbound and detached there but lives on in b's framebuffer.
    assert_eq!(
        call(m, &mut a, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 1]),
        0
    );
    assert_eq!(
        call(
            m,
            &mut a,
            FEnum_glFramebufferRenderbuffer,
            &[GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 9]
        ),
        0
    );
    native(|n| n.draw = unsafe { (*object(&mut a, 1)).native });
    assert_eq!(data(m, &mut a, FEnum_glDeleteRenderbuffers, &[9]), 0);
    assert_eq!(get(m, &mut a, GL_RENDERBUFFER_BINDING), 0);
    unsafe {
        assert!((*object(&mut a, 1)).attachments[STENCIL]
            .renderbuffer
            .is_null());
        assert_eq!((*rb).deleted, 1);
        assert_eq!((*rb).refs, 1);
        assert_eq!((*object(&mut b, 1)).attachments[0].renderbuffer, rb);
    }
    assert_eq!(
        call(m, &mut b, FEnum_glBindRenderbuffer, &[GL_RENDERBUFFER, 9]),
        0
    );
    unsafe { assert_ne!((*s(&mut b)).renderbuffer, rb, "the name is free again") };
    assert_eq!(errors(&mut a) | errors(&mut b), 0);
    release(m, a);
    release(m, b);
    assert_eq!(*f.bytes, 0);
    assert!(f.allocator.live.is_empty());
    assert!(native(|n| n.renderbuffers.is_empty() && n.fences.is_empty()));
}

#[test]
fn rendering_is_fenced_and_versioned_when_the_context_stops_drawing() {
    let f = fixture();
    let m = &f.memory;
    let mut a = context(m, null_mut());
    let shared = unsafe { (*s(&mut a)).textures };
    let mut b = context(m, shared);
    let t = texture(m, &mut a, GL_TEXTURE_2D, 1, 16, 16);
    assert_eq!(
        call_on(m, &mut a, 7, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 1]),
        0
    );
    assert_eq!(
        call_on(
            m,
            &mut a,
            7,
            FEnum_glFramebufferTexture2D,
            &[GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 1, 0]
        ),
        0
    );
    let version = unsafe { (*t).version };
    // A texture read in the same context sees a new version.
    unsafe { touch(s(&mut a)) };
    assert_ne!(unsafe { (*t).version }, version);
    // Switching contexts fences the draw attachments for the next reader.
    unsafe { dreamgpu_framebuffer_settle(m.api, s(&mut a), 7) };
    unsafe {
        assert_eq!((*t).writer_serial, 7);
        assert!(!(*t).last_write.is_null());
    }
    // b binds a framebuffer with the same image and waits for a's writes.
    assert_eq!(
        call_on(m, &mut b, 8, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 1]),
        0
    );
    let waits = native(|n| n.waits);
    assert_eq!(
        call_on(
            m,
            &mut b,
            8,
            FEnum_glFramebufferTexture2D,
            &[GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 1, 0]
        ),
        0
    );
    assert_eq!(native(|n| n.waits), waits + 1);
    // Rebinding away from the draw framebuffer fences it as well.
    assert_eq!(
        call_on(
            m,
            &mut b,
            8,
            FEnum_glBindFramebuffer,
            &[GL_DRAW_FRAMEBUFFER, 0]
        ),
        0
    );
    unsafe { assert_eq!((*t).writer_serial, 8) };
    // So does replacing the attachment of a bound draw framebuffer (after the
    // platform made a current again).
    native(|n| {
        n.draw = WINDOW;
        n.read = WINDOW;
    });
    unsafe { rebind(&*m.api, s(&mut a)) };
    assert_eq!(
        call_on(
            m,
            &mut a,
            9,
            FEnum_glFramebufferTexture2D,
            &[GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0]
        ),
        0
    );
    unsafe { assert_eq!((*t).writer_serial, 9) };
    assert_eq!(errors(&mut a) | errors(&mut b), 0);
    release(m, a);
    release(m, b);
    assert!(native(|n| n.fences.is_empty()));
    assert!(f.allocator.live.is_empty());
}

#[test]
fn reads_refuse_uninitialized_images_and_incomplete_framebuffers() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    let t = texture(m, &mut c, GL_TEXTURE_2D, 1, 8, 8);
    assert_eq!(
        call(m, &mut c, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 1]),
        0
    );
    assert_eq!(
        call(
            m,
            &mut c,
            FEnum_glFramebufferTexture2D,
            &[GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 1, 0]
        ),
        0
    );
    let blit_args = [
        0,
        u32::MAX,
        8,
        8,
        0x8000_0000,
        0,
        8,
        8,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST,
    ];
    assert_eq!(call(m, &mut c, FEnum_glBlitFramebuffer, &blit_args), 0);
    assert_eq!(
        call(m, &mut c, FEnum_glCopyPixels, &[0, 0, 4, 4, GL_COLOR]),
        0
    );
    assert_eq!(native(|n| n.blits.len()), 2);
    assert_eq!(native(|n| n.blits[0]), [0, -1, 8, 8, i32::MIN, 0, 8, 8]);
    assert_eq!(errors(&mut c), 0);
    // An uninitialized level: every read is INVALID_FRAMEBUFFER_OPERATION.
    unsafe {
        let mut level = image_levels(t, GL_TEXTURE_2D);
        level.undefined_levels = 1;
        set_image_levels(t, GL_TEXTURE_2D, &level);
    }
    assert_eq!(call(m, &mut c, FEnum_glBlitFramebuffer, &blit_args), 0);
    assert_eq!(errors(&mut c), INVALID_FRAMEBUFFER_OPERATION);
    assert_eq!(
        call(m, &mut c, FEnum_glCopyPixels, &[0, 0, 4, 4, GL_COLOR]),
        0
    );
    assert_eq!(errors(&mut c), INVALID_FRAMEBUFFER_OPERATION);
    let mut result = [0xffu8; 4];
    unsafe {
        read_pixels(
            &*m.api,
            s(&mut c),
            [0, 0, 1, 1],
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            4,
            result.as_mut_ptr(),
        )
        .unwrap()
    };
    assert_eq!(
        (result, errors(&mut c)),
        ([0; 4], INVALID_FRAMEBUFFER_OPERATION)
    );
    assert_eq!(native(|n| n.blits.len()), 2);
    // An incomplete framebuffer reads zeros with the same error.
    unsafe {
        let mut level = image_levels(t, GL_TEXTURE_2D);
        level.undefined_levels = 0;
        set_image_levels(t, GL_TEXTURE_2D, &level);
    }
    native(|n| n.status = GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);
    unsafe {
        read_pixels(
            &*m.api,
            s(&mut c),
            [0, 0, 1, 1],
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            4,
            result.as_mut_ptr(),
        )
        .unwrap()
    };
    assert_eq!(
        (result, errors(&mut c)),
        ([0; 4], INVALID_FRAMEBUFFER_OPERATION)
    );
    assert!(native(|n| n.reads.is_empty()));
    // Native statuses pass through; GL 3 ones become UNSUPPORTED; the window is complete.
    assert_eq!(
        query(
            m,
            &mut c,
            FEnum_glCheckFramebufferStatus,
            [GL_FRAMEBUFFER, 0, 0]
        ),
        Ok(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
    );
    native(|n| n.status = GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE);
    assert_eq!(
        query(
            m,
            &mut c,
            FEnum_glCheckFramebufferStatus,
            [GL_READ_FRAMEBUFFER, 0, 0]
        ),
        Ok(GL_FRAMEBUFFER_UNSUPPORTED)
    );
    assert_eq!(
        call(
            m,
            &mut c,
            FEnum_glBindFramebuffer,
            &[GL_READ_FRAMEBUFFER, 0]
        ),
        0
    );
    assert_eq!(
        query(
            m,
            &mut c,
            FEnum_glCheckFramebufferStatus,
            [GL_READ_FRAMEBUFFER, 0, 0]
        ),
        Ok(GL_FRAMEBUFFER_COMPLETE)
    );
    assert_eq!(errors(&mut c), 0);
    release(m, c);
    assert!(f.allocator.live.is_empty());
}

#[test]
fn generate_mipmap_defines_and_charges_the_chain() {
    let mut f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    let t = texture(m, &mut c, GL_TEXTURE_2D, 1, 8, 4);
    let base = *f.bytes;
    assert_eq!(call(m, &mut c, FEnum_glGenerateMipmap, &[GL_TEXTURE_2D]), 0);
    assert_eq!(errors(&mut c), 0);
    assert_eq!(native(|n| n.generated.clone()), vec![GL_TEXTURE_2D]);
    let f2 = unsafe { image_levels(t, GL_TEXTURE_2D) };
    assert_eq!(&f2.widths[..5], &[8, 4, 2, 1, 0]);
    assert_eq!(&f2.heights[..5], &[4, 2, 1, 1, 0]);
    assert_eq!(*f.bytes - base, (8 + 2 + 1) * 4);
    // Again: the chain is replaced, not charged twice.
    assert_eq!(call(m, &mut c, FEnum_glGenerateMipmap, &[GL_TEXTURE_2D]), 0);
    assert_eq!(*f.bytes - base, (8 + 2 + 1) * 4);
    // A compressed base level is INVALID_OPERATION without native work.
    native(|n| n.level_compressed = 1);
    assert_eq!(call(m, &mut c, FEnum_glGenerateMipmap, &[GL_TEXTURE_2D]), 0);
    assert_eq!(errors(&mut c), INVALID_OPERATION);
    assert_eq!(native(|n| n.generated.len()), 2);
    native(|n| n.level_compressed = 0);
    // An uninitialized base level generates uninitialized levels.
    unsafe {
        let mut level = image_levels(t, GL_TEXTURE_2D);
        level.undefined_levels = 1;
        set_image_levels(t, GL_TEXTURE_2D, &level);
    }
    assert_eq!(call(m, &mut c, FEnum_glGenerateMipmap, &[GL_TEXTURE_2D]), 0);
    assert_eq!(
        unsafe { image_levels(t, GL_TEXTURE_2D).undefined_levels },
        0b1111
    );
    // Cube maps generate every face.
    let cube = texture(m, &mut c, GL_TEXTURE_CUBE_MAP, 2, 4, 4);
    assert_eq!(
        call(m, &mut c, FEnum_glGenerateMipmap, &[GL_TEXTURE_CUBE_MAP]),
        0
    );
    unsafe {
        assert_eq!((*cube).levels[1], 6 * 2 * 2 * 4);
        assert_eq!((*cube).levels[2], 6 * 4);
    }
    // Budget exhaustion is OUT_OF_MEMORY without native work.
    let volume = texture(m, &mut c, GL_TEXTURE_3D, 3, 8, 8);
    unsafe { (*volume).levels[0] = 8 * 8 * 4 * 4 };
    *f.bytes += 8 * 8 * 3 * 4;
    let saved = *f.bytes;
    *unsafe { &mut *m.bytes } = u64::from(DG_GL_MAX_TEXTURE_BYTES);
    assert_eq!(call(m, &mut c, FEnum_glGenerateMipmap, &[GL_TEXTURE_3D]), 0);
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    *unsafe { &mut *m.bytes } = saved;
    assert_eq!(call(m, &mut c, FEnum_glGenerateMipmap, &[GL_TEXTURE_3D]), 0);
    unsafe {
        let depths = (*volume).depths;
        assert_eq!(depths[..4], [4, 2, 1, 1]);
        assert_eq!((*volume).levels[1], 4 * 4 * 2 * 4);
    }
    assert_eq!(errors(&mut c), 0);
    release(m, c);
    assert_eq!(*f.bytes, 0);
}

#[test]
fn table_and_budget_exhaustion_are_out_of_memory() {
    let f = fixture();
    let m = &f.memory;
    let mut c = context(m, null_mut());
    for name in 1..=DG_GL_MAX_FRAMEBUFFERS {
        assert_eq!(
            call(m, &mut c, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, name]),
            0
        );
    }
    assert_eq!(errors(&mut c), 0);
    assert_eq!(
        call(m, &mut c, FEnum_glBindFramebuffer, &[GL_FRAMEBUFFER, 5000]),
        0
    );
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    assert_eq!(
        get(m, &mut c, GL_FRAMEBUFFER_BINDING),
        DG_GL_MAX_FRAMEBUFFERS
    );
    let names: Vec<u32> = (1..=DG_GL_MAX_FRAMEBUFFERS).collect();
    assert_eq!(data(m, &mut c, FEnum_glDeleteFramebuffers, &names), 0);
    assert_eq!(native(|n| n.framebuffers.len()), 0);
    let used = *f.bytes;
    *unsafe { &mut *m.bytes } = u64::from(DG_GL_MAX_TEXTURE_BYTES);
    assert_eq!(
        call(m, &mut c, FEnum_glBindRenderbuffer, &[GL_RENDERBUFFER, 1]),
        0
    );
    assert_eq!(errors(&mut c), OUT_OF_MEMORY);
    assert_eq!(get(m, &mut c, GL_RENDERBUFFER_BINDING), 0);
    *unsafe { &mut *m.bytes } = used;
    release(m, c);
    assert_eq!(*f.bytes, 0);
    assert!(f.allocator.live.is_empty());
}
