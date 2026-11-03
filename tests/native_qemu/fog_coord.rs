//! EXT_fog_coord through the device: immediate fog coordinates inside Begin/End,
//! the fog coordinate source, per-vertex fog from DrawArraysEXT slot 14 (FLOAT
//! and DOUBLE), restoration of the current fog coordinate and host queries.
use super::textures::{call, data_call, query, words};
use super::*;

const DRAW_EXT: u32 = 0x1d6;
const FOG_COORD: u32 = 595;
const FOG_F: u32 = 605;
const FOG_FV: u32 = 607;
const FOG: u32 = 0x0b60;
const SOURCE: u32 = 0x8450;
const COORDINATE: u32 = 0x8451;
const DEPTH: u32 = 0x8452;

fn submit(qemu: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    qemu.batch(*sequence, records);
    qemu.await_completion(*sequence);
}
fn f(v: f32) -> u32 {
    v.to_bits()
}
fn pixel(qemu: &mut Qemu, sequence: &mut u32, x: u32) -> [u8; 4] {
    let (_, result) = query(qemu, sequence, 0x7a4, &[x, 4, 1 | (1 << 16)]);
    result[..4].try_into().unwrap()
}
fn close(actual: [u8; 4], expected: [u8; 3], label: &str) {
    for c in 0..3 {
        let d = i32::from(actual[c]) - i32::from(expected[c]);
        assert!(d.abs() <= 3, "{label}: {actual:?} expected {expected:?}");
    }
}
/// White quad whose four vertices carry the given immediate fog coordinates.
fn quad(fog: [f32; 4]) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![
        call(0x0a3, &[0, 0, 0, f(1.)]),
        call(0x09a, &[0x4000]),
        call(0x0db, &[f(1.); 4]),
        call(0x01a, &[7]),
    ];
    for ((x, y), c) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)]
        .into_iter()
        .zip(fog)
    {
        out.push(call(FOG_COORD, &[f(c)]));
        out.push(call(0x9dc, &[f(x), f(y)]));
    }
    out.push(call(0x216, &[]));
    out
}
/// DrawArraysEXT packet: FLOAT2 positions and a fog coordinate per vertex.
fn arrays(fog: [f32; 4], double: bool) -> Vec<u8> {
    let mut bytes = vec![0u8; 64];
    bytes[..4].copy_from_slice(&(0x1406u32 | (2 << 16)).to_le_bytes());
    let kind = if double { 0x140a } else { 0x1406 };
    bytes[56..60].copy_from_slice(&(kind | (1u32 << 16)).to_le_bytes());
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        bytes.extend([x, y].iter().flat_map(|v| v.to_le_bytes()));
    }
    for c in fog {
        if double {
            bytes.extend(f64::from(c).to_le_bytes());
        } else {
            bytes.extend(c.to_le_bytes());
        }
    }
    bytes
}

#[test]
#[ignore = "requires native host GPU and fog-coordinate QEMU producer; diskless acceptance"]
fn qemu_fog_coordinates_immediate_arrays_source_and_restore() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-fc-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 0;
    assert_ne!(qemu.read(0x1008) & 0x80000, 0);
    // Linear fog from coordinate 0 (clear) to 1 (fully blue).
    submit(
        &mut qemu,
        &mut sequence,
        &[
            (1, vec![0]),
            (3, vec![8, 8]),
            (5, vec![]),
            call(0x209, &[FOG]),
            call(FOG_F, &[0x0b65, f(0x2601 as f32)]), // FOG_MODE LINEAR
            call(FOG_F, &[0x0b63, f(0.)]),            // FOG_START
            call(FOG_F, &[0x0b64, f(1.)]),            // FOG_END
            data_call(
                FOG_FV,
                &[0x0b66],
                &[0f32, 0., 1., 1.].map(f32::to_le_bytes).concat(),
            ),
            data_call(FOG_FV, &[SOURCE], &(COORDINATE as f32).to_le_bytes()),
        ],
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[SOURCE]).1),
        [COORDINATE]
    );

    // Immediate coordinates inside Begin/End: half fog, then full fog.
    submit(&mut qemu, &mut sequence, &quad([0.5; 4]));
    close(
        pixel(&mut qemu, &mut sequence, 4),
        [128, 128, 255],
        "half fog",
    );
    submit(&mut qemu, &mut sequence, &quad([1.; 4]));
    close(pixel(&mut qemu, &mut sequence, 4), [0, 0, 255], "full fog");
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x305, &[0x8453]).1),
        [f(1.)]
    );

    // Per-vertex coordinates from slot 14: clear on the left, fogged on the
    // right; FLOAT and DOUBLE agree. The current coordinate survives the draw.
    submit(&mut qemu, &mut sequence, &[call(FOG_COORD, &[f(0.25)])]);
    for double in [false, true] {
        submit(
            &mut qemu,
            &mut sequence,
            &[
                call(0x0a3, &[0, 0, 0, f(1.)]),
                call(0x09a, &[0x4000]),
                data_call(
                    DRAW_EXT,
                    &[7, 0, 4, 0x8000_0000 | 1 | 0x4000],
                    &arrays([0., 1., 1., 0.], double),
                ),
            ],
        );
        let left = pixel(&mut qemu, &mut sequence, 0);
        let right = pixel(&mut qemu, &mut sequence, 7);
        close(left, [239, 239, 255], "array fog left");
        close(right, [16, 16, 255], "array fog right");
        assert_eq!(
            words(&query(&mut qemu, &mut sequence, 0x305, &[0x8453]).1),
            [f(0.25)],
            "current fog coordinate restored"
        );
    }

    // FRAGMENT_DEPTH ignores the coordinate: an eye-space depth of zero is clear.
    submit(
        &mut qemu,
        &mut sequence,
        &[data_call(FOG_FV, &[SOURCE], &(DEPTH as f32).to_le_bytes())],
    );
    submit(&mut qemu, &mut sequence, &quad([1.; 4]));
    close(
        pixel(&mut qemu, &mut sequence, 4),
        [255, 255, 255],
        "depth fog",
    );
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
