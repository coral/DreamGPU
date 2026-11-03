//! EXT_texture3D through the device: slices sampled at explicit R, NEAREST and
//! LINEAR filtering along R, mip levels, TEXTURE_WRAP_R, subimages, copies,
//! readback through every image, zero-defined storage, tight non-aligned
//! payloads and packed types, proxies, display lists, per-unit bindings with
//! TEXTURE_BIT, target precedence and the incomplete fallback, deletion and
//! state-dependent failures as GL errors that keep the batch alive.
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;

const TEXTURE_3D: u32 = 0x806f;
const PROXY_3D: u32 = 0x8070;
const TEXTURE_DEPTH: u32 = 0x8071;
const WRAP_R: u32 = 0x8072;
const MAX_3D_SIZE: u32 = 0x8073;
const BINDING_3D: u32 = 0x806a;
const TEXTURE_2D: u32 = 0x0de1;
const TEXTURE0: u32 = 0x84c0;
const MIN_FILTER: u32 = 0x2801;
const MAG_FILTER: u32 = 0x2800;
const WRAP_S: u32 = 0x2802;
const WRAP_T: u32 = 0x2803;
const NEAREST: u32 = 0x2600;
const LINEAR: u32 = 0x2601;
const NEAREST_MIPMAP_NEAREST: u32 = 0x2700;
const REPEAT: u32 = 0x2901;
const CLAMP_TO_EDGE: u32 = 0x812f;
const BASE_LEVEL: u32 = 0x813c;
const MAX_LEVEL: u32 = 0x813d;
const RGBA8: u32 = 0x8058;
const RGB8: u32 = 0x8051;
const RGBA: u32 = 0x1908;
const RGB: u32 = 0x1907;
const UNSIGNED_BYTE: u32 = 0x1401;
const UNSIGNED_SHORT_5_6_5: u32 = 0x8363;
const TEXTURE_BIT: u32 = 0x40000;
const ACTIVE_TEXTURE: u32 = 0x84e0;
const DG_GL_RESULT_BOOL: u32 = 1;

const BIND: u32 = 0x04e;
const PARAMETER: u32 = 0x8df;
const IMAGE_2D: u32 = 0x8d4;
const IMAGE_3D: u32 = 0x8d7;
const SUB_IMAGE_3D: u32 = 0x8f7;
const COPY_SUB_IMAGE_3D: u32 = 0x15c;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const ACTIVE: u32 = 0x006;
const PUSH: u32 = 0x775;
const POP: u32 = 0x6af;
const DELETE: u32 = 0x1b0;
const GET_TEX_IMAGE: u32 = 0x414;
const LEVEL_PARAMETER: u32 = 0x416;
const GET_PARAMETER: u32 = 0x41d;
const IS_ENABLED: u32 = 0x4ba;
const GET_INTEGER: u32 = 0x329;
const GEN_LISTS: u32 = 671;
const NEW_LIST: u32 = 1592;
const END_LIST: u32 = 539;

/// One opaque colour per slice of the 4x4x4 volume.
const SLICES: [[u8; 4]; 4] = [
    [255, 0, 0, 255],
    [0, 255, 0, 255],
    [0, 0, 255, 255],
    [255, 255, 0, 255],
];

fn submit(qemu: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    qemu.batch(*sequence, records);
    qemu.await_completion(*sequence);
}
fn f(v: f32) -> u32 {
    v.to_bits()
}
/// `images` images of `side`x`side` texels, image z filled with `colours[z]`.
fn volume(side: usize, colours: &[[u8; 4]]) -> Vec<u8> {
    colours
        .iter()
        .flat_map(|rgba| rgba.repeat(side * side))
        .collect()
}
fn image(level: u32, side: u32, colours: &[[u8; 4]]) -> (u32, Vec<u32>) {
    data_call(
        IMAGE_3D,
        &[
            TEXTURE_3D,
            level,
            RGBA8,
            side,
            side,
            colours.len() as u32,
            0,
            RGBA,
            UNSIGNED_BYTE,
        ],
        &volume(side as usize, colours),
    )
}
fn parameters(name: u32, min: u32, mag: u32, wrap: u32) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(BIND, &[TEXTURE_3D, name]),
        call(PARAMETER, &[TEXTURE_3D, MIN_FILTER, min]),
        call(PARAMETER, &[TEXTURE_3D, MAG_FILTER, mag]),
        call(PARAMETER, &[TEXTURE_3D, WRAP_S, CLAMP_TO_EDGE]),
        call(PARAMETER, &[TEXTURE_3D, WRAP_T, CLAMP_TO_EDGE]),
        call(PARAMETER, &[TEXTURE_3D, WRAP_R, wrap]),
    ]
}
/// A white quad over the 8x8 drawable whose corners carry STR coordinates
/// `s`/`t` from 0 to `span` and a constant `r`.
fn quad(span: f32, r: f32) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(0x0db, &[f(1.); 4]), call(0x01a, &[7])];
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        let (s, t) = ((x + 1.) / 2. * span, (y + 1.) / 2. * span);
        out.push(call(0x8a9, &[f(s), f(t), f(r), f(1.)]));
        out.push(call(0x9ea, &[f(x), f(y), f(0.)]));
    }
    out.push(call(0x216, &[]));
    out
}
/// A constant STR quad: every fragment samples the texel column at (0.5, 0.5).
fn at(r: f32) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(0x0db, &[f(1.); 4]), call(0x01a, &[7])];
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        out.push(call(0x8a9, &[f(0.5), f(0.5), f(r), f(1.)]));
        out.push(call(0x9ea, &[f(x), f(y), f(0.)]));
    }
    out.push(call(0x216, &[]));
    out
}
fn pixel(qemu: &mut Qemu, sequence: &mut u32, x: u32, y: u32) -> [u8; 4] {
    let (_, result) = query(qemu, sequence, 0x7a4, &[x, y, 1 | (1 << 16)]);
    result[..4].try_into().unwrap()
}
fn expect(qemu: &mut Qemu, sequence: &mut u32, rgb: [u8; 4], label: &str) {
    let actual = pixel(qemu, sequence, 4, 4);
    assert_eq!(actual[..3], rgb[..3], "{label}: {actual:?}");
}
fn draw(qemu: &mut Qemu, sequence: &mut u32, records: Vec<(u32, Vec<u32>)>) -> [u8; 4] {
    submit(qemu, sequence, &records);
    pixel(qemu, sequence, 4, 4)
}
fn integer(qemu: &mut Qemu, sequence: &mut u32, pname: u32) -> u32 {
    words(&query(qemu, sequence, GET_INTEGER, &[pname]).1)[0]
}
fn level(qemu: &mut Qemu, sequence: &mut u32, target: u32, level: u32, pname: u32) -> u32 {
    words(&query(qemu, sequence, LEVEL_PARAMETER, &[target, level, pname]).1)[0]
}
fn error(qemu: &mut Qemu, sequence: &mut u32) -> u32 {
    words(&query(qemu, sequence, 0x2fc, &[]).1)[0]
}
/// Every RGBA8 texel of a level, `count` pixels from `first`.
fn read(qemu: &mut Qemu, sequence: &mut u32, level: u32, count: u32, first: u32) -> Vec<u8> {
    query_capacity(
        qemu,
        sequence,
        GET_TEX_IMAGE,
        &[TEXTURE_3D, level | (count << 16), first],
        count * 4,
    )
    .1
}

#[test]
#[ignore = "requires native host GPU and 3D texture QEMU producer; diskless acceptance"]
fn qemu_texture_3d_slices_filters_levels_wrap_updates_readback_and_bindings() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-3d-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 0;
    assert_ne!(qemu.read(0x1008) & 0x800000, 0);
    submit(
        &mut qemu,
        &mut sequence,
        &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])],
    );
    assert_eq!(integer(&mut qemu, &mut sequence, MAX_3D_SIZE), 256);

    // Four NEAREST slices, each sampled exactly at its centre; R 0 and 1
    // clamp to the first and last slice, 0.5 is slice 2's first texel.
    let mut records = parameters(30, NEAREST, NEAREST, CLAMP_TO_EDGE);
    records.push(image(0, 4, &SLICES));
    records.push(call(ENABLE, &[TEXTURE_3D]));
    submit(&mut qemu, &mut sequence, &records);
    for (slice, rgba) in SLICES.iter().enumerate() {
        let r = (slice as f32 + 0.5) / 4.;
        submit(&mut qemu, &mut sequence, &at(r));
        expect(&mut qemu, &mut sequence, *rgba, &format!("slice {slice}"));
    }
    for (r, slice) in [(0., 0), (0.5, 2), (1., 3)] {
        submit(&mut qemu, &mut sequence, &at(r));
        expect(&mut qemu, &mut sequence, SLICES[slice], &format!("r={r}"));
    }
    assert_eq!(error(&mut qemu, &mut sequence), 0);
    assert_eq!(
        query(&mut qemu, &mut sequence, IS_ENABLED, &[TEXTURE_3D]),
        (DG_GL_RESULT_BOOL, vec![1])
    );
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 30);
    assert_eq!(
        level(&mut qemu, &mut sequence, TEXTURE_3D, 0, TEXTURE_DEPTH),
        4
    );
    // The level reads back image after image, rows within each.
    let texels = read(&mut qemu, &mut sequence, 0, 64, 0);
    assert_eq!(texels, volume(4, &SLICES));
    let tail = read(&mut qemu, &mut sequence, 0, 32, 32);
    assert_eq!(tail, volume(4, &SLICES[2..]));

    // Two slices along R: NEAREST picks one at 0.5, LINEAR blends the slice
    // centres at 0.5 and reaches each slice exactly at 0.25 and 0.75.
    let dark = [0, 0, 0, 255];
    let warm = [200, 100, 50, 255];
    let mut records = parameters(31, NEAREST, NEAREST, CLAMP_TO_EDGE);
    records.push(image(0, 2, &[dark, warm]));
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(
        draw(&mut qemu, &mut sequence, at(0.5))[..3],
        warm[..3],
        "nearest 0.5"
    );
    assert_eq!(
        draw(&mut qemu, &mut sequence, at(0.25))[..3],
        dark[..3],
        "nearest 0.25"
    );
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(PARAMETER, &[TEXTURE_3D, MIN_FILTER, LINEAR]),
            call(PARAMETER, &[TEXTURE_3D, MAG_FILTER, LINEAR]),
        ],
    );
    let middle = draw(&mut qemu, &mut sequence, at(0.5));
    for (channel, expected) in [100u8, 50, 25].into_iter().enumerate() {
        assert!(
            middle[channel].abs_diff(expected) <= 1,
            "linear r=0.5: {middle:?}"
        );
    }
    assert_eq!(draw(&mut qemu, &mut sequence, at(0.25))[..3], dark[..3]);
    assert_eq!(draw(&mut qemu, &mut sequence, at(0.75))[..3], warm[..3]);
    assert_eq!(draw(&mut qemu, &mut sequence, at(1.))[..3], warm[..3]);

    // TEXTURE_WRAP_R: beyond the last slice, CLAMP_TO_EDGE keeps it and
    // REPEAT returns to the first; below zero REPEAT reaches the last.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(PARAMETER, &[TEXTURE_3D, MIN_FILTER, NEAREST]),
            call(PARAMETER, &[TEXTURE_3D, MAG_FILTER, NEAREST]),
        ],
    );
    assert_eq!(draw(&mut qemu, &mut sequence, at(1.25))[..3], warm[..3]);
    submit(
        &mut qemu,
        &mut sequence,
        &[call(PARAMETER, &[TEXTURE_3D, WRAP_R, REPEAT])],
    );
    assert_eq!(draw(&mut qemu, &mut sequence, at(1.25))[..3], dark[..3]);
    assert_eq!(draw(&mut qemu, &mut sequence, at(-0.25))[..3], warm[..3]);
    assert_eq!(
        words(
            &query(
                &mut qemu,
                &mut sequence,
                GET_PARAMETER,
                &[TEXTURE_3D, WRAP_R]
            )
            .1
        )[0],
        REPEAT
    );

    // Mip levels: 4x4x4, 2x2x2 and 1x1x1 of one colour each. Minification
    // by 2 and 4 selects levels 1 and 2; BASE_LEVEL magnifies any level.
    let levels = [[255, 0, 0, 255], [0, 200, 0, 255], [0, 0, 160, 255]];
    let mut records = parameters(32, NEAREST_MIPMAP_NEAREST, NEAREST, REPEAT);
    records.extend([
        call(PARAMETER, &[TEXTURE_3D, WRAP_S, REPEAT]),
        call(PARAMETER, &[TEXTURE_3D, WRAP_T, REPEAT]),
    ]);
    for (index, rgba) in levels.iter().enumerate() {
        let side = 4 >> index;
        records.push(image(index as u32, side, &vec![*rgba; side as usize]));
    }
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(
        draw(&mut qemu, &mut sequence, quad(2., 0.5))[..3],
        levels[0][..3]
    );
    assert_eq!(
        draw(&mut qemu, &mut sequence, quad(4., 0.5))[..3],
        levels[1][..3]
    );
    assert_eq!(
        draw(&mut qemu, &mut sequence, quad(8., 0.5))[..3],
        levels[2][..3]
    );
    submit(
        &mut qemu,
        &mut sequence,
        &[call(PARAMETER, &[TEXTURE_3D, BASE_LEVEL, 1])],
    );
    assert_eq!(draw(&mut qemu, &mut sequence, at(0.5))[..3], levels[1][..3]);
    assert_eq!(
        level(&mut qemu, &mut sequence, TEXTURE_3D, 1, TEXTURE_DEPTH),
        2
    );
    assert_eq!(read(&mut qemu, &mut sequence, 2, 1, 0), levels[2]);

    // Incomplete fallback: without level 1 and 2 the mipmapped texture is
    // incomplete, so the enabled 2D texture below it samples instead; with
    // no other target the quad stays white. A complete 3D texture takes
    // precedence over the enabled 2D one.
    let grey = [90, 90, 90, 255];
    let mut records = parameters(33, NEAREST_MIPMAP_NEAREST, NEAREST, CLAMP_TO_EDGE);
    records.extend([
        image(0, 4, &SLICES),
        call(BIND, &[TEXTURE_2D, 40]),
        call(PARAMETER, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        call(PARAMETER, &[TEXTURE_2D, MAG_FILTER, NEAREST]),
        data_call(
            IMAGE_2D,
            &[TEXTURE_2D, 0, RGBA8, 1, 1, 0, RGBA, UNSIGNED_BYTE],
            &grey,
        ),
        call(ENABLE, &[TEXTURE_2D]),
    ]);
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(draw(&mut qemu, &mut sequence, at(0.6))[..3], grey[..3]);
    submit(&mut qemu, &mut sequence, &[call(DISABLE, &[TEXTURE_2D])]);
    assert_eq!(draw(&mut qemu, &mut sequence, at(0.6))[..3], [255; 3]);
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(PARAMETER, &[TEXTURE_3D, MAX_LEVEL, 0]),
            call(ENABLE, &[TEXTURE_2D]),
        ],
    );
    assert_eq!(draw(&mut qemu, &mut sequence, at(0.6))[..3], SLICES[2][..3]);
    submit(&mut qemu, &mut sequence, &[call(DISABLE, &[TEXTURE_2D])]);

    // Subimages replace exactly their box; outside the level they are the
    // guest's GL error and the rest of the batch (a draw) still executes.
    let violet = [120, 0, 200, 255];
    let mut records = vec![
        call(BIND, &[TEXTURE_3D, 30]),
        data_call(
            SUB_IMAGE_3D,
            &[TEXTURE_3D, 0, 1, 1, 2, 2, 2, 1, RGBA, UNSIGNED_BYTE],
            &violet.repeat(4),
        ),
        data_call(
            SUB_IMAGE_3D,
            &[TEXTURE_3D, 0, 3, 0, 3, 2, 1, 1, RGBA, UNSIGNED_BYTE],
            &violet.repeat(2),
        ),
    ];
    records.extend(at(0.625));
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(error(&mut qemu, &mut sequence), 0x0501);
    expect(
        &mut qemu,
        &mut sequence,
        violet,
        "subimage at (0.5, 0.5, slice 2)",
    );
    let texels = read(&mut qemu, &mut sequence, 0, 64, 0);
    for (index, texel) in texels.as_chunks::<4>().0.iter().enumerate() {
        let (x, y, z) = (index % 4, index / 4 % 4, index / 16);
        let inside = z == 2 && (1..3).contains(&x) && (1..3).contains(&y);
        let expected = if inside { violet } else { SLICES[z] };
        assert_eq!(*texel, expected, "texel {x},{y},{z}");
    }

    // A tight 3x2x5 RGB payload (nine-byte rows) and RGB565 images.
    let rgb: Vec<u8> = (0..30u8).flat_map(|i| [i * 8, 255 - i * 8, i]).collect();
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(BIND, &[TEXTURE_3D, 34]),
            data_call(
                IMAGE_3D,
                &[TEXTURE_3D, 0, RGB8, 3, 2, 5, 0, RGB, UNSIGNED_BYTE],
                &rgb,
            ),
        ],
    );
    let texels = read(&mut qemu, &mut sequence, 0, 30, 0);
    for (index, texel) in texels.as_chunks::<4>().0.iter().enumerate() {
        assert_eq!(
            texel[..3],
            rgb[index * 3..index * 3 + 3],
            "rgb texel {index}"
        );
        assert_eq!(texel[3], 255);
    }
    let packed: Vec<u8> = [0xf800u16, 0x07e0, 0x001f, 0xffff]
        .iter()
        .flat_map(|v| v.to_le_bytes())
        .collect();
    submit(
        &mut qemu,
        &mut sequence,
        &[data_call(
            IMAGE_3D,
            &[TEXTURE_3D, 0, RGB8, 1, 1, 4, 0, RGB, UNSIGNED_SHORT_5_6_5],
            &packed,
        )],
    );
    assert_eq!(
        read(&mut qemu, &mut sequence, 0, 4, 0),
        [
            [255, 0, 0, 255],
            [0, 255, 0, 255],
            [0, 0, 255, 255],
            [255, 255, 255, 255]
        ]
        .concat()
    );

    // An image without payload is zero-defined, read back from its last texel.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(BIND, &[TEXTURE_3D, 35]),
            data_call(
                IMAGE_3D,
                &[TEXTURE_3D, 0, RGBA8, 64, 64, 64, 0, RGBA, UNSIGNED_BYTE],
                &[],
            ),
        ],
    );
    assert_eq!(
        level(&mut qemu, &mut sequence, TEXTURE_3D, 0, TEXTURE_DEPTH),
        64
    );
    assert_eq!(
        read(&mut qemu, &mut sequence, 0, 16, 64 * 64 * 64 - 16),
        vec![0; 64]
    );
    // CopyTexSubImage3D writes one image of the level from the framebuffer.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(DISABLE, &[TEXTURE_3D]),
            call(0x0a3, &[f(0.), f(1.), f(0.), f(1.)]),
            call(0x09a, &[0x4000]),
            call(COPY_SUB_IMAGE_3D, &[TEXTURE_3D, 0, 1, 2, 63, 0, 0, 2, 2]),
            call(COPY_SUB_IMAGE_3D, &[TEXTURE_3D, 0, 0, 0, 64, 0, 0, 1, 1]),
        ],
    );
    assert_eq!(error(&mut qemu, &mut sequence), 0x0501);
    let last = read(&mut qemu, &mut sequence, 0, 256, 63 * 64 * 64);
    for (index, texel) in last.as_chunks::<4>().0.iter().enumerate() {
        let (x, y) = (index % 64, index / 64);
        let inside = (1..3).contains(&x) && (2..4).contains(&y);
        let expected = if inside { [0, 255, 0, 255] } else { [0; 4] };
        assert_eq!(*texel, expected, "copied texel {x},{y}");
    }

    // A border is refused as a GL error; the level keeps its image.
    submit(
        &mut qemu,
        &mut sequence,
        &[data_call(
            IMAGE_3D,
            &[TEXTURE_3D, 0, RGBA8, 3, 3, 3, 1, RGBA, UNSIGNED_BYTE],
            &[7; 108],
        )],
    );
    assert_eq!(error(&mut qemu, &mut sequence), 0x0502);
    assert_eq!(
        level(&mut qemu, &mut sequence, TEXTURE_3D, 0, TEXTURE_DEPTH),
        64
    );

    // Proxies: native feasibility within the virtual limit, zero beyond it
    // (and for a border).
    submit(
        &mut qemu,
        &mut sequence,
        &[
            data_call(
                IMAGE_3D,
                &[PROXY_3D, 0, RGBA8, 64, 32, 16, 0, RGBA, UNSIGNED_BYTE],
                &[],
            ),
            data_call(
                IMAGE_3D,
                &[PROXY_3D, 1, RGBA8, 256, 4, 4, 0, RGBA, UNSIGNED_BYTE],
                &[],
            ),
            data_call(
                IMAGE_3D,
                &[PROXY_3D, 2, RGBA8, 4, 4, 4, 1, RGBA, UNSIGNED_BYTE],
                &[],
            ),
        ],
    );
    assert_eq!(level(&mut qemu, &mut sequence, PROXY_3D, 0, 0x1000), 64);
    assert_eq!(
        level(&mut qemu, &mut sequence, PROXY_3D, 0, TEXTURE_DEPTH),
        16
    );
    assert_eq!(level(&mut qemu, &mut sequence, PROXY_3D, 1, 0x1000), 0);
    assert_eq!(level(&mut qemu, &mut sequence, PROXY_3D, 2, 0x1000), 0);
    assert_eq!(error(&mut qemu, &mut sequence), 0);

    // A display list captures the bind, image and subimage for replay; the
    // proxy inside it executes at compile time.
    let list = words(&query(&mut qemu, &mut sequence, GEN_LISTS, &[1, 0, 0]).1)[1];
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, NEW_LIST, &[list, 0x1300, 0]).1),
        [0]
    );
    let teal = [0, 128, 128, 255];
    let mut records = parameters(36, NEAREST, NEAREST, CLAMP_TO_EDGE);
    records.extend([
        image(0, 2, &[SLICES[0], SLICES[1]]),
        data_call(
            SUB_IMAGE_3D,
            &[TEXTURE_3D, 0, 0, 0, 1, 2, 2, 1, RGBA, UNSIGNED_BYTE],
            &teal.repeat(4),
        ),
        data_call(
            IMAGE_3D,
            &[PROXY_3D, 0, RGBA8, 8, 8, 8, 0, RGBA, UNSIGNED_BYTE],
            &[],
        ),
    ]);
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, END_LIST, &[0; 3]).1),
        [0]
    );
    assert_eq!(level(&mut qemu, &mut sequence, PROXY_3D, 0, 0x1000), 8);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 35);
    let mut records = vec![call(0x092, &[list]), call(ENABLE, &[TEXTURE_3D])];
    records.extend(at(0.75));
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 36);
    expect(
        &mut qemu,
        &mut sequence,
        teal,
        "slice uploaded by a display list",
    );
    assert_eq!(
        draw(&mut qemu, &mut sequence, at(0.25))[..3],
        SLICES[0][..3]
    );

    // Per-unit bindings: unit 1 binds 31; TEXTURE_BIT saves both units.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(BIND, &[TEXTURE_3D, 30]),
            call(ACTIVE, &[TEXTURE0 + 1]),
            call(BIND, &[TEXTURE_3D, 31]),
        ],
    );
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 31);
    submit(&mut qemu, &mut sequence, &[call(ACTIVE, &[TEXTURE0])]);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 30);
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(PUSH, &[TEXTURE_BIT]),
            call(BIND, &[TEXTURE_3D, 0]),
            call(ACTIVE, &[TEXTURE0 + 1]),
            call(BIND, &[TEXTURE_3D, 32]),
        ],
    );
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 32);
    submit(&mut qemu, &mut sequence, &[call(POP, &[])]);
    assert_eq!(integer(&mut qemu, &mut sequence, ACTIVE_TEXTURE), TEXTURE0);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 30);
    submit(&mut qemu, &mut sequence, &[call(ACTIVE, &[TEXTURE0 + 1])]);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 31);
    let mut records = vec![call(ACTIVE, &[TEXTURE0]), call(ENABLE, &[TEXTURE_3D])];
    records.extend(at(0.875));
    submit(&mut qemu, &mut sequence, &records);
    expect(
        &mut qemu,
        &mut sequence,
        SLICES[3],
        "restored unit 0 volume",
    );

    // A 3D name is not a 2D texture: INVALID_OPERATION, binding kept, and the
    // rest of the batch (a draw) still executes.
    let mut records = vec![call(BIND, &[TEXTURE_2D, 30])];
    records.extend(at(0.125));
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(error(&mut qemu, &mut sequence), 0x0502);
    expect(
        &mut qemu,
        &mut sequence,
        SLICES[0],
        "draw after target mismatch",
    );
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 30);

    // Deleting reverts every unit holding the name to the default 3D texture.
    let names: Vec<u8> = [30u32, 31].iter().flat_map(|n| n.to_le_bytes()).collect();
    submit(&mut qemu, &mut sequence, &[data_call(DELETE, &[2], &names)]);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 0);
    submit(&mut qemu, &mut sequence, &[call(ACTIVE, &[TEXTURE0 + 1])]);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_3D), 0);
    submit(
        &mut qemu,
        &mut sequence,
        &[call(ACTIVE, &[TEXTURE0]), call(BIND, &[TEXTURE_2D, 30])],
    );
    assert_eq!(integer(&mut qemu, &mut sequence, 0x8069), 30);
    assert_eq!(error(&mut qemu, &mut sequence), 0);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
