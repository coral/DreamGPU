// SPDX-License-Identifier: GPL-2.0-or-later
use super::*;
use std::alloc::{alloc, dealloc, Layout};
#[test]
fn output_fifo_wrap_and_inflight_backpressure() {
    unsafe {
        let mut q: OutputQueue = core::mem::zeroed();
        let mut p = [0; 128];
        for i in 0..64 {
            p[0] = i;
            assert_eq!(dreamgpu_output_push(&mut q, ptr::null_mut(), p.as_ptr()), 1);
        }
        assert_eq!(dreamgpu_output_push(&mut q, ptr::null_mut(), p.as_ptr()), 0);
        let mut out = Output::EMPTY;
        assert_eq!(dreamgpu_output_pop(&mut q, &mut out), 1);
        assert_eq!(out.packet[0], 0);
        assert_eq!((q.len, q.pending, q.desktop), (63, 64, 64));
        assert_eq!(dreamgpu_output_push(&mut q, ptr::null_mut(), p.as_ptr()), 0);
        dreamgpu_output_done(&mut q, 1);
        let mut slots: [Slot; 96] = core::mem::zeroed();
        for s in &mut slots {
            assert_eq!(dreamgpu_output_push(&mut q, s, ptr::null()), 1);
        }
        assert_eq!(dreamgpu_output_push(&mut q, ptr::null_mut(), p.as_ptr()), 1);
        assert_eq!(
            dreamgpu_output_push(&mut q, slots.as_mut_ptr(), ptr::null()),
            0
        );
        for i in 1..64 {
            assert_eq!(dreamgpu_output_pop(&mut q, &mut out), 1);
            assert_eq!(out.packet[0], i);
            dreamgpu_output_done(&mut q, 1);
        }
        for s in &mut slots {
            assert_eq!(dreamgpu_output_pop(&mut q, &mut out), 1);
            assert_eq!(out.slot, s as *mut Slot);
            dreamgpu_output_done(&mut q, 0);
        }
        assert_eq!(dreamgpu_output_pop(&mut q, &mut out), 1);
        dreamgpu_output_done(&mut q, 1);
        for _ in 0..500 {
            assert_eq!(dreamgpu_output_push(&mut q, ptr::null_mut(), p.as_ptr()), 1);
            assert_eq!(dreamgpu_output_pop(&mut q, &mut out), 1);
            dreamgpu_output_done(&mut q, 1);
        }
        assert_eq!((q.len, q.pending, q.desktop), (0, 0, 0));
        assert_eq!(dreamgpu_output_pop(&mut q, &mut out), 0);
    }
}
struct Allocation {
    made: u32,
    freed: u32,
    fail: bool,
    slot: *const CpuSlot,
}
unsafe extern "C" fn allocate(o: *mut c_void, n: usize, fd: *mut i32) -> *mut u8 {
    unsafe {
        let a = &mut *o.cast::<Allocation>();
        if a.fail {
            return ptr::null_mut();
        }
        a.made += 1;
        *fd = 77;
        let p = alloc(Layout::from_size_align(n, 8).unwrap());
        p.write_bytes(0xa5, n);
        p
    }
}
unsafe extern "C" fn free(o: *mut c_void, p: *mut u8, n: usize, fd: i32) {
    unsafe {
        let a = &mut *o.cast::<Allocation>();
        assert_eq!(fd, 77);
        assert!((*a.slot).pixels.is_null());
        assert_eq!((*a.slot).fd, -1);
        a.freed += 1;
        dealloc(p, Layout::from_size_align(n, 8).unwrap());
    }
}
#[test]
fn cpu_lease_generation_mapping_resize_and_failed_allocation() {
    unsafe {
        let mut slots: [CpuSlot; 8] = core::mem::zeroed();
        for s in &mut slots {
            s.fd = -1;
        }
        for i in 0..8 {
            assert_eq!(dreamgpu_cpu_claim(slots.as_mut_ptr(), 10, i as u64), i);
        }
        assert_eq!(dreamgpu_cpu_claim(slots.as_mut_ptr(), 11, 0), u32::MAX);
        dreamgpu_cpu_release(&mut slots[0], 9, 0);
        assert_eq!(slots[0].published, 1);
        dreamgpu_cpu_release(&mut slots[0], 10, 1);
        assert_eq!(slots[0].published, 1);
        dreamgpu_cpu_release(&mut slots[0], 10, 0);
        assert_eq!(dreamgpu_cpu_claim(slots.as_mut_ptr(), 11, 4), 0);
        let s = &mut slots[0];
        let mut a = Allocation {
            made: 0,
            freed: 0,
            fail: false,
            slot: s,
        };
        let m = CpuMemory {
            opaque: (&mut a as *mut Allocation).cast(),
            allocate,
            free,
        };
        assert_eq!(dreamgpu_cpu_storage(s, &m, 4, 4), 0);
        assert_eq!((s.bytes, s.stride), (65536, 256));
        assert!(core::slice::from_raw_parts(s.pixels, s.bytes)
            .iter()
            .all(|&b| b == 0));
        *s.pixels = 42;
        assert_eq!(dreamgpu_cpu_storage(s, &m, 4, 4), 0);
        assert_eq!(*s.pixels, 42);
        assert_eq!(dreamgpu_cpu_storage(s, &m, 8, 4), 0);
        assert_eq!(*s.pixels, 0);
        assert_eq!(a.made, 1);
        assert_eq!(dreamgpu_cpu_storage(s, &m, 512, 512), 0);
        assert_eq!((a.made, a.freed), (2, 1));
        let old = s.pixels;
        assert_eq!(
            dreamgpu_cpu_storage(s, &m, u32::MAX, u32::MAX),
            DG_GL_ERROR_LIMIT
        );
        assert_eq!(s.pixels, old);
        a.fail = true;
        assert_eq!(dreamgpu_cpu_storage(s, &m, 1024, 512), DG_GL_ERROR_HOST);
        assert!(s.pixels.is_null());
        assert_eq!(a.freed, 2);
        dreamgpu_cpu_storage_free(s, &m);
        assert_eq!(a.freed, 2);
    }
}
#[test]
fn cpu_packet_preserves_wire_and_return_identity() {
    unsafe {
        let s = CpuSlot {
            pixels: ptr::null_mut(),
            bytes: 65536,
            fd: 1,
            width: 7,
            height: 9,
            stride: 256,
            published: 1,
            epoch: 0x100000001,
            sequence: 0x200000002,
        };
        let mut r = [0; DG_DESKTOP_BYTES as usize];
        put(&mut r, DG_DESKTOP_DST_X, 8);
        put(&mut r, DG_DESKTOP_DST_Y, 9);
        let mut p = [0; 128];
        dreamgpu_cpu_packet(
            &s,
            3,
            DG_TRANSPORT_CPU_RETURN,
            r.as_ptr(),
            44,
            55,
            p.as_mut_ptr(),
        );
        assert_eq!(get(&p, DG_TRANSPORT_OFF_MAGIC), DG_TRANSPORT_MAGIC);
        assert_eq!(
            get(&p, DG_TRANSPORT_OFF_SLOT),
            DG_TRANSPORT_CPU_SLOT_BASE + 3
        );
        assert_eq!(getq(&p, DG_TRANSPORT_OFF_EPOCH), s.epoch);
        assert_eq!(getq(&p, DG_TRANSPORT_OFF_GENERATION), s.sequence);
        assert_eq!(getq(&p, DG_TRANSPORT_CPU_OFF_LEGACY_EPOCH), 44);
        assert_eq!(getq(&p, DG_TRANSPORT_CPU_OFF_LEGACY_FRAME), 55);
        assert_eq!(get(&p, DG_TRANSPORT_CPU_OFF_DST_X), 8);
        assert_eq!(get(&p, DG_TRANSPORT_OFF_STRIDE), 256);
        dreamgpu_cpu_packet(
            &s,
            3,
            DG_TRANSPORT_CPU_SEED,
            r.as_ptr(),
            44,
            55,
            p.as_mut_ptr(),
        );
        assert_eq!(getq(&p, DG_TRANSPORT_CPU_OFF_LEGACY_FRAME), 0);
    }
}
fn packet(epoch: u64, seq: u64) -> [u8; 128] {
    let mut p = [0; 128];
    putq(&mut p, DG_TRANSPORT_OFF_EPOCH, epoch);
    putq(&mut p, DG_TRANSPORT_OFF_GENERATION, seq);
    putq(&mut p, DG_TRANSPORT_DESKTOP_OFF_TOKEN, seq);
    put(&mut p, DG_TRANSPORT_OFF_WIDTH, 4);
    put(&mut p, DG_TRANSPORT_OFF_HEIGHT, 8);
    put(&mut p, DG_TRANSPORT_OFF_STRIDE, 16);
    putq(&mut p, DG_TRANSPORT_CPU_OFF_ALLOCATION, 128);
    p
}
#[test]
fn reply_exact_token_transfer_cancel_duplicate_and_layout_bounds() {
    unsafe {
        let mut s: Reply = core::mem::zeroed();
        s.fd = -1;
        let mut p = packet(8, 10);
        dreamgpu_reply_begin(&mut s, 8, 10, p.as_mut_ptr());
        let stale = packet(7, 10);
        assert_eq!(dreamgpu_reply_receive(&mut s, stale.as_ptr(), 41), 0);
        assert_eq!(s.fd, -1);
        assert_eq!(dreamgpu_reply_receive(&mut s, p.as_ptr(), 42), 2);
        assert_eq!(dreamgpu_reply_receive(&mut s, p.as_ptr(), 43), 0);
        let mut r: ReplyResult = core::mem::zeroed();
        dreamgpu_reply_finish(&mut s, 0, 0, 0, &mut r);
        assert_eq!((s.fd, s.pending, s.ready, r.fd, r.error), (-1, 0, 0, 42, 0));
        let (mut bytes, mut stride) = (0, 0);
        assert_eq!(
            dreamgpu_reply_layout(&r, 4, 8, 1, 128, &mut bytes, &mut stride),
            0
        );
        assert_eq!((bytes, stride), (128, 16));
        for (stat, n) in [(0, 128), (1, 127), (1, -1)] {
            assert_eq!(
                dreamgpu_reply_layout(&r, 4, 8, stat, n, &mut bytes, &mut stride),
                DG_GL_ERROR_DESKTOP
            );
        }
        for (off, value) in [
            (16, 1),
            (DG_TRANSPORT_OFF_WIDTH, 5),
            (DG_TRANSPORT_OFF_HEIGHT, 9),
            (DG_TRANSPORT_OFF_STRIDE, u32::MAX),
        ] {
            let old = get(&r.packet, off);
            put(&mut r.packet, off, value);
            assert_eq!(
                dreamgpu_reply_layout(&r, 4, 8, 1, 128, &mut bytes, &mut stride),
                DG_GL_ERROR_DESKTOP
            );
            put(&mut r.packet, off, old);
        }
        assert_eq!(dreamgpu_reply_receive(&mut s, p.as_ptr(), 44), 1);
        dreamgpu_reply_begin(&mut s, 8, 11, p.as_mut_ptr());
        assert_eq!(
            dreamgpu_reply_receive(&mut s, packet(8, 10).as_ptr(), 44),
            0
        );
        dreamgpu_reply_finish(&mut s, 0, 1, 0, &mut r);
        assert_eq!(r.error, DG_GL_ERROR_GENERATION);
        assert_eq!(
            dreamgpu_reply_receive(&mut s, packet(8, 11).as_ptr(), 45),
            1
        );
        dreamgpu_reply_begin(&mut s, 8, 12, p.as_mut_ptr());
        dreamgpu_reply_finish(&mut s, 0, 0, 0, &mut r);
        assert_eq!(r.error, DG_GL_ERROR_TRANSPORT);
    }
}
