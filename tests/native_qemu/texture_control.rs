// SPDX-License-Identifier: GPL-2.0-or-later
//! Exact 1D border copies and real object residency/priority queries.
use super::textures::{call, data_call, query, words};
use super::*;
fn submit(q: &mut Qemu, s: &mut u32, r: &[(u32, Vec<u32>)]) {
    *s += 1;
    q.batch(*s, r);
    q.await_completion(*s);
}
fn clear(q: &mut Qemu, s: &mut u32, color: [f32; 4]) {
    submit(
        q,
        s,
        &[call(163, &color.map(f32::to_bits)), call(154, &[0x4000])],
    );
}
#[test]
#[ignore = "requires native GPU; diskless texture-control acceptance"]
fn qemu_texture_control_1d_priority_and_actual_residency() {
    run(false);
}
#[test]
#[ignore = "requires native GPU; exact legacy border storage, upload and copy conformance"]
fn qemu_texture_control_1d_border_conformance() {
    run(true);
}
fn run(border: bool) {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-texctl-{}", uuid::Uuid::new_v4().simple()));
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
            (3, vec![16, 8]),
            (5, vec![]),
            call(78, &[0x0de0, 77]),
            call(78, &[0x0de1, 88]),
        ],
    );
    clear(&mut q, &mut seq, [1., 0., 0., 1.]);
    if border {
        // Six texels = four interior plus two border texels, with full RGBA16 storage.
        submit(
            &mut q,
            &mut seq,
            &[call(340, &[0x0de0, 0, 0x805b, 0, 0, 6, 1])],
        );
        assert_eq!(
            words(&query(&mut q, &mut seq, 1046, &[0x0de0, 0, 0x1000]).1),
            [6]
        );
        assert_eq!(
            words(&query(&mut q, &mut seq, 1046, &[0x0de0, 0, 0x1005]).1),
            [1]
        );
        assert_eq!(
            query(&mut q, &mut seq, 1044, &[0x0de0, 6 << 16, 0]).1,
            [255, 0, 0, 255].repeat(6)
        );
        clear(&mut q, &mut seq, [0., 0., 1., 1.]);
        submit(
            &mut q,
            &mut seq,
            &[call(344, &[0x0de0, 0, (-1i32) as u32, 0, 0, 1])],
        );
        clear(&mut q, &mut seq, [0., 1., 0., 1.]);
        submit(&mut q, &mut seq, &[call(344, &[0x0de0, 0, 4, 0, 0, 1])]);
        assert_eq!(
            query(&mut q, &mut seq, 1044, &[0x0de0, 6 << 16, 0]).1,
            [
                [0, 0, 255, 255].as_slice(),
                &[255, 0, 0, 255].repeat(4),
                &[0, 255, 0, 255]
            ]
            .concat()
        );
        // Actual immutable DATA path: full RGBA16 image including two borders.
        let image = [0x0de0, 0, 0x805b, 6, 1, 1, 0x1908, 0x1401];
        let colors = [
            [91, 12, 203, 255],
            [4, 88, 31, 255],
            [9, 44, 61, 255],
            [72, 81, 66, 255],
            [11, 57, 13, 255],
            [224, 42, 17, 255],
        ]
        .concat();
        submit(&mut q, &mut seq, &[data_call(0x8d3, &image, &colors)]);
        assert_eq!(
            query(&mut q, &mut seq, 1044, &[0x0de0, 6 << 16, 0]).1,
            colors
        );
        assert_eq!(
            words(&query(&mut q, &mut seq, 1046, &[0x0de0, 0, 0x1000]).1),
            [6]
        );
        assert_eq!(
            words(&query(&mut q, &mut seq, 1046, &[0x0de0, 0, 0x1005]).1),
            [1]
        );
        // Null upload initializes both borders too, under neutral pixel transfer.
        submit(
            &mut q,
            &mut seq,
            &[
                call(0x68b, &[0x0d14, 2f32.to_bits()]),
                data_call(0x8d3, &image, &[]),
                call(0x68b, &[0x0d14, 1f32.to_bits()]),
            ],
        );
        assert_eq!(
            query(&mut q, &mut seq, 1044, &[0x0de0, 6 << 16, 0]).1,
            [0; 24]
        );
        // Three separately submitted packets retain signed full-image offsets.
        for i in 0..3 {
            let sub = [
                0x0de0,
                0,
                (-1 + i as i32 * 2) as u32,
                0,
                2,
                1,
                0x1908,
                0x1401,
            ];
            submit(
                &mut q,
                &mut seq,
                &[data_call(0x8f3, &sub, &colors[i * 8..i * 8 + 8])],
            );
        }
        assert_eq!(
            query(&mut q, &mut seq, 1044, &[0x0de0, 6 << 16, 0]).1,
            colors
        );
        assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
        assert_eq!(words(&query(&mut q, &mut seq, 0x329, &[0x8068]).1), [77]);
        assert_eq!(words(&query(&mut q, &mut seq, 0x329, &[0x8069]).1), [88]);
        drop(q);
        drop(server);
        std::fs::remove_dir_all(dir).unwrap();
        return;
    }
    // Signed source coordinates remain valid; only visible source texels checked.
    clear(&mut q, &mut seq, [1., 0., 0., 1.]);
    submit(
        &mut q,
        &mut seq,
        &[call(340, &[0x0de0, 0, 0x8058, (-1i32) as u32, 0, 4, 0])],
    );
    assert_eq!(
        &query(&mut q, &mut seq, 1044, &[0x0de0, 4 << 16, 0]).1[4..],
        &[255, 0, 0, 255].repeat(3)
    );
    // Real GL clamps priorities; unknown/zero names are ignored without rebinding.
    let pairs = [
        77,
        1.5f32.to_bits(),
        88,
        (-0.5f32).to_bits(),
        0,
        1f32.to_bits(),
        9999,
        1f32.to_bits(),
    ]
    .into_iter()
    .flat_map(u32::to_le_bytes)
    .collect::<Vec<_>>();
    submit(&mut q, &mut seq, &[data_call(1723, &[4], &pairs)]);
    assert_eq!(
        words(&query(&mut q, &mut seq, 1048, &[0x0de0, 0x8066]).1),
        [1f32.to_bits()]
    );
    assert_eq!(
        words(&query(&mut q, &mut seq, 1048, &[0x0de1, 0x8066]).1),
        [0]
    );
    let residence = query(&mut q, &mut seq, 18, &[77, 88, 77]);
    assert_eq!(residence.1.len(), 5);
    assert_eq!(residence.1[0], 1);
    assert!(residence.1.iter().all(|x| *x <= 1));
    if residence.1[1] != 0 {
        assert_eq!(&residence.1[2..], &[1, 1, 1]);
    } else {
        assert!(residence.1[2..].contains(&0));
    }
    let bound_resident = words(&query(&mut q, &mut seq, 1048, &[0x0de0, 0x8067]).1)[0];
    assert!(matches!(bound_resident, 0 | 0x3f800000));
    assert_eq!(query(&mut q, &mut seq, 18, &[77, 0, 88]).1, [0; 5]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0x0501]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    // An unknown logical name is rejected without leaking a native object result.
    assert_eq!(query(&mut q, &mut seq, 18, &[9999, 77, 88]).1, [0; 5]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0x0501]);
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
#[ignore = "requires native GL; invalid1D rectangle is recoverable and never mutates pixels"]
fn qemu_texture_1d_invalid_rectangle_preserves_pixels_and_context() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-texerr-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&dir).unwrap();
    let socket = dir.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut q = Qemu::start(&dir, &socket);
    let mut seq = 0;
    let original = [71, 19, 123, 255].repeat(4);
    submit(
        &mut q,
        &mut seq,
        &[
            (1, vec![0]),
            (3, vec![16, 8]),
            (5, vec![]),
            call(78, &[0x0de0, 77]),
        ],
    );
    submit(
        &mut q,
        &mut seq,
        &[data_call(
            0x8f3,
            &[0x0de0, 0, 0, 0, 1, 1, 0x1908, 0x1401],
            &[0, 255, 0, 255],
        )],
    );
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0x0502]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    submit(&mut q, &mut seq, &[call(0x158, &[0x0de0, 0, 0, 0, 0, 1])]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0x0502]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    submit(
        &mut q,
        &mut seq,
        &[data_call(
            0x8d3,
            &[0x0de0, 0, 0x8058, 4, 1, 0, 0x1908, 0x1401],
            &original,
        )],
    );
    let green = [0, 255, 0, 255].repeat(2);
    for offset in [3, u32::MAX, i32::MAX as u32] {
        submit(
            &mut q,
            &mut seq,
            &[data_call(
                0x8f3,
                &[0x0de0, 0, offset, 0, 2, 1, 0x1908, 0x1401],
                &green,
            )],
        );
        assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0x0501]);
        assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
        assert_eq!(
            query(&mut q, &mut seq, 1044, &[0x0de0, 4 << 16, 0]).1,
            original
        );
    }
    for offset in [3, u32::MAX, i32::MAX as u32] {
        submit(
            &mut q,
            &mut seq,
            &[call(0x158, &[0x0de0, 0, offset, 0, 0, 2])],
        );
        assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0x0501]);
        assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
        assert_eq!(
            query(&mut q, &mut seq, 1044, &[0x0de0, 4 << 16, 0]).1,
            original
        );
    }
    // Compiled commands are validated against execution-time image bounds.
    // A GL error must not abort replay before the following valid update.
    assert_eq!(
        words(&query(&mut q, &mut seq, 1592, &[1, 0x1300, 0]).1),
        [0]
    );
    submit(
        &mut q,
        &mut seq,
        &[
            data_call(0x8f3, &[0x0de0, 0, 3, 0, 2, 1, 0x1908, 0x1401], &green),
            call(0x158, &[0x0de0, 0, 3, 0, 0, 2]),
        ],
    );
    submit(
        &mut q,
        &mut seq,
        &[data_call(
            0x8f3,
            &[0x0de0, 0, 1, 0, 2, 1, 0x1908, 0x1401],
            &green,
        )],
    );
    assert_eq!(words(&query(&mut q, &mut seq, 539, &[0, 0, 0]).1), [0]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    assert_eq!(
        query(&mut q, &mut seq, 1044, &[0x0de0, 4 << 16, 0]).1,
        original
    );
    submit(&mut q, &mut seq, &[call(146, &[1])]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0x0501]);
    let mut expected = original;
    expected[4..12].copy_from_slice(&green);
    assert_eq!(
        query(&mut q, &mut seq, 1044, &[0x0de0, 4 << 16, 0]).1,
        expected
    );
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
#[ignore = "requires native GPU; focused packed16 texture transport pixels"]
fn qemu_packed16_texture_uploads_preserve_pixels_mips_zero_and_unpack() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-packed16-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&dir).unwrap();
    let socket = dir.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut q = Qemu::start(&dir, &socket);
    let mut seq = 0;
    submit(
        &mut q,
        &mut seq,
        &[(1, vec![0]), (3, vec![16, 8]), (5, vec![])],
    );
    let expected = [
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    ];
    for target in [0x0de0, 0x0de1] {
        let one = target == 0x0de0;
        let image = if one { 0x8d3 } else { 0x8d4 };
        let subimage = if one { 0x8f3 } else { 0x8f5 };
        let (w, h) = if one { (4, 1) } else { (2, 2) };
        submit(
            &mut q,
            &mut seq,
            &[call(78, &[target, 91 + u32::from(!one)])],
        );
        for (format, kind, values) in [
            (0x1907, 0x8363, [0xf800u16, 0x07e0, 0x001f, 0xffff]),
            (0x1908, 0x8033, [0xf00fu16, 0x0f0f, 0x00ff, 0xffff]),
            (0x1908, 0x8034, [0xf801u16, 0x07c1, 0x003f, 0xffff]),
            (0x80e1, 0x8365, [0xff00u16, 0xf0f0, 0xf00f, 0xffff]),
            (0x80e1, 0x8366, [0xfc00u16, 0x83e0, 0x801f, 0xffff]),
        ] {
            let bytes = values
                .iter()
                .flat_map(|v| v.to_le_bytes())
                .collect::<Vec<_>>();
            let args = [target, 0, 0x8058, w, h, 0, format, kind];
            submit(&mut q, &mut seq, &[data_call(image, &args, &bytes)]);
            assert_eq!(
                query(&mut q, &mut seq, 1044, &[target, 4 << 16, 0]).1,
                expected,
                "{target:x}/{format:x}/{kind:x}"
            );
            let sub = [target, 0, 1, 0, 1, 1, format, kind];
            submit(
                &mut q,
                &mut seq,
                &[data_call(subimage, &sub, &values[2].to_le_bytes())],
            );
            let mut changed = expected;
            changed[4..8].copy_from_slice(&[0, 0, 255, 255]);
            assert_eq!(
                query(&mut q, &mut seq, 1044, &[target, 4 << 16, 0]).1,
                changed
            );
            let mip = [target, 1, 0x8058, 1, 1, 0, format, kind];
            submit(
                &mut q,
                &mut seq,
                &[data_call(image, &mip, &values[3].to_le_bytes())],
            );
            assert_eq!(
                query(&mut q, &mut seq, 1044, &[target, (1 << 16) | 1, 0]).1,
                [255; 4]
            );
            submit(&mut q, &mut seq, &[data_call(image, &args, &[])]);
            assert_eq!(
                query(&mut q, &mut seq, 1044, &[target, 4 << 16, 0]).1,
                [0; 16]
            );
            assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
        }
    }
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}
