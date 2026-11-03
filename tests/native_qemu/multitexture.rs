//! ARB_multitexture through the device: per-unit bindings and environments,
//! MultiTexCoord inside Begin/End and display lists, sixteen-slot arrays with
//! per-unit current-coordinate restoration, TEXTURE_BIT and deletion.
use super::textures::{call, data_call, query, words};
use super::*;

const ACTIVE: u32 = 0x006;
const MULTI: u32 = 0x5cf;
const DRAW_EXT: u32 = 0x1d6;
const TEXTURE0: u32 = 0x84c0;
const TEXTURE_2D: u32 = 0x0de1;
const ENV: u32 = 0x2300;
const ENV_MODE: u32 = 0x2200;
const MODULATE: u32 = 0x2100;
const COMBINE: u32 = 0x8570;
const COMBINE_RGB: u32 = 0x8571;
const SOURCE0_RGB: u32 = 0x8580;
const SOURCE1_RGB: u32 = 0x8581;
const PREVIOUS: u32 = 0x8578;
const TEXTURE: u32 = 0x1702;
const ADD: u32 = 0x0104;
const DOT3_RGB: u32 = 0x86ae;
const ACTIVE_TEXTURE: u32 = 0x84e0;
const MAX_UNITS: u32 = 0x84e2;
const BINDING_2D: u32 = 0x8069;
const CURRENT_COORDS: u32 = 0x0b03;
const TEXTURE_BIT: u32 = 0x40000;

const BASE: [u8; 4] = [128, 64, 32, 255];
const SECOND: [u8; 4] = [128, 255, 64, 255];
const DOT: [u8; 4] = [200, 60, 100, 255];

fn submit(qemu: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    qemu.batch(*sequence, records);
    qemu.await_completion(*sequence);
}

fn texture(unit: u32, name: u32, rgba: [u8; 4]) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(ACTIVE, &[TEXTURE0 + unit]),
        call(0x04e, &[TEXTURE_2D, name]),
        call(0x8df, &[TEXTURE_2D, 0x2801, 0x2600]),
        call(0x8df, &[TEXTURE_2D, 0x2800, 0x2600]),
        data_call(
            0x8d4,
            &[TEXTURE_2D, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401],
            &rgba,
        ),
        call(0x209, &[TEXTURE_2D]),
        call(0x8c6, &[ENV, ENV_MODE, MODULATE]),
    ]
}

fn combine(op: u32) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(0x8c6, &[ENV, ENV_MODE, COMBINE]),
        call(0x8c6, &[ENV, COMBINE_RGB, op]),
        call(0x8c6, &[ENV, SOURCE0_RGB, PREVIOUS]),
        call(0x8c6, &[ENV, SOURCE1_RGB, TEXTURE]),
    ]
}

/// White quad with coordinates on units 0 (TexCoord) and 1 (MultiTexCoord).
fn quad() -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(0x0db, &[1f32.to_bits(); 4]), call(0x01a, &[7])];
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        out.push(call(0x883, &[0.5f32.to_bits(), 0.5f32.to_bits()]));
        out.push(call(
            MULTI,
            &[
                TEXTURE0 + 1,
                0.5f32.to_bits(),
                0.5f32.to_bits(),
                0,
                1f32.to_bits(),
            ],
        ));
        out.push(call(0x9dc, &[x.to_bits(), y.to_bits()]));
    }
    out.push(call(0x216, &[]));
    out
}

/// Sixteen descriptors: FLOAT2 position, FLOAT2 units 0 and 1, then sections.
fn arrays() -> Vec<u8> {
    let mut bytes = vec![0u8; 64];
    for slot in [0, 3, 7] {
        bytes[slot * 4..slot * 4 + 4].copy_from_slice(&(0x1406u32 | (2 << 16)).to_le_bytes());
    }
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        bytes.extend([x, y].iter().flat_map(|v| v.to_le_bytes()));
    }
    for _ in 0..2 {
        for _ in 0..4 {
            bytes.extend([0.5f32, 0.5].iter().flat_map(|v| v.to_le_bytes()));
        }
    }
    bytes
}

fn expect(qemu: &mut Qemu, sequence: &mut u32, rgb: [u8; 3], label: &str) {
    let (_, result) = query(qemu, sequence, 0x7a4, &[0, 0, 8 | (8 << 16)]);
    assert_eq!(result.len(), 256, "{label}");
    for pixel in result.as_chunks::<4>().0 {
        for c in 0..3 {
            let delta = i32::from(pixel[c]) - i32::from(rgb[c]);
            assert!(
                (-2..=2).contains(&delta),
                "{label}: pixel {pixel:?} expected {rgb:?}"
            );
        }
    }
}

fn modulate(a: [u8; 4], b: [u8; 4]) -> [u8; 3] {
    [0, 1, 2].map(|i| ((u32::from(a[i]) * u32::from(b[i]) + 127) / 255) as u8)
}

fn gl_error(qemu: &mut Qemu, sequence: &mut u32) -> u32 {
    words(&query(qemu, sequence, 0x2fc, &[]).1)[0]
}

fn integer(qemu: &mut Qemu, sequence: &mut u32, pname: u32) -> u32 {
    words(&query(qemu, sequence, 0x329, &[pname]).1)[0]
}

#[test]
#[ignore = "requires native host GPU and multitexture QEMU producer; diskless acceptance"]
fn qemu_multitexture_units_environments_arrays_lists_and_attributes() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-mt-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 0;
    // The capability and every new function width are advertised by the device.
    assert_ne!(qemu.read(0x1008) & 0x8000, 0);
    for (function, words) in [(ACTIVE, 1), (MULTI, 5), (DRAW_EXT, 0x8000_0004)] {
        qemu.write(0x1130, function);
        assert_eq!(qemu.read(0x1134) & 0x8000_00ff, words, "{function:#x}");
    }
    submit(
        &mut qemu,
        &mut sequence,
        &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])],
    );
    let units = integer(&mut qemu, &mut sequence, MAX_UNITS);
    assert!((2..=8).contains(&units), "units {units}");
    assert_eq!(integer(&mut qemu, &mut sequence, ACTIVE_TEXTURE), TEXTURE0);

    // Immediate mode: two units modulate.
    let mut records = texture(0, 17, BASE);
    records.extend(texture(1, 18, SECOND));
    records.push(call(ACTIVE, &[TEXTURE0]));
    records.extend(quad());
    submit(&mut qemu, &mut sequence, &records);
    // MODULATE with a white primary leaves unit 0 exactly at BASE.
    let product = modulate(BASE, SECOND);
    expect(&mut qemu, &mut sequence, product, "two-unit modulate");
    assert_eq!(gl_error(&mut qemu, &mut sequence), 0);

    // Unit 1 COMBINE ADD, then DOT3 with a different texture.
    let mut records = vec![call(ACTIVE, &[TEXTURE0 + 1])];
    records.extend(combine(ADD));
    records.push(call(ACTIVE, &[TEXTURE0]));
    records.extend(quad());
    submit(&mut qemu, &mut sequence, &records);
    let added = [0, 1, 2].map(|i| (u32::from(BASE[i]) + u32::from(SECOND[i])).min(255) as u8);
    expect(&mut qemu, &mut sequence, added, "combine add");
    let mut records = texture(1, 19, DOT);
    records.extend(combine(DOT3_RGB));
    records.extend(quad());
    submit(&mut qemu, &mut sequence, &records);
    let centered = |v: u8| 2.0 * f32::from(v) / 255.0 - 1.0;
    let dot = (0..3)
        .map(|i| centered(BASE[i]) * centered(DOT[i]))
        .sum::<f32>();
    let dot = (dot.clamp(0.0, 1.0) * 255.0).round() as u8;
    expect(&mut qemu, &mut sequence, [dot; 3], "combine dot3");
    assert_eq!(
        integer(&mut qemu, &mut sequence, ACTIVE_TEXTURE),
        TEXTURE0 + 1
    );
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_2D), 19);

    // Back to modulate on unit 1 with texture 18 for the remaining checks.
    let records = vec![
        call(0x04e, &[TEXTURE_2D, 18]),
        call(0x8c6, &[ENV, ENV_MODE, MODULATE]),
        call(
            MULTI,
            &[
                TEXTURE0 + 1,
                0.25f32.to_bits(),
                0.75f32.to_bits(),
                0,
                1f32.to_bits(),
            ],
        ),
        call(ACTIVE, &[TEXTURE0]),
    ];
    submit(&mut qemu, &mut sequence, &records);

    // Sixteen-slot arrays draw both units and restore unit 1's current coordinates.
    submit(
        &mut qemu,
        &mut sequence,
        &[data_call(
            DRAW_EXT,
            &[7, 0, 4, 0x8000_0000 | 1 | 8 | 0x80],
            &arrays(),
        )],
    );
    expect(&mut qemu, &mut sequence, product, "multitexture arrays");
    submit(&mut qemu, &mut sequence, &[call(ACTIVE, &[TEXTURE0 + 1])]);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x305, &[CURRENT_COORDS]).1),
        [0.25f32, 0.75, 0., 1.].map(f32::to_bits)
    );
    assert_eq!(
        integer(&mut qemu, &mut sequence, ACTIVE_TEXTURE),
        TEXTURE0 + 1
    );

    // An out-of-range unit is GL_INVALID_ENUM and leaves the active unit alone.
    submit(
        &mut qemu,
        &mut sequence,
        &[call(ACTIVE, &[TEXTURE0 + units])],
    );
    assert_eq!(gl_error(&mut qemu, &mut sequence), 0x500);
    assert_eq!(
        integer(&mut qemu, &mut sequence, ACTIVE_TEXTURE),
        TEXTURE0 + 1
    );

    // TEXTURE_BIT restores every unit's binding and the active unit.
    let mut records = vec![
        call(0x775, &[TEXTURE_BIT]),
        call(0x04e, &[TEXTURE_2D, 19]),
        call(ACTIVE, &[TEXTURE0]),
        call(0x04e, &[TEXTURE_2D, 18]),
        call(0x6af, &[]),
        call(ACTIVE, &[TEXTURE0]),
    ];
    records.extend(quad());
    submit(&mut qemu, &mut sequence, &records);
    expect(&mut qemu, &mut sequence, product, "attribute restoration");
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_2D), 17);
    submit(&mut qemu, &mut sequence, &[call(ACTIVE, &[TEXTURE0 + 1])]);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_2D), 18);

    // A display list records unit selection, environment and MultiTexCoord.
    // NewList/EndList are sole-record queries returning the list error.
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x638, &[5, 0x1300, 0]).1),
        [0]
    );
    let mut list = vec![call(ACTIVE, &[TEXTURE0 + 1])];
    list.extend(combine(ADD));
    list.push(call(ACTIVE, &[TEXTURE0]));
    list.extend(quad());
    submit(&mut qemu, &mut sequence, &list);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x21b, &[0; 3]).1),
        [0]
    );
    // Compilation did not execute: unit 1 is still active and modulating.
    assert_eq!(
        integer(&mut qemu, &mut sequence, ACTIVE_TEXTURE),
        TEXTURE0 + 1
    );
    submit(&mut qemu, &mut sequence, &[call(0x092, &[5])]);
    expect(&mut qemu, &mut sequence, added, "display list replay");
    assert_eq!(integer(&mut qemu, &mut sequence, ACTIVE_TEXTURE), TEXTURE0);

    // Deleting a texture bound only on inactive unit 1 reverts that unit.
    let mut records = vec![
        call(ACTIVE, &[TEXTURE0 + 1]),
        call(0x8c6, &[ENV, ENV_MODE, MODULATE]),
        call(ACTIVE, &[TEXTURE0]),
        data_call(0x1b0, &[1], &18u32.to_le_bytes()),
    ];
    records.extend(quad());
    submit(&mut qemu, &mut sequence, &records);
    expect(
        &mut qemu,
        &mut sequence,
        modulate(BASE, [255; 4]),
        "deleted unit 1",
    );
    submit(&mut qemu, &mut sequence, &[call(ACTIVE, &[TEXTURE0 + 1])]);
    assert_eq!(integer(&mut qemu, &mut sequence, BINDING_2D), 0);
    assert_eq!(gl_error(&mut qemu, &mut sequence), 0);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
