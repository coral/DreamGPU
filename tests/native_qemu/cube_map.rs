//! ARB_texture_cube_map through the device: six faces sampled by explicit STR
//! directions, NORMAL_MAP and REFLECTION_MAP texgen, per-face redefinition,
//! subimages, copies and readback, compressed faces, proxies, display lists,
//! per-unit bindings with TEXTURE_BIT, deletion and target mismatch as GL
//! errors that keep the batch alive.
use super::textures::{call, data_call, query, words};
use super::*;

const CUBE: u32 = 0x8513;
const BINDING_CUBE: u32 = 0x8514;
const POSITIVE_X: u32 = 0x8515;
const PROXY_CUBE: u32 = 0x851b;
const MAX_CUBE_SIZE: u32 = 0x851c;
const TEXTURE_2D: u32 = 0x0de1;
const TEXTURE0: u32 = 0x84c0;
const WRAP_R: u32 = 0x8072;
const CLAMP_TO_EDGE: u32 = 0x812f;
const NORMAL_MAP: u32 = 0x8511;
const REFLECTION_MAP: u32 = 0x8512;
const TEXTURE_GEN_MODE: u32 = 0x2500;
const GEN_S: u32 = 0x0c60;
const GEN_T: u32 = 0x0c61;
const GEN_R: u32 = 0x0c62;
const S: u32 = 0x2000;
const T: u32 = 0x2001;
const R: u32 = 0x2002;
const TEXTURE_BIT: u32 = 0x40000;
const ACTIVE_TEXTURE: u32 = 0x84e0;
const DXT1: u32 = 0x83f0;
const DG_GL_RESULT_BOOL: u32 = 1;

const BIND: u32 = 0x04e;
const PARAMETER: u32 = 0x8df;
const IMAGE: u32 = 0x8d4;
const SUB_IMAGE: u32 = 0x8f5;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const ACTIVE: u32 = 0x006;
const TEX_GEN: u32 = 0x8cd;
const PUSH: u32 = 0x775;
const POP: u32 = 0x6af;
const DELETE: u32 = 0x1b0;
const COPY_SUB_IMAGE: u32 = 0x15a;
const GET_TEX_IMAGE: u32 = 0x414;
const LEVEL_PARAMETER: u32 = 0x416;
const GET_PARAMETER: u32 = 0x41d;
const IS_ENABLED: u32 = 0x4ba;
const GET_INTEGER: u32 = 0x329;
const GET_TEX_GEN: u32 = 0x412;
const COMPRESSED_IMAGE: u32 = 0x11e;
const GET_COMPRESSED: u32 = 0x2e7;
const GEN_LISTS: u32 = 671;
const NEW_LIST: u32 = 1592;
const END_LIST: u32 = 539;

/// One opaque colour per face, +X through -Z.
const FACES: [[u8; 4]; 6] = [
    [255, 0, 0, 255],
    [0, 255, 0, 255],
    [0, 0, 255, 255],
    [255, 255, 0, 255],
    [255, 0, 255, 255],
    [0, 255, 255, 255],
];
/// A direction whose major axis selects each face.
const DIRECTIONS: [[f32; 3]; 6] = [
    [1., 0.2, -0.3],
    [-1., -0.3, 0.2],
    [0.1, 1., 0.3],
    [-0.2, -1., -0.1],
    [0.3, -0.2, 1.],
    [-0.1, 0.3, -1.],
];

fn submit(qemu: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    qemu.batch(*sequence, records);
    qemu.await_completion(*sequence);
}
fn f(v: f32) -> u32 {
    v.to_bits()
}
fn solid(rgba: [u8; 4], side: usize) -> Vec<u8> {
    rgba.repeat(side * side)
}
/// A white quad (the whole 8x8 drawable at extent 1) at depth `z`; every vertex
/// carries the same explicit `coordinates`, or the quad sets a texgen normal.
fn quad(
    coordinates: Option<[f32; 3]>,
    normal: Option<[f32; 3]>,
    z: f32,
    extent: f32,
) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(0x0db, &[f(1.); 4])];
    if let Some([x, y, z]) = normal {
        out.push(call(0x63e, &[f(x), f(y), f(z)]));
    }
    out.push(call(0x01a, &[7]));
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        if let Some([s, t, r]) = coordinates {
            out.push(call(0x8a9, &[f(s), f(t), f(r), f(1.)]));
        }
        out.push(call(0x9ea, &[f(x * extent), f(y * extent), f(z)]));
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
fn integer(qemu: &mut Qemu, sequence: &mut u32, pname: u32) -> u32 {
    words(&query(qemu, sequence, GET_INTEGER, &[pname]).1)[0]
}
fn level(qemu: &mut Qemu, sequence: &mut u32, target: u32, pname: u32) -> u32 {
    words(&query(qemu, sequence, LEVEL_PARAMETER, &[target, 0, pname]).1)[0]
}
fn error(qemu: &mut Qemu, sequence: &mut u32) -> u32 {
    words(&query(qemu, sequence, 0x2fc, &[]).1)[0]
}
fn face_image(target: u32, side: u32, rgba: [u8; 4]) -> (u32, Vec<u32>) {
    data_call(
        IMAGE,
        &[target, 0, 0x8058, side, side, 0, 0x1908, 0x1401],
        &solid(rgba, side as usize),
    )
}
/// A complete NEAREST cube map: six 4x4 faces of one colour each.
fn cube(name: u32) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![
        call(BIND, &[CUBE, name]),
        call(PARAMETER, &[CUBE, 0x2801, 0x2600]),
        call(PARAMETER, &[CUBE, 0x2800, 0x2600]),
        call(PARAMETER, &[CUBE, WRAP_R, CLAMP_TO_EDGE]),
    ];
    for (face, rgba) in FACES.iter().enumerate() {
        out.push(face_image(POSITIVE_X + face as u32, 4, *rgba));
    }
    out
}

#[test]
#[ignore = "requires native host GPU and cube map QEMU producer; diskless acceptance"]
fn qemu_cube_map_faces_texgen_levels_readback_units_and_deletion() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-cube-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 0;
    assert_ne!(qemu.read(0x1008) & 0x100000, 0);
    submit(
        &mut qemu,
        &mut sequence,
        &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])],
    );
    assert_eq!(integer(&mut qemu, &mut sequence, MAX_CUBE_SIZE), 2048);

    // Six faces, each sampled exactly through an explicit STR direction.
    let mut records = cube(20);
    records.push(call(ENABLE, &[CUBE]));
    submit(&mut qemu, &mut sequence, &records);
    for (face, direction) in DIRECTIONS.iter().enumerate() {
        submit(
            &mut qemu,
            &mut sequence,
            &quad(Some(*direction), None, 0., 1.),
        );
        expect(
            &mut qemu,
            &mut sequence,
            FACES[face],
            &format!("face {face}"),
        );
    }
    assert_eq!(error(&mut qemu, &mut sequence), 0);
    assert_eq!(
        query(&mut qemu, &mut sequence, IS_ENABLED, &[CUBE]),
        (DG_GL_RESULT_BOOL, vec![1])
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, GET_PARAMETER, &[CUBE, WRAP_R]).1)[0],
        CLAMP_TO_EDGE
    );
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 20);

    // NORMAL_MAP: the eye-space normal (-Y) selects the -Y face; REFLECTION_MAP
    // of a small quad facing the eye at z=-0.5 reflects into +Z.
    let mut records = vec![call(TEX_GEN, &[S, TEXTURE_GEN_MODE, f(NORMAL_MAP as f32)])];
    for coordinate in [T, R] {
        records.push(call(
            TEX_GEN,
            &[coordinate, TEXTURE_GEN_MODE, f(NORMAL_MAP as f32)],
        ));
    }
    records.extend([GEN_S, GEN_T, GEN_R].map(|cap| call(ENABLE, &[cap])));
    records.extend(quad(None, Some([0.1, -1., 0.2]), 0., 1.));
    submit(&mut qemu, &mut sequence, &records);
    expect(&mut qemu, &mut sequence, FACES[3], "normal map -Y");
    let mut records: Vec<_> = [S, T, R]
        .map(|coordinate| {
            call(
                TEX_GEN,
                &[coordinate, TEXTURE_GEN_MODE, f(REFLECTION_MAP as f32)],
            )
        })
        .into();
    records.extend(quad(None, Some([0., 0., 1.]), -0.5, 0.25));
    submit(&mut qemu, &mut sequence, &records);
    expect(&mut qemu, &mut sequence, FACES[4], "reflection map +Z");
    assert_eq!(
        words(
            &query(
                &mut qemu,
                &mut sequence,
                GET_TEX_GEN,
                &[R, TEXTURE_GEN_MODE]
            )
            .1
        )[0],
        REFLECTION_MAP
    );
    submit(
        &mut qemu,
        &mut sequence,
        &[GEN_S, GEN_T, GEN_R].map(|cap| call(DISABLE, &[cap])),
    );

    // Redefining one face replaces only that face; the others keep theirs.
    let blue_grey = [40, 80, 160, 255];
    submit(
        &mut qemu,
        &mut sequence,
        &[face_image(POSITIVE_X + 4, 4, blue_grey)],
    );
    submit(
        &mut qemu,
        &mut sequence,
        &quad(Some(DIRECTIONS[4]), None, 0., 1.),
    );
    expect(&mut qemu, &mut sequence, blue_grey, "redefined +Z");
    submit(
        &mut qemu,
        &mut sequence,
        &quad(Some(DIRECTIONS[5]), None, 0., 1.),
    );
    expect(&mut qemu, &mut sequence, FACES[5], "untouched -Z");
    // A larger +Z: its level is 8x8 while -Z stays 4x4. A subimage in the
    // larger face's new area succeeds; the same rectangle in -Z is the
    // guest's GL error and the batch (and its draw) completes.
    let olive = [128, 128, 0, 255];
    let mut records = vec![
        face_image(POSITIVE_X + 4, 8, olive),
        data_call(
            SUB_IMAGE,
            &[POSITIVE_X + 4, 0, 4, 4, 4, 4, 0x1908, 0x1401],
            &solid(blue_grey, 4),
        ),
        data_call(
            SUB_IMAGE,
            &[POSITIVE_X + 5, 0, 4, 4, 4, 4, 0x1908, 0x1401],
            &solid(blue_grey, 4),
        ),
    ];
    records.extend(quad(Some(DIRECTIONS[0]), None, 0., 1.));
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(error(&mut qemu, &mut sequence), 0x0501);
    // Faces of different sizes leave the cube map incomplete: native GL then
    // samples as if texturing were disabled, so the quad is drawn white.
    expect(
        &mut qemu,
        &mut sequence,
        [255; 4],
        "incomplete cube map after face errors",
    );
    assert_eq!(level(&mut qemu, &mut sequence, POSITIVE_X + 4, 0x1000), 8);
    assert_eq!(level(&mut qemu, &mut sequence, POSITIVE_X + 5, 0x1000), 4);
    // GetTexImage of one face: the olive face with its blue-grey quadrant.
    let (_, image) = query(
        &mut qemu,
        &mut sequence,
        GET_TEX_IMAGE,
        &[POSITIVE_X + 4, 64 << 16, 0],
    );
    assert_eq!(image.len(), 256);
    for (index, texel) in image.chunks_exact(4).enumerate() {
        let (x, y) = (index % 8, index / 8);
        let expected = if x >= 4 && y >= 4 { blue_grey } else { olive };
        assert_eq!(texel, expected, "+Z texel {x},{y}");
    }
    // A face of the same texture is its own image, not a cached continuation.
    let (_, other) = query(
        &mut qemu,
        &mut sequence,
        GET_TEX_IMAGE,
        &[POSITIVE_X + 1, 16 << 16, 0],
    );
    assert_eq!(other, solid(FACES[1], 4));
    // Back to a complete cube map with a TexSubImage2D replacing -Y entirely.
    let mut records = vec![
        face_image(POSITIVE_X + 4, 4, FACES[4]),
        data_call(
            SUB_IMAGE,
            &[POSITIVE_X + 3, 0, 0, 0, 4, 4, 0x1908, 0x1401],
            &solid([90, 30, 200, 255], 4),
        ),
    ];
    records.extend(quad(Some(DIRECTIONS[3]), None, 0., 1.));
    submit(&mut qemu, &mut sequence, &records);
    expect(&mut qemu, &mut sequence, [90, 30, 200, 255], "replaced -Y");

    // CopyTexSubImage2D into a face, then that face's readback.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(DISABLE, &[CUBE]),
            call(0x0a3, &[f(0.), f(1.), f(0.), f(1.)]),
            call(0x09a, &[0x4000]),
            call(COPY_SUB_IMAGE, &[POSITIVE_X + 2, 0, 1, 1, 0, 0, 2, 2]),
        ],
    );
    let (_, copied) = query(
        &mut qemu,
        &mut sequence,
        GET_TEX_IMAGE,
        &[POSITIVE_X + 2, 16 << 16, 0],
    );
    for (index, texel) in copied.chunks_exact(4).enumerate() {
        let (x, y) = (index % 4, index / 4);
        let inside = (1..3).contains(&x) && (1..3).contains(&y);
        assert_eq!(texel, if inside { [0, 255, 0, 255] } else { FACES[2] });
    }
    // An allocation without pixels reads back as zero, through its face.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(BIND, &[CUBE, 23]),
            data_call(
                IMAGE,
                &[POSITIVE_X + 5, 0, 0x8058, 4, 4, 0, 0x1908, 0x1401],
                &[],
            ),
        ],
    );
    let (_, zero) = query(
        &mut qemu,
        &mut sequence,
        GET_TEX_IMAGE,
        &[POSITIVE_X + 5, 16 << 16, 0],
    );
    assert_eq!(zero, vec![0; 64]);
    // A compressed face: the exact block back through GetCompressedTexImage.
    let block = [0x1fu8, 0xf8, 0xe0, 0x07, 0x1b, 0x6c, 0xb1, 0xc6];
    submit(
        &mut qemu,
        &mut sequence,
        &[data_call(
            COMPRESSED_IMAGE,
            &[POSITIVE_X + 1, 0, DXT1, 4, 4, 0, 8],
            &block,
        )],
    );
    assert_eq!(level(&mut qemu, &mut sequence, POSITIVE_X + 1, 0x86a1), 1);
    let (_, compressed) = query(
        &mut qemu,
        &mut sequence,
        GET_COMPRESSED,
        &[POSITIVE_X + 1, 2 << 16, 0],
    );
    assert_eq!(compressed, block);
    // Proxies: native feasibility within the virtual limit, zero beyond it.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            data_call(
                IMAGE,
                &[PROXY_CUBE, 0, 0x8058, 64, 64, 0, 0x1908, 0x1401],
                &[],
            ),
            data_call(
                IMAGE,
                &[PROXY_CUBE, 1, 0x8058, 2048, 2048, 0, 0x1908, 0x1401],
                &[],
            ),
        ],
    );
    assert_eq!(level(&mut qemu, &mut sequence, PROXY_CUBE, 0x1000), 64);
    assert_eq!(
        words(
            &query(
                &mut qemu,
                &mut sequence,
                LEVEL_PARAMETER,
                &[PROXY_CUBE, 1, 0x1000]
            )
            .1
        )[0],
        0
    );
    assert_eq!(error(&mut qemu, &mut sequence), 0);

    // A display list captures the bind and face uploads for replay; the proxy
    // inside it executes at compile time.
    let list = words(&query(&mut qemu, &mut sequence, GEN_LISTS, &[1, 0, 0]).1)[1];
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, NEW_LIST, &[list, 0x1300, 0]).1),
        [0]
    );
    let teal = [0, 128, 128, 255];
    let mut records = cube(24);
    records[4] = face_image(POSITIVE_X, 4, teal);
    records.push(data_call(
        IMAGE,
        &[PROXY_CUBE, 0, 0x8058, 32, 32, 0, 0x1908, 0x1401],
        &[],
    ));
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, END_LIST, &[0; 3]).1),
        [0]
    );
    assert_eq!(level(&mut qemu, &mut sequence, PROXY_CUBE, 0x1000), 32);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 23);
    let mut records = vec![call(0x092, &[list]), call(ENABLE, &[CUBE])];
    records.extend(quad(Some(DIRECTIONS[0]), None, 0., 1.));
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 24);
    expect(
        &mut qemu,
        &mut sequence,
        teal,
        "face uploaded by a display list",
    );
    submit(&mut qemu, &mut sequence, &[call(DISABLE, &[CUBE])]);

    // Per-unit bindings: unit 1 binds 21; TEXTURE_BIT saves both units.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(BIND, &[CUBE, 20]),
            call(ACTIVE, &[TEXTURE0 + 1]),
            call(BIND, &[CUBE, 21]),
        ],
    );
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 21);
    submit(&mut qemu, &mut sequence, &[call(ACTIVE, &[TEXTURE0])]);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 20);
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(PUSH, &[TEXTURE_BIT]),
            call(BIND, &[CUBE, 0]),
            call(ACTIVE, &[TEXTURE0 + 1]),
            call(BIND, &[CUBE, 22]),
        ],
    );
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 22);
    submit(&mut qemu, &mut sequence, &[call(POP, &[])]);
    assert_eq!(integer(&mut qemu, &mut sequence, ACTIVE_TEXTURE), TEXTURE0);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 20);
    submit(&mut qemu, &mut sequence, &[call(ACTIVE, &[TEXTURE0 + 1])]);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 21);
    // The restored binding samples: unit 0's cube map, enabled, through STR.
    let mut records = vec![call(ACTIVE, &[TEXTURE0]), call(ENABLE, &[CUBE])];
    records.extend(quad(Some(DIRECTIONS[1]), None, 0., 1.));
    submit(&mut qemu, &mut sequence, &records);
    expect(
        &mut qemu,
        &mut sequence,
        FACES[1],
        "restored unit 0 cube map",
    );

    // A cube map name is not a 2D texture: INVALID_OPERATION, binding kept,
    // and the rest of the batch (here a draw) still executes.
    let mut records = vec![call(BIND, &[TEXTURE_2D, 20])];
    records.extend(quad(Some(DIRECTIONS[5]), None, 0., 1.));
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(error(&mut qemu, &mut sequence), 0x0502);
    expect(
        &mut qemu,
        &mut sequence,
        FACES[5],
        "draw after target mismatch",
    );
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 20);
    assert_eq!(integer(&mut qemu, &mut sequence, 0x8069), 0);

    // Deleting reverts every unit holding the name to the default cube map:
    // 21 on the inactive unit 1, 20 on the active unit 0.
    let names: Vec<u8> = [20u32, 21].iter().flat_map(|n| n.to_le_bytes()).collect();
    submit(&mut qemu, &mut sequence, &[data_call(DELETE, &[2], &names)]);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 0);
    submit(&mut qemu, &mut sequence, &[call(ACTIVE, &[TEXTURE0 + 1])]);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_CUBE), 0);
    // The freed name is new again: it may now become a 2D texture.
    submit(
        &mut qemu,
        &mut sequence,
        &[call(ACTIVE, &[TEXTURE0]), call(BIND, &[TEXTURE_2D, 20])],
    );
    assert_eq!(integer(&mut qemu, &mut sequence, 0x8069), 20);
    assert_eq!(error(&mut qemu, &mut sequence), 0);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
