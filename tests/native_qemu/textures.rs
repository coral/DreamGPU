//! Real native texture pixels, guest namespaces, sharing, and deleted bindings.

use super::*;

const TEXTURE_2D: u32 = 0x0de1;
const BIND_TEXTURE: u32 = 0x04e;
const TEX_IMAGE_2D: u32 = 0x8d4;
const TEX_SUB_IMAGE_2D: u32 = 0x8f5;
const DELETE_TEXTURES: u32 = 0x1b0;
const TEX_PARAMETER_I: u32 = 0x8df;
const GUEST_TEXTURE: u32 = 17;

pub(super) fn call(function: u32, args: &[u32]) -> (u32, Vec<u32>) {
    (
        6,
        std::iter::once(function)
            .chain(args.iter().copied())
            .collect(),
    )
}

pub(super) fn data_call(function: u32, args: &[u32], pixels: &[u8]) -> (u32, Vec<u32>) {
    let mut words = vec![function, pixels.len() as u32];
    words.extend_from_slice(args);
    for bytes in pixels.chunks(4) {
        let mut padded = [0; 4];
        padded[..bytes.len()].copy_from_slice(bytes);
        words.push(u32::from_le_bytes(padded));
    }
    (10, words)
}

fn upload(pixels: &[u8]) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(BIND_TEXTURE, &[TEXTURE_2D, GUEST_TEXTURE]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2801, 0x2600]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2800, 0x2600]),
        data_call(
            TEX_IMAGE_2D,
            &[TEXTURE_2D, 0, 0x1908, 2, 2, 0, 0x1908, 0x1401],
            pixels,
        ),
    ]
}

fn quad() -> Vec<(u32, Vec<u32>)> {
    let mut records = vec![call(0x209, &[TEXTURE_2D]), call(0x01a, &[7])];
    for (s, t, x, y) in [
        (0f32, 0f32, -1f32, -1f32),
        (1f32, 0f32, 1f32, -1f32),
        (1f32, 1f32, 1f32, 1f32),
        (0f32, 1f32, -1f32, 1f32),
    ] {
        records.push(call(0x883, &[s.to_bits(), t.to_bits()]));
        records.push(call(0x9dc, &[x.to_bits(), y.to_bits()]));
    }
    records.push(call(0x216, &[]));
    records.push((7, vec![]));
    records
}

pub(super) fn receive(
    server: &GpuServer,
    wake: &mpsc::Receiver<()>,
    client: u32,
    drawable: u32,
) -> GpuFrameLease {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(error) = server.take_error() {
            panic!("GPU transport: {error}");
        }
        let mut images = server.take_drawables();
        if !images.is_empty() {
            assert_eq!(images.len(), 1);
            let image = images.pop().unwrap();
            assert_eq!((image.client, image.drawable), (client, drawable));
            assert!(!server.desktop_active());
            return image.frame;
        }
        assert!(Instant::now() < deadline, "texture export timed out");
        let _ = wake.recv_timeout(Duration::from_millis(2));
    }
}

fn checker(x: usize, y: usize, patched: bool) -> [u8; 4] {
    match (x < 16, y < 16) {
        (true, true) => [255, 0, 0, 255],
        (false, true) => [255; 4],
        (true, false) if patched => [255, 0, 255, 255],
        (true, false) => [0, 0, 255, 255],
        (false, false) => [0, 255, 0, 255],
    }
}

fn vertices(color: [f32; 4]) -> Vec<u8> {
    let mut bytes = Vec::new();
    for (s, t, x, y) in [
        (0f32, 0f32, -1f32, -1f32),
        (1f32, 0f32, 1f32, -1f32),
        (1f32, 1f32, 1f32, 1f32),
        (0f32, 1f32, -1f32, 1f32),
    ] {
        for value in [x, y, 0., 1.]
            .into_iter()
            .chain(color)
            .chain([0., 0., 1.])
            .chain([s, t, 0., 1.])
        {
            bytes.extend_from_slice(&value.to_bits().to_le_bytes());
        }
        bytes.extend_from_slice(&0u32.to_le_bytes());
    }
    bytes
}

pub(super) fn query_capacity(
    qemu: &mut Qemu,
    sequence: &mut u32,
    function: u32,
    args: &[u32],
    capacity: u32,
) -> (u32, Vec<u8>) {
    *sequence += 1;
    let mut payload = vec![function];
    payload.extend_from_slice(args);
    payload.resize(4, 0);
    qemu.command(&format!("memset 0x200000 {} 0xcc", capacity + 16));
    qemu.write(0x115c, 0x200000);
    qemu.write(0x1160, 0);
    qemu.write(0x1164, capacity);
    qemu.batch(*sequence, &[(11, payload)]);
    // The destination/capacity belong to the accepted submission.
    qemu.write(0x115c, 0x300000);
    qemu.write(0x1164, 1);
    qemu.await_completion(*sequence);
    let length = qemu.read(0x1168) as usize;
    let kind = qemu.read(0x116c);
    assert!(length > 0 && length <= capacity as usize);
    let response = qemu.command(&format!("read 0x200000 {}", capacity + 16));
    let hex = response
        .split_whitespace()
        .nth(1)
        .unwrap()
        .trim_start_matches("0x");
    let bytes = hex
        .as_bytes()
        .as_chunks::<2>()
        .0
        .iter()
        .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
        .collect::<Vec<_>>();
    assert_eq!(bytes.len(), capacity as usize + 16);
    assert!(bytes[length..].iter().all(|byte| *byte == 0xcc));
    (kind, bytes[..length].to_vec())
}

pub(super) fn query(
    qemu: &mut Qemu,
    sequence: &mut u32,
    function: u32,
    args: &[u32],
) -> (u32, Vec<u8>) {
    query_capacity(qemu, sequence, function, args, 512)
}

pub(super) fn words(bytes: &[u8]) -> Vec<u32> {
    bytes
        .as_chunks::<4>()
        .0
        .iter()
        .map(|word| u32::from_le_bytes(*word))
        .collect()
}

fn flat_quad(z: f32, color: [f32; 4]) -> Vec<(u32, Vec<u32>)> {
    let mut records = vec![call(0x0db, &color.map(f32::to_bits)), call(0x01a, &[7])];
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        records.push(call(0x9ea, &[x.to_bits(), y.to_bits(), z.to_bits()]));
    }
    records.push(call(0x216, &[]));
    records
}

fn vector(function: u32, args: &[u32], values: &[f32]) -> (u32, Vec<u32>) {
    let bytes = values
        .iter()
        .flat_map(|value| value.to_le_bytes())
        .collect::<Vec<_>>();
    data_call(function, args, &bytes)
}

fn vector64(function: u32, args: &[u32], values: &[f64]) -> (u32, Vec<u32>) {
    let bytes = values
        .iter()
        .flat_map(|value| value.to_le_bytes())
        .collect::<Vec<_>>();
    data_call(function, args, &bytes)
}

fn homogeneous_quad(s: Option<f32>) -> Vec<(u32, Vec<u32>)> {
    let mut records = vec![call(0x209, &[TEXTURE_2D]), call(0x01a, &[7])];
    for (u, v, x, y) in [
        (0f32, 0f32, -1f32, -1f32),
        (1., 0., 1., -1.),
        (1., 1., 1., 1.),
        (0., 1., -1., 1.),
    ] {
        // Both q and w must be applied, rather than discarding the fourth value.
        records.push(call(
            0x8a9,
            &[
                (s.unwrap_or(u) * 2.).to_bits(),
                (v * 2.).to_bits(),
                0,
                2f32.to_bits(),
            ],
        ));
        records.push(call(
            0x9f8,
            &[(x * 2.).to_bits(), (y * 2.).to_bits(), 0, 2f32.to_bits()],
        ));
    }
    records.push(call(0x216, &[]));
    records.push((7, vec![]));
    records
}

fn assert_normalized_color(bytes: &[u8], expected: [f32; 4]) {
    let actual = words(bytes);
    assert_eq!(actual.len(), expected.len());
    for (actual, expected) in actual.into_iter().zip(expected) {
        // Signed normalized GLint zero may become a tiny positive float on
        // Mesa and exact zero on CGL. Both must produce the same 8-bit pixels.
        assert!((f32::from_bits(actual) - expected).abs() < 1e-6);
    }
}

#[test]
#[ignore = "requires real double-buffer exchange, front-only export and bounded GPU readback"]
fn qemu_front_back_selection_exchange_and_readpixels_preserve_exact_images() {
    const DRAW: u32 = 0x1dc;
    const READ: u32 = 0x7a0;
    const PIXELS: u32 = 0x7a4;
    const FRONT: u32 = 0x404;
    const BACK: u32 = 0x405;
    let clear = |color: [f32; 4]| {
        vec![
            call(0x0a3, &color.map(f32::to_bits)),
            call(0x09a, &[0x4000]),
        ]
    };
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-fb-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    assert_ne!(qemu.read(0x1008) & 0x200, 0);
    assert_ne!(qemu.read(0x1008) & 0x400, 0);
    let mut sequence = 1;
    qemu.batch(sequence, &[(1, vec![0]), (3, vec![16, 8]), (5, vec![])]);
    qemu.await_completion(sequence);
    for (name, expected) in [(0x0c01, BACK), (0x0c02, BACK), (0x0c32, 1)] {
        assert_eq!(
            words(&query(&mut qemu, &mut sequence, 0x329, &[name]).1),
            [expected]
        );
    }

    // Distinct buffers: blue BACK, green FRONT with a red lower half.
    let mut records = clear([0., 0., 1., 1.]);
    records.push(call(DRAW, &[FRONT]));
    records.extend(clear([0., 1., 0., 1.]));
    records.extend([call(0x209, &[0x0c11]), call(0x7f0, &[0, 0, 16, 4])]);
    records.extend(clear([1., 0., 0., 1.]));
    records.extend([call(0x1c5, &[0x0c11]), call(READ, &[FRONT])]);
    sequence += 1;
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    let (kind, rgba) = query(&mut qemu, &mut sequence, PIXELS, &[0, 0, 16 | (8 << 16)]);
    assert_eq!(kind, 2);
    for (pixel, bytes) in rgba.as_chunks::<4>().0.iter().enumerate() {
        assert_eq!(
            bytes,
            if pixel / 16 < 4 {
                &[255, 0, 0, 255]
            } else {
                &[0, 255, 0, 255]
            }
        );
    }
    // A window resize between frontend validation and the locked WNDOBJ
    // snapshot must reject every presentation mode before exchange/export.
    // Retrying with the current size must leave both original buffers intact.
    let original_front = rgba;
    let generation = qemu.read(0x114c);
    for (flags, dimensions) in [(16, [17, 8]), (20, [16, 9]), (24, [15, 8]), (18, [16, 7])] {
        sequence += 1;
        qemu.batch_for(sequence, 1, 1, 1, flags, &[(7, dimensions.to_vec())]);
        let deadline = Instant::now() + Duration::from_secs(5);
        while qemu.read(0x1120) != sequence {
            assert!(Instant::now() < deadline);
            std::thread::sleep(Duration::from_millis(2));
        }
        assert_eq!(qemu.read(0x1124), 5);
        assert_eq!(qemu.read(0x114c), generation);
        assert!(server.take_drawables().is_empty());
        assert_eq!(
            query(&mut qemu, &mut sequence, PIXELS, &[0, 0, 16 | (8 << 16)]).1,
            original_front
        );
    }
    // Explicit front flush exports FRONT with top-left orientation, without
    // exchange, even when the application selected BACK for readback.
    sequence += 1;
    qemu.batch_for(
        sequence,
        1,
        1,
        1,
        20,
        &[call(READ, &[BACK]), (7, vec![16, 8])],
    );
    qemu.await_completion(sequence);
    let front = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &front, |_, y| {
        if y < 4 {
            [0, 255, 0, 255]
        } else {
            [0, 0, 255, 255]
        }
    });
    let (_, rgba) = query(&mut qemu, &mut sequence, PIXELS, &[0, 0, 16 | (8 << 16)]);
    assert!(
        rgba.as_chunks::<4>()
            .0
            .iter()
            .all(|pixel| *pixel == [0, 0, 255, 255])
    );
    drop(front);

    // A regular swap exchanges the existing allocations, exporting blue.
    sequence += 1;
    qemu.batch_for(sequence, 1, 1, 1, 16, &[(7, vec![16, 8])]);
    qemu.await_completion(sequence);
    let blue = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &blue, |_, _| [255, 0, 0, 255]);
    drop(blue);
    let (_, rgba) = query(&mut qemu, &mut sequence, PIXELS, &[3, 2, 4 | (4 << 16)]);
    for (pixel, bytes) in rgba.as_chunks::<4>().0.iter().enumerate() {
        assert_eq!(
            bytes,
            if pixel / 4 < 2 {
                &[255, 0, 0, 255]
            } else {
                &[0, 255, 0, 255]
            }
        );
    }
    // An occluded swap still exchanges, without taking/exporting any slot.
    let generation = qemu.read(0x114c);
    sequence += 1;
    qemu.batch_for(sequence, 1, 1, 1, 24, &[(7, vec![16, 8])]);
    qemu.await_completion(sequence);
    assert_eq!(qemu.read(0x114c), generation);
    assert!(server.take_drawables().is_empty());
    let (_, rgba) = query(&mut qemu, &mut sequence, PIXELS, &[0, 0, 1 | (1 << 16)]);
    assert_eq!(rgba, [0, 0, 255, 255]);

    // Context2 targets the same drawable but has its own selection state.
    sequence += 1;
    qemu.batch_for(
        sequence,
        1,
        2,
        1,
        0,
        &[(1, vec![1]), (5, vec![]), call(DRAW, &[0x408])],
    );
    qemu.await_completion(sequence);
    let mut records = clear([1., 1., 0., 1.]);
    records.push(call(DRAW, &[0]));
    records.extend(clear([1., 0., 1., 1.]));
    records.push(call(0x484, &[0x0c50, 0x1102]));
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 1, 0, &records);
    qemu.await_completion(sequence);
    // Switching back must preserve context1's FRONT draw and BACK read modes.
    sequence += 1;
    qemu.batch(sequence, &[(5, vec![])]);
    qemu.await_completion(sequence);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x0c01]).1),
        [FRONT]
    );
    for buffer in [FRONT, BACK] {
        sequence += 1;
        qemu.batch(sequence, &[call(READ, &[buffer])]);
        qemu.await_completion(sequence);
        let (_, rgba) = query(&mut qemu, &mut sequence, PIXELS, &[0, 0, 16 | (8 << 16)]);
        assert!(
            rgba.as_chunks::<4>()
                .0
                .iter()
                .all(|pixel| *pixel == [255, 255, 0, 255])
        );
    }
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 1, 0, &[(5, vec![])]);
    qemu.await_completion(sequence);
    // Restore current ID for the generic query helper while retaining the
    // other context's independent state long enough to verify it explicitly.
    qemu.write(0x115c, 0x200000);
    qemu.write(0x1164, 512);
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 1, 0, &[(11, vec![0x329, 0x0c01, 0, 0])]);
    qemu.await_completion(sequence);
    assert!(qemu.command("read 0x200000 4").ends_with("0x00000000"));
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 1, 0, &[(11, vec![0x329, 0x0c50, 0, 0])]);
    qemu.await_completion(sequence);
    assert!(qemu.command("read 0x200000 4").ends_with("0x02110000"));

    // A globally valid tile that crosses this drawable's edge is rejected
    // before touching its destination, and later valid queries still work.
    qemu.command("memset 0x200000 512 0x5a");
    qemu.write(0x115c, 0x200000);
    qemu.write(0x1164, 512);
    sequence += 1;
    qemu.batch(sequence, &[(11, vec![PIXELS, 15, 0, 2 | (1 << 16)])]);
    let deadline = Instant::now() + Duration::from_secs(5);
    while qemu.read(0x1120) != sequence {
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(2));
    }
    assert_eq!(qemu.read(0x1124), 5);
    assert_eq!(qemu.read(0x1168), 0);
    assert!(
        qemu.command("read 0x200000 512")
            .ends_with(&format!("0x{}", "5a".repeat(512)))
    );
    // No GL errors leak from export FBO selection/restoration or readback.
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    sequence += 1;
    qemu.batch(sequence, &[(8, vec![])]);
    qemu.await_completion(sequence);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires native query completion; verifies result DMA cancellation on reset"]
fn qemu_reset_fences_queued_query_dma_before_guest_result_page_reuse() {
    use dreamgpu::desktop::DesktopOp;
    use std::io::Write as _;

    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-qr-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let cpu_socket = directory.join("cpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut cpu = crate::ShmemServer::new(cpu_socket.to_str().unwrap()).unwrap();
    let mut qemu = Qemu::start_with_display(&directory, &socket, Some(&cpu_socket));
    for (index, value) in [(1, 64), (2, 32), (3, 32), (4, 0x41)] {
        qemu.command(&format!(
            "writew {:#x} {value:#x}",
            0xf0000500u32 + index * 2
        ));
    }
    qemu.command("outb 0x3c0 0x20");
    drop(wait_cpu_frame(&mut cpu, None));
    qemu.batch(1, &[(1, vec![0]), (3, vec![32, 32]), (5, vec![])]);
    qemu.await_completion(1);
    let generation = qemu.read(0x103c);
    let mut hex = String::from("0x");
    for word in [11u32, 48, 1, 1, 1, 0, 0, generation, 0x329, 0x0ba2, 0, 0] {
        for byte in word.to_le_bytes() {
            write!(hex, "{byte:02x}").unwrap();
        }
    }
    qemu.command(&format!("write 0x100000 48 {hex}"));
    for (register, value) in [
        (0x1104, 0x100000),
        (0x1108, 0),
        (0x110c, 48),
        (0x1110, 2),
        (0x1114, generation),
        (0x115c, 0x200000),
        (0x1160, 0),
        (0x1164, 512),
    ] {
        qemu.write(register, value);
    }
    // One short qtest socket write executes under its BQL callback. The large
    // unrelated RAM write lets the GL worker queue its completion while BQL
    // keeps the result DMA pending, then RESET fences it before page reuse.
    let commands = "writel 0xf0001118 1\nmemset 0x800000 0x2000000 0x5a\nreadl 0xf000111c\nwritel 0xf0001038 1\nmemset 0x200000 512 0xa5\n";
    qemu.commands.write_all(commands.as_bytes()).unwrap();
    let mut responses = Vec::new();
    while responses.len() < 5 {
        let mut response = String::new();
        assert_ne!(qemu.replies.read_line(&mut response).unwrap(), 0);
        if response.starts_with("IRQ ") {
            continue;
        }
        assert!(response.starts_with("OK"), "{response}");
        responses.push(response);
    }
    assert_eq!(
        u32::from_str_radix(
            responses[2]
                .split_whitespace()
                .nth(1)
                .unwrap()
                .trim_start_matches("0x"),
            16
        )
        .unwrap(),
        1,
        "query was pending at reset"
    );
    assert_ne!(qemu.read(0x103c), generation);
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(error) = server.take_error() {
            panic!("reset transport: {error}");
        }
        if let Some(batch) = server.take_desktop_update()
            && matches!(batch.operations.as_slice(), [DesktopOp::Reset])
        {
            break;
        }
        assert!(
            Instant::now() < deadline,
            "worker reset marker did not arrive"
        );
        std::thread::sleep(Duration::from_millis(2));
    }
    qemu.batch(3, &[(1, vec![0]), (3, vec![32, 32]), (5, vec![])]);
    qemu.await_completion(3);
    assert_eq!(qemu.read(0x1168), 0);
    let result = qemu.command("read 0x200000 512");
    assert_eq!(
        result.split_whitespace().nth(1).unwrap(),
        format!("0x{}", "a5".repeat(512))
    );
    drop(qemu);
    drop(cpu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires native GPU; exact large-batch ordering and overflow pixels"]
fn qemu_large_scalar_batches_preserve_order_and_reject_overflow() {
    fn persistent_batch(
        qemu: &mut Qemu,
        sequence: u32,
        generation: u32,
        records: &[(u32, Vec<u32>)],
    ) {
        use std::fmt::Write as _;
        let mut hex = String::from("0x");
        let mut bytes = 0;
        for (opcode, args) in records {
            let size = 32 + args.len() as u32 * 4;
            for word in [*opcode, size, 1, 1, 1, 0, 0, generation]
                .iter()
                .chain(args)
            {
                for byte in word.to_le_bytes() {
                    write!(hex, "{byte:02x}").unwrap();
                }
            }
            bytes += size;
        }
        qemu.command(&format!("write 0x100000 {bytes} {hex}"));
        // The driver may retain command GPA and programmed generation until
        // reset. Only the current byte count/sequence/doorbell need MMIO.
        qemu.write(0x110c, bytes);
        qemu.write(0x1110, sequence);
        qemu.write(0x1118, 1);
    }
    let (device, queue, host) = native_gpu();
    let directory =
        std::env::temp_dir().join(format!("dg-biggl-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    assert_eq!(qemu.read(0x112c), 1024);
    qemu.batch(1, &[(1, vec![0]), (3, vec![32, 32]), (5, vec![])]);
    qemu.await_completion(1);
    let generation = qemu.read(0x103c);
    let red = call(0x0db, &[1f32.to_bits(), 0, 0, 1f32.to_bits()]);
    let mut records = vec![
        call(0x0a3, &[0, 0, 0, 1f32.to_bits()]),
        call(0x09a, &[0x4000]),
        red.clone(),
        call(0x01a, &[7]),
        call(0x9dc, &[(-1f32).to_bits(), (-1f32).to_bits()]),
        call(0x9dc, &[1f32.to_bits(), (-1f32).to_bits()]),
    ];
    records.resize(1024, call(0x0db, &[0, 0, 1f32.to_bits(), 1f32.to_bits()]));
    // The final allowed record must execute: dropping the tail leaves blue
    // current color and changes the last two vertices after the boundary.
    records[1023] = red.clone();
    persistent_batch(&mut qemu, 2, generation, &records);
    qemu.await_completion(2);
    // An unfinished Begin survives the submission boundary. Completing its
    // vertices and swapping must retain every scalar's original ordering.
    persistent_batch(
        &mut qemu,
        3,
        generation,
        &[
            call(0x9dc, &[1f32.to_bits(), 1f32.to_bits()]),
            call(0x9dc, &[(-1f32).to_bits(), 1f32.to_bits()]),
            call(0x216, &[]),
            (7, vec![]),
        ],
    );
    qemu.await_completion(3);
    let frame = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &frame, |_, _| [0, 0, 255, 255]);
    drop(frame);
    persistent_batch(
        &mut qemu,
        4,
        generation,
        &[
            call(0x1dc, &[0x0404]), // FRONT
            call(0x7a0, &[0x0404]),
            call(0x0a3, &[0, 0, 1f32.to_bits(), 1f32.to_bits()]),
        ],
    );
    qemu.await_completion(4);
    // If validation executes a prefix, this clear turns FRONT blue. The
    // rejected 1025th record must prevent the entire batch from executing.
    records.clear();
    records.push(call(0x09a, &[0x4000]));
    records.resize(1025, red);
    persistent_batch(&mut qemu, 5, generation, &records);
    assert_eq!(qemu.read(0x1120), 5);
    assert_eq!(qemu.read(0x1124), 1); // DG_GL_ERROR_BATCH
    let mut sequence = 5;
    let (_, pixels) = query(&mut qemu, &mut sequence, 0x7a4, &[0, 0, 1 | (1 << 16)]);
    assert_eq!(pixels, [255, 0, 0, 255]); // canonical query result is RGBA
    assert_eq!(qemu.read(0x111c), 2); // DONE without ERROR
    assert_eq!(qemu.read(0x1124), 0); // prior failure cannot leak into success
    qemu.write(0x115c, 0x200000);
    qemu.write(0x1160, 0);
    qemu.write(0x1164, 512);
    for x in 0..2 {
        sequence += 1;
        qemu.command("memset 0x200000 512 0xcc");
        persistent_batch(
            &mut qemu,
            sequence,
            generation,
            &[(11, vec![0x7a4, x, 0, 1 | (1 << 16)])],
        );
        qemu.await_completion(sequence);
        assert_eq!(qemu.read(0x1168), 4);
        assert_eq!(qemu.read(0x115c), 0x200000);
        assert_eq!(qemu.read(0x1164), 512);
        let result = qemu.command("read 0x200000 4");
        assert_eq!(result.split_whitespace().nth(1).unwrap(), "0xff0000ff");
    }
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires native zero allocation and tiled texture upload; exact GPU pixels"]
fn qemu_zero_texture_allocation_accepts_small_tiles_and_preserves_state() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-tz-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 1;
    qemu.batch(
        sequence,
        &[
            (1, vec![0]),
            (3, vec![256, 256]),
            (5, vec![]),
            call(BIND_TEXTURE, &[TEXTURE_2D, 17]),
            call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2801, 0x2600]),
            call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2800, 0x2600]),
            call(
                0x0a3,
                &[
                    0.25f32.to_bits(),
                    0.5f32.to_bits(),
                    0.75f32.to_bits(),
                    1f32.to_bits(),
                ],
            ),
            call(0x0f5, &[0, 1, 0, 0]),
            call(0x7f0, &[1, 2, 3, 4]),
            call(0x209, &[0x0c11]),
            data_call(
                TEX_IMAGE_2D,
                &[TEXTURE_2D, 0, 0x1908, 256, 256, 0, 0x1908, 0x1401],
                &[],
            ),
        ],
    );
    qemu.await_completion(sequence);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x305, &[0x0c22]).1),
        [
            0.25f32.to_bits(),
            0.5f32.to_bits(),
            0.75f32.to_bits(),
            1f32.to_bits()
        ]
    );
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x2cb, &[0x0c23]).1,
        [0, 1, 0, 0]
    );
    assert_eq!(query(&mut qemu, &mut sequence, 0x4ba, &[0x0c11]).1, [1]);
    let mut records = vec![call(0x0f5, &[1, 1, 1, 1]), call(0x1c5, &[0x0c11])];
    records.extend(homogeneous_quad(None));
    sequence += 1;
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0; 4]);
    drop(image);

    // Every upload record fits the guest's fixed 64 KiB channel, though the image
    // is 256 KiB. Verify every texel, including boundaries between row tiles.
    for y in (0..256u32).step_by(32) {
        let mut pixels = Vec::new();
        for row in y..y + 32 {
            for x in 0..256u32 {
                pixels.extend_from_slice(&[x as u8, row as u8, 0, 255]);
            }
        }
        let record = data_call(
            TEX_SUB_IMAGE_2D,
            &[TEXTURE_2D, 0, 0, y, 256, 32, 0x1908, 0x1401],
            &pixels,
        );
        assert!(32 + record.1.len() * 4 < 65536);
        sequence += 1;
        qemu.batch(sequence, &[record]);
        qemu.await_completion(sequence);
    }
    sequence += 1;
    qemu.batch(sequence, &homogeneous_quad(None));
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |x, y| {
        [0, (255 - y) as u8, x as u8, 255]
    });
    drop(image);

    // Default texture0 and legacy alpha/luminance formats cannot universally
    // attach to native FBOs. Their allocation still returns defined zero texels.
    for (name, internal, format, expected) in [
        (0, 0x1908, 0x1907, [0; 4]),
        (31, 0x803c, 0x1906, [255, 255, 255, 0]),
        (32, 0x8040, 0x1909, [0, 0, 0, 255]),
        (33, 0x8045, 0x190a, [0; 4]),
        (34, 0x8051, 0x1907, [0, 0, 0, 255]),
    ] {
        records = vec![
            call(BIND_TEXTURE, &[TEXTURE_2D, name]),
            call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2801, 0x2600]),
            call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2800, 0x2600]),
            data_call(
                TEX_IMAGE_2D,
                &[TEXTURE_2D, 0, internal, 256, 256, 0, format, 0x1401],
                &[],
            ),
        ];
        records.extend(homogeneous_quad(None));
        sequence += 1;
        qemu.batch(sequence, &records);
        qemu.await_completion(sequence);
        let image = receive(&server, &ready, 1, 1);
        assert_gpu_image(&device, &queue, &image, |_, _| expected);
        drop(image);
    }

    // Retain object17 in context2, delete/reuse its name in context1, then zero
    // the retained object. A host-name attachment must never clear the new one.
    sequence += 1;
    qemu.batch_for(
        sequence,
        1,
        2,
        1,
        0,
        &[
            (1, vec![1]),
            (5, vec![]),
            call(BIND_TEXTURE, &[TEXTURE_2D, 17]),
        ],
    );
    qemu.await_completion(sequence);
    sequence += 1;
    qemu.batch(
        sequence,
        &[
            (5, vec![]),
            data_call(DELETE_TEXTURES, &[1], &17u32.to_le_bytes()),
            call(BIND_TEXTURE, &[TEXTURE_2D, 17]),
            call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2801, 0x2600]),
            data_call(
                TEX_IMAGE_2D,
                &[TEXTURE_2D, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401],
                &[255, 0, 255, 255],
            ),
        ],
    );
    qemu.await_completion(sequence);
    records = vec![data_call(
        TEX_IMAGE_2D,
        &[TEXTURE_2D, 0, 0x1908, 256, 256, 0, 0x1908, 0x1401],
        &[],
    )];
    records.extend(homogeneous_quad(None));
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 1, 0, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0; 4]);
    drop(image);
    sequence += 1;
    qemu.batch(sequence, &homogeneous_quad(None));
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [255, 0, 255, 255]);
    drop(image);
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU native texture copies; exact GPU pixel acceptance"]
fn qemu_texture_copies_vectors_and_homogeneous_vertices_preserve_pixels() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-tc-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    let mut records = vec![(1, vec![0]), (3, vec![32, 32]), (5, vec![])];
    records.extend(upload(&[
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    ]));
    records.extend(quad());
    qemu.batch(1, &records);
    qemu.await_completion(1);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |x, y| checker(x, y, false));
    drop(image);

    // Copy the canonical drawable directly to a new guest texture, then erase
    // the source. The exported image is top-left, GL's source remains bottom-left.
    records = vec![
        call(BIND_TEXTURE, &[TEXTURE_2D, 29]),
        data_call(0x8e4, &[TEXTURE_2D, 0x2801], &0x2600i32.to_le_bytes()),
        vector(0x8de, &[TEXTURE_2D, 0x2800], &[0x2600 as f32]),
        call(0x7a0, &[0x404]), // read the image that was just swapped to FRONT
        call(0x156, &[TEXTURE_2D, 0, 0x1908, 0, 0, 32, 32, 0]),
        call(0x7a0, &[0x405]), // subsequent source edits/readback use BACK
        call(0x0a3, &[0, 0, 0, 1f32.to_bits()]),
        call(0x09a, &[0x4000]),
    ];
    records.extend(homogeneous_quad(None));
    qemu.batch(2, &records);
    qemu.await_completion(2);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |x, y| checker(x, y, false));
    drop(image);

    // Copy a yellow top-left source rectangle into the texture's bottom-left.
    records = vec![
        call(0x209, &[0x0c11]),
        call(0x7f0, &[0, 24, 8, 8]),
        call(0x0a3, &[1f32.to_bits(), 1f32.to_bits(), 0, 1f32.to_bits()]),
        call(0x09a, &[0x4000]),
        call(0x1c5, &[0x0c11]),
        call(0x15a, &[TEXTURE_2D, 0, 0, 0, 0, 24, 8, 8]),
    ];
    records.extend(homogeneous_quad(None));
    qemu.batch(3, &records);
    qemu.await_completion(3);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |x, y| {
        if x < 8 && y >= 24 {
            [0, 255, 255, 255]
        } else {
            checker(x, y, false)
        }
    });
    drop(image);

    // Shared contexts see the copied guest object. Invalid destination bounds
    // must leave prior texels intact; out-of-source pixels are legal undefined values.
    let mut sequence = 3;
    for args in [
        [TEXTURE_2D, 0, 31, 0, 0, 0, 2, 2],
        [TEXTURE_2D, 0, 0, 31, 0, 0, 2, 2],
    ] {
        sequence += 1;
        qemu.batch(sequence, &[call(0x15a, &args)]);
        let deadline = Instant::now() + Duration::from_secs(5);
        while qemu.read(0x1120) != sequence {
            assert!(Instant::now() < deadline);
            std::thread::sleep(Duration::from_millis(2));
        }
        assert_eq!(qemu.read(0x1124), 11); // DG_GL_ERROR_TEXTURE
    }
    sequence += 1;
    qemu.batch_for(
        sequence,
        1,
        2,
        1,
        0,
        &[
            (1, vec![1]),
            (5, vec![]),
            call(BIND_TEXTURE, &[TEXTURE_2D, 29]),
        ],
    );
    qemu.await_completion(sequence);
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 1, 0, &homogeneous_quad(None));
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |x, y| {
        if x < 8 && y >= 24 {
            [0, 255, 255, 255]
        } else {
            checker(x, y, false)
        }
    });
    drop(image);

    // Return to context1 to use the bounded query helper. Four-element integer
    // border colors normalize into RGBA; environment vectors remain per-context.
    sequence += 1;
    qemu.batch(
        sequence,
        &[
            (5, vec![]),
            data_call(
                0x8e4,
                &[TEXTURE_2D, 0x1004],
                &[i32::MAX, 0, 0, i32::MAX]
                    .iter()
                    .flat_map(|v| v.to_le_bytes())
                    .collect::<Vec<_>>(),
            ),
            call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2802, 0x812d]),
        ],
    );
    qemu.await_completion(sequence);
    assert_normalized_color(
        &query(&mut qemu, &mut sequence, 0x418, &[TEXTURE_2D, 0x1004]).1,
        [1., 0., 0., 1.],
    );
    sequence += 1;
    qemu.batch(sequence, &homogeneous_quad(Some(-2.)));
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0, 0, 255, 255]);
    drop(image);

    sequence += 1;
    records = vec![
        vector(0x8de, &[TEXTURE_2D, 0x1004], &[1., 1., 1., 1.]),
        vector(0x8c5, &[0x2300, 0x2201], &[0., 1., 0., 1.]),
        data_call(0x8c7, &[0x2300, 0x2200], &0x0be2i32.to_le_bytes()),
    ];
    records.extend(homogeneous_quad(Some(-2.)));
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0, 255, 0, 255]);
    drop(image);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x40c, &[0x2300, 0x2201]).1),
        [0, 1f32.to_bits(), 0, 1f32.to_bits()]
    );
    sequence += 1;
    qemu.batch(
        sequence,
        &[data_call(
            0x8c7,
            &[0x2300, 0x2201],
            &[0, 0, i32::MAX, i32::MAX]
                .iter()
                .flat_map(|v| v.to_le_bytes())
                .collect::<Vec<_>>(),
        )],
    );
    qemu.await_completion(sequence);
    assert_normalized_color(
        &query(&mut qemu, &mut sequence, 0x40c, &[0x2300, 0x2201]).1,
        [0., 0., 1., 1.],
    );
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU fixed-function lighting and native GPU; exact pixel acceptance"]
fn qemu_lighting_fog_texgen_and_clip_planes_draw_native_pixels() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-lit-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    let mut records = vec![
        (1, vec![0]),
        (3, vec![32, 32]),
        (5, vec![]),
        vector(0x4ee, &[0x0b53], &[0., 0., 0., 1.]), // global ambient
        vector(0x4ea, &[0x4000, 0x1201], &[1., 0., 0., 1.]), // light diffuse
        vector(0x4ea, &[0x4000, 0x1203], &[0., 0., 1., 0.]), // directional
        vector(0x537, &[0x408, 0x1201], &[1.; 4]),   // material diffuse
        call(0x4e9, &[0x4000, 0x1207, 1f32.to_bits()]), // attenuation
        call(0x536, &[0x408, 0x1601, 16f32.to_bits()]), // shininess
        call(0x63e, &[0, 0, 1f32.to_bits()]),
        call(0x209, &[0x0b50]),
        call(0x209, &[0x4000]),
    ];
    records.extend(flat_quad(0., [1.; 4]));
    records.push((7, vec![]));
    qemu.batch(1, &records);
    qemu.await_completion(1);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0, 0, 255, 255]);
    drop(image);
    records = vec![call(0x63e, &[0, 0, (-1f32).to_bits()])];
    records.extend(flat_quad(0., [1.; 4]));
    records.push((7, vec![]));
    qemu.batch(2, &records);
    qemu.await_completion(2);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0, 0, 0, 255]);
    drop(image);
    records = vec![vector(0x537, &[0x408, 0x1600], &[0., 1., 0., 1.])];
    records.extend(flat_quad(0., [1.; 4]));
    records.push((7, vec![]));
    qemu.batch(3, &records);
    qemu.await_completion(3);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0, 255, 0, 255]);
    drop(image);
    let mut sequence = 3;
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x341, &[0x404, 0x1600]).1),
        [0, 1f32.to_bits(), 0, 1f32.to_bits()]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x330, &[0x4000, 0x1203]).1),
        [0, 0, 1f32.to_bits(), 0]
    );
    records = vec![
        vector(0x537, &[0x408, 0x1600], &[0., 0., 0., 1.]),
        vector(0x4ea, &[0x4000, 0x1201], &[1.; 4]),
        call(0x63e, &[0, 0, 1f32.to_bits()]),
        call(0x4ed, &[0x0b52, 1f32.to_bits()]),
        call(0x0f8, &[0x408, 0x1201]),
        call(0x209, &[0x0b57]),
    ];
    records.extend(flat_quad(0., [0., 0., 1., 1.]));
    records.push((7, vec![]));
    sequence += 1;
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [255, 0, 0, 255]);
    drop(image);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x0b52]).1),
        [1]
    );
    records = vec![
        call(0x1c5, &[0x0b57]),
        call(0x1c5, &[0x0b50]),
        call(0x209, &[0x0b60]),
        call(0x25d, &[0x0b65, (0x2601 as f32).to_bits()]),
        call(0x25d, &[0x0b63, 0]),
        call(0x25d, &[0x0b64, 1f32.to_bits()]),
        vector(0x25f, &[0x0b66], &[0., 0., 1., 1.]),
    ];
    records.extend(flat_quad(-1., [1.; 4]));
    records.push((7, vec![]));
    sequence += 1;
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [255, 0, 0, 255]);
    drop(image);

    records = vec![call(0x1c5, &[0x0b60])];
    records.extend(upload(&[
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    ]));
    records.extend([
        call(0x209, &[TEXTURE_2D]),
        call(0x8cd, &[0x2000, 0x2500, (0x2401 as f32).to_bits()]),
        call(0x8cd, &[0x2001, 0x2500, (0x2401 as f32).to_bits()]),
        vector64(0x8cc, &[0x2000, 0x2501], &[0.5, 0., 0., 0.5]),
        vector(0x8ce, &[0x2001, 0x2501], &[0., 0.5, 0., 0.5]),
        call(0x209, &[0x0c60]),
        call(0x209, &[0x0c61]),
    ]);
    records.extend(flat_quad(0., [1.; 4]));
    records.push((7, vec![]));
    sequence += 1;
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |x, y| checker(x, y, false));
    drop(image);
    let plane = query(&mut qemu, &mut sequence, 0x410, &[0x2000, 0x2501]).1;
    assert_eq!(
        plane,
        [0.5f64, 0., 0., 0.5]
            .into_iter()
            .flat_map(f64::to_le_bytes)
            .collect::<Vec<_>>()
    );

    records = vec![
        call(0x93c, &[0.25f32.to_bits(), 0, 0]),
        vector64(0x0be, &[0x3000], &[1., 0., 0., 0.]),
        call(0x500, &[]),
        call(0x209, &[0x3000]),
        call(0x0a3, &[0, 0, 0, 1f32.to_bits()]),
        call(0x09a, &[0x4000]),
    ];
    records.extend(flat_quad(0., [1.; 4]));
    records.push((7, vec![]));
    sequence += 1;
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |x, y| {
        if x < 20 {
            [0, 0, 0, 255]
        } else {
            checker(x, y, false)
        }
    });
    drop(image);
    let plane = query(&mut qemu, &mut sequence, 0x2d4, &[0x3000]).1;
    assert_eq!(
        plane,
        [1f64, 0., 0., -0.25]
            .into_iter()
            .flat_map(f64::to_le_bytes)
            .collect::<Vec<_>>()
    );
    let frustum = [-1f64, 1., -1., 1., 1., 10.]
        .into_iter()
        .flat_map(|value| {
            let bits = value.to_bits();
            [bits as u32, (bits >> 32) as u32]
        })
        .collect::<Vec<_>>();
    sequence += 1;
    qemu.batch(
        sequence,
        &[
            call(0x549, &[0x1701]),
            call(0x500, &[]),
            call(0x28f, &frustum),
        ],
    );
    qemu.await_completion(sequence);
    let matrix = query(&mut qemu, &mut sequence, 0x2fb, &[0x0ba7]).1;
    let expected = [
        1f64,
        0.,
        0.,
        0.,
        0.,
        1.,
        0.,
        0.,
        0.,
        0.,
        -11. / 9.,
        -1.,
        0.,
        0.,
        -20. / 9.,
        0.,
    ];
    for (value, expected) in matrix.as_chunks::<8>().0.iter().zip(expected) {
        assert!((f64::from_le_bytes(*value) - expected).abs() < 0.000001);
    }
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    sequence += 1;
    qemu.batch(sequence, &[(8, vec![])]);
    drive_lifecycle(&mut qemu, sequence, &server, &device, &ready);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU raster state and native GPU; exact pixel acceptance"]
fn qemu_raster_state_preserves_masks_depth_stencil_alpha_and_culling() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-rs-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    let mut records = vec![
        (1, vec![0]),
        (3, vec![32, 32]),
        (5, vec![]),
        // This test accumulates masked color writes across publications.
        // Explicit FRONT rendering retains those pixels without a swap.
        call(0x1dc, &[0x404]),
        call(0x0a3, &[0, 0, 1f32.to_bits(), 1f32.to_bits()]),
        call(0x09a, &[0x4500]),
        call(0x209, &[0x0b71]), // GL_DEPTH_TEST
        call(0x1ba, &[0]),      // no depth write for the near red quad
    ];
    records.extend(flat_quad(0.5, [1., 0., 0., 1.]));
    records.push(call(0x1ba, &[1]));
    records.extend(flat_quad(0.8, [0., 1., 0., 1.]));
    records.extend(flat_quad(0.9, [0., 0., 1., 1.]));
    records.push((7, vec![]));
    qemu.batch_for(1, 1, 1, 1, 4, &records);
    qemu.await_completion(1);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0, 255, 0, 255]);
    drop(image);

    // The export blit must ignore guest color masks while preserving them.
    qemu.batch_for(
        2,
        1,
        1,
        1,
        4,
        &[
            call(0x0f5, &[1, 0, 1, 0]),
            call(0x0a3, &[1f32.to_bits(), 0, 0, 0]),
            call(0x09a, &[0x4000]),
            (7, vec![]),
        ],
    );
    qemu.await_completion(2);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0, 255, 255, 255]);
    drop(image);
    let mut sequence = 2;
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x2cb, &[0x0c23]).1,
        [1, 0, 1, 0]
    );

    records = vec![
        call(0x1c5, &[0x0b71]), // GL_DEPTH_TEST disabled
        call(0x0b5, &[0]),
        call(0x847, &[0xff]),
        call(0x09a, &[0x400]),
        call(0x209, &[0x0b90]), // GL_STENCIL_TEST
        call(0x209, &[0x0c11]),
        call(0x7f0, &[0, 0, 16, 32]),
        call(0x844, &[0x207, 3, 0xff]),         // ALWAYS, reference3
        call(0x849, &[0x1e00, 0x1e00, 0x1e01]), // KEEP, KEEP, REPLACE
        call(0x0f5, &[0, 0, 0, 0]),
    ];
    records.extend(flat_quad(0., [0., 0., 1., 1.]));
    records.extend([
        call(0x1c5, &[0x0c11]),
        call(0x0f5, &[1, 1, 1, 1]),
        call(0x844, &[0x202, 3, 0xff]), // EQUAL
        call(0x849, &[0x1e00, 0x1e00, 0x1e00]),
    ]);
    records.extend(flat_quad(0., [0., 0., 1., 1.]));
    records.push((7, vec![]));
    sequence += 1;
    qemu.batch_for(sequence, 1, 1, 1, 4, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |x, _| {
        if x < 16 {
            [255, 0, 0, 255]
        } else {
            [0, 255, 255, 255]
        }
    });
    drop(image);
    records = vec![call(0x847, &[0]), call(0x09a, &[0x400])];
    records.extend(flat_quad(0., [1., 0., 0., 1.]));
    records.push((7, vec![]));
    sequence += 1;
    qemu.batch_for(sequence, 1, 1, 1, 4, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |x, _| {
        if x < 16 {
            [0, 0, 255, 255]
        } else {
            [0, 255, 255, 255]
        }
    });
    drop(image);

    records = vec![
        call(0x1c5, &[0x0b90]),
        call(0x0a3, &[0, 0, 1f32.to_bits(), 1f32.to_bits()]),
        call(0x09a, &[0x4000]),
        call(0x209, &[0x0bc0]),
        call(0x00c, &[0x204, 0.5f32.to_bits()]),
    ];
    records.extend(flat_quad(0., [1., 0., 0., 0.25]));
    records.extend([call(0x209, &[0x0b44]), call(0x180, &[0x404])]);
    records.extend(flat_quad(0., [0., 1., 0., 1.]));
    records.push((7, vec![]));
    sequence += 1;
    qemu.batch_for(sequence, 1, 1, 1, 4, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [255, 0, 0, 255]);
    drop(image);
    records = vec![call(0x28e, &[0x900])]; // clockwise front, our quad is now back
    records.extend(flat_quad(0., [1., 0., 0., 1.]));
    records.extend([
        call(0x6a8, &[0x408, 0x1b02]),
        call(0x6a9, &[1f32.to_bits(), 2f32.to_bits()]),
        call(0x4f6, &[2f32.to_bits()]),
        call(0x4f5, &[2, 0xaaaa]),
        call(0x6a4, &[3f32.to_bits()]),
        call(0x829, &[0x1d00]),
        call(0x1bb, &[0, 0x3fd00000, 0, 0x3fe80000]),
        (7, vec![]),
    ]);
    sequence += 1;
    qemu.batch_for(sequence, 1, 1, 1, 4, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0, 0, 255, 255]);
    drop(image);
    for (pname, expected) in [
        (0x0b46, 0x900),
        (0x0b97, 3),
        (0x0b98, 0),
        (0x0b25, 0xaaaa),
        (0x0b26, 2),
        (0x0b54, 0x1d00),
    ] {
        assert_eq!(
            words(&query(&mut qemu, &mut sequence, 0x329, &[pname]).1),
            [expected]
        );
    }
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x305, &[0x0b70]).1),
        [0.25f32.to_bits(), 0.75f32.to_bits()]
    );
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    sequence += 1;
    qemu.batch(sequence, &[(8, vec![])]);
    drive_lifecycle(&mut qemu, sequence, &server, &device, &ready);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU and native GPU; context-before-image teardown acceptance"]
fn qemu_export_images_outlive_the_last_current_context() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-ctx-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    qemu.batch(
        1,
        &[
            (1, vec![0]),
            (3, vec![32, 32]),
            (5, vec![]),
            call(0x0a3, &[1f32.to_bits(), 0, 0, 1f32.to_bits()]),
            call(0x09a, &[0x4000]),
            (7, vec![]),
        ],
    );
    qemu.await_completion(1);
    let image = receive(&server, &ready, 1, 1);
    assert_gpu_image(&device, &queue, &image, |_, _| [0, 0, 255, 255]);

    // This is the first image destruction in a fresh QEMU process. Destroy
    // its current guest context first, while the consumer still owns storage.
    qemu.batch(2, &[(2, vec![])]);
    qemu.await_completion(2);
    assert_gpu_image(&device, &queue, &image, |_, _| [0, 0, 255, 255]);
    qemu.batch(3, &[(4, vec![])]);
    assert_eq!(
        qemu.read(0x111c) & 1,
        1,
        "image lease must delay retirement"
    );
    drop(image);
    drive_lifecycle(&mut qemu, 3, &server, &device, &ready);

    // Resource retirement leaves the platform usable for another client.
    qemu.batch_for(
        4,
        2,
        1,
        1,
        0,
        &[(1, vec![0]), (3, vec![16, 16]), (5, vec![])],
    );
    qemu.await_completion(4);
    qemu.batch_for(5, 2, 1, 1, 0, &[(8, vec![])]);
    drive_lifecycle(&mut qemu, 5, &server, &device, &ready);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU coherence fault-stop and native GPU; diskless acceptance"]
fn qemu_failed_desktop_readback_stops_guest_and_preserves_the_canvas() {
    let (device, queue, host) = native_gpu();
    let directory =
        std::env::temp_dir().join(format!("dg-fault-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    for (index, value) in [(1, 64), (2, 32), (3, 32), (4, 0x41)] {
        qemu.command(&format!(
            "writew {:#x} {value:#x}",
            0xf0000500u32 + index * 2
        ));
    }
    qemu.command("memset 0xe0000000 8192 0x55");
    let mut canvas = dreamgpu::presentation::desktop::DesktopCanvas::default();
    qemu.batch(1, &[(9, desktop_command(1, 0, 0, 64, 32, 0, 256))]);
    drive_desktop(
        &mut qemu,
        1,
        1,
        true,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    qemu.batch(2, &[(9, desktop_command(7, 0, 0, 64, 32, 0, 256))]);
    let deadline = Instant::now() + Duration::from_secs(5);
    let mut readback = loop {
        if let Some(batch) = server.take_desktop_update() {
            break batch;
        }
        assert!(Instant::now() < deadline);
        let _ = ready.recv_timeout(Duration::from_millis(2));
    };
    assert_eq!(readback.operations.len(), 1);
    let dreamgpu::desktop::DesktopOp::Readback { reply, .. } = &readback.operations[0] else {
        panic!("expected a desktop readback");
    };
    reply.complete(Err("injected renderer readback failure".into()));
    while qemu.read(0x1170) == 0 {
        assert!(Instant::now() < deadline);
        let _ = ready.recv_timeout(Duration::from_millis(2));
    }
    assert_eq!(qemu.read(0x1170), 1);
    assert_eq!(qemu.read(0x1174), 7);
    assert_eq!(qemu.read(0x1178), 2);
    assert!(server.desktop_active());
    assert!(server.take_desktop_update().is_none());
    assert!(canvas.is_mixed());
    assert_eq!(canvas.generation(), 1);
    assert!(canvas.texture().is_some());
    qemu.write(0x1038, 1); // engine reset cannot clear a coherence fault
    assert_eq!(qemu.read(0x1170), 1);

    // Diagnostic-only local readback proves the retained canvas still contains
    // its exact pixels; it is not a guest CPU-ownership transition.
    let (complete, pixels) = mpsc::sync_channel(1);
    let dreamgpu::desktop::DesktopOp::Readback { reply, .. } = &mut readback.operations[0] else {
        unreachable!()
    };
    *reply = complete.into();
    canvas.apply(&device, &queue, readback).unwrap();
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    let pixels = pixels
        .recv_timeout(Duration::from_secs(5))
        .unwrap()
        .unwrap();
    assert!(pixels.bytes().iter().all(|pixel| *pixel == 0x55));
    drop(pixels);
    drop(canvas);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU query producer and native GPU; diskless acceptance"]
fn qemu_gl_queries_return_bounded_guest_state_and_hide_host_objects() {
    let (_device, _queue, host) = native_gpu();
    let directory =
        std::env::temp_dir().join(format!("dg-query-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 1;
    qemu.batch(
        sequence,
        &[
            (1, vec![0]),
            (3, vec![32, 32]),
            (5, vec![]),
            call(0x93c, &[0.25f32.to_bits(), (-0.5f32).to_bits(), 0]),
            call(
                0x0a3,
                &[
                    0.125f32.to_bits(),
                    0.25f32.to_bits(),
                    0.5f32.to_bits(),
                    1f32.to_bits(),
                ],
            ),
        ],
    );
    qemu.await_completion(sequence);
    let (kind, data) = query(&mut qemu, &mut sequence, 0x329, &[0x0ba2]);
    assert_eq!(kind, 2);
    assert_eq!(words(&data), [0, 0, 32, 32]);
    let (kind, data) = query(&mut qemu, &mut sequence, 0x2fb, &[0x0ba6]);
    assert_eq!(kind, 4);
    let matrix = data
        .as_chunks::<8>()
        .0
        .iter()
        .map(|word| f64::from_le_bytes(*word))
        .collect::<Vec<_>>();
    assert_eq!(
        matrix,
        [
            1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0.25, -0.5, 0., 1.
        ]
    );
    // Double matrix entrypoints retain the full sixteen-double wire payload;
    // test both decoding and multiplication through actual native GL state.
    let wide_words = |values: [f64; 16]| {
        values
            .into_iter()
            .flat_map(|value| {
                let bits = value.to_bits();
                [bits as u32, (bits >> 32) as u32]
            })
            .collect::<Vec<_>>()
    };
    sequence += 1;
    qemu.batch(
        sequence,
        &[
            call(
                0x502,
                &wide_words([
                    1., 0., 0., 0., 0., 2., 0., 0., 0., 0., 3., 0., 0.5, -0.25, 0.125, 1.,
                ]),
            ),
            call(
                0x5ff,
                &wide_words([
                    0.5, 0., 0., 0., 0., 2., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.,
                ]),
            ),
        ],
    );
    qemu.await_completion(sequence);
    let (kind, data) = query(&mut qemu, &mut sequence, 0x2fb, &[0x0ba6]);
    assert_eq!(kind, 4);
    assert_eq!(
        data.as_chunks::<8>()
            .0
            .iter()
            .map(|word| f64::from_le_bytes(*word))
            .collect::<Vec<_>>(),
        [
            0.5, 0., 0., 0., 0., 4., 0., 0., 0., 0., 3., 0., 0.5, -0.25, 0.125, 1.
        ]
    );
    let (kind, data) = query(&mut qemu, &mut sequence, 0x305, &[0x0c22]);
    assert_eq!(kind, 3);
    assert_eq!(
        words(&data),
        [
            0.125f32.to_bits(),
            0.25f32.to_bits(),
            0.5f32.to_bits(),
            1f32.to_bits()
        ]
    );
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x2cb, &[0x0b72]),
        (1, vec![1])
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x0d33]).1),
        [2048]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x0d3a]).1),
        [4096, 4096]
    );
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x405, &[0x1f00]),
        (5, b"DreamGPU\0".to_vec())
    );
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x405, &[0x1f03]),
        (5, vec![0])
    );

    sequence += 1;
    let mut records = vec![call(0x209, &[u32::MAX])];
    records.extend(upload(&[255; 16]));
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    // Internal texture upload consumes native errors for its own result, but
    // the earlier guest error must remain observable through GetError.
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x8069]).1),
        [GUEST_TEXTURE]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x416, &[TEXTURE_2D, 0, 0x1000]).1),
        [2]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x41d, &[TEXTURE_2D, 0x2801]).1),
        [0x2600]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1),
        [0x0500]
    );
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    sequence += 1;
    qemu.batch(sequence, &[call(BIND_TEXTURE, &[TEXTURE_2D, 0xfffffff1])]);
    qemu.await_completion(sequence);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x8069]).1),
        [0xfffffff1]
    );
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x4da, &[0xfffffff1]),
        (1, vec![1])
    );
    sequence += 1;
    qemu.batch(
        sequence,
        &[data_call(
            DELETE_TEXTURES,
            &[1],
            &0xfffffff1u32.to_le_bytes(),
        )],
    );
    qemu.await_completion(sequence);
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x4da, &[0xfffffff1]),
        (1, vec![0])
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x8069]).1),
        [0]
    );

    // Unsupported version and native framebuffer-name queries are rejected;
    // they never copy host capability claims or private object identities.
    for (function, pname) in [(0x405, 0x1f02), (0x329, 0x8ca6)] {
        sequence += 1;
        qemu.batch(sequence, &[(11, vec![function, pname, 0, 0])]);
        assert_eq!(qemu.read(0x1124), 3);
        assert_eq!(qemu.read(0x1168), 0);
    }
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU client-array producer and native GPU; diskless acceptance"]
fn qemu_client_arrays_draw_native_pixels_without_retaining_guest_pointers() {
    let (device, queue, host) = native_gpu();
    let directory =
        std::env::temp_dir().join(format!("dg-array-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 1;
    qemu.batch(
        sequence,
        &[
            (1, vec![0]),
            (3, vec![32, 32]),
            (5, vec![]),
            call(0x0db, &[0, 1f32.to_bits(), 0, 1f32.to_bits()]),
            data_call(0x1d5, &[7, 0, 4, 3], &vertices([1., 0., 0., 1.])),
            (7, vec![]),
        ],
    );
    // Mutating guest DMA memory after submission must not change this draw.
    qemu.command("memset 0x100000 512 0xff");
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |_, _| {
        [0, 0, 255, 255]
    });
    sequence += 1;
    qemu.batch(
        sequence,
        &[
            data_call(0x1d5, &[7, 0, 4, 1], &vertices([1., 0., 0., 1.])),
            (7, vec![]),
        ],
    );
    qemu.await_completion(sequence);
    // A color array must not leave host current-color or client state behind.
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |_, _| {
        [0, 255, 0, 255]
    });
    let colors = [
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    ];
    sequence += 1;
    let mut records = upload(&colors);
    records.push(call(0x209, &[TEXTURE_2D]));
    records.push(call(0x0db, &[1f32.to_bits(); 4]));
    records.push(data_call(0x1d5, &[7, 0, 4, 9], &vertices([1.; 4])));
    records.push((7, vec![]));
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |x, y| {
        checker(x, y, false)
    });
    for (index_type, index_size) in [(0x1401, 1), (0x1403, 2), (0x1405, 4)] {
        sequence += 1;
        let mut data = vertices([1.; 4]);
        for index in [0u32, 1, 2, 0, 2, 3] {
            data.extend_from_slice(&index.to_le_bytes()[..index_size]);
        }
        qemu.batch(
            sequence,
            &[
                data_call(0x1e6, &[4, 6, index_type, 4, 15], &data),
                (7, vec![]),
            ],
        );
        qemu.await_completion(sequence);
        assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |x, y| {
            checker(x, y, false)
        });
    }
    sequence += 1;
    qemu.batch(sequence, &[(8, vec![])]);
    drive_lifecycle(&mut qemu, sequence, &server, &device, &ready);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU texture producer and native host GPU; diskless acceptance"]
fn qemu_textures_isolate_names_share_explicitly_and_preserve_deleted_bindings() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-tex-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    qemu.write(0x1130, TEX_IMAGE_2D);
    assert_eq!(qemu.read(0x1134), 0x80000008);
    let mut sequence = 1;
    qemu.batch(
        sequence,
        &[(1, vec![0]), (3, vec![32, 32]), (5, vec![]), (7, vec![])],
    );
    qemu.await_completion(sequence);
    // Newly created attachments must never export uninitialized host contents.
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |_, _| {
        [0; 4]
    });

    let colors = [
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    ];
    sequence += 1;
    let mut records = upload(&colors);
    records.extend(quad());
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |x, y| {
        checker(x, y, false)
    });

    // Identical numeric names in another client and in an unshared context
    // must not alias the original texture, despite native internal sharing.
    for (client, context, drawable, rgba) in
        [(2, 1, 1, [255, 255, 0, 255]), (1, 2, 2, [0, 255, 255, 255])]
    {
        sequence += 1;
        let mut records = vec![(1, vec![0]), (3, vec![32, 32]), (5, vec![])];
        records.extend(upload(&rgba.repeat(4)));
        records.extend(quad());
        qemu.batch_for(sequence, client, context, drawable, 0, &records);
        qemu.await_completion(sequence);
        assert_gpu_image(
            &device,
            &queue,
            &receive(&server, &ready, client, drawable),
            |_, _| [rgba[2], rgba[1], rgba[0], rgba[3]],
        );
    }
    // Two independent readers both need a GPU dependency on the shared upload.
    for context in [3, 4] {
        sequence += 1;
        let mut records = vec![
            (1, vec![1]),
            (3, vec![32, 32]),
            (5, vec![]),
            call(BIND_TEXTURE, &[TEXTURE_2D, GUEST_TEXTURE]),
        ];
        records.extend(quad());
        qemu.batch_for(sequence, 1, context, context, 0, &records);
        qemu.await_completion(sequence);
        assert_gpu_image(
            &device,
            &queue,
            &receive(&server, &ready, 1, context),
            |x, y| checker(x, y, false),
        );
    }
    sequence += 1;
    qemu.batch(
        sequence,
        &[
            (5, vec![]),
            data_call(
                TEX_SUB_IMAGE_2D,
                &[TEXTURE_2D, 0, 0, 0, 1, 1, 0x1908, 0x1401],
                &[255, 0, 255, 255],
            ),
        ],
    );
    qemu.await_completion(sequence);
    sequence += 1;
    qemu.batch_for(sequence, 1, 3, 3, 0, &quad());
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 3), |x, y| {
        checker(x, y, true)
    });

    // Delete/recreate name17 in context1. Context3 still owns the old bound
    // object. Presentation must not temporarily unbind and recreate it either.
    sequence += 1;
    let mut records = vec![
        (5, vec![]),
        data_call(DELETE_TEXTURES, &[1], &GUEST_TEXTURE.to_le_bytes()),
    ];
    records.extend(upload(&[255, 255, 0, 255].repeat(4)));
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    for _ in 0..2 {
        sequence += 1;
        qemu.batch_for(sequence, 1, 3, 3, 0, &quad());
        qemu.await_completion(sequence);
        assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 3), |x, y| {
            checker(x, y, true)
        });
    }
    sequence += 1;
    let mut records = vec![call(BIND_TEXTURE, &[TEXTURE_2D, GUEST_TEXTURE])];
    records.extend(quad());
    qemu.batch_for(sequence, 1, 3, 3, 0, &records);
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 3), |_, _| {
        [0, 255, 255, 255]
    });

    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 2, 0, &quad());
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 2), |_, _| {
        [255, 255, 0, 255]
    });

    // A batch boundary can split an immediate primitive; presentation cannot.
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 2, 0, &[call(0x01a, &[7])]);
    qemu.await_completion(sequence);
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 2, 0, &[(7, vec![])]);
    let deadline = Instant::now() + Duration::from_secs(5);
    while qemu.read(0x111c) & 1 != 0 {
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(2));
    }
    assert_eq!(qemu.read(0x1124), 4);
    assert!(server.take_drawables().is_empty());
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 2, 0, &[call(0x216, &[]), (7, vec![])]);
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 2), |_, _| {
        [255, 255, 0, 255]
    });

    for client in [1, 2] {
        sequence += 1;
        qemu.batch_for(sequence, client, 1, 1, 0, &[(8, vec![])]);
        drive_lifecycle(&mut qemu, sequence, &server, &device, &ready);
    }
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU 1D textures/attribs and native GPU; diskless pixel acceptance"]
fn qemu_one_dimensional_textures_attrib_stack_and_texture_reads() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-1d-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    let one = 0x0de0;
    let pixels = (0..300)
        .flat_map(|i| {
            if i < 150 {
                [255, 0, 0, 255]
            } else {
                [0, 255, 0, 255]
            }
        })
        .collect::<Vec<_>>();
    let mut records = vec![
        (1, vec![0]),
        (3, vec![32, 32]),
        (5, vec![]),
        call(BIND_TEXTURE, &[one, 42]),
        call(TEX_PARAMETER_I, &[one, 0x2801, 0x2600]),
        call(TEX_PARAMETER_I, &[one, 0x2800, 0x2600]),
        data_call(0x8d3, &[one, 0, 0x1908, 300, 1, 0, 0x1908, 0x1401], &pixels),
        call(0x209, &[one]),
    ];
    // Full public attrib stack must still permit internal client-array draws.
    records.extend((0..16).map(|_| call(0x775, &[1])));
    records.push(data_call(0x1d5, &[7, 0, 4, 9], &vertices([1.; 4])));
    records.extend((0..16).map(|_| call(0x6af, &[])));
    records.push((7, vec![]));
    let mut sequence = 1;
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    let image = receive(&server, &ready, 1, 1);
    // Imported diagnostic images are BGRA; texture query payloads below are RGBA.
    assert_gpu_image(&device, &queue, &image, |x, _| {
        if x < 16 {
            [0, 0, 255, 255]
        } else {
            [0, 255, 0, 255]
        }
    });
    drop(image);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x8068]).1),
        [42]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x416, &[one, 0, 0x1000]).1),
        [300]
    );
    let (kind, data) = query(&mut qemu, &mut sequence, 0x414, &[one, 0, 0]);
    assert_eq!(kind, 2);
    assert_eq!(data, &pixels[..512]);
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x414, &[one, 0, 128]).1,
        &pixels[512..1024]
    );
    let tail = query(&mut qemu, &mut sequence, 0x414, &[one, 0, 256]).1;
    assert_eq!(&tail[..176], &pixels[1024..]);
    assert!(tail[176..].iter().all(|v| *v == 0));
    // A shared-object write invalidates the bounded texture-read snapshot.
    query(&mut qemu, &mut sequence, 0x414, &[one, 0, 0]);
    sequence += 1;
    qemu.batch(
        sequence,
        &[data_call(
            0x8f3,
            &[one, 0, 128, 0, 128, 1, 0x1908, 0x1401],
            &[0, 0, 255, 255].repeat(128),
        )],
    );
    qemu.await_completion(sequence);
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x414, &[one, 0, 128]).1,
        [0, 0, 255, 255].repeat(128)
    );
    sequence += 1;
    qemu.batch(
        sequence,
        &[
            call(0x775, &[0x40000 | 0x4000 | 0x800]),
            call(BIND_TEXTURE, &[one, 43]),
            data_call(0x8d3, &[one, 0, 0x1908, 2, 1, 0, 0x1908, 0x1401], &[255; 8]),
            call(0xb37, &[0, 0, 8, 8]), // viewport
            call(0x1dc, &[0x404]),      // draw buffer FRONT
            call(0x6af, &[]),
        ],
    );
    qemu.await_completion(sequence);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x8068]).1),
        [42]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x0c01]).1),
        [0x405]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[0x0ba2]).1),
        [0, 0, 32, 32]
    );
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    sequence += 1;
    qemu.batch(sequence, &[(8, vec![])]);
    drive_lifecycle(&mut qemu, sequence, &server, &device, &ready);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU query producer and native GPU; diskless acceptance"]
fn qemu_wine_legacy_limits_and_sized_texture_formats() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!(
        "jwc-{}",
        &uuid::Uuid::new_v4().simple().to_string()[..8]
    ));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let _server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 1;
    qemu.batch(sequence, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])]);
    qemu.await_completion(sequence);
    for pname in [0x846d, 0x846e] {
        // Aliased point/line ranges, exactly two floats.
        let (kind, bytes) = query(&mut qemu, &mut sequence, 0x305, &[pname]);
        assert_eq!(kind, 3);
        assert_eq!(bytes.len(), 8);
        let values = bytes
            .as_chunks::<4>()
            .0
            .iter()
            .map(|b| f32::from_le_bytes(*b))
            .collect::<Vec<_>>();
        assert!(values[0].is_finite() && values[0] > 0.0);
        assert!(values[1].is_finite() && values[1] >= values[0]);
    }
    for internal in [0x2a10, 0x8043, 0x804f, 0x8050, 0x8056, 0x8057] {
        sequence += 1;
        qemu.batch(
            sequence,
            &[
                call(BIND_TEXTURE, &[TEXTURE_2D, 17]),
                data_call(
                    TEX_IMAGE_2D,
                    &[TEXTURE_2D, 0, internal, 2, 1, 0, 0x1908, 0x1401],
                    &[255, 255, 255, 255, 0, 0, 0, 255],
                ),
            ],
        );
        qemu.await_completion(sequence);
        let (kind, bytes) = query(&mut qemu, &mut sequence, 0x414, &[TEXTURE_2D, 0, 0]);
        assert_eq!(kind, 2);
        assert_eq!(bytes.len(), 512);
        // GL 2.1 table 6.1: GetTexImage returns luminance in R only;
        // texture sampling replicates it, but an RGBA image read does not.
        let expected = if internal == 0x8043 {
            [255, 0, 0, 255, 0, 0, 0, 255]
        } else {
            [255, 255, 255, 255, 0, 0, 0, 255]
        };
        assert_eq!(&bytes[..8], &expected, "internal format {internal:x}");
        assert!(bytes[8..].iter().all(|b| *b == 0));
    }
    drop(qemu);
    drop(_server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU query producer and native GPU; diskless acceptance"]
fn qemu_wine_texture_combine_preserves_state_and_pixels() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!(
        "jwe-{}",
        &uuid::Uuid::new_v4().simple().to_string()[..8]
    ));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 1;
    qemu.batch(sequence, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])]);
    qemu.await_completion(sequence);
    let mut records = upload(&[255; 16]);
    // Exercise the exact Wine failure and the whole bounded scalar family.
    for (pname, value) in [
        (0x8571, 0x1e01),
        (0x8572, 0x1e01),
        (0x8580, 0x8577),
        (0x8581, 0x1702),
        (0x8582, 0x8578),
        (0x8588, 0x8577),
        (0x8589, 0x1702),
        (0x858a, 0x8578),
        (0x8590, 0x0300),
        (0x8591, 0x0300),
        (0x8592, 0x0302),
        (0x8598, 0x0302),
        (0x8599, 0x0302),
        (0x859a, 0x0302),
        (0x8573, 1),
        (0x0d1c, 1),
    ] {
        records.push(call(0x8c6, &[0x2300, pname, value]));
    }
    records.push(call(0x8c6, &[0x2300, 0x2200, 0x8570])); // ENV_MODE COMBINE
    records.push(call(0x0db, &[0, 1f32.to_bits(), 0, 1f32.to_bits()])); // PRIMARY_COLOR green
    let mut draw = quad();
    draw.pop(); // Query current BACK without export.
    records.extend(draw);
    sequence += 1;
    qemu.batch(sequence, &records);
    qemu.await_completion(sequence);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x40d, &[0x2300, 0x8571]).1),
        [0x1e01]
    );
    let (_, pixels) = query(&mut qemu, &mut sequence, 0x7a4, &[0, 0, 8 | (8 << 16)]);
    assert_eq!(pixels.len(), 256);
    for &pixel in pixels.as_chunks::<4>().0 {
        assert_eq!(pixel, [0, 255, 0, 255]);
    }
    // Vector float scale updates and typed queries share the same bounds.
    sequence += 1;
    qemu.batch(
        sequence,
        &[data_call(0x8c5, &[0x2300, 0x8573], &2f32.to_le_bytes())],
    );
    qemu.await_completion(sequence);
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x40c, &[0x2300, 0x8573]).1,
        2f32.to_le_bytes()
    );
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU query producer and native GPU; diskless acceptance"]
fn qemu_texture_component_queries_return_actual_storage() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!(
        "jtb-{}",
        &uuid::Uuid::new_v4().simple().to_string()[..8]
    ));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 1;
    qemu.batch(sequence, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])]);
    qemu.await_completion(sequence);
    for (format, expected) in [(0x8058, [8, 8, 8, 8, 0, 0]), (0x8051, [8, 8, 8, 0, 0, 0])] {
        sequence += 1;
        qemu.batch(
            sequence,
            &[
                call(BIND_TEXTURE, &[TEXTURE_2D, 17]),
                data_call(
                    TEX_IMAGE_2D,
                    &[TEXTURE_2D, 0, format, 2, 1, 0, 0x1908, 0x1401],
                    &[255; 8],
                ),
            ],
        );
        qemu.await_completion(sequence);
        for (pname, value) in (0x805c..=0x8061).zip(expected) {
            let (kind, result) = query(&mut qemu, &mut sequence, 0x416, &[TEXTURE_2D, 0, pname]);
            assert_eq!(kind, 2);
            assert_eq!(words(&result), [value], "format {format:x} pname {pname:x}");
        }
    }
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires native GL copies; out-of-source pixels are undefined, destination stays bounded"]
fn qemu_texture_copy_accepts_padded_and_signed_source_rectangles() {
    let (_, _, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!(
        "jrc-{}",
        &uuid::Uuid::new_v4().simple().to_string()[..16]
    ));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 1;
    qemu.batch(
        sequence,
        &[
            (1, vec![0]),
            (3, vec![32, 32]),
            (5, vec![]),
            call(0x0a3, &[1f32.to_bits(), 0, 0, 1f32.to_bits()]),
            call(0x09a, &[0x4000]),
            call(BIND_TEXTURE, &[TEXTURE_2D, 51]),
        ],
    );
    qemu.await_completion(sequence);
    // Wine copies framebuffer-sized/padded storage; source overflow is not
    // destination overflow. Exact samples cover only the defined intersection.
    for (x, y, w, h, dx, dy, valid_x, valid_y, valid_w, valid_h) in [
        (0i32, 0i32, 64, 64, 0, 0, 0, 0, 32, 32),
        (-2, -3, 8, 8, 10, 12, 12, 15, 6, 5),
        (30, 29, 8, 8, 4, 6, 4, 6, 2, 3),
        (i32::MIN, i32::MIN, 8, 8, 0, 0, 0, 0, 0, 0),
        (i32::MAX, i32::MAX, 8, 8, 0, 0, 0, 0, 0, 0),
    ] {
        sequence += 1;
        qemu.batch(
            sequence,
            &[
                data_call(
                    TEX_IMAGE_2D,
                    &[TEXTURE_2D, 0, 0x1908, 64, 64, 0, 0x1908, 0x1401],
                    &[],
                ),
                call(0x15a, &[TEXTURE_2D, 0, dx, dy, x as u32, y as u32, w, h]),
            ],
        );
        qemu.await_completion(sequence);
        assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
        for row in valid_y..valid_y + valid_h {
            let (_, pixels) = query(
                &mut qemu,
                &mut sequence,
                0x414,
                &[TEXTURE_2D, 0, row * 64 + valid_x],
            );
            assert_eq!(
                &pixels[..valid_w as usize * 4],
                [255, 0, 0, 255].repeat(valid_w as usize)
            );
        }
    }
    sequence += 1;
    qemu.batch(
        sequence,
        &[call(
            0x156,
            &[
                TEXTURE_2D,
                0,
                0x1908,
                (-4i32) as u32,
                (-4i32) as u32,
                64,
                64,
                0,
            ],
        )],
    );
    qemu.await_completion(sequence);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x416, &[TEXTURE_2D, 0, 0x1000]).1),
        [64]
    );
    for row in 4..36 {
        let (_, pixels) = query(
            &mut qemu,
            &mut sequence,
            0x414,
            &[TEXTURE_2D, 0, row * 64 + 4],
        );
        assert_eq!(&pixels[..128], [255, 0, 0, 255].repeat(32));
    }
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires bundled QEMU query producer and native GPU; diskless acceptance"]
fn qemu_pixel_store_queries_have_typed_bounded_results() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!(
        "jps-{}",
        &uuid::Uuid::new_v4().simple().to_string()[..8]
    ));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let _server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 1;
    qemu.batch(sequence, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])]);
    qemu.await_completion(sequence);
    // Application pixel stores live in the frontend; native transfer helpers
    // restore their own pack/unpack state. Typed queries still have a complete
    // bounded contract if a protocol client requests the native state directly.
    for base in [0x0cf0, 0x0d00] {
        for offset in 0..6 {
            let expected = if offset == 5 { 4u32 } else { 0 };
            assert_eq!(
                words(&query(&mut qemu, &mut sequence, 0x329, &[base + offset]).1),
                [expected]
            );
            assert_eq!(
                query(&mut qemu, &mut sequence, 0x2cb, &[base + offset]).1,
                [(expected != 0) as u8]
            );
            assert_eq!(
                query(&mut qemu, &mut sequence, 0x305, &[base + offset]).1,
                (expected as f32).to_le_bytes()
            );
            assert_eq!(
                query(&mut qemu, &mut sequence, 0x2fb, &[base + offset]).1,
                (expected as f64).to_le_bytes()
            );
        }
    }
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(qemu);
    drop(_server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires native GPU; negotiated64KiB readback and legacy512 contract"]
fn qemu_bulk_readback_pixels_legacy_and_dma_bounds() {
    let (_, _, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!(
        "jrb-{}",
        &uuid::Uuid::new_v4().simple().to_string()[..8]
    ));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 1;
    assert_ne!(qemu.read(0x1008) & 0x800, 0);
    qemu.batch(
        sequence,
        &[
            (1, vec![0]),
            (3, vec![128, 128]),
            (5, vec![]),
            call(0x0a3, &[0, 1f32.to_bits(), 0, 1f32.to_bits()]),
            call(0x09a, &[0x4000]),
            call(BIND_TEXTURE, &[TEXTURE_2D, 51]),
            call(0x156, &[TEXTURE_2D, 0, 0x1908, 0, 0, 128, 128, 0]),
        ],
    );
    qemu.await_completion(sequence);
    let expected = [0, 255, 0, 255].repeat(16384);
    assert_eq!(
        query_capacity(
            &mut qemu,
            &mut sequence,
            0x414,
            &[TEXTURE_2D, 16384 << 16, 0],
            65536
        )
        .1,
        expected
    );
    assert_eq!(
        query_capacity(
            &mut qemu,
            &mut sequence,
            0x7a4,
            &[0, 0, 128 | (128 << 16)],
            65536
        )
        .1,
        expected
    );
    assert_eq!(
        query(&mut qemu, &mut sequence, 0x414, &[TEXTURE_2D, 0, 128]).1,
        [0, 255, 0, 255].repeat(128)
    );
    assert_eq!(
        query_capacity(
            &mut qemu,
            &mut sequence,
            0x414,
            &[TEXTURE_2D, 3 << 16, 16381],
            12
        )
        .1,
        [0, 255, 0, 255].repeat(3)
    );
    // Oversize tile count and undersized DMA destination cannot write memory.
    for (count, capacity) in [(16385, 65536), (16384, 512)] {
        qemu.command("memset 0x200000 65552 0x5a");
        qemu.write(0x115c, 0x200000);
        qemu.write(0x1164, capacity);
        sequence += 1;
        qemu.batch(sequence, &[(11, vec![0x414, TEXTURE_2D, count << 16, 0])]);
        let deadline = Instant::now() + Duration::from_secs(5);
        while qemu.read(0x1120) != sequence {
            assert!(Instant::now() < deadline);
            std::thread::sleep(Duration::from_millis(2));
        }
        assert_ne!(qemu.read(0x1124), 0);
        assert_eq!(qemu.read(0x1168), 0);
        assert!(
            qemu.command("read 0x200000 65552")
                .ends_with(&format!("0x{}", "5a".repeat(65552)))
        );
    }
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires native GPU; compact original-type arrays and immutable list replay"]
fn qemu_compact_arrays_preserve_native_types_pixels_current_state_and_lists() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-raw-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let server = GpuServer::new(&directory.join("gpu.sock"), host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &directory.join("gpu.sock"));
    let mut sequence = 1;
    qemu.batch(
        sequence,
        &[
            (1, vec![0]),
            (3, vec![32, 32]),
            (5, vec![]),
            call(219, &[0, 1f32.to_bits(), 0, 1f32.to_bits()]),
        ],
    );
    qemu.await_completion(sequence);
    // Compact double2 positions retain native missing z=0,w=1 defaults;
    // unsigned-byte colors are normalized by native GL, not the guest packer.
    let mut data = vec![0u8; 112];
    data[..4].copy_from_slice(&(0x140au32 | (2 << 16)).to_le_bytes());
    data[4..8].copy_from_slice(&(0x1401u32 | (4 << 16)).to_le_bytes());
    for (i, value) in [-1f64, -1., 1., -1., 1., 1., -1., 1.]
        .into_iter()
        .enumerate()
    {
        data[32 + i * 8..40 + i * 8].copy_from_slice(&value.to_le_bytes());
    }
    data[96..112].copy_from_slice(&[255, 0, 0, 255].repeat(4));
    sequence += 1;
    qemu.batch(
        sequence,
        &[data_call(469, &[7, 0, 4, 0x80000003], &data), (7, vec![])],
    );
    qemu.command("memset 0x100000 512 0xff");
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |_, _| {
        [0, 0, 255, 255]
    });
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 773, &[0x0b00]).1),
        [0, 1f32.to_bits(), 0, 1f32.to_bits()]
    );
    // Compile snapshots the whole compact descriptor+data command. Overwrite
    // the DMA region before replay; lists cannot retain caller data or pointers.
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 1592, &[7, 0x1300, 0]).1),
        [0]
    );
    sequence += 1;
    qemu.batch(sequence, &[data_call(469, &[7, 0, 4, 0x80000003], &data)]);
    qemu.await_completion(sequence);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 539, &[0, 0, 0]).1),
        [0]
    );
    data[4..8].fill(0);
    data.truncate(96);
    sequence += 1;
    qemu.batch(
        sequence,
        &[data_call(469, &[7, 0, 4, 0x80000001], &data), (7, vec![])],
    );
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |_, _| {
        [0, 255, 0, 255]
    });
    sequence += 1;
    qemu.batch(sequence, &[call(146, &[7]), (7, vec![])]);
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |_, _| {
        [0, 0, 255, 255]
    });
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
