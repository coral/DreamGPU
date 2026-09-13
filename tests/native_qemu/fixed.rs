// SPDX-License-Identifier: GPL-2.0-or-later
//! Exact RGBA stipple/logic/edge pixels and true no-accumulation-buffer state.
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;
const ACCUM: u32 = 0;
const CLEARACCUM: u32 = 155;
const CLEARINDEX: u32 = 172;
const EDGEFLAG: u32 = 511;
const GETPOLYGONSTIPPLE: u32 = 965;
const INDEXD: u32 = 1172;
const INDEXMASK: u32 = 1180;
const LOGICOP: u32 = 1293;
const POLYGONSTIPPLE: u32 = 1710;
const POLYGONMODE: u32 = 1704;
const ENABLE: u32 = 521;
const DISABLE: u32 = 453;
const BEGIN: u32 = 26;
const END: u32 = 534;
const VERTEX2F: u32 = 2524;
const COLOR4F: u32 = 219;
const CLEAR: u32 = 154;
const CLEARCOLOR: u32 = 163;
const DRAWARRAYS: u32 = 469;
const GETINTEGERV: u32 = 809;
const GETFLOATV: u32 = 773;
const GETERROR: u32 = 764;
const READPIXELS: u32 = 1956;
const PUSHATTRIB: u32 = 1909;
const POPATTRIB: u32 = 1711;

fn submit(q: &mut Qemu, s: &mut u32, r: &[(u32, Vec<u32>)]) {
    *s += 1;
    q.batch(*s, r);
    q.await_completion(*s);
}
fn pixels(q: &mut Qemu, s: &mut u32) -> Vec<u8> {
    query_capacity(q, s, READPIXELS, &[0, 0, 32 | (32 << 16)], 4096).1
}
fn clear(q: &mut Qemu, s: &mut u32) {
    submit(
        q,
        s,
        &[
            call(CLEARCOLOR, &[0, 0, 0, 1f32.to_bits()]),
            call(CLEAR, &[0x4000]),
        ],
    );
}
fn quad() -> Vec<(u32, Vec<u32>)> {
    let mut r = vec![call(COLOR4F, &[1f32.to_bits(); 4]), call(BEGIN, &[7])];
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        r.push(call(VERTEX2F, &[x.to_bits(), y.to_bits()]));
    }
    r.push(call(END, &[]));
    r
}
fn integer(q: &mut Qemu, s: &mut u32, name: u32) -> Vec<u32> {
    words(&query(q, s, GETINTEGERV, &[name]).1)
}
#[test]
#[ignore = "requires native GPU; fixed-state/edge-array acceptance"]
fn qemu_fixed_stipple_logic_edge_index_and_absent_accum() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-fixed-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&dir).unwrap();
    let socket = dir.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut q = Qemu::start(&dir, &socket);
    let mut seq = 0;
    submit(
        &mut q,
        &mut seq,
        &[
            (1, vec![0]),
            (3, vec![32, 32]),
            (5, vec![]),
            call(DISABLE, &[0x0bd0]),
        ],
    );
    // The offered RGBA visual has no accumulation buffer: query it, do not synthesize capacity.
    for name in 0x0d58..=0x0d5b {
        assert_eq!(integer(&mut q, &mut seq, name), [0]);
    }
    submit(
        &mut q,
        &mut seq,
        &[call(
            CLEARACCUM,
            &[
                (-2f32).to_bits(),
                0.25f32.to_bits(),
                0.5f32.to_bits(),
                2f32.to_bits(),
            ],
        )],
    );
    assert_eq!(
        words(&query(&mut q, &mut seq, GETFLOATV, &[0x0b80]).1),
        [-1f32, 0.25, 0.5, 1.].map(f32::to_bits)
    );
    clear(&mut q, &mut seq);
    let before = pixels(&mut q, &mut seq);
    for op in 0x100..=0x104 {
        submit(&mut q, &mut seq, &[call(ACCUM, &[op, 1f32.to_bits()])]);
        assert_eq!(words(&query(&mut q, &mut seq, GETERROR, &[]).1), [0x502]);
    }
    assert_eq!(pixels(&mut q, &mut seq), before);
    submit(
        &mut q,
        &mut seq,
        &[
            call(CLEARINDEX, &[7f32.to_bits()]),
            call(INDEXMASK, &[0x1357]),
            call(INDEXD, &[0, 2.25f64.to_bits().wrapping_shr(32) as u32]),
        ],
    );
    assert_eq!(integer(&mut q, &mut seq, 0x0c20), [7]);
    assert_eq!(integer(&mut q, &mut seq, 0x0c21), [0x1357]);
    assert_eq!(
        words(&query(&mut q, &mut seq, GETFLOATV, &[0x0b01]).1),
        [2.25f32.to_bits()]
    );
    // Canonical stipple bytes reach actual native polygon rasterization.
    let pattern: Vec<u8> = (0..32)
        .flat_map(|y| [if y % 2 == 0 { 0xaa } else { 0x55 }; 4])
        .collect();
    submit(
        &mut q,
        &mut seq,
        &[data_call(POLYGONSTIPPLE, &[], &pattern)],
    );
    assert_eq!(query(&mut q, &mut seq, GETPOLYGONSTIPPLE, &[]).1, pattern);
    submit(
        &mut q,
        &mut seq,
        &[
            call(PUSHATTRIB, &[0x10]),
            data_call(POLYGONSTIPPLE, &[], &[0; 128]),
            call(POPATTRIB, &[]),
        ],
    );
    assert_eq!(query(&mut q, &mut seq, GETPOLYGONSTIPPLE, &[]).1, pattern);
    submit(&mut q, &mut seq, &[call(ENABLE, &[0x0b42])]);
    submit(&mut q, &mut seq, &quad());
    let rendered = pixels(&mut q, &mut seq);
    for y in 0..32 {
        for x in 0..32 {
            let expected = if x % 2 == y % 2 { 255 } else { 0 };
            assert_eq!(
                &rendered[(y * 32 + x) * 4..(y * 32 + x) * 4 + 3],
                &[expected; 3],
                "stipple {x},{y}"
            );
        }
    }
    // XOR exercises actual fixed-function framebuffer logic, including repeatability.
    submit(
        &mut q,
        &mut seq,
        &[
            call(DISABLE, &[0x0b42]),
            call(LOGICOP, &[0x1506]),
            call(ENABLE, &[0x0bf2]),
        ],
    );
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &quad());
    assert_eq!(&pixels(&mut q, &mut seq)[0..3], &[255; 3]);
    submit(&mut q, &mut seq, &quad());
    assert_eq!(&pixels(&mut q, &mut seq)[0..3], &[0; 3]);
    assert_eq!(integer(&mut q, &mut seq, 0x0bf0), [0x1506]);
    submit(
        &mut q,
        &mut seq,
        &[
            call(DISABLE, &[0x0bf2]),
            call(POLYGONMODE, &[0x0408, 0x1b01]),
        ],
    );
    clear(&mut q, &mut seq);
    let vertices = [(-0.75f32, -0.75f32), (0.75, -0.75), (-0.75, 0.75)];
    let mut r = vec![call(BEGIN, &[4])];
    for (i, (x, y)) in vertices.iter().enumerate() {
        r.push(call(EDGEFLAG, &[u32::from(i == 0)]));
        r.push(call(VERTEX2F, &[x.to_bits(), y.to_bits()]));
    }
    r.push(call(END, &[]));
    submit(&mut q, &mut seq, &r);
    let immediate = pixels(&mut q, &mut seq);
    assert!(immediate.as_chunks::<4>().0.iter().any(|p| p[0] == 255));
    for y in 8..32 {
        for x in 0..32 {
            assert_eq!(immediate[(y * 32 + x) * 4], 0, "suppressed triangle edge");
        }
    }
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &[call(EDGEFLAG, &[1])]);
    let mut array = vec![0u8; 96 * 3];
    for (i, (x, y)) in vertices.iter().enumerate() {
        for (n, v) in [*x, *y, 0., 1.].iter().enumerate() {
            array[i * 96 + n * 4..i * 96 + n * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        array[i * 96 + 80..i * 96 + 88].copy_from_slice(&(123.5 + i as f64).to_le_bytes());
        array[i * 96 + 88] = u8::from(i == 0);
    }
    submit(
        &mut q,
        &mut seq,
        &[data_call(DRAWARRAYS, &[4, 0, 3, 1 | 32 | 64], &array)],
    );
    assert_eq!(pixels(&mut q, &mut seq), immediate);
    assert_eq!(integer(&mut q, &mut seq, 0x0b43), [1]);
    assert_eq!(
        words(&query(&mut q, &mut seq, GETFLOATV, &[0x0b01]).1),
        [2.25f32.to_bits()]
    );
    assert_eq!(words(&query(&mut q, &mut seq, GETERROR, &[]).1), [0]);
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}
