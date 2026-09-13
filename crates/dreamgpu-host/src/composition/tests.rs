use super::*;
#[derive(Default)]
struct Check {
    capture_error: u32,
    queue_error: u32,
    read_error: u32,
    retained: u32,
    captures: Vec<(u32, u64, u64)>,
    packets: Vec<[u8; 128]>,
    failed: u32,
    state: *const State,
}
unsafe extern "C" fn capture(p: *mut c_void, _: *const u8, kind: u32) -> u32 {
    let c = unsafe { &mut *p.cast::<Check>() };
    c.captures
        .push(unsafe { (kind, (*c.state).epoch, (*c.state).sequence) });
    c.capture_error
}
unsafe extern "C" fn queue(p: *mut c_void, packet: *const u8) -> u32 {
    let c = unsafe { &mut *p.cast::<Check>() };
    c.packets.push(unsafe { packet.cast::<[u8; 128]>().read() });
    c.queue_error
}
unsafe extern "C" fn readback(p: *mut c_void, _: *const u8, packet: *mut u8) -> u32 {
    let c = unsafe { &mut *p.cast::<Check>() };
    c.packets.push(unsafe { packet.cast::<[u8; 128]>().read() });
    unsafe { packet.add(120).write(23) };
    c.read_error
}
unsafe extern "C" fn retained(p: *mut c_void, _: *const u8) -> u32 {
    unsafe { (*p.cast::<Check>()).retained }
}
unsafe extern "C" fn failed(p: *mut c_void) {
    unsafe { (*p.cast::<Check>()).failed += 1 }
}
fn ops(c: &mut Check, s: &State) -> Ops {
    c.state = s;
    Ops {
        opaque: (c as *mut Check).cast(),
        capture,
        readback,
        queue,
        retained,
        failed,
    }
}
fn record(op: u32, w: u32, h: u32) -> Vec<u8> {
    let mut r = vec![0; (DG_GL_HEADER_BYTES + DG_DESKTOP_BYTES) as usize];
    put(&mut r, DG_GL_OFF_CLIENT, 7);
    put(&mut r, DG_GL_OFF_DRAWABLE, 8);
    let p = &mut r[32..];
    put(p, DG_DESKTOP_OP, op);
    put(p, DG_DESKTOP_WIDTH, w);
    put(p, DG_DESKTOP_HEIGHT, h);
    r
}
fn run(s: &mut State, o: &Ops, op: u32) -> u32 {
    unsafe { dreamgpu_desktop_execute(s, o, record(op, 640, 480).as_ptr(), 640, 480) }
}
#[test]
fn seed_fill_readback_return_preserve_coherence_and_roll_back_failed_capture() {
    let mut s = State::default();
    let mut c = Check {
        capture_error: DG_GL_ERROR_HOST,
        ..Check::default()
    };
    let o = ops(&mut c, &s);
    assert_eq!(run(&mut s, &o, DG_DESKTOP_SEED), DG_GL_ERROR_HOST);
    assert_eq!((s.epoch, s.sequence, s.active), (1, 0, 0));
    c.capture_error = 0;
    assert_eq!(run(&mut s, &o, DG_DESKTOP_SEED), 0);
    assert_eq!((s.epoch, s.sequence, s.active, s.coherent), (2, 1, 1, 1));
    assert_eq!(run(&mut s, &o, DG_DESKTOP_FILL), 0);
    assert_eq!((s.sequence, s.coherent), (2, 0));
    assert_eq!(run(&mut s, &o, DG_DESKTOP_RETURN), DG_GL_ERROR_DESKTOP);
    assert_eq!(s.sequence, 2);
    assert_eq!(c.captures.len(), 2);
    assert_eq!(run(&mut s, &o, DG_DESKTOP_READBACK), 0);
    assert_eq!((s.sequence, s.coherent), (3, 1));
    s.exclusive = 1;
    assert_eq!(run(&mut s, &o, DG_DESKTOP_RETURN), 0);
    assert_eq!(
        (s.sequence, s.active, s.coherent, s.exclusive),
        (4, 0, 0, 0)
    );
    assert_eq!(c.captures.last(), Some(&(DG_TRANSPORT_CPU_RETURN, 2, 4)));
}
#[test]
fn detached_discard_keeps_desktop_epoch_and_only_transport_failure_marks_failed() {
    let mut s = State::default();
    let mut c = Check {
        retained: 1,
        ..Check::default()
    };
    let o = ops(&mut c, &s);
    assert_eq!(run(&mut s, &o, DG_DESKTOP_DISCARD), 0);
    assert_eq!((s.epoch, s.sequence, s.active, s.coherent), (0, 0, 0, 0));
    let p = c.packets.last().unwrap();
    assert_eq!(qword(p, DG_TRANSPORT_OFF_EPOCH), 0);
    assert_eq!(qword(p, DG_TRANSPORT_OFF_GENERATION), 0);
    assert_eq!(word(p, DG_TRANSPORT_OFF_CLIENT), 7);
    c.retained = 0;
    assert_eq!(run(&mut s, &o, DG_DESKTOP_DISCARD), DG_GL_ERROR_DESKTOP);
    assert_eq!(c.failed, 0);
    c.retained = 1;
    c.queue_error = DG_GL_ERROR_GENERATION;
    assert_eq!(run(&mut s, &o, DG_DESKTOP_DISCARD), DG_GL_ERROR_GENERATION);
    assert_eq!(c.failed, 0);
    c.queue_error = DG_GL_ERROR_TRANSPORT;
    assert_eq!(run(&mut s, &o, DG_DESKTOP_DISCARD), DG_GL_ERROR_TRANSPORT);
    assert_eq!(c.failed, 1);
}
#[test]
fn retained_slot_requires_matching_identity_pending_or_published_and_source_bounds() {
    let mut s = Slot {
        image: core::ptr::null_mut(),
        state: PENDING,
        sending: 0,
        release_pending: 0,
        generation: 22,
        packet: [0; 128],
    };
    put(&mut s.packet, DG_TRANSPORT_OFF_CLIENT, 7);
    put(&mut s.packet, DG_TRANSPORT_OFF_DRAWABLE, 8);
    putq(&mut s.packet, DG_TRANSPORT_OFF_EPOCH, 11);
    put(
        &mut s.packet,
        DG_TRANSPORT_OFF_FLAGS,
        DG_TRANSPORT_FLAG_RETAIN_FOR_DESKTOP,
    );
    put(&mut s.packet, DG_TRANSPORT_OFF_WIDTH, 640);
    put(&mut s.packet, DG_TRANSPORT_OFF_HEIGHT, 480);
    let mut r = record(DG_DESKTOP_BLIT, 640, 480);
    putq(&mut r[32..], DG_DESKTOP_IMAGE_EPOCH, 11);
    putq(&mut r[32..], DG_DESKTOP_IMAGE_FRAME, 22);
    assert_eq!(unsafe { dreamgpu_desktop_retained(&s, r.as_ptr()) }, 1);
    for state in [crate::publication::FREE, crate::publication::RENDERING] {
        s.state = state;
        assert_eq!(unsafe { dreamgpu_desktop_retained(&s, r.as_ptr()) }, 0);
    }
    s.state = PUBLISHED;
    put(&mut r[32..], DG_DESKTOP_SRC_X, 1);
    assert_eq!(unsafe { dreamgpu_desktop_retained(&s, r.as_ptr()) }, 0);
    put(&mut r[32..], DG_DESKTOP_OP, DG_DESKTOP_DISCARD);
    assert_eq!(unsafe { dreamgpu_desktop_retained(&s, r.as_ptr()) }, 1);
    putq(&mut r[32..], DG_DESKTOP_IMAGE_FRAME, 21);
    assert_eq!(unsafe { dreamgpu_desktop_retained(&s, r.as_ptr()) }, 0);
}
#[test]
fn stale_primary_and_counter_exhaustion_fail_without_sequence_reuse() {
    let mut s = State {
        active: 1,
        coherent: 1,
        width: 640,
        height: 480,
        epoch: 1,
        sequence: 9,
        ..State::default()
    };
    let mut c = Check::default();
    let o = ops(&mut c, &s);
    assert_eq!(
        unsafe {
            dreamgpu_desktop_execute(
                &mut s,
                &o,
                record(DG_DESKTOP_FILL, 640, 480).as_ptr(),
                800,
                600,
            )
        },
        DG_GL_ERROR_DESKTOP
    );
    assert_eq!(s.sequence, 9);
    s.sequence = u64::MAX;
    assert_eq!(run(&mut s, &o, DG_DESKTOP_FILL), DG_GL_ERROR_LIMIT);
    assert_eq!(s.sequence, u64::MAX);
    s.active = 0;
    s.epoch = u64::MAX;
    assert_eq!(run(&mut s, &o, DG_DESKTOP_SEED), DG_GL_ERROR_LIMIT);
    assert_eq!(s.epoch, u64::MAX);
    assert!(c.packets.is_empty());
}
