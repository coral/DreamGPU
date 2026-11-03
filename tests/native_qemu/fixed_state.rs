//! Fixed-function state through the device: constant blend colour, blend
//! equations, separate RGB/alpha factors, separate front/back stencil with
//! wrapping operations, anisotropy/LOD-bias parameters and point sprites.
use super::textures::{call, data_call, query, words};
use super::*;

const BLEND_COLOR: u32 = 0x06b;
const BLEND_EQUATION: u32 = 0x06e;
const BLEND_FUNC_SEPARATE: u32 = 0x07c;
const STENCIL_FUNC_SEPARATE: u32 = 0x845;
const STENCIL_OP_SEPARATE: u32 = 0x84a;
const POINT_PARAMETER_FV: u32 = 0x69b;
const BLEND: u32 = 0x0be2;
const STENCIL_TEST: u32 = 0x0b90;
const TEXTURE_2D: u32 = 0x0de1;
const FRONT: u32 = 0x0404;
const BACK: u32 = 0x0405;

fn submit(qemu: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    qemu.batch(*sequence, records);
    qemu.await_completion(*sequence);
}

fn f(v: f32) -> u32 {
    v.to_bits()
}

/// Full-viewport quad; counter-clockwise (front) unless `back`.
fn quad(color: [f32; 4], back: bool) -> Vec<(u32, Vec<u32>)> {
    let mut corners = vec![(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)];
    if back {
        corners.reverse();
    }
    let mut out = vec![call(0x0db, &color.map(f)), call(0x01a, &[7])];
    for (x, y) in corners {
        out.push(call(0x9dc, &[f(x), f(y)]));
    }
    out.push(call(0x216, &[]));
    out
}

fn clear(color: [f32; 4]) -> Vec<(u32, Vec<u32>)> {
    vec![call(0x0a3, &color.map(f)), call(0x09a, &[0x4000 | 0x400])]
}

fn pixel(qemu: &mut Qemu, sequence: &mut u32, x: u32, y: u32) -> [u8; 4] {
    let (_, result) = query(qemu, sequence, 0x7a4, &[x, y, 1 | (1 << 16)]);
    result[..4].try_into().unwrap()
}

fn close(actual: [u8; 4], expected: [u8; 4], label: &str) {
    for c in 0..4 {
        let d = i32::from(actual[c]) - i32::from(expected[c]);
        assert!(
            (-2..=2).contains(&d),
            "{label}: {actual:?} expected {expected:?}"
        );
    }
}

fn float_query(qemu: &mut Qemu, sequence: &mut u32, function: u32, args: &[u32]) -> Vec<f32> {
    words(&query(qemu, sequence, function, args).1)
        .into_iter()
        .map(f32::from_bits)
        .collect()
}

#[test]
#[ignore = "requires native host GPU and fixed-state QEMU producer; diskless acceptance"]
fn qemu_fixed_state_blend_stencil_sampler_and_point_sprites() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-fs-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 0;
    assert_ne!(qemu.read(0x1008) & 0x10000, 0);
    submit(
        &mut qemu,
        &mut sequence,
        &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])],
    );

    // CONSTANT_COLOR source factor with BlendColor.
    let mut records = clear([0., 0., 0., 1.]);
    records.extend([
        call(0x209, &[BLEND]),
        call(BLEND_COLOR, &[f(0.25), f(0.5), f(0.75), f(1.)]),
        call(0x078, &[0x8001, 0]),
    ]);
    records.extend(quad([1.; 4], false));
    submit(&mut qemu, &mut sequence, &records);
    close(
        pixel(&mut qemu, &mut sequence, 3, 3),
        [64, 128, 191, 255],
        "constant colour",
    );
    assert_eq!(
        float_query(&mut qemu, &mut sequence, 0x305, &[0x8005]),
        [0.25, 0.5, 0.75, 1.]
    );

    // FUNC_REVERSE_SUBTRACT, then MIN and MAX.
    let dst = [200. / 255., 200. / 255., 200. / 255., 1.];
    let src = [50. / 255., 100. / 255., 150. / 255., 1.];
    for (equation, expected) in [
        (0x800b, [150, 100, 50]),
        (0x8007, [50, 100, 150]),
        (0x8008, [200, 200, 200]),
    ] {
        let mut records = clear(dst);
        records.extend([call(0x078, &[1, 1]), call(BLEND_EQUATION, &[equation])]);
        records.extend(quad(src, false));
        submit(&mut qemu, &mut sequence, &records);
        let p = pixel(&mut qemu, &mut sequence, 4, 4);
        close(
            [p[0], p[1], p[2], 255],
            [expected[0], expected[1], expected[2], 255],
            "equation",
        );
        assert_eq!(
            words(&query(&mut qemu, &mut sequence, 0x329, &[0x8009]).1),
            [equation]
        );
    }

    // Separate factors: RGB from the source, alpha kept from the destination.
    let mut records = clear([0., 0., 0., 0.25]);
    records.extend([
        call(BLEND_EQUATION, &[0x8006]),
        call(BLEND_FUNC_SEPARATE, &[1, 0, 0, 1]),
    ]);
    records.extend(quad([1., 0.5, 0., 1.], false));
    submit(&mut qemu, &mut sequence, &records);
    close(
        pixel(&mut qemu, &mut sequence, 2, 2),
        [255, 128, 0, 64],
        "separate alpha",
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x80cb]).1),
        [0]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x80ca]).1),
        [1]
    );
    submit(&mut qemu, &mut sequence, &[call(0x1c5, &[BLEND])]);

    // Two-sided stencil: a front quad writes 1, a back quad writes 2; each
    // face's reference then selects only its own coverage. INCR_WRAP wraps.
    let mut records = vec![
        call(0x0b5, &[0]),
        call(0x09a, &[0x400]),
        call(0x209, &[STENCIL_TEST]),
        call(STENCIL_FUNC_SEPARATE, &[FRONT, 0x207, 1, 0xff]),
        call(STENCIL_OP_SEPARATE, &[FRONT, 0x1e00, 0x1e00, 0x1e01]),
        call(STENCIL_FUNC_SEPARATE, &[BACK, 0x207, 2, 0xff]),
        call(STENCIL_OP_SEPARATE, &[BACK, 0x1e00, 0x1e00, 0x1e01]),
    ];
    records.extend(quad([0., 0., 0., 1.], true));
    records.push(call(0x844, &[0x202, 2, 0xff]));
    records.push(call(0x849, &[0x1e00, 0x1e00, 0x1e00]));
    records.extend(quad([0., 0., 1., 1.], false));
    submit(&mut qemu, &mut sequence, &records);
    close(
        pixel(&mut qemu, &mut sequence, 5, 5),
        [0, 0, 255, 255],
        "back face reference",
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x8ca3]).1),
        [2]
    );
    let mut records = vec![
        call(0x0b5, &[255]),
        call(0x09a, &[0x400]),
        call(0x844, &[0x207, 0, 0xff]),
        call(0x849, &[0x1e00, 0x1e00, 0x8507]),
    ];
    records.extend(quad([0., 0., 0., 1.], false));
    records.push(call(0x844, &[0x202, 0, 0xff]));
    records.push(call(0x849, &[0x1e00, 0x1e00, 0x1e00]));
    records.extend(quad([0., 1., 0., 1.], false));
    records.push(call(0x1c5, &[STENCIL_TEST]));
    submit(&mut qemu, &mut sequence, &records);
    close(
        pixel(&mut qemu, &mut sequence, 1, 6),
        [0, 255, 0, 255],
        "INCR_WRAP",
    );

    // Sampler parameters round-trip through the host.
    let records = vec![
        call(0x04e, &[TEXTURE_2D, 9]),
        call(0x8dd, &[TEXTURE_2D, 0x84fe, f(4.)]),
        call(0x8c4, &[0x8500, 0x8501, f(0.5)]),
    ];
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(
        float_query(&mut qemu, &mut sequence, 0x418, &[TEXTURE_2D, 0x84fe]),
        [4.]
    );
    assert_eq!(
        float_query(&mut qemu, &mut sequence, 0x40c, &[0x8500, 0x8501]),
        [0.5]
    );
    assert!(float_query(&mut qemu, &mut sequence, 0x305, &[0x84ff])[0] >= 4.);

    // Point sprites: COORD_REPLACE spans the 2x1 red|green texture across an
    // eight-pixel point, so the left and right halves sample different texels.
    let mut records = clear([0., 0., 0., 1.]);
    records.extend([
        call(0x8df, &[TEXTURE_2D, 0x2801, 0x2600]),
        call(0x8df, &[TEXTURE_2D, 0x2800, 0x2600]),
        data_call(
            0x8d4,
            &[TEXTURE_2D, 0, 0x1908, 2, 1, 0, 0x1908, 0x1401],
            &[255, 0, 0, 255, 0, 255, 0, 255],
        ),
        call(0x209, &[TEXTURE_2D]),
        call(0x209, &[0x8861]),
        call(0x8c6, &[0x8861, 0x8862, 1]),
        data_call(
            POINT_PARAMETER_FV,
            &[0x8129],
            &[1f32, 0., 0.].map(f32::to_le_bytes).concat(),
        ),
        call(0x6a4, &[f(8.)]),
        call(0x0db, &[f(1.); 4]),
        call(0x01a, &[0]),
        call(0x9dc, &[f(0.), f(0.)]),
        call(0x216, &[]),
    ]);
    submit(&mut qemu, &mut sequence, &records);
    close(
        pixel(&mut qemu, &mut sequence, 1, 4),
        [255, 0, 0, 255],
        "sprite left",
    );
    close(
        pixel(&mut qemu, &mut sequence, 6, 4),
        [0, 255, 0, 255],
        "sprite right",
    );
    assert_eq!(
        float_query(&mut qemu, &mut sequence, 0x305, &[0x8129]),
        [1., 0., 0.]
    );
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
