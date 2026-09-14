//! Whole-image staging and actual native Bitmap/DrawPixels raster behavior.
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;
fn submit(q: &mut Qemu, s: &mut u32, r: &[(u32, Vec<u32>)]) {
    *s += 1;
    q.batch(*s, r);
    q.await_completion(*s);
}
fn raster(x: f64, y: f64) -> (u32, Vec<u32>) {
    // 32x32 viewport: specify exact requested window coordinate.
    let values = [x / 16. - 1., y / 16. - 1., 0., 1.];
    call(
        1941,
        &values
            .into_iter()
            .flat_map(|v| {
                let b = v.to_bits();
                [b as u32, (b >> 32) as u32]
            })
            .collect::<Vec<_>>(),
    )
}
fn stream(
    q: &mut Qemu,
    s: &mut u32,
    id: u32,
    function: u32,
    desc: [u32; 4],
    bitmap: [f32; 4],
    pixels: &[u8],
) {
    let mut offset = 0;
    loop {
        let n = (pixels.len() - offset).min(32767);
        let flags = u32::from(offset == 0) | if offset + n == pixels.len() { 2 } else { 0 };
        let mut data = Vec::new();
        if offset == 0 && function == 103 {
            data.extend(bitmap.into_iter().flat_map(f32::to_le_bytes));
        }
        data.extend_from_slice(&pixels[offset..offset + n]);
        submit(
            q,
            s,
            &[data_call(
                function,
                &[
                    desc[0],
                    desc[1],
                    desc[2],
                    desc[3],
                    pixels.len() as u32,
                    offset as u32,
                    flags,
                    id,
                ],
                &data,
            )],
        );
        offset += n;
        if offset == pixels.len() {
            break;
        }
    }
}
fn pixels(q: &mut Qemu, s: &mut u32) -> Vec<u8> {
    query_capacity(q, s, 0x7a4, &[0, 0, 32 | (32 << 16)], 4096).1
}
fn clear(q: &mut Qemu, s: &mut u32) {
    submit(
        q,
        s,
        &[
            call(163, &[0, 0, 1f32.to_bits(), 1f32.to_bits()]),
            call(154, &[0x4000]),
        ],
    );
}
#[test]
#[ignore = "requires native GPU; diskless whole-image upload acceptance"]
fn qemu_bitmap_drawpixels_stream_once_with_zoom_and_raster_state() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-image-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&dir).unwrap();
    let socket = dir.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut q = Qemu::start(&dir, &socket);
    let mut seq = 0;
    submit(
        &mut q,
        &mut seq,
        &[(1, vec![0]), (3, vec![32, 32]), (5, vec![])],
    );
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &[raster(0., 0.)]);
    // Another context can observe the same drawable without altering ownerstate.
    seq += 1;
    q.batch_for(seq, 1, 2, 1, 0, &[(1, vec![0]), (5, vec![])]);
    q.await_completion(seq);
    let image = [255, 0, 0, 255].repeat(256 * 128);
    let cut = 65531;
    submit(
        &mut q,
        &mut seq,
        &[data_call(
            498,
            &[256, 128, 0x1908, 0x1401, image.len() as u32, 0, 1, 1],
            &image[..cut],
        )],
    );
    seq += 1;
    q.write(0x115c, 0x200000);
    q.write(0x1160, 0);
    q.write(0x1164, 4);
    q.batch_for(seq, 1, 2, 1, 0, &[(11, vec![0x7a4, 0, 0, 1 | (1 << 16)])]);
    q.await_completion(seq);
    let before = q.command("readl 0x200000");
    assert_eq!(
        u32::from_str_radix(
            before
                .split_whitespace()
                .nth(1)
                .unwrap()
                .trim_start_matches("0x"),
            16
        )
        .unwrap(),
        0xffff0000
    );
    submit(&mut q, &mut seq, &[call(581, &[])]); // glFlush is allowed between chunks.
    let middle = cut + 65531;
    submit(
        &mut q,
        &mut seq,
        &[data_call(
            498,
            &[
                256,
                128,
                0x1908,
                0x1401,
                image.len() as u32,
                cut as u32,
                0,
                1,
            ],
            &image[cut..middle],
        )],
    );
    submit(
        &mut q,
        &mut seq,
        &[data_call(
            498,
            &[
                256,
                128,
                0x1908,
                0x1401,
                image.len() as u32,
                middle as u32,
                2,
                1,
            ],
            &image[middle..],
        )],
    );
    assert_eq!(pixels(&mut q, &mut seq), [255, 0, 0, 255].repeat(1024));
    assert_eq!(
        words(&query(&mut q, &mut seq, 0x305, &[0x0b07]).1),
        [0, 0, 0.5f32.to_bits(), 1f32.to_bits()]
    );
    // Nonintegral negative x zoom and positive y zoom; fractional origin retained.
    clear(&mut q, &mut seq);
    submit(
        &mut q,
        &mut seq,
        &[
            raster(6.25, 1.25),
            call(1682, &[(-1.5f32).to_bits(), 1.5f32.to_bits()]),
        ],
    );
    let colors = [
        [255, 0, 0, 255],
        [0, 255, 0, 255],
        [0, 0, 255, 255],
        [255, 255, 255, 255],
    ];
    stream(
        &mut q,
        &mut seq,
        2,
        498,
        [2, 2, 0x1908, 0x1401],
        [0.; 4],
        &colors.concat(),
    );
    let actual = pixels(&mut q, &mut seq);
    let mut zoom_mismatches = Vec::new();
    for y in 0..32 {
        for x in 0..32 {
            let cx = x as f32 + 0.5;
            let cy = y as f32 + 0.5;
            let mut expected = [0, 0, 255, 255];
            for j in 0..2 {
                for i in 0..2 {
                    let right = 6.25 - i as f32 * 1.5;
                    let left = right - 1.5;
                    let bottom = 1.25 + j as f32 * 1.5;
                    if cx >= left && cx < right && cy >= bottom && cy < bottom + 1.5 {
                        expected = colors[j * 2 + i];
                    }
                }
            }
            let pixel = &actual[(y * 32 + x) * 4..(y * 32 + x + 1) * 4];
            if pixel != expected {
                zoom_mismatches.push((x, y, pixel.to_vec(), expected));
            }
        }
    }
    assert_eq!(
        words(&query(&mut q, &mut seq, 0x305, &[0x0b07]).1),
        [
            6.25f32.to_bits(),
            1.25f32.to_bits(),
            0.5f32.to_bits(),
            1f32.to_bits()
        ]
    );
    // Large Bitmap exceeds transport packet, but movement occurs only once.
    clear(&mut q, &mut seq);
    submit(
        &mut q,
        &mut seq,
        &[
            call(0x0db, &[0, 1f32.to_bits(), 0, 1f32.to_bits()]),
            raster(1.25, 2.25),
        ],
    );
    let mut bitmap = vec![0; 1024 * 600 / 8];
    bitmap[0] = 0x81;
    stream(
        &mut q,
        &mut seq,
        3,
        103,
        [1024, 600, 0x1900, 0x1a00],
        [0.25, 0.25, 2.5, -1.25],
        &bitmap,
    );
    let actual = pixels(&mut q, &mut seq);
    for y in 0..32 {
        for x in 0..32 {
            assert_eq!(
                &actual[(y * 32 + x) * 4..(y * 32 + x + 1) * 4],
                if y == 2 && (x == 1 || x == 8) {
                    &[0, 255, 0, 255]
                } else {
                    &[0, 0, 255, 255]
                }
            );
        }
    }
    assert_eq!(
        words(&query(&mut q, &mut seq, 0x305, &[0x0b07]).1),
        [
            3.75f32.to_bits(),
            1f32.to_bits(),
            0.5f32.to_bits(),
            1f32.to_bits()
        ]
    );
    stream(
        &mut q,
        &mut seq,
        4,
        103,
        [0, 0, 0x1900, 0x1a00],
        [0., 0., 1.25, 0.25],
        &[],
    );
    assert_eq!(
        words(&query(&mut q, &mut seq, 0x305, &[0x0b07]).1),
        [
            5f32.to_bits(),
            1.25f32.to_bits(),
            0.5f32.to_bits(),
            1f32.to_bits()
        ]
    );
    // Invalid raster positions draw no image, and DrawPixels never advances it.
    submit(&mut q, &mut seq, &[raster(80., 0.)]);
    let before = pixels(&mut q, &mut seq);
    stream(
        &mut q,
        &mut seq,
        5,
        498,
        [2, 2, 0x1908, 0x1401],
        [0.; 4],
        &[255; 16],
    );
    assert_eq!(pixels(&mut q, &mut seq), before);
    assert_eq!(words(&query(&mut q, &mut seq, 0x329, &[0x0b08]).1), [0]);
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    // Native typed conversions keep exact endpoints for all scalar widths.
    submit(
        &mut q,
        &mut seq,
        &[call(1682, &[1f32.to_bits(), 1f32.to_bits()])],
    );
    let typed = [
        (0x1400, vec![127, 0, 0, 127]),
        (0x1401, vec![255, 0, 0, 255]),
        (
            0x1402,
            [32767i16, 0, 0, 32767]
                .into_iter()
                .flat_map(i16::to_le_bytes)
                .collect(),
        ),
        (
            0x1403,
            [65535u16, 0, 0, 65535]
                .into_iter()
                .flat_map(u16::to_le_bytes)
                .collect(),
        ),
        (
            0x1404,
            [i32::MAX, 0, 0, i32::MAX]
                .into_iter()
                .flat_map(i32::to_le_bytes)
                .collect(),
        ),
        (
            0x1405,
            [u32::MAX, 0, 0, u32::MAX]
                .into_iter()
                .flat_map(u32::to_le_bytes)
                .collect(),
        ),
        (
            0x1406,
            [1f32, 0., 0., 1.]
                .into_iter()
                .flat_map(f32::to_le_bytes)
                .collect(),
        ),
    ];
    let mut id = 6;
    for (kind, data) in typed {
        clear(&mut q, &mut seq);
        submit(&mut q, &mut seq, &[raster(0., 0.)]);
        stream(
            &mut q,
            &mut seq,
            id,
            498,
            [1, 1, 0x1908, kind],
            [0.; 4],
            &data,
        );
        id += 1;
        assert_eq!(
            &pixels(&mut q, &mut seq)[..8],
            &[255, 0, 0, 255, 0, 0, 255, 255],
            "typed {kind:x}"
        );
    }
    // Transfer is applied to DrawPixels exactly once; restore it before reading.
    submit(
        &mut q,
        &mut seq,
        &[
            raster(0., 0.),
            call(1675, &[0x0d14, 0]),
            call(1675, &[0x0d19, 1f32.to_bits()]),
        ],
    );
    stream(
        &mut q,
        &mut seq,
        id,
        498,
        [1, 1, 0x1908, 0x1401],
        [0.; 4],
        &[255, 0, 0, 255],
    );
    id += 1;
    submit(
        &mut q,
        &mut seq,
        &[
            call(1675, &[0x0d14, 1f32.to_bits()]),
            call(1675, &[0x0d19, 0]),
        ],
    );
    assert_eq!(&pixels(&mut q, &mut seq)[..4], &[0, 255, 0, 255]);
    // Depth images and depth CopyPixels use the drawable depth attachment.
    clear(&mut q, &mut seq);
    submit(
        &mut q,
        &mut seq,
        &[
            call(154, &[0x0100]),
            call(521, &[0x0b71]),
            call(441, &[0x0201]),
            call(245, &[0, 0, 0, 0]),
            raster(0., 0.),
        ],
    );
    stream(
        &mut q,
        &mut seq,
        id,
        498,
        [1, 1, 0x1902, 0x1406],
        [0.; 4],
        &0.25f32.to_le_bytes(),
    );
    id += 1;
    submit(
        &mut q,
        &mut seq,
        &[
            raster(2., 0.),
            call(339, &[0, 0, 1, 1, 0x1801]),
            call(245, &[1, 1, 1, 1]),
            raster(0., 0.),
        ],
    );
    stream(
        &mut q,
        &mut seq,
        id,
        498,
        [4, 1, 0x1908, 0x1401],
        [0.; 4],
        &[255, 0, 0, 255].repeat(4),
    );
    id += 1;
    assert_eq!(
        &pixels(&mut q, &mut seq)[..16],
        &[
            0, 0, 255, 255, 255, 0, 0, 255, 0, 0, 255, 255, 255, 0, 0, 255
        ]
    );
    submit(&mut q, &mut seq, &[call(453, &[0x0b71])]);
    // Stencil Bitmap input and stencil CopyPixels preserve one-bit unpack order.
    clear(&mut q, &mut seq);
    submit(&mut q, &mut seq, &[call(154, &[0x0400]), raster(0., 0.)]);
    stream(
        &mut q,
        &mut seq,
        id,
        498,
        [8, 1, 0x1901, 0x1a00],
        [0.; 4],
        &[0x80],
    );
    id += 1;
    submit(
        &mut q,
        &mut seq,
        &[
            raster(2., 0.),
            call(339, &[0, 0, 1, 1, 0x1802]),
            call(521, &[0x0b90]),
            call(2116, &[0x0202, 1, 255]),
            raster(0., 0.),
        ],
    );
    stream(
        &mut q,
        &mut seq,
        id,
        498,
        [4, 1, 0x1908, 0x1401],
        [0.; 4],
        &[255, 0, 0, 255].repeat(4),
    );
    assert_eq!(
        &pixels(&mut q, &mut seq)[..16],
        &[
            255, 0, 0, 255, 0, 0, 255, 255, 255, 0, 0, 255, 0, 0, 255, 255
        ]
    );
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    // Packed COLOR_INDEX rows preserve padding and native index-to-RGBA maps.
    id += 1;
    submit(&mut q, &mut seq, &[call(453, &[0x0b90])]);
    clear(&mut q, &mut seq);
    for (map, values) in [
        (3186, [0f32, 1.]),
        (3187, [0., 0.]),
        (3188, [0., 0.]),
        (3189, [1., 1.]),
    ] {
        submit(
            &mut q,
            &mut seq,
            &[data_call(
                1663,
                &[map, 2],
                &values
                    .into_iter()
                    .flat_map(f32::to_le_bytes)
                    .collect::<Vec<_>>(),
            )],
        );
    }
    submit(&mut q, &mut seq, &[raster(0., 0.)]);
    stream(
        &mut q,
        &mut seq,
        id,
        498,
        [9, 2, 0x1900, 0x1a00],
        [0.; 4],
        &[0x80, 0x80, 0x40, 0],
    );
    let indexed = pixels(&mut q, &mut seq);
    for y in 0..3 {
        for x in 0..10 {
            let expected = if x >= 9 || y >= 2 {
                [0, 0, 255, 255]
            } else if (y == 0 && (x == 0 || x == 8)) || (y == 1 && x == 1) {
                [255, 0, 0, 255]
            } else {
                [0, 0, 0, 255]
            };
            assert_eq!(
                &indexed[(y * 32 + x) * 4..(y * 32 + x + 1) * 4],
                &expected,
                "index pixel{x},{y}"
            );
        }
    }
    assert_eq!(words(&query(&mut q, &mut seq, 0x2fc, &[]).1), [0]);
    eprintln!("whole-image remaining semantics PASS; zoom mismatches={zoom_mismatches:?}");
    // Keep exact spec coverage mandatory. Linux Mesa26.1.8 has an independently
    // reproduced native-driver failure here; do not silently narrow the oracle.
    assert!(
        zoom_mismatches.is_empty(),
        "native fractional PixelZoom coverage: {zoom_mismatches:?}"
    );
    drop(q);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}
