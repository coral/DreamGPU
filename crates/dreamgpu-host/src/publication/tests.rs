use super::*;
fn drawable() -> Drawable {
    Drawable {
        client: 4,
        id: 5,
        width: 640,
        height: 480,
        epoch: 9,
        generation: 0,
        published_epoch: 0,
        published_generation: 0,
        native: ptr::null_mut(),
    }
}
fn metadata() -> ImageMetadata {
    ImageMetadata {
        stride: 2560,
        offset: 16,
        modifier: 0x0102030405060708,
        ready_fence: 1,
        uuid: [0xab; 16],
    }
}
#[test]
fn three_slot_credits_wait_for_matching_release_including_release_during_send() {
    let mut slots: [Slot; COUNT] = core::array::from_fn(|_| Slot::EMPTY);
    let mut d = drawable();
    let m = metadata();
    for i in 0..3 {
        assert_eq!(unsafe { dreamgpu_slot_claim(slots.as_mut_ptr(), 0) }, i);
        unsafe {
            assert_eq!(
                dreamgpu_slot_prepare(&mut slots[i as usize], &mut d, i, 0, &m),
                0
            );
            dreamgpu_slot_pending(&mut slots[i as usize]);
            assert_eq!(dreamgpu_slot_publish(&mut slots[i as usize], 1, 0), 1);
        }
    }
    assert_eq!(
        unsafe { dreamgpu_slot_claim(slots.as_mut_ptr(), 0) },
        u32::MAX
    );
    unsafe { dreamgpu_slot_release(&mut slots[0], 9, 1) };
    assert_eq!((slots[0].state, slots[0].release_pending), (PUBLISHED, 1));
    assert_eq!(
        unsafe { dreamgpu_slot_claim(slots.as_mut_ptr(), 0) },
        u32::MAX
    );
    assert_eq!(
        unsafe { dreamgpu_slot_sent(&mut slots[0], &mut d, 1, 1, 1) },
        0
    );
    assert_eq!(
        (slots[0].state, d.published_epoch, d.published_generation),
        (FREE, 9, 1)
    );
    assert_eq!(unsafe { dreamgpu_slot_claim(slots.as_mut_ptr(), 0) }, 0);
    assert_eq!(slots[0].release_pending, 0);
    for (i, slot) in slots.iter_mut().enumerate().take(3).skip(1) {
        unsafe {
            assert_eq!(dreamgpu_slot_sent(slot, &mut d, 1, 1, 1), 0);
            dreamgpu_slot_release(slot, 8, (i + 1) as u64);
            dreamgpu_slot_release(slot, 9, 99);
        };
        assert_eq!(slot.state, PUBLISHED);
        unsafe { dreamgpu_slot_release(slot, 9, (i + 1) as u64) };
        assert_eq!(slot.state, FREE);
    }
}
#[test]
fn cancellation_and_failed_send_free_credit_but_do_not_publish_false_cutoff() {
    for (ready, unavailable, sent, expected_failure) in
        [(1, 1, 0, 0), (0, 0, 0, 1), (1, 0, 0, 1), (1, 0, 1, 0)]
    {
        let mut s = Slot::EMPTY;
        s.state = RENDERING;
        let mut d = drawable();
        unsafe {
            assert_eq!(dreamgpu_slot_prepare(&mut s, &mut d, 0, 0, &metadata()), 0);
            dreamgpu_slot_pending(&mut s)
        };
        let publish = unsafe { dreamgpu_slot_publish(&mut s, ready, unavailable) };
        assert_eq!(
            unsafe { dreamgpu_slot_sent(&mut s, &mut d, sent, publish, ready) },
            expected_failure
        );
        assert_eq!(d.published_generation, u64::from(sent));
        assert_eq!(s.state, if sent != 0 { PUBLISHED } else { FREE });
    }
}
#[test]
fn frame_packet_matches_fixed_wire_layout_and_generation_never_wraps() {
    let mut s = Slot::EMPTY;
    s.state = RENDERING;
    let mut d = drawable();
    let m = metadata();
    assert_eq!(
        unsafe { dreamgpu_slot_prepare(&mut s, &mut d, 2, DG_GL_PRESENT_RETAIN, &m) },
        0
    );
    let mut expected = [0u8; 128];
    for (off, value) in [
        (0, 0x4a475055u32),
        (4, 2),
        (8, 5),
        (12, 128),
        (16, 7),
        (20, 640),
        (24, 480),
        (28, 2560),
        (32, 0x34325241),
        (36, 16),
        (40, 2),
        (44, 4),
        (48, 5),
    ] {
        expected[off..off + 4].copy_from_slice(&value.to_le_bytes());
    }
    expected[56] = 9;
    expected[64] = 1;
    expected[72..80].copy_from_slice(&[8, 7, 6, 5, 4, 3, 2, 1]);
    expected[80..96].fill(0xab);
    assert_eq!(s.packet, expected);
    d.generation = u64::MAX;
    let old = s.packet;
    assert_eq!(
        unsafe { dreamgpu_slot_prepare(&mut s, &mut d, 2, 0, &m) },
        DG_GL_ERROR_LIMIT
    );
    assert_eq!(s.packet, old);
    assert_eq!(d.generation, u64::MAX);
    assert_eq!(
        core::mem::offset_of!(Slot, generation),
        if core::mem::size_of::<usize>() == 8 {
            24
        } else {
            16
        }
    );
    assert_eq!(
        core::mem::size_of::<Slot>(),
        if core::mem::size_of::<usize>() == 8 {
            160
        } else {
            152
        }
    );
}
