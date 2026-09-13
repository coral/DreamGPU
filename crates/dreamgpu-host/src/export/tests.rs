use super::*;
use std::{cell::RefCell, collections::VecDeque};
#[derive(Default)]
struct Native {
    read: i32,
    draw: i32,
    buffer: i32,
    binding: i32,
    scissor: u8,
    next: u32,
    fail_gen: u32,
    generated: Vec<u32>,
    deleted: Vec<u32>,
    complete: bool,
    blits: Vec<[i32; 8]>,
    statuses: VecDeque<u32>,
    sync_deleted: usize,
    waits: usize,
}
std::thread_local! {static N:RefCell<Native>=RefCell::new(Native::default());}
unsafe extern "C" fn get(name: u32, p: *mut i32) {
    N.with(|n| {
        let n = n.borrow();
        unsafe {
            *p = match name {
                GL_READ_FRAMEBUFFER_BINDING => n.read,
                GL_DRAW_FRAMEBUFFER_BINDING => n.draw,
                GL_READ_BUFFER => n.buffer,
                GL_TEXTURE_BINDING_RECTANGLE_ARB | GL_RENDERBUFFER_BINDING => n.binding,
                _ => panic!("unexpected query"),
            };
        }
    })
}
unsafe extern "C" fn enabled(_: u32) -> u8 {
    N.with(|n| n.borrow().scissor)
}
unsafe extern "C" fn enable(_: u32) {
    N.with(|n| n.borrow_mut().scissor = 1)
}
unsafe extern "C" fn disable(_: u32) {
    N.with(|n| n.borrow_mut().scissor = 0)
}
unsafe extern "C" fn bind_fb(target: u32, name: u32) {
    N.with(|n| {
        let mut n = n.borrow_mut();
        if target == GL_READ_FRAMEBUFFER || target == GL_FRAMEBUFFER {
            n.read = name as i32;
        }
        if target == GL_DRAW_FRAMEBUFFER || target == GL_FRAMEBUFFER {
            n.draw = name as i32;
        }
    })
}
unsafe extern "C" fn read_buffer(value: u32) {
    N.with(|n| n.borrow_mut().buffer = value as i32)
}
unsafe extern "C" fn bind(target: u32, name: u32) {
    assert!(matches!(target, GL_TEXTURE_RECTANGLE_ARB | GL_RENDERBUFFER));
    N.with(|n| n.borrow_mut().binding = name as i32)
}
unsafe extern "C" fn gen(n: i32, p: *mut u32) {
    assert_eq!(n, 1);
    N.with(|v| {
        let mut v = v.borrow_mut();
        v.next += 1;
        let x = if v.next == v.fail_gen { 0 } else { v.next };
        if x != 0 {
            v.generated.push(x);
        }
        unsafe { *p = x };
    })
}
unsafe extern "C" fn delete(n: i32, p: *const u32) {
    assert_eq!(n, 1);
    N.with(|v| v.borrow_mut().deleted.push(unsafe { *p }))
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
unsafe extern "C" fn blit(
    a: i32,
    b: i32,
    c: i32,
    d: i32,
    e: i32,
    f: i32,
    g: i32,
    h: i32,
    bits: u32,
    filter: u32,
) {
    assert_eq!((bits, filter), (GL_COLOR_BUFFER_BIT, GL_NEAREST));
    N.with(|v| {
        let mut v = v.borrow_mut();
        assert_eq!(v.scissor, 0);
        v.blits.push([a, b, c, d, e, f, g, h]);
    })
}
unsafe extern "C" fn wait(_: *mut __GLsync, flags: u32, timeout: u64) -> u32 {
    assert_eq!((flags, timeout), (0, 0));
    N.with(|v| {
        let mut v = v.borrow_mut();
        v.waits += 1;
        v.statuses.pop_front().unwrap_or(GL_TIMEOUT_EXPIRED)
    })
}
unsafe extern "C" fn delete_sync(_: *mut __GLsync) {
    N.with(|v| v.borrow_mut().sync_deleted += 1)
}
fn api() -> DreamGpuGlApi {
    let mut a: DreamGpuGlApi = unsafe { core::mem::zeroed() };
    a.dg_glGetIntegerv = Some(get);
    a.dg_glIsEnabled = Some(enabled);
    a.dg_glEnable = Some(enable);
    a.dg_glDisable = Some(disable);
    a.dg_glBindFramebuffer = Some(bind_fb);
    a.dg_glReadBuffer = Some(read_buffer);
    a.dg_glBindTexture = Some(bind);
    a.dg_glBindRenderbuffer = Some(bind);
    a.dg_glGenTextures = Some(gen);
    a.dg_glGenRenderbuffers = Some(gen);
    a.dg_glGenFramebuffers = Some(gen);
    a.dg_glDeleteTextures = Some(delete);
    a.dg_glDeleteRenderbuffers = Some(delete);
    a.dg_glDeleteFramebuffers = Some(delete);
    a.dg_glFramebufferTexture2D = Some(attach);
    a.dg_glFramebufferRenderbuffer = Some(attach_rb);
    a.dg_glCheckFramebufferStatus = Some(check);
    a.dg_glBlitFramebuffer = Some(blit);
    a.dg_glClientWaitSync = Some(wait);
    a.dg_glDeleteSync = Some(delete_sync);
    a
}
struct Callback {
    stage: u32,
    binds: u32,
    fences: u32,
    now: i64,
    sleeps: usize,
    step: i64,
}
unsafe extern "C" fn os_bind(p: *mut c_void) -> u32 {
    let c = unsafe { &mut *p.cast::<Callback>() };
    c.binds += 1;
    if c.stage == 0 {
        DG_GL_ERROR_HOST
    } else {
        0
    }
}
unsafe extern "C" fn os_fence(p: *mut c_void) -> u32 {
    let c = unsafe { &mut *p.cast::<Callback>() };
    c.fences += 1;
    if c.stage == 2 {
        DG_GL_ERROR_HOST
    } else {
        0
    }
}
unsafe extern "C" fn clock(p: *mut c_void) -> i64 {
    unsafe { (*p.cast::<Callback>()).now }
}
unsafe extern "C" fn sleep(p: *mut c_void, us: u64) {
    assert_eq!(us, 100);
    let c = unsafe { &mut *p.cast::<Callback>() };
    c.sleeps += 1;
    c.now = c.now.saturating_add(c.step);
}
#[test]
fn export_restores_every_state_and_deletes_temporary_objects_on_each_failure_stage() {
    let a = api();
    let mut d = Drawable {
        width: 640,
        height: 480,
        color: 7,
        front: 8,
        depth: 9,
        last_write: core::ptr::null_mut(),
    };
    let state: ContextState = unsafe { core::mem::zeroed() };
    for rectangle in [0, 1] {
        for stage in 0..4 {
            N.with(|v| {
                *v.borrow_mut() = Native {
                    read: 11,
                    draw: 12,
                    buffer: GL_FRONT as i32,
                    binding: 23,
                    scissor: 1,
                    next: 30,
                    complete: stage != 1,
                    ..Native::default()
                }
            });
            let mut c = Callback {
                stage,
                binds: 0,
                fences: 0,
                now: 0,
                sleeps: 0,
                step: 0,
            };
            let result = unsafe {
                dreamgpu_export(
                    &a,
                    &mut d,
                    &state,
                    42,
                    0,
                    rectangle,
                    Some(os_bind),
                    Some(os_fence),
                    (&mut c as *mut Callback).cast(),
                )
            };
            assert_eq!(result, if stage == 3 { 0 } else { DG_GL_ERROR_HOST });
            assert_eq!(c.binds, 1);
            assert_eq!(c.fences, u32::from(stage >= 2));
            N.with(|v| {
                let v = v.borrow();
                assert_eq!(
                    (v.read, v.draw, v.buffer, v.binding, v.scissor),
                    (11, 12, GL_FRONT as i32, 23, 1)
                );
                let mut gen = v.generated.clone();
                let mut deleted = v.deleted.clone();
                gen.sort();
                deleted.sort();
                assert_eq!(gen, deleted);
                assert_eq!(
                    v.blits,
                    if stage >= 2 {
                        vec![[0, 480, 640, 0, 0, 0, 640, 480]]
                    } else {
                        vec![]
                    }
                );
            });
        }
    }
}
#[test]
fn completion_poll_is_immediate_or_sleeps_bounded_and_always_consumes_fence() {
    let a = api();
    for (statuses, step, expected, sleeps) in [
        (vec![GL_ALREADY_SIGNALED], 100, GL_ALREADY_SIGNALED, 0),
        (
            vec![GL_TIMEOUT_EXPIRED, GL_CONDITION_SATISFIED],
            100,
            GL_CONDITION_SATISFIED,
            1,
        ),
        (vec![], 1_000_000, GL_TIMEOUT_EXPIRED, 5),
        (vec![GL_WAIT_FAILED], 100, GL_WAIT_FAILED, 0),
    ] {
        N.with(|v| {
            *v.borrow_mut() = Native {
                statuses: statuses.into(),
                ..Native::default()
            }
        });
        let mut c = Callback {
            stage: 0,
            binds: 0,
            fences: 0,
            now: 0,
            sleeps: 0,
            step,
        };
        assert_eq!(
            unsafe {
                dreamgpu_export_wait(
                    &a,
                    2usize as *mut __GLsync,
                    clock,
                    sleep,
                    (&mut c as *mut Callback).cast(),
                )
            },
            expected
        );
        assert_eq!(c.sleeps, sleeps);
        N.with(|v| assert_eq!(v.borrow().sync_deleted, 1));
    }
}

#[test]
fn zero_export_names_do_not_bind_os_image_or_blit_default_framebuffer() {
    let a = api();
    let state: ContextState = unsafe { core::mem::zeroed() };
    let mut d = Drawable {
        width: 640,
        height: 480,
        color: 7,
        front: 8,
        depth: 9,
        last_write: core::ptr::null_mut(),
    };
    for rectangle in [0, 1] {
        for fail_gen in [1, 2] {
            N.with(|v| {
                *v.borrow_mut() = Native {
                    read: 11,
                    draw: 12,
                    buffer: GL_FRONT as i32,
                    binding: 23,
                    scissor: 1,
                    complete: true,
                    fail_gen,
                    ..Native::default()
                }
            });
            let mut c = Callback {
                stage: 3,
                binds: 0,
                fences: 0,
                now: 0,
                sleeps: 0,
                step: 0,
            };
            assert_eq!(
                unsafe {
                    dreamgpu_export(
                        &a,
                        &mut d,
                        &state,
                        42,
                        0,
                        rectangle,
                        Some(os_bind),
                        Some(os_fence),
                        (&mut c as *mut Callback).cast(),
                    )
                },
                DG_GL_ERROR_HOST
            );
            assert_eq!(c.binds, u32::from(fail_gen == 2));
            assert_eq!(c.fences, 0);
            N.with(|v| {
                let v = v.borrow();
                assert!(v.blits.is_empty());
                assert_eq!(
                    (v.read, v.draw, v.buffer, v.binding, v.scissor),
                    (11, 12, GL_FRONT as i32, 23, 1)
                );
                assert_eq!(v.generated, v.deleted);
            });
        }
    }
}
