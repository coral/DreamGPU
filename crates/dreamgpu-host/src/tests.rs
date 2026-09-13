use super::*;

fn packet(words: [u32; 10]) -> Vec<u8> {
    words.into_iter().flat_map(u32::to_le_bytes).collect()
}

// Model QEMU's memory primitive only on private test RAM. Production Rust never
// borrows guest RAM because emulated DMA can race guest CPU memory accesses.
fn execute(
    commands: &[u8],
    vram: &mut [u8],
    progress: &mut Progress,
    budget: u32,
    mut dirty: impl FnMut(u64, u32),
) -> Result<Work, u32> {
    super::execute(
        commands,
        vram.len() as u64,
        progress,
        budget,
        |op, bpp, src, dst, n, color| {
            let dst = dst as usize;
            match op {
                1 => {
                    let color = color.to_le_bytes();
                    for p in vram[dst..dst + n as usize].chunks_exact_mut(bpp as usize) {
                        p.copy_from_slice(&color[..bpp as usize]);
                    }
                }
                2 => vram.copy_within(src as usize..src as usize + n as usize, dst),
                3 => {}
                _ => panic!("unknown transfer"),
            }
            dirty(dst as u64, n);
        },
    )
}

#[test]
fn whole_batch_rejects_before_mutation() {
    let mut p = packet([1, 4, 0, 0, 0, 16, 4, 4, 0xff123456, 0]);
    p.extend(packet([1, 4, 0, 64, 0, 16, 4, 4, 0, 0]));
    let mut vram = [0xa5; 64];
    let mut progress = Progress::default();
    assert_eq!(
        execute(&p, &mut vram, &mut progress, 4096, |_, _| panic!()).unwrap_err(),
        BOUNDS
    );
    assert_eq!(vram, [0xa5; 64]);
    assert_eq!(progress, Progress::default());
}

#[test]
fn rejects_bad_fields_overflow_and_work() {
    assert_eq!(validate(&[], 64), Err(COUNT));
    assert_eq!(validate(&[0; 41], 64), Err(COUNT));
    assert_eq!(validate(&[0; COMMAND_BYTES * 65], 64), Err(COUNT));
    for bad in [
        [0, 4, 0, 0, 0, 16, 4, 4, 0, 0],
        [1, 3, 0, 0, 0, 16, 4, 4, 0, 0],
        [1, 4, 1, 0, 0, 16, 4, 4, 0, 0],
        [3, 4, 0, 0, 0, 16, 4, 4, 1, 0],
        [1, 4, 0, 0, 0, 16, 4, 4, 0, 1],
    ] {
        assert_eq!(validate(&packet(bad), 64), Err(COMMAND));
    }
    for bad in [
        [1, 4, 0, 0, 0, 16, 0, 4, 0, 0],
        [1, 4, 0, 0, 0, 16, 4, 0, 0, 0],
        [1, 4, 0, 1, 0, 16, 4, 4, 0, 0],
        [1, 4, 0, 0, 0, 15, 4, 4, 0, 0],
        [1, 4, 0, u32::MAX - 3, 0, 16, 4, 4, 0, 0],
        [2, 4, 0, 0, 12, 16, 3, 4, 0, 0],
    ] {
        assert_eq!(validate(&packet(bad), 64), Err(BOUNDS));
    }
    assert_eq!(
        validate(
            &packet([1, 4, 0, u32::MAX - 3, 0, u32::MAX, u32::MAX, u32::MAX, 0, 0]),
            u64::MAX
        ),
        Err(BOUNDS)
    );
    assert_eq!(
        validate(
            &packet([1, 4, 0, 0, 0, u32::MAX - 3, u32::MAX, 1, 0, 0]),
            u64::MAX
        ),
        Err(BOUNDS)
    );
    assert_eq!(
        validate(&packet([1, 4, 0, 0, 0, 32768, 8192, 4096, 0, 0]), 1 << 30),
        Err(WORK_LIMIT)
    );
}

#[test]
fn fill_formats_padding_dirty_and_resume() {
    for bpp in [1, 2, 4] {
        let p = packet([1, bpp, 0, bpp, 0, 8 * bpp, 5, 3, 0xa1b2c3d4, 0]);
        let mut vram = vec![0x55; 32 * bpp as usize];
        let mut progress = Progress::default();
        let mut spans = Vec::new();
        let mut total = 0;
        loop {
            let w = execute(&p, &mut vram, &mut progress, 3 * bpp + 1, |o, n| {
                spans.push((o, n))
            })
            .unwrap();
            assert!(w.bytes <= u64::from(3 * bpp + 1));
            total += w.bytes;
            if w.complete != 0 {
                break;
            }
        }
        assert_eq!(total, u64::from(15 * bpp));
        let color = 0xa1b2c3d4u32.to_le_bytes();
        for y in 0..3usize {
            for x in 0..8usize {
                let pixel = &vram[(y * 8 + x) * bpp as usize..(y * 8 + x + 1) * bpp as usize];
                if (1..6).contains(&x) {
                    assert_eq!(pixel, &color[..bpp as usize]);
                } else {
                    assert!(pixel.iter().all(|&v| v == 0x55));
                }
            }
        }
        assert_eq!(spans.iter().map(|s| u64::from(s.1)).sum::<u64>(), total);
    }
}

#[test]
fn overlapping_copy_matches_immutable_source_both_directions() {
    for bpp in [1u32, 2, 4] {
        for (src, dst) in [(0, 1), (1, 0), (0, 8), (8, 0), (0, 9), (9, 0), (0, 0)] {
            for budget in [bpp, 3 * bpp, 1024] {
                let p = packet([2, bpp, src * bpp, dst * bpp, 8 * bpp, 8 * bpp, 6, 4, 0, 0]);
                let original: Vec<u8> = (0..64 * bpp).map(|x| x as u8).collect();
                let mut expected = original.clone();
                for row in 0..4u32 {
                    let s = ((src + row * 8) * bpp) as usize;
                    let d = ((dst + row * 8) * bpp) as usize;
                    expected[d..d + 6 * bpp as usize]
                        .copy_from_slice(&original[s..s + 6 * bpp as usize]);
                }
                let mut actual = original;
                let mut progress = Progress::default();
                while execute(&p, &mut actual, &mut progress, budget, |_, _| {})
                    .unwrap()
                    .complete
                    == 0
                {}
                assert_eq!(
                    actual, expected,
                    "src={src} dst={dst} budget={budget} bpp={bpp}"
                );
            }
        }
    }
}

#[test]
fn damage_row_bound_and_invalid_resume() {
    let p = packet([3, 1, 0, 0, 0, 1, 1, 3000, 0, 0]);
    let mut vram = vec![0xa5; 3000];
    let mut progress = Progress::default();
    let first = execute(&p, &mut vram, &mut progress, 4000, |_, _| {}).unwrap();
    assert_eq!((first.chunks, first.bytes, first.complete), (2048, 2048, 0));
    assert_eq!(progress.row, 2048);
    let second = execute(&p, &mut vram, &mut progress, 4000, |_, _| {}).unwrap();
    assert_eq!((second.bytes, second.complete), (952, 1));
    assert!(vram.iter().all(|&v| v == 0xa5));
    for mut bad in [
        Progress {
            command: 2,
            row: 0,
            column: 0,
        },
        Progress {
            command: 0,
            row: 3000,
            column: 0,
        },
        Progress {
            command: 0,
            row: 0,
            column: 1,
        },
        Progress {
            command: 1,
            row: 1,
            column: 0,
        },
    ] {
        assert_eq!(
            execute(&p, &mut vram, &mut bad, 4000, |_, _| panic!()).unwrap_err(),
            BOUNDS
        );
    }
}

#[test]
fn c_abi_nulls_and_layout() {
    assert_eq!(core::mem::size_of::<Progress>(), 12);
    assert_eq!(core::mem::size_of::<Work>(), 16);
    let mut work = 99;
    unsafe {
        assert_eq!(
            dreamgpu_2d_validate(core::ptr::null(), 1, 64, &mut work),
            BOUNDS
        );
        assert_eq!(work, 0);
        assert_eq!(
            dreamgpu_2d_validate(core::ptr::null(), 65, 64, &mut work),
            COUNT
        );
    }
}
