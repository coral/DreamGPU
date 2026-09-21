//! Native depth/stencil contents across CPU edits, upload and partial clears.
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;

fn submit(q: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    q.batch(*sequence, records);
    q.await_completion(*sequence);
}
fn double(value: f64) -> [u32; 2] {
    let bits = value.to_bits();
    [bits as u32, (bits >> 32) as u32]
}
fn depth(q: &mut Qemu, sequence: &mut u32) -> Vec<f32> {
    words(&query_capacity(q, sequence, 0x7a4, &[0x10000, 0, 8 | (8 << 16)], 256).1)
        .into_iter()
        .map(f32::from_bits)
        .collect()
}
fn stencil(q: &mut Qemu, sequence: &mut u32) -> Vec<u32> {
    words(&query_capacity(q, sequence, 0x7a4, &[0x20000, 0, 8 | (8 << 16)], 256).1)
}

#[test]
#[ignore = "requires native GPU; diskless depth/stencil lifecycle acceptance"]
fn qemu_depth_stencil_cpu_edit_upload_preserves_other_aspects_and_guard_pixels() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-ds-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&dir).unwrap();
    let socket = dir.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut q = Qemu::start(&dir, &socket);
    let mut sequence = 0;
    submit(
        &mut q,
        &mut sequence,
        &[
            (1, vec![0]),
            (3, vec![8, 8]),
            (5, vec![]),
            call(163, &[1f32.to_bits(), 0, 0, 1f32.to_bits()]),
            call(167, &double(0.75)),
            call(181, &[0x7b]),
            call(154, &[0x4500]),
        ],
    );
    assert!(
        depth(&mut q, &mut sequence)
            .iter()
            .all(|value| (*value - 0.75).abs() < 2.0 / 16_777_215.0)
    );
    assert_eq!(stencil(&mut q, &mut sequence), [0x7b; 64]);

    // Use the normalized unsigned-int upload used by Wine's CPU depth path.
    // Every row differs, including both exact range endpoints. Caller buffers
    // are overwritten after submission; completed GPU data must remain owned.
    let expected: Vec<u32> = (0..64)
        .map(|i| ((i as u64 * u32::MAX as u64) / 63) as u32)
        .collect();
    let mut bytes: Vec<u8> = expected
        .iter()
        .flat_map(|value| value.to_le_bytes())
        .collect();
    let raster: Vec<u32> = [-1.0, -1.0, 0.0, 1.0]
        .into_iter()
        .flat_map(double)
        .collect();
    submit(
        &mut q,
        &mut sequence,
        &[
            call(521, &[0x0b71]),
            call(441, &[0x0207]),
            call(442, &[1]),
            call(245, &[0, 0, 0, 0]),
            call(1941, &raster),
            data_call(498, &[8, 8, 0x1902, 0x1405, 256, 0, 3, 1], &bytes),
        ],
    );
    bytes.fill(0xcc);
    for (value, expected) in depth(&mut q, &mut sequence).iter().zip(&expected) {
        assert!((*value as f64 - *expected as f64 / u32::MAX as f64).abs() < 2.0 / 16_777_215.0);
    }
    assert_eq!(stencil(&mut q, &mut sequence), [0x7b; 64]);

    // A stencil upload must not overwrite depth even with depth testing enabled.
    let mut indices: Vec<u8> = (0..64).map(|i| (i * 3) as u8).collect();
    let before = depth(&mut q, &mut sequence);
    submit(
        &mut q,
        &mut sequence,
        &[
            call(2119, &[255]),
            data_call(498, &[8, 8, 0x1901, 0x1401, 64, 0, 3, 2], &indices),
        ],
    );
    indices.fill(0xdd);
    assert_eq!(depth(&mut q, &mut sequence), before);
    assert_eq!(
        stencil(&mut q, &mut sequence),
        (0..64).map(|i| i * 3).collect::<Vec<_>>()
    );

    // Clear only a depth rectangle, then only a stencil rectangle. The other
    // aspect and all untouched pixels must survive each operation exactly.
    submit(
        &mut q,
        &mut sequence,
        &[
            call(521, &[0x0c11]),
            call(2032, &[1, 2, 3, 2]),
            call(167, &double(0.25)),
            call(154, &[0x0100]),
        ],
    );
    let after = depth(&mut q, &mut sequence);
    for y in 0..8 {
        for x in 0..8 {
            let index = y * 8 + x;
            if (1..4).contains(&x) && (2..4).contains(&y) {
                assert!((after[index] - 0.25).abs() < 2.0 / 16_777_215.0);
            } else {
                assert_eq!(after[index], before[index]);
            }
        }
    }
    assert_eq!(
        stencil(&mut q, &mut sequence),
        (0..64).map(|i| i * 3).collect::<Vec<_>>()
    );
    submit(
        &mut q,
        &mut sequence,
        &[
            call(2032, &[5, 0, 2, 3]),
            call(181, &[0x11]),
            call(154, &[0x0400]),
        ],
    );
    assert_eq!(depth(&mut q, &mut sequence), after);
    let actual = stencil(&mut q, &mut sequence);
    for y in 0..8 {
        for x in 0..8 {
            let index = y * 8 + x;
            assert_eq!(
                actual[index],
                if (5..7).contains(&x) && y < 3 {
                    0x11
                } else {
                    (index * 3) as u32
                }
            );
        }
    }
    assert_eq!(
        query_capacity(&mut q, &mut sequence, 0x7a4, &[0, 0, 8 | (8 << 16)], 256).1,
        [255, 0, 0, 255].repeat(64)
    );
    assert_eq!(words(&query(&mut q, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}
