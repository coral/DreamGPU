//! Supplied borders must retain fixed-function environment/fog semantics.
use super::textures::{call, data_call, query, words};
use super::*;

fn submit(q: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    q.batch(*sequence, records);
    q.await_completion(*sequence);
}
fn vector(function: u32, args: &[u32], values: &[f32]) -> (u32, Vec<u32>) {
    data_call(
        function,
        args,
        &values
            .iter()
            .flat_map(|x| x.to_le_bytes())
            .collect::<Vec<_>>(),
    )
}
fn draw(q: &mut Qemu, sequence: &mut u32, texture: u32, coord: [f32; 4]) -> Vec<u8> {
    let mut records = vec![
        call(78, &[0xde1, texture]),
        call(2217, &coord.map(f32::to_bits)),
        call(26, &[7]),
    ];
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        records.push(call(2538, &[x.to_bits(), y.to_bits(), (-0.5f32).to_bits()]));
    }
    records.push(call(534, &[]));
    submit(q, sequence, &records);
    query(q, sequence, 0x7a4, &[8, 8, 1 | (1 << 16)]).1
}

#[test]
#[ignore = "requires native GPU; supplied-border environment, projective coordinates and fog oracle"]
fn qemu_border_sampling_matches_fixed_function_formats_environment_and_fog() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-benv-{}", uuid::Uuid::new_v4().simple()));
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
            (3, vec![16, 16]),
            (5, vec![]),
            call(453, &[0xbd0]), // no dithering
            call(521, &[0xde1]),
            call(
                219,
                &[
                    0.25f32.to_bits(),
                    0.5f32.to_bits(),
                    0.75f32.to_bits(),
                    0.5f32.to_bits(),
                ],
            ),
            vector(2245, &[0x2300, 0x2201], &[0.2, 0.4, 0.6, 0.8]),
            call(605, &[0xb65, (0x2601 as f32).to_bits()]), // linear fog, eye distance .5
            call(605, &[0xb63, 0]),
            call(605, &[0xb64, 1f32.to_bits()]),
            vector(607, &[0xb66], &[0., 0., 1., 1.]),
        ],
    );
    for format in [0x1908, 0x1907, 0x1906, 0x1909, 0x190a, 0x8049] {
        let mut bordered = Vec::new();
        for y in 0..4 {
            for x in 0..4 {
                bordered.extend_from_slice(if x == 0 || x == 3 || y == 0 || y == 3 {
                    &[0, 255, 0, 64]
                } else {
                    &[255, 0, 0, 192]
                });
            }
        }
        for (name, width, border, pixels) in [
            (71, 4, 1, bordered),
            (72, 2, 0, [128u8, 128, 0, 128].repeat(4)),
        ] {
            submit(
                &mut q,
                &mut seq,
                &[
                    call(78, &[0xde1, name]),
                    call(2271, &[0xde1, 0x2801, 0x2601]),
                    call(2271, &[0xde1, 0x2800, 0x2601]),
                    call(2271, &[0xde1, 0x2802, 0x2900]),
                    call(2271, &[0xde1, 0x2803, 0x2900]),
                    data_call(
                        2260,
                        &[0xde1, 0, format, width, width, border, 0x1908, 0x1401],
                        &pixels,
                    ),
                ],
            );
        }
        for environment in [0x1e01, 0x2100, 0x2101, 0xbe2, 0x8570] {
            // DECAL is undefined for non-RGB(A) base formats.
            if environment == 0x2101 && format != 0x1908 && format != 0x1907 {
                continue;
            }
            for fog in [false, true] {
                submit(
                    &mut q,
                    &mut seq,
                    &[
                        call(2246, &[0x2300, 0x2200, environment]),
                        call(if fog { 521 } else { 453 }, &[0xb60]),
                    ],
                );
                let actual = draw(&mut q, &mut seq, 71, [0., 0.5, 0., 2.]);
                // Native fixed-function reference samples the same averaged
                // texel without a border. Binding it after every emulated draw
                // also checks that private sampler state does not escape.
                let expected = draw(&mut q, &mut seq, 72, [0.5, 0.5, 0., 1.]);
                assert_eq!(actual.len(), 4);
                assert_eq!(expected.len(), 4);
                for (a, b) in actual.iter().zip(&expected) {
                    assert!(
                        (i16::from(*a) - i16::from(*b)).abs() <= 2,
                        "format={format:x} env={environment:x} fog={fog}: border={actual:?} reference={expected:?}"
                    );
                }
                assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
            }
        }
    }
    // A guest error already pending in native GL must survive private sampler
    // work and must not turn the next valid draw into a transport failure.
    submit(&mut q, &mut seq, &[call(441, &[0xffff])]);
    let actual = draw(&mut q, &mut seq, 71, [0., 0.5, 0., 2.]);
    let expected = draw(&mut q, &mut seq, 72, [0.5, 0.5, 0., 1.]);
    for (a, b) in actual.iter().zip(&expected) {
        assert!((i16::from(*a) - i16::from(*b)).abs() <= 2);
    }
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0x500]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
#[ignore = "requires native GPU; all six minification filters on supplied-border mip chains"]
fn qemu_border_sampling_mip_filters_match_native_lod() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-bmip-{}", uuid::Uuid::new_v4().simple()));
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
            (3, vec![16, 16]),
            (5, vec![]),
            call(453, &[0xbd0]),
            call(521, &[0xde1]),
            call(2246, &[0x2300, 0x2200, 0x1e01]),
        ],
    );
    for (name, border) in [(81, 1), (82, 0)] {
        submit(
            &mut q,
            &mut seq,
            &[
                call(78, &[0xde1, name]),
                call(2271, &[0xde1, 0x2800, 0x2600]),
                call(2271, &[0xde1, 0x2802, 0x2901]),
                call(2271, &[0xde1, 0x2803, 0x2901]),
            ],
        );
        for (level, color) in [[255u8, 0, 0, 255], [0, 255, 0, 255], [0, 0, 255, 255]]
            .into_iter()
            .enumerate()
        {
            let width = (4 >> level) + 2 * border;
            submit(
                &mut q,
                &mut seq,
                &[data_call(
                    2260,
                    &[
                        0xde1,
                        level as u32,
                        0x8058,
                        width,
                        width,
                        border,
                        0x1908,
                        0x1401,
                    ],
                    &color.repeat((width * width) as usize),
                )],
            );
        }
    }
    for filter in [0x2600, 0x2601, 0x2700, 0x2701, 0x2702, 0x2703] {
        for lambda in [0.25f32, 1.25, 1.75, 3.0] {
            let range = 4.0 * 2f32.powf(lambda); // 4 base texels over 16 pixels
            let mut results = Vec::new();
            for name in [81, 82] {
                let mut records = vec![
                    call(78, &[0xde1, name]),
                    call(2271, &[0xde1, 0x2801, filter]),
                    call(26, &[7]),
                ];
                for (s, t, x, y) in [
                    (0f32, 0f32, -1f32, -1f32),
                    (range, 0., 1., -1.),
                    (range, range, 1., 1.),
                    (0., range, -1., 1.),
                ] {
                    records.push(call(
                        2217,
                        &[(s * 2.).to_bits(), (t * 2.).to_bits(), 0, 2f32.to_bits()],
                    ));
                    records.push(call(2524, &[x.to_bits(), y.to_bits()]));
                }
                records.push(call(534, &[]));
                submit(&mut q, &mut seq, &records);
                results.push(query(&mut q, &mut seq, 0x7a4, &[8, 8, 1 | (1 << 16)]).1);
            }
            assert_eq!(results[0].len(), 4);
            assert_eq!(results[1].len(), 4);
            // Driver LOD approximations can differ slightly; at most two UNORM
            // steps are allowed, far below a wrong mip's 255-channel error.
            for (a, b) in results[0].iter().zip(&results[1]) {
                assert!(
                    (i16::from(*a) - i16::from(*b)).abs() <= 2,
                    "filter={filter:x} lambda={lambda}: bordered={:?} native={:?}",
                    results[0],
                    results[1]
                );
            }
            assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
        }
    }
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}
