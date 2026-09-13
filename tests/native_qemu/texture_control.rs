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
#[ignore = "requires native GPU; exact legacy border conformance (known Mesa failure)"]
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
