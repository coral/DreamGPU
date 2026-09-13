//! Pixel transfer semantics, typed maps, and isolation from internal GPU export.
use super::textures::{call, data_call, query, query_capacity, receive, words};
use super::*;
fn submit(q: &mut Qemu, s: &mut u32, records: &[(u32, Vec<u32>)]) {
    *s += 1;
    q.batch(*s, records);
    q.await_completion(*s);
}
fn value(q: &mut Qemu, s: &mut u32, p: u32) -> u32 {
    words(&query(q, s, 0x329, &[p]).1)[0]
}
#[test]
#[ignore = "requires native GPU; diskless pixel-state acceptance"]
fn qemu_pixel_transfer_maps_preserve_internal_export_and_zero_initialization() {
    let (device, queue, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-pixel-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&dir).unwrap();
    let socket = dir.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut q = Qemu::start(&dir, &socket);
    let mut seq = 0;
    submit(
        &mut q,
        &mut seq,
        &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])],
    );
    let max = value(&mut q, &mut seq, 0x0d34);
    assert!((32..=256).contains(&max));
    // Full INT_MAX is a preserved Linux Mesa26.1.8 failure: native
    // PixelTransferi converts through float. This oracle uses an exactly
    // representable large integer to isolate the remaining pixel semantics.
    // See pixels-linux-index-offset-failure.json; no full-range claim.
    submit(
        &mut q,
        &mut seq,
        &[
            call(163, &[1f32.to_bits(), 0, 0, 1f32.to_bits()]),
            call(154, &[0x4000]),
            call(1682, &[2f32.to_bits(), (-3f32).to_bits()]),
            call(1676, &[0x0d13, 0x7fff_ff80]),
            call(1675, &[0x0d14, 0]),
            call(1675, &[0x0d19, 1f32.to_bits()]),
        ],
    );
    assert_eq!(value(&mut q, &mut seq, 0x0d13), 0x7fff_ff80);
    assert_eq!(
        words(&query(&mut q, &mut seq, 0x305, &[0x0d16]).1),
        [2f32.to_bits()]
    );
    assert_eq!(
        query(&mut q, &mut seq, 0x7a4, &[0, 0, 8 | (8 << 16)]).1,
        [0, 255, 0, 255].repeat(64)
    );
    // Guest transfer changes ReadPixels; publication remains the real red framebuffer.
    submit(&mut q, &mut seq, &[(7, vec![])]);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |_, _| {
        [0, 0, 255, 255]
    });
    // Copy front red into back blue at a raster destination, with transfer to
    // green and scissor clipping. Read and draw selection refer to private FBOs.
    let raster_origin: Vec<u32> = [-1f64, -1., 0., 1.]
        .into_iter()
        .flat_map(|v| {
            let b = v.to_bits();
            [b as u32, (b >> 32) as u32]
        })
        .collect();
    submit(
        &mut q,
        &mut seq,
        &[
            call(163, &[0, 0, 1f32.to_bits(), 1f32.to_bits()]),
            call(154, &[0x4000]),
            call(1682, &[1f32.to_bits(), 1f32.to_bits()]),
            call(1941, &raster_origin),
            call(1952, &[0x0404]), // ReadBuffer FRONT
            call(2032, &[0, 0, 1, 2]),
            call(521, &[0x0c11]), // Scissor + Enable
            call(339, &[0, 0, 2, 2, 0x1800]),
            call(453, &[0x0c11]),
            call(1952, &[0x0405]), // Disable scissor; ReadBuffer BACK
            call(1675, &[0x0d14, 1f32.to_bits()]),
            call(1675, &[0x0d19, 0]),
        ],
    );
    let copied = query(&mut q, &mut seq, 0x7a4, &[0, 0, 8 | (8 << 16)]).1;
    for y in 0..8 {
        for x in 0..8 {
            assert_eq!(
                &copied[(y * 8 + x) * 4..(y * 8 + x + 1) * 4],
                if x == 0 && y < 2 {
                    &[0, 255, 0, 255]
                } else {
                    &[0, 0, 255, 255]
                }
            );
        }
    }
    // CopyPixels must not advance or mutate current raster position.
    assert_eq!(
        words(&query(&mut q, &mut seq, 0x305, &[0x0b07]).1),
        [0, 0, 0.5f32.to_bits(), 1f32.to_bits()]
    );
    submit(
        &mut q,
        &mut seq,
        &[
            call(1675, &[0x0d14, 0]),
            call(1675, &[0x0d19, 1f32.to_bits()]),
        ],
    );
    // Export readback is BGRA; the guest ReadPixels reply above is RGBA.
    // The representable large index offset survives internal initialization.
    // With green/alpha bias1, an unprotected zero upload would turn opaque green.
    submit(
        &mut q,
        &mut seq,
        &[
            call(1675, &[0x0d1d, 1f32.to_bits()]),
            call(0x04e, &[0x0de0, 31]),
            data_call(2259, &[0x0de0, 0, 0x1908, 4, 1, 0, 0x1908, 0x1401], &[]),
        ],
    );
    assert_eq!(
        query_capacity(&mut q, &mut seq, 1044, &[0x0de0, 4 << 16, 0], 16).1,
        vec![0; 16]
    );
    assert_eq!(value(&mut q, &mut seq, 0x0d13), 0x7fff_ff80);
    assert_eq!(
        words(&query(&mut q, &mut seq, 0x305, &[0x0d19]).1),
        [1f32.to_bits()]
    );
    // A real guest texture upload must retain those same transfer settings.
    submit(
        &mut q,
        &mut seq,
        &[data_call(
            2259,
            &[0x0de0, 0, 0x1908, 4, 1, 0, 0x1908, 0x1401],
            &[255; 16],
        )],
    );
    assert_eq!(
        query_capacity(&mut q, &mut seq, 1044, &[0x0de0, 4 << 16, 0], 16).1,
        [0, 255, 255, 255].repeat(4)
    );
    // All three input/output types, including two-byte data padding and full-sized
    // tables. Integer indices stay integers; color maps normalize unsigned values.
    let fp: Vec<u8> = [0f32, 1.].into_iter().flat_map(f32::to_le_bytes).collect();
    let ui: Vec<u8> = [0u32, u32::MAX]
        .into_iter()
        .flat_map(u32::to_le_bytes)
        .collect();
    let us: Vec<u8> = [0u16, u16::MAX]
        .into_iter()
        .flat_map(u16::to_le_bytes)
        .collect();
    for (function, data) in [(1663, &fp), (1664, &ui), (1665, &us)] {
        submit(&mut q, &mut seq, &[data_call(function, &[0x0c76, 2], data)]);
        assert_eq!(value(&mut q, &mut seq, 0x0cb6), 2);
        assert_eq!(
            words(&query_capacity(&mut q, &mut seq, 953, &[0x0c76, 2, 0], 8).1),
            [0, 1f32.to_bits()]
        );
        assert_eq!(
            words(&query_capacity(&mut q, &mut seq, 954, &[0x0c76, 2, 0], 8).1),
            [0, u32::MAX]
        );
        assert_eq!(
            words(&query_capacity(&mut q, &mut seq, 955, &[0x0c76, 2, 0], 8).1),
            [0, 65535]
        );
    }
    let table: Vec<u8> = (0..max)
        .flat_map(|i| (i as f32 / (max - 1) as f32).to_le_bytes())
        .collect();
    submit(&mut q, &mut seq, &[data_call(1663, &[0x0c76, max], &table)]);
    assert_eq!(
        query_capacity(&mut q, &mut seq, 953, &[0x0c76, max, 0], max * 4).1,
        table
    );
    // One-element16-bit maps exercise the inline payload padding contract.
    submit(
        &mut q,
        &mut seq,
        &[
            data_call(1665, &[0x0c76, 1], &65535u16.to_le_bytes()),
            call(1676, &[0x0d10, 1]),
        ],
    );
    assert_eq!(
        query(&mut q, &mut seq, 0x7a4, &[0, 0, 1 | (1 << 16)]).1,
        [255, 0, 0, 0]
    );
    // Enabled color mapping cannot affect new zero texels. It is restored afterward.
    submit(
        &mut q,
        &mut seq,
        &[data_call(
            2259,
            &[0x0de0, 0, 0x1908, 4, 1, 0, 0x1908, 0x1401],
            &[],
        )],
    );
    assert_eq!(
        query_capacity(&mut q, &mut seq, 1044, &[0x0de0, 4 << 16, 0], 16).1,
        vec![0; 16]
    );
    assert_eq!(value(&mut q, &mut seq, 0x0d10), 1);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    // Declared size must equal actual size before native getter writes fixed storage.
    seq += 1;
    q.write(0x115c, 0x200000);
    q.write(0x1160, 0);
    q.write(0x1164, 1024);
    q.batch(seq, &[(11, vec![953, 0x0c76, 2, 0])]);
    let deadline = Instant::now() + Duration::from_secs(5);
    while q.read(0x111c) & 4 == 0 {
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(1));
    }
    assert_eq!(q.read(0x1124), 9);
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}
