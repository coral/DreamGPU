// SPDX-License-Identifier: GPL-2.0-or-later
//! Real selection hits, clipped primitives, feedback attributes, overflow, and
//! retained-buffer mode transitions. Neither mode may modify framebuffer pixels.
use super::textures::{call, query, query_capacity, words};
use super::*;
const SELECT: u32 = 0x81e;
const FEEDBACK: u32 = 0x23b;
const MODE: u32 = 0x7b9;
const INIT: u32 = 0x4a7;
const LOAD: u32 = 0x505;
const PUSH: u32 = 0x77b;
const POP: u32 = 0x6b4;
const PASS: u32 = 0x663;
const RENDER: u32 = 0x1c00;
const SELECT_MODE: u32 = 0x1c02;
const FEEDBACK_MODE: u32 = 0x1c01;
fn submit(q: &mut Qemu, s: &mut u32, r: &[(u32, Vec<u32>)]) {
    *s += 1;
    q.batch(*s, r);
    q.await_completion(*s);
}
fn get(q: &mut Qemu, s: &mut u32, f: u32, a: &[u32]) -> Vec<u32> {
    words(&query_capacity(q, s, f, a, 65536).1)
}
fn vertex(x: f32, y: f32, z: f32) -> (u32, Vec<u32>) {
    call(2538, &[x.to_bits(), y.to_bits(), z.to_bits()])
}
fn points(p: &[[f32; 3]]) -> Vec<(u32, Vec<u32>)> {
    let mut r = vec![call(26, &[0])];
    r.extend(p.iter().map(|p| vertex(p[0], p[1], p[2])));
    r.push(call(534, &[]));
    r
}
fn pixels(q: &mut Qemu, s: &mut u32) -> Vec<u8> {
    query_capacity(q, s, 1956, &[0, 0, 32 | (32 << 16)], 4096).1
}
#[test]
#[ignore = "requires native GPU; bounded selection and feedback buffers"]
fn qemu_selection_feedback_native_records_overflow_and_lifetime() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dgsel-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&dir).unwrap();
    let socket = dir.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut q = Qemu::start(&dir, &socket);
    let mut s = 0;
    submit(
        &mut q,
        &mut s,
        &[
            (1, vec![0]),
            (3, vec![32, 32]),
            (5, vec![]),
            call(453, &[0x0bd0]),
            call(163, &[0, 0, 0, 1f32.to_bits()]),
            call(154, &[0x4000]),
        ],
    );
    let original = pixels(&mut q, &mut s);
    assert_eq!(
        get(&mut q, &mut s, MODE, &[SELECT_MODE, 0, 0]),
        [0x502, 0, 0]
    );
    assert_eq!(get(&mut q, &mut s, SELECT, &[64, 0, 0]), [0]);
    assert_eq!(get(&mut q, &mut s, MODE, &[SELECT_MODE, 0, 0]), [0, 0, 0]);
    assert_eq!(get(&mut q, &mut s, SELECT, &[32, 0, 0]), [0x502]);
    submit(
        &mut q,
        &mut s,
        &[
            call(INIT, &[]),
            call(PUSH, &[0xf1234567]),
            call(PUSH, &[0xabcdef01]),
        ],
    );
    submit(&mut q, &mut s, &points(&[[0., 0., 0.], [0.5, 0.5, 0.5]]));
    submit(&mut q, &mut s, &[call(LOAD, &[42]), call(POP, &[])]);
    submit(&mut q, &mut s, &points(&[[3., 0., 0.]])); // clipped: no hit
    submit(&mut q, &mut s, &points(&[[-0.5, -0.5, -0.5]]));
    let header = get(&mut q, &mut s, MODE, &[RENDER, 0, 0]);
    assert_eq!(header, [0, 2, 9]);
    let hits = get(&mut q, &mut s, MODE, &[0, 0, 9]);
    assert_eq!(hits[0], 2);
    assert_eq!(&hits[3..5], &[0xf1234567, 0xabcdef01]);
    assert_eq!(hits[5], 1);
    assert_eq!(hits[8], 0xf1234567);
    assert!((0x7fffff00..=0x80000100).contains(&hits[1]));
    assert!((0xbfffff00..=0xc0000100).contains(&hits[2]));
    assert!((0x3fffff00..=0x40000100).contains(&hits[6]));
    assert_eq!(hits[6], hits[7]);
    assert_eq!(pixels(&mut q, &mut s), original);
    assert!(get(&mut q, &mut s, 809, &[0x0d37])[0] >= 64); // real name stack capacity
    assert_eq!(get(&mut q, &mut s, SELECT, &[2, 0, 0]), [0]);
    get(&mut q, &mut s, MODE, &[SELECT_MODE, 0, 0]);
    submit(&mut q, &mut s, &[call(INIT, &[]), call(PUSH, &[9])]);
    submit(&mut q, &mut s, &points(&[[0., 0., 0.]]));
    assert_eq!(get(&mut q, &mut s, MODE, &[RENDER, 0, 0]), [0, u32::MAX, 2]);
    assert_eq!(get(&mut q, &mut s, MODE, &[0, 0, 2])[0], 1);
    // Every mandated feedback layout uses the actual native vertex attributes.
    for (kind, n) in [(0x600, 2), (0x601, 3), (0x602, 7), (0x603, 11), (0x604, 12)] {
        assert_eq!(get(&mut q, &mut s, FEEDBACK, &[64, kind, 0]), [0]);
        assert_eq!(get(&mut q, &mut s, MODE, &[FEEDBACK_MODE, 0, 0]), [0, 0, 0]);
        submit(
            &mut q,
            &mut s,
            &[
                call(219, &[1f32.to_bits(), 0, 0, 1f32.to_bits()]),
                call(PASS, &[(-0.25f32).to_bits()]),
            ],
        );
        submit(&mut q, &mut s, &points(&[[0., 0., 0.], [3., 0., 0.]]));
        assert_eq!(
            get(&mut q, &mut s, MODE, &[RENDER, 0, 0]),
            [0, 3 + n, 3 + n]
        );
        let values: Vec<f32> = get(&mut q, &mut s, MODE, &[0, 0, 3 + n])
            .into_iter()
            .map(f32::from_bits)
            .collect();
        assert_eq!(&values[..5], &[0x700 as f32, -0.25, 0x701 as f32, 16., 16.]);
        if n >= 3 {
            assert_eq!(values[5], 0.5);
        }
        let color = if n == 12 { 7 } else { 6 };
        if n >= 7 {
            assert_eq!(&values[color..color + 4], &[1., 0., 0., 1.]);
        }
        if n >= 11 {
            assert_eq!(&values[values.len() - 4..], &[0., 0., 0., 1.]);
        }
    }
    assert_eq!(pixels(&mut q, &mut s), original);
    assert_eq!(get(&mut q, &mut s, FEEDBACK, &[1, 0x600, 0]), [0]);
    get(&mut q, &mut s, MODE, &[FEEDBACK_MODE, 0, 0]);
    submit(&mut q, &mut s, &[call(PASS, &[7f32.to_bits()])]);
    assert_eq!(get(&mut q, &mut s, MODE, &[RENDER, 0, 0]), [0, u32::MAX, 1]);
    assert_eq!(
        get(&mut q, &mut s, MODE, &[0, 0, 1]),
        [(0x700 as f32).to_bits()]
    );
    get(&mut q, &mut s, SELECT, &[64, 0, 0]);
    get(&mut q, &mut s, MODE, &[SELECT_MODE, 0, 0]);
    submit(&mut q, &mut s, &[call(INIT, &[]), call(POP, &[])]);
    assert_eq!(words(&query(&mut q, &mut s, 764, &[]).1), [0x504]);
    submit(&mut q, &mut s, &[call(LOAD, &[1])]);
    assert_eq!(words(&query(&mut q, &mut s, 764, &[]).1), [0x502]);
    get(&mut q, &mut s, MODE, &[RENDER, 0, 0]);
    assert_eq!(words(&query(&mut q, &mut s, 764, &[]).1), [0]);
    // Configured empty buffers preserve native overflow without exposing their
    // private safety word; true queried client size is zero in both modes.
    assert_eq!(get(&mut q, &mut s, SELECT, &[0, 0, 0]), [0]);
    assert_eq!(get(&mut q, &mut s, 809, &[0xdf4]), [0]);
    assert_eq!(get(&mut q, &mut s, MODE, &[SELECT_MODE, 0, 0]), [0, 0, 0]);
    submit(&mut q, &mut s, &points(&[[0., 0., 0.]]));
    assert_eq!(get(&mut q, &mut s, MODE, &[RENDER, 0, 0]), [0, u32::MAX, 0]);
    assert_eq!(get(&mut q, &mut s, FEEDBACK, &[0, 0x600, 0]), [0]);
    assert_eq!(get(&mut q, &mut s, 809, &[0xdf1]), [0]);
    assert_eq!(get(&mut q, &mut s, MODE, &[FEEDBACK_MODE, 0, 0]), [0, 0, 0]);
    submit(&mut q, &mut s, &[call(PASS, &[1f32.to_bits()])]);
    assert_eq!(get(&mut q, &mut s, MODE, &[RENDER, 0, 0]), [0, u32::MAX, 0]);
    // Teardown while native GL still retains the buffer is an exercised cleanup path.
    get(&mut q, &mut s, MODE, &[FEEDBACK_MODE, 0, 0]);
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}
