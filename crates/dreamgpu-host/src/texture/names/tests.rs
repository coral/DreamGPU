use super::*;
use std::alloc::{alloc_zeroed, dealloc, Layout};
use std::collections::BTreeMap;
use std::sync::atomic::{AtomicU32, Ordering};
#[derive(Default)]
struct Allocator {
    live: BTreeMap<usize, Layout>,
    forgotten: Vec<usize>,
    fail: bool,
}
unsafe extern "C" fn allocate(p: *mut c_void, bytes: usize) -> *mut c_void {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    if a.fail {
        return core::ptr::null_mut();
    }
    let layout = Layout::from_size_align(bytes, 8).unwrap();
    let p = unsafe { alloc_zeroed(layout) };
    a.live.insert(p as usize, layout);
    p.cast()
}
unsafe extern "C" fn free(p: *mut c_void, object: *mut c_void) {
    let a = unsafe { &mut *p.cast::<Allocator>() };
    let layout = a
        .live
        .remove(&(object as usize))
        .expect("double or foreign free");
    unsafe {
        dealloc(object.cast(), layout);
    }
}
unsafe extern "C" fn forget(p: *mut c_void, object: *mut Texture) {
    unsafe { &mut *p.cast::<Allocator>() }
        .forgotten
        .push(object as usize);
}
unsafe extern "C" fn gen(_: i32, out: *mut u32) {
    static NEXT: AtomicU32 = AtomicU32::new(1);
    unsafe {
        *out = NEXT.fetch_add(1, Ordering::Relaxed);
    }
}
unsafe extern "C" fn bind_native(_: u32, _: u32) {}
unsafe extern "C" fn delete_native(_: i32, _: *const u32) {}
unsafe extern "C" fn error() -> u32 {
    0
}
unsafe extern "C" fn gen_zero(_: i32, out: *mut u32) {
    unsafe {
        *out = 0;
    }
}

#[test]
fn allocation_and_native_name_failure_publish_no_resource() {
    let mut allocator = Allocator::default();
    let mut total = 0u64;
    let mut count = 2u32;
    let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    api.dg_glGenTextures = Some(gen_zero);
    api.dg_glGetError = Some(error);
    let memory = Memory {
        api: &api,
        bytes: &mut total,
        count: &mut count,
        opaque: (&mut allocator as *mut Allocator).cast(),
        allocate,
        free,
        forget_read: forget,
    };
    let mut ns = Box::new(Namespace::default());
    let mut default = Texture {
        refs: 2,
        ..Texture::default()
    };
    let mut binding = &mut default as *mut Texture;
    let mut errors = 0;
    allocator.fail = true;
    assert_eq!(
        unsafe {
            bind(
                &memory,
                &mut *ns,
                GL_TEXTURE_2D,
                7,
                &mut default,
                &mut binding,
                1,
                &mut errors,
            )
        },
        Err(6)
    );
    allocator.fail = false;
    assert_eq!(
        unsafe {
            bind(
                &memory,
                &mut *ns,
                GL_TEXTURE_2D,
                7,
                &mut default,
                &mut binding,
                1,
                &mut errors,
            )
        },
        Err(6)
    );
    assert!(allocator.live.is_empty());
    assert_eq!(count, 2);
    assert_eq!(total, 0);
    assert_eq!(default.refs, 2);
    assert_eq!(binding, &mut default as *mut Texture);
    assert!(unsafe { dreamgpu_texture_lookup(&mut *ns, 7) }.is_null());
    count = DG_GL_MAX_TEXTURES;
    assert_eq!(
        unsafe {
            bind(
                &memory,
                &mut *ns,
                GL_TEXTURE_2D,
                7,
                &mut default,
                &mut binding,
                1,
                &mut errors,
            )
        },
        Err(9)
    );
    assert_eq!(count, DG_GL_MAX_TEXTURES);
    assert!(allocator.live.is_empty());
}

#[test]
fn deleted_bound_storage_lives_until_last_owner_and_names_reuse() {
    let mut a = Allocator::default();
    let mut total = 0u64;
    let mut count = 2u32;
    let mut api: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    api.dg_glGenTextures = Some(gen);
    api.dg_glBindTexture = Some(bind_native);
    api.dg_glDeleteTextures = Some(delete_native);
    api.dg_glGetError = Some(error);
    let memory = Memory {
        api: &api,
        bytes: &mut total,
        count: &mut count,
        opaque: (&mut a as *mut Allocator).cast(),
        allocate,
        free,
        forget_read: forget,
    };
    unsafe {
        let ns: *mut Namespace = allocate(memory.opaque, core::mem::size_of::<Namespace>()).cast();
        (*ns).refs = 1;
        let default2: *mut Texture =
            allocate(memory.opaque, core::mem::size_of::<Texture>()).cast();
        let default1: *mut Texture =
            allocate(memory.opaque, core::mem::size_of::<Texture>()).cast();
        core::ptr::write(
            default2,
            Texture {
                refs: 2,
                ..Texture::default()
            },
        );
        core::ptr::write(
            default1,
            Texture {
                refs: 2,
                target: GL_TEXTURE_1D,
                ..Texture::default()
            },
        );
        let mut binding2 = default2;
        let mut binding1 = default1;
        let mut errors = 0;
        bind(
            &memory,
            ns,
            GL_TEXTURE_2D,
            7,
            default2,
            &mut binding2,
            1,
            &mut errors,
        )
        .unwrap();
        let old = binding2;
        let old_name = (*old).name;
        assert_eq!(((*old).refs, count), (2, 3));
        (*old).refs += 1; // Another context/attribute stack retains this storage.
        (*old).levels[0] = 64;
        total = 64;
        dreamgpu_texture_delete(
            &memory,
            ns,
            7u32.to_le_bytes().as_ptr(),
            1,
            default2,
            default1,
            &mut binding2,
            &mut binding1,
        );
        assert_eq!(binding2, default2);
        assert_eq!(((*old).refs, (*old).deleted, total), (1, 1, 64));
        assert!(dreamgpu_texture_lookup(ns, 7).is_null());
        bind(
            &memory,
            ns,
            GL_TEXTURE_2D,
            7,
            default2,
            &mut binding2,
            1,
            &mut errors,
        )
        .unwrap();
        assert_ne!(binding2, old);
        assert_ne!((*binding2).name, old_name);
        let original_binding1 = binding1;
        bind(
            &memory,
            ns,
            GL_TEXTURE_1D,
            7,
            default1,
            &mut binding1,
            1,
            &mut errors,
        )
        .unwrap();
        assert_eq!(binding1, original_binding1);
        assert_eq!(errors, flag(GL_INVALID_OPERATION));
        unref(&memory, old);
        assert_eq!(total, 0);
        dreamgpu_texture_namespace_unref(&memory, ns);
        unref(&memory, binding2);
        unref(&memory, binding1);
        unref(&memory, default2);
        unref(&memory, default1);
    }
    assert_eq!(count, 0);
    assert!(a.live.is_empty());
    assert_eq!(a.forgotten.len(), 4);
}

#[test]
fn collisions_backward_shift_full_table_and_zero_name() {
    let mut ns = Box::new(Namespace::default());
    assert_eq!(unsafe { locate(&mut *ns, 0) }, Err(None));
    for (i, name) in [1u32, 8193, 16385, u32::MAX].into_iter().enumerate() {
        let Err(Some(slot)) = (unsafe { locate(&mut *ns, name) }) else {
            panic!()
        };
        ns.entries[slot] = Entry {
            name,
            texture: (i + 1) as *mut Texture,
        };
    }
    let first = unsafe { locate(&mut *ns, 1) }.unwrap();
    unsafe {
        remove(&mut *ns, first);
    }
    assert!(unsafe { locate(&mut *ns, 16385) }.is_ok());
    assert!(unsafe { locate(&mut *ns, u32::MAX) }.is_ok());
    assert!(matches!(unsafe { locate(&mut *ns, 24577) }, Err(Some(_))));
    for (i, entry) in ns.entries.iter_mut().enumerate() {
        *entry = Entry {
            name: (i + 1) as u32,
            texture: std::ptr::dangling_mut::<Texture>(),
        };
    }
    assert_eq!(unsafe { locate(&mut *ns, 99999) }, Err(None));
    ns.entries[123].texture = core::ptr::null_mut();
    assert_eq!(unsafe { locate(&mut *ns, 99999) }, Err(Some(123)));
}

#[test]
fn long_lived_name_churn_leaves_no_dead_probe_chains() {
    let mut ns = Box::new(Namespace::default());
    for base in (1u32..65536).step_by(17) {
        let mut live = Vec::new();
        // Collision chains include wraparound buckets as well as ordinary names.
        for n in 0..17 {
            let name = base.wrapping_add(n * 8192);
            let Err(Some(slot)) = (unsafe { locate(&mut *ns, name) }) else {
                panic!()
            };
            ns.entries[slot] = Entry {
                name,
                texture: (name as usize + 1) as *mut Texture,
            };
            live.push(name);
        }
        while !live.is_empty() {
            let name = live.remove(live.len() / 2);
            let slot = unsafe { locate(&mut *ns, name) }.unwrap();
            unsafe {
                remove(&mut *ns, slot);
            }
            for &remaining in &live {
                let i = unsafe { locate(&mut *ns, remaining) }.unwrap();
                assert_eq!(ns.entries[i].texture as usize, remaining as usize + 1);
            }
        }
    }
    assert!(ns
        .entries
        .iter()
        .all(|entry| entry.name == 0 && entry.texture.is_null()));
}
