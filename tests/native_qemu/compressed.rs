//! S3TC compressed textures through the device: exact block upload, sampling,
//! subimage replacement, exact native readback, zero-defined allocation, the
//! TexImage2D compression path Wine uses (including band-aligned zero fill),
//! proxies, host-answered format lists and level-dependent GL errors.
use super::textures::{call, data_call, query, words};
use super::*;

const TEXTURE_2D: u32 = 0x0de1;
const PROXY_TEXTURE_2D: u32 = 0x8064;
const DXT1: u32 = 0x83f0;
const DXT5: u32 = 0x83f3;
const COMPRESSED_IMAGE_2D: u32 = 286;
const COMPRESSED_SUB_IMAGE_2D: u32 = 292;
const GET_COMPRESSED_IMAGE: u32 = 743;

fn submit(qemu: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    qemu.batch(*sequence, records);
    qemu.await_completion(*sequence);
}
fn f(v: f32) -> u32 {
    v.to_bits()
}
/// One opaque DXT1 block of a single RGB565 colour (four-colour mode, index 0).
fn block(color: u16) -> [u8; 8] {
    let [a, b] = color.to_le_bytes();
    [a, b, 0, 0, 0, 0, 0, 0]
}
fn draw() -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![
        call(0x0a3, &[0, 0, 0, f(1.)]),
        call(0x09a, &[0x4000]),
        call(0x0db, &[f(1.); 4]),
        call(0x01a, &[7]),
    ];
    for (x, y, s, t) in [
        (-1f32, -1f32, 0f32, 0f32),
        (1., -1., 1., 0.),
        (1., 1., 1., 1.),
        (-1., 1., 0., 1.),
    ] {
        out.push(call(0x883, &[f(s), f(t)]));
        out.push(call(0x9dc, &[f(x), f(y)]));
    }
    out.push(call(0x216, &[]));
    out
}
fn pixel(qemu: &mut Qemu, sequence: &mut u32, x: u32, y: u32) -> [u8; 4] {
    let (_, result) = query(qemu, sequence, 0x7a4, &[x, y, 1 | (1 << 16)]);
    result[..4].try_into().unwrap()
}
fn close(actual: [u8; 4], expected: [u8; 3], tolerance: i32, label: &str) {
    for c in 0..3 {
        let d = i32::from(actual[c]) - i32::from(expected[c]);
        assert!(
            d.abs() <= tolerance,
            "{label}: {actual:?} expected {expected:?}"
        );
    }
}
fn level_int(qemu: &mut Qemu, sequence: &mut u32, target: u32, pname: u32) -> u32 {
    words(&query(qemu, sequence, 0x416, &[target, 0, pname]).1)[0]
}
fn error(qemu: &mut Qemu, sequence: &mut u32) -> u32 {
    words(&query(qemu, sequence, 0x2fc, &[]).1)[0]
}

#[test]
#[ignore = "requires native host GPU and compressed-texture QEMU producer; diskless acceptance"]
fn qemu_s3tc_upload_sample_subimage_readback_zero_and_compression() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-s3-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 0;
    assert_ne!(qemu.read(0x1008) & 0x40000, 0);
    submit(
        &mut qemu,
        &mut sequence,
        &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])],
    );

    // 8x4 DXT1: a red block then a green block, sampled left|right.
    let red_green = [block(0xf800), block(0x07e0)].concat();
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(0x04e, &[TEXTURE_2D, 1]),
            call(0x8df, &[TEXTURE_2D, 0x2801, 0x2600]),
            call(0x8df, &[TEXTURE_2D, 0x2800, 0x2600]),
            data_call(
                COMPRESSED_IMAGE_2D,
                &[TEXTURE_2D, 0, DXT1, 8, 4, 0, 16],
                &red_green,
            ),
            call(0x209, &[TEXTURE_2D]),
        ],
    );
    submit(&mut qemu, &mut sequence, &draw());
    close(
        pixel(&mut qemu, &mut sequence, 1, 4),
        [255, 0, 0],
        0,
        "DXT1 red block",
    );
    close(
        pixel(&mut qemu, &mut sequence, 6, 4),
        [0, 255, 0],
        0,
        "DXT1 green block",
    );
    assert_eq!(level_int(&mut qemu, &mut sequence, TEXTURE_2D, 0x86a1), 1);
    assert_eq!(level_int(&mut qemu, &mut sequence, TEXTURE_2D, 0x86a0), 16);
    // Exact native readback: four words starting at word zero, then at word 2.
    let (_, image) = query(
        &mut qemu,
        &mut sequence,
        GET_COMPRESSED_IMAGE,
        &[TEXTURE_2D, 4 << 16, 0],
    );
    assert_eq!(&image[..16], &red_green[..]);
    let (_, tail) = query(
        &mut qemu,
        &mut sequence,
        GET_COMPRESSED_IMAGE,
        &[TEXTURE_2D, 2 << 16, 2],
    );
    assert_eq!(&tail[..8], &red_green[8..]);

    // Replace the right block with blue; reject an out-of-image update as a
    // GL error while the batch (and the following draw) completes.
    let mut records = vec![
        data_call(
            COMPRESSED_SUB_IMAGE_2D,
            &[TEXTURE_2D, 0, 4, 0, 4, 4, DXT1, 8],
            &block(0x001f),
        ),
        data_call(
            COMPRESSED_SUB_IMAGE_2D,
            &[TEXTURE_2D, 0, 8, 0, 4, 4, DXT1, 8],
            &block(0x001f),
        ),
    ];
    records.extend(draw());
    submit(&mut qemu, &mut sequence, &records);
    assert_eq!(error(&mut qemu, &mut sequence), 0x0502);
    close(
        pixel(&mut qemu, &mut sequence, 1, 4),
        [255, 0, 0],
        0,
        "unchanged left block",
    );
    close(
        pixel(&mut qemu, &mut sequence, 6, 4),
        [0, 0, 255],
        0,
        "replaced right block",
    );
    // A partial block that does not reach the edge is a GL error too.
    submit(
        &mut qemu,
        &mut sequence,
        &[data_call(
            COMPRESSED_SUB_IMAGE_2D,
            &[TEXTURE_2D, 0, 0, 0, 2, 4, DXT1, 8],
            &block(0),
        )],
    );
    assert_eq!(error(&mut qemu, &mut sequence), 0x0502);

    // No payload: zero-defined storage, read back exactly as zero blocks.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(0x04e, &[TEXTURE_2D, 2]),
            data_call(
                COMPRESSED_IMAGE_2D,
                &[TEXTURE_2D, 0, DXT5, 8, 8, 0, 64],
                &[],
            ),
        ],
    );
    let (_, zero) = query(
        &mut qemu,
        &mut sequence,
        GET_COMPRESSED_IMAGE,
        &[TEXTURE_2D, 16 << 16, 0],
    );
    assert_eq!(&zero[..64], &[0; 64][..]);

    // Wine's path: TexImage2D with an S3TC internal format and RGBA pixels is
    // compressed natively; its level then reports compressed storage.
    let orange: Vec<u8> = [255u8, 128, 0, 255].repeat(64);
    let mut records = vec![
        call(0x04e, &[TEXTURE_2D, 3]),
        call(0x8df, &[TEXTURE_2D, 0x2801, 0x2600]),
        call(0x8df, &[TEXTURE_2D, 0x2800, 0x2600]),
        data_call(
            0x8d4,
            &[TEXTURE_2D, 0, DXT5, 8, 8, 0, 0x1908, 0x1401],
            &orange,
        ),
    ];
    records.extend(draw());
    submit(&mut qemu, &mut sequence, &records);
    close(
        pixel(&mut qemu, &mut sequence, 4, 4),
        [255, 128, 0],
        6,
        "compressed by TexImage2D",
    );
    assert_eq!(level_int(&mut qemu, &mut sequence, TEXTURE_2D, 0x86a1), 1);
    assert_eq!(
        level_int(&mut qemu, &mut sequence, TEXTURE_2D, 0x1003),
        DXT5
    );

    // Allocation without pixels zero-fills in bands. A width whose 64 KiB band
    // is not a block multiple (1536 -> 10 rows) must still start every band on
    // a block row, or the compressed subimage fails and loses the batch.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(0x04e, &[TEXTURE_2D, 4]),
            data_call(
                0x8d4,
                &[TEXTURE_2D, 0, DXT1, 1536, 16, 0, 0x1908, 0x1401],
                &[],
            ),
        ],
    );
    assert_eq!(error(&mut qemu, &mut sequence), 0);
    assert_eq!(
        level_int(&mut qemu, &mut sequence, TEXTURE_2D, 0x1000),
        1536
    );

    // Proxy: no payload, answered from native feasibility.
    submit(
        &mut qemu,
        &mut sequence,
        &[data_call(
            COMPRESSED_IMAGE_2D,
            &[PROXY_TEXTURE_2D, 0, DXT1, 64, 64, 0, 2048],
            &[],
        )],
    );
    assert_eq!(
        level_int(&mut qemu, &mut sequence, PROXY_TEXTURE_2D, 0x1000),
        64
    );

    // The guaranteed format list, not whatever else the native driver offers.
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x86a2]).1),
        [4]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x86a3]).1),
        [0x83f0, 0x83f1, 0x83f2, 0x83f3]
    );
    assert_eq!(error(&mut qemu, &mut sequence), 0);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
