// SPDX-License-Identifier: GPL-2.0-or-later
//! Native curves, surfaces, map queries, evaluator grid state and automatic normals.
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;
const MAP1D: u32 = 1306;
const MAP1F: u32 = 1307;
const MAP2D: u32 = 1309;
const MAP2F: u32 = 1310;
const MAPGRID1D: u32 = 1316;
const MAPGRID2D: u32 = 1319;
const EVALCOORD1D: u32 = 551;
const EVALCOORD2D: u32 = 557;
const EVALPOINT1: u32 = 566;
const EVALPOINT2: u32 = 567;
const EVALMESH1: u32 = 564;
const EVALMESH2: u32 = 565;
const GETMAPDV: u32 = 827;
const GETMAPFV: u32 = 828;
const GETMAPIV: u32 = 829;
const GETINTEGERV: u32 = 809;
const GETFLOATV: u32 = 773;
const GETERROR: u32 = 764;
const READPIXELS: u32 = 1956;
const ENABLE: u32 = 521;
const DISABLE: u32 = 453;
const BEGIN: u32 = 26;
const END: u32 = 534;
const VERTEX3F: u32 = 2538;
const COLOR4F: u32 = 219;
const CLEAR: u32 = 154;
const CLEARCOLOR: u32 = 163;
const LIGHTMODELFV: u32 = 1262;
const MATERIALFV: u32 = 1335;
const PUSHATTRIB: u32 = 1909;
const POPATTRIB: u32 = 1711;
const NORMAL3F: u32 = 1598;

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
fn doubles(v: &[f64]) -> Vec<u32> {
    v.iter()
        .flat_map(|v| {
            let b = v.to_bits();
            [b as u32, (b >> 32) as u32]
        })
        .collect()
}
fn grid1(n: u32) -> (u32, Vec<u32>) {
    let mut a = vec![n];
    a.extend(doubles(&[0., 1.]));
    call(MAPGRID1D, &a)
}
fn grid2(n: u32) -> (u32, Vec<u32>) {
    let mut a = vec![n];
    a.extend(doubles(&[0., 1.]));
    a.push(n);
    a.extend(doubles(&[0., 1.]));
    call(MAPGRID2D, &a)
}
fn mapd(f: u32, target: u32, orders: &[u32], domain: &[f64], points: &[f64]) -> (u32, Vec<u32>) {
    let mut a = vec![target];
    a.extend(orders);
    let mut p: Vec<u8> = domain.iter().flat_map(|v| v.to_le_bytes()).collect();
    p.extend(points.iter().flat_map(|v| v.to_le_bytes()));
    data_call(f, &a, &p)
}
fn mapf(f: u32, target: u32, orders: &[u32], domain: &[f32], points: &[f32]) -> (u32, Vec<u32>) {
    let mut a = vec![target];
    a.extend(orders);
    let mut p: Vec<u8> = domain.iter().flat_map(|v| v.to_le_bytes()).collect();
    p.extend(points.iter().flat_map(|v| v.to_le_bytes()));
    data_call(f, &a, &p)
}
fn qd(q: &mut Qemu, s: &mut u32, target: u32, pname: u32, count: u32) -> Vec<f64> {
    query_capacity(q, s, GETMAPDV, &[target, pname, count], 2048)
        .1
        .as_chunks::<8>()
        .0
        .iter()
        .map(|v| f64::from_le_bytes(*v))
        .collect()
}
fn explicit(mode: u32, points: &[[f32; 3]]) -> Vec<(u32, Vec<u32>)> {
    let mut r = vec![
        call(COLOR4F, &[1f32.to_bits(), 0, 0, 1f32.to_bits()]),
        call(BEGIN, &[mode]),
    ];
    for p in points {
        r.push(call(VERTEX3F, &p.map(f32::to_bits)));
    }
    r.push(call(END, &[]));
    r
}
#[test]
#[ignore = "requires native GPU; complete evaluator acceptance"]
fn qemu_evaluator_maps_grids_curves_surfaces_and_queries() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-eval-{}", uuid::Uuid::new_v4().simple()));
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
    assert_eq!(
        words(&query(&mut q, &mut seq, GETINTEGERV, &[0x0d30]).1),
        [8]
    );
    // Unequal orders prove coefficient ordering; both native input precisions are exercised.
    let coeff: Vec<f64> = (0..18).map(|i| i as f64 + 0.25).collect();
    submit(
        &mut q,
        &mut seq,
        &[mapd(MAP2D, 0x0db7, &[2, 3], &[-1., 2., 0.25, 0.75], &coeff)],
    );
    assert_eq!(
        words(&query(&mut q, &mut seq, GETMAPIV, &[0x0db7, 0x0a01, 2]).1),
        [2, 3]
    );
    assert_eq!(
        qd(&mut q, &mut seq, 0x0db7, 0x0a02, 4),
        [-1., 2., 0.25, 0.75]
    );
    assert_eq!(qd(&mut q, &mut seq, 0x0db7, 0x0a00, 18), coeff);
    assert_eq!(
        words(&query(&mut q, &mut seq, GETMAPFV, &[0x0db7, 0x0a00, 18]).1),
        (0..18)
            .map(|i| (i as f32 + 0.25).to_bits())
            .collect::<Vec<_>>()
    );
    assert_eq!(
        words(&query(&mut q, &mut seq, GETMAPIV, &[0x0db7, 0x0a00, 18]).1),
        (0..18).collect::<Vec<u32>>()
    );
    let curve = [-0.75, 0., 0., 0., 0.75, 0., 0.75, 0., 0.];
    submit(
        &mut q,
        &mut seq,
        &[
            mapd(MAP1D, 0x0d97, &[3], &[0., 1.], &curve),
            mapf(MAP1F, 0x0d90, &[1], &[0., 1.], &[1., 0., 0., 1.]),
            call(ENABLE, &[0x0d97]),
            call(ENABLE, &[0x0d90]),
            grid1(2),
        ],
    );
    let expected = [[-0.75f32, 0., 0.], [0., 0.375, 0.], [0.75, 0., 0.]];
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &explicit(0, &expected));
    let reference = pixels(&mut q, &mut seq);
    assert!(reference.as_chunks::<4>().0.iter().any(|v| v[0] == 255));
    clear(&mut q, &mut seq);
    submit(
        &mut q,
        &mut seq,
        &[
            call(COLOR4F, &[0, 1f32.to_bits(), 0, 1f32.to_bits()]),
            call(BEGIN, &[0]),
            call(EVALCOORD1D, &doubles(&[0.])),
            call(EVALCOORD1D, &doubles(&[0.5])),
            call(EVALCOORD1D, &doubles(&[1.])),
            call(END, &[]),
        ],
    );
    assert_eq!(pixels(&mut q, &mut seq), reference);
    assert_eq!(
        words(&query(&mut q, &mut seq, GETFLOATV, &[0x0b00]).1),
        [0, 1f32.to_bits(), 0, 1f32.to_bits()]
    );
    clear(&mut q, &mut seq);
    submit(
        &mut q,
        &mut seq,
        &[
            call(BEGIN, &[0]),
            call(EVALPOINT1, &[0]),
            call(EVALPOINT1, &[1]),
            call(EVALPOINT1, &[2]),
            call(END, &[]),
        ],
    );
    assert_eq!(pixels(&mut q, &mut seq), reference);
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &[call(EVALMESH1, &[0x1b00, 0, 2])]);
    assert_eq!(pixels(&mut q, &mut seq), reference);
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &explicit(3, &expected));
    let lines = pixels(&mut q, &mut seq);
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &[call(EVALMESH1, &[0x1b01, 0, 2])]);
    assert_eq!(pixels(&mut q, &mut seq), lines);
    // Eval attribute push/pop restores enables and grids; map definitions themselves are not attrib-stack state.
    submit(
        &mut q,
        &mut seq,
        &[
            call(PUSHATTRIB, &[0x10000]),
            grid1(7),
            call(DISABLE, &[0x0d97]),
            call(POPATTRIB, &[]),
        ],
    );
    assert_eq!(
        words(&query(&mut q, &mut seq, GETINTEGERV, &[0x0dd1]).1),
        [2]
    );
    let surface = [
        -0.75f32, -0.75, 0., -0.75, 0.75, 0., 0.75, -0.75, 0., 0.75, 0.75, 0.,
    ];
    submit(
        &mut q,
        &mut seq,
        &[
            mapf(MAP2F, 0x0db7, &[2, 2], &[0., 1., 0., 1.], &surface),
            call(ENABLE, &[0x0db7]),
            grid2(1),
            call(COLOR4F, &[1f32.to_bits(); 4]),
        ],
    );
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &[call(EVALMESH2, &[0x1b02, 0, 1, 0, 1])]);
    let surface_image = pixels(&mut q, &mut seq);
    for y in 0..32 {
        for x in 0..32 {
            let v = if (4..28).contains(&x) && (4..28).contains(&y) {
                255
            } else {
                0
            };
            assert_eq!(
                &surface_image[(y * 32 + x) * 4..(y * 32 + x) * 4 + 3],
                &[v; 3]
            );
        }
    }
    // Automatic normals replace a negative explicit normal map during lighting,
    // and evaluation never overwrites the current normal.
    submit(
        &mut q,
        &mut seq,
        &[
            mapd(MAP2D, 0x0db2, &[1, 1], &[0., 1., 0., 1.], &[0., 0., -1.]),
            call(ENABLE, &[0x0db2]),
            call(ENABLE, &[0x0b50]),
            call(ENABLE, &[0x4000]),
            call(NORMAL3F, &[1f32.to_bits(), 0, 0]),
            data_call(LIGHTMODELFV, &[0x0b53], &[0; 16]),
            data_call(
                MATERIALFV,
                &[0x0408, 0x1201],
                &[1f32; 4]
                    .into_iter()
                    .flat_map(f32::to_le_bytes)
                    .collect::<Vec<_>>(),
            ),
        ],
    );
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &[call(EVALMESH2, &[0x1b02, 0, 1, 0, 1])]);
    assert_eq!(
        &pixels(&mut q, &mut seq)[(16 * 32 + 16) * 4..(16 * 32 + 16) * 4 + 3],
        &[0; 3]
    );
    submit(&mut q, &mut seq, &[call(ENABLE, &[0x0d80])]);
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &[call(EVALMESH2, &[0x1b02, 0, 1, 0, 1])]);
    assert_eq!(
        &pixels(&mut q, &mut seq)[(16 * 32 + 16) * 4..(16 * 32 + 16) * 4 + 3],
        &[255; 3]
    );
    assert_eq!(
        words(&query(&mut q, &mut seq, GETFLOATV, &[0x0b02]).1),
        [1f32.to_bits(), 0, 0]
    );
    submit(
        &mut q,
        &mut seq,
        &[
            call(DISABLE, &[0x0b50]),
            call(COLOR4F, &[1f32.to_bits(); 4]),
        ],
    );
    clear(&mut q, &mut seq);
    submit(
        &mut q,
        &mut seq,
        &[
            call(BEGIN, &[0]),
            call(EVALCOORD2D, &doubles(&[0.5, 0.5])),
            call(END, &[]),
        ],
    );
    let center = pixels(&mut q, &mut seq);
    clear(&mut q, &mut seq);
    submit(
        &mut q,
        &mut seq,
        &[
            grid2(2),
            call(BEGIN, &[0]),
            call(EVALPOINT2, &[1, 1]),
            call(END, &[]),
        ],
    );
    assert_eq!(pixels(&mut q, &mut seq), center);
    // Terminal inclusive indices remain bounded and use the native grid endpoints.
    let terminal = i32::MAX as u32;
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &explicit(0, &[[0.75, 0., 0.]]));
    let endpoint = pixels(&mut q, &mut seq);
    clear(&mut q, &mut seq);
    submit(
        &mut q,
        &mut seq,
        &[
            grid1(terminal),
            call(EVALMESH1, &[0x1b00, terminal, terminal]),
        ],
    );
    assert_eq!(pixels(&mut q, &mut seq), endpoint);
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &explicit(0, &[[0.75, 0.75, 0.]]));
    let endpoint = pixels(&mut q, &mut seq);
    clear(&mut q, &mut seq);
    submit(
        &mut q,
        &mut seq,
        &[
            grid2(terminal),
            call(EVALMESH2, &[0x1b00, terminal, terminal, terminal, terminal]),
        ],
    );
    assert_eq!(pixels(&mut q, &mut seq), endpoint);
    submit(
        &mut q,
        &mut seq,
        &[call(EVALMESH2, &[0x1b01, 1, 0, i32::MIN as u32, terminal])],
    );
    assert_eq!(pixels(&mut q, &mut seq), endpoint);
    assert_eq!(words(&query(&mut q, &mut seq, GETERROR, &[]).1), [0]);
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}
