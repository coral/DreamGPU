//! Actual native raster transform/clipping and associated current-state oracle.
use super::textures::{call, query, query_capacity, words};
use super::*;

const RASTER: u32 = 1941;
const POSITION: u32 = 0x0b07;
const VALID: u32 = 0x0b08;
fn submit(q: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    q.batch(*sequence, records);
    q.await_completion(*sequence);
}
fn raster(values: [f64; 4]) -> (u32, Vec<u32>) {
    let args: Vec<u32> = values
        .into_iter()
        .flat_map(|v| {
            let b = v.to_bits();
            [b as u32, (b >> 32) as u32]
        })
        .collect();
    call(RASTER, &args)
}
fn floats(q: &mut Qemu, seq: &mut u32, pname: u32) -> Vec<f32> {
    words(&query(q, seq, 0x305, &[pname]).1)
        .into_iter()
        .map(f32::from_bits)
        .collect()
}
#[test]
#[ignore = "requires native host GPU; diskless raster-state acceptance"]
fn qemu_raster_position_transform_clip_and_current_state() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-raster-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&dir).unwrap();
    let socket = dir.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut q = Qemu::start(&dir, &socket);
    let mut seq = 0;
    q.write(0x1130, RASTER);
    assert_eq!(q.read(0x1134), 8);
    submit(
        &mut q,
        &mut seq,
        &[(1, vec![0]), (3, vec![64, 32]), (5, vec![])],
    );
    assert_eq!(floats(&mut q, &mut seq, POSITION), [0., 0., 0., 1.]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x329, &[VALID]).1), [1]);
    // Defaults supplied by RasterPos2 adapters: z=0, w=1. Nonzero viewport
    // verifies actual window transform rather than merely stored input values.
    submit(
        &mut q,
        &mut seq,
        &[
            call(2871, &[3, 5, 40, 20]), // glViewport
            call(
                0x0db,
                &[
                    0.25f32.to_bits(),
                    0.5f32.to_bits(),
                    0.75f32.to_bits(),
                    1f32.to_bits(),
                ],
            ),
            call(
                2217,
                &[0.5f32.to_bits(), 0.25f32.to_bits(), 0, 1f32.to_bits()],
            ),
            raster([0., 0., 0., 1.]),
        ],
    );
    assert_eq!(floats(&mut q, &mut seq, POSITION), [23., 15., 0.5, 1.]);
    assert_eq!(floats(&mut q, &mut seq, 0x0b04), [0.25, 0.5, 0.75, 1.]);
    assert_eq!(floats(&mut q, &mut seq, 0x0b06), [0.5, 0.25, 0., 1.]);
    assert_eq!(floats(&mut q, &mut seq, 0x0b09), [0.]);
    assert_eq!(floats(&mut q, &mut seq, 0x0b05), [1.]);
    // Model-view translation and projection scaling are both applied.
    submit(
        &mut q,
        &mut seq,
        &[
            call(1353, &[0x1700]),
            call(1280, &[]),
            call(2364, &[0.5f32.to_bits(), 0, 0]),
            call(1353, &[0x1701]),
            call(1280, &[]),
            call(2030, &[0.5f32.to_bits(), 1f32.to_bits(), 1f32.to_bits()]), // glScalef
            raster([0., 0., 0., 1.]),
        ],
    );
    assert_eq!(floats(&mut q, &mut seq, POSITION), [28., 15., 0.5, 1.]);
    // GL1.1 sections 2.12/3.9 permit abs(eye.z) instead of Euclidean
    // distance. Test that value on the eye axis below, where both agree.
    // Attribute stack must preserve all native raster values and validity.
    submit(
        &mut q,
        &mut seq,
        &[call(1909, &[1]), raster([8., 0., 0., 1.])],
    );
    assert_eq!(words(&query(&mut q, &mut seq, 0x329, &[VALID]).1), [0]);
    // Clipped associated values are indeterminate: only test the validity bit.
    submit(&mut q, &mut seq, &[call(1711, &[])]);
    assert_eq!(floats(&mut q, &mut seq, POSITION), [28., 15., 0.5, 1.]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x329, &[VALID]).1), [1]);
    submit(
        &mut q,
        &mut seq,
        &[
            call(1353, &[0x1700]),
            call(1280, &[]),
            call(1353, &[0x1701]),
            call(1280, &[]),
            raster([0., 0., 0.5, 1.]),
        ],
    );
    assert_eq!(floats(&mut q, &mut seq, 0x0b09), [0.5]);
    submit(&mut q, &mut seq, &[raster([1., 0., 0., 2.])]);
    assert_eq!(floats(&mut q, &mut seq, POSITION), [33., 15., 0.5, 2.]);
    // Typed output dimensions and exact readback guard bytes, including bool.
    for (pname, count) in [
        (POSITION, 4),
        (0x0b04, 4),
        (0x0b06, 4),
        (0x0b05, 1),
        (VALID, 1),
        (0x0b09, 1),
    ] {
        for (function, size) in [(0x2cb, 1), (0x329, 4), (0x305, 4), (0x2fb, 8)] {
            assert_eq!(
                query_capacity(&mut q, &mut seq, function, &[pname], count * size)
                    .1
                    .len(),
                (count * size) as usize
            );
        }
    }
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    // An undersized immutable scalar packet rejects before native execution.
    seq += 1;
    q.batch(seq, &[call(RASTER, &[0; 7])]);
    let deadline = Instant::now() + Duration::from_secs(5);
    while q.read(0x111c) & 4 == 0 {
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(1));
    }
    assert_eq!(q.read(0x1124), 1);
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}
