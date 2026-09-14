use super::*;

fn command(op: u32, bpp: u32, width: u32, height: u32, offset: u32, stride: u32) -> Vec<u32> {
    let mut words = desktop_command(op, 0, 0, width, height, offset, stride);
    words[8] = if bpp == 16 { 16 } else { 0 };
    words
}

fn cpu_frame(
    cpu: &mut ShmemServer,
    gate: Option<&GpuServer>,
    width: u32,
    height: u32,
) -> dreamgpu::FrameLease {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        cpu.poll();
        if let Some(display) = cpu.display_mut() {
            display.refresh();
            let epoch = display.epoch();
            if let Some(frame) = display.acquire_frame() {
                if frame.width == width
                    && frame.height == height
                    && gate.is_none_or(|g| g.permits_cpu_frame(epoch, frame.generation))
                {
                    return frame;
                }
                display.discard_current();
            }
        }
        assert!(
            Instant::now() < deadline,
            "coherent primary frame {width}x{height} unavailable"
        );
        std::thread::sleep(Duration::from_millis(2));
    }
}

#[test]
#[ignore = "requires changed native QEMU and host GPU; RGB565 primary ownership boundaries"]
fn qemu_rgb565_primary_seed_gl_blit_patch_readback_and_mode_return() {
    let (device, queue, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dg-p16-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&dir).unwrap();
    let socket = dir.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let cpu_socket = dir.join("cpu.sock");
    let mut cpu = ShmemServer::new(cpu_socket.to_str().unwrap()).unwrap();
    let mut qemu = Qemu::start_with_display(&dir, &socket, Some(&cpu_socket));
    let mut canvas = dreamgpu::presentation::desktop::DesktopCanvas::default();
    let mut seq = 0;
    // A real 32 -> 16 -> 32 mode cycle keeps the original fast path covered.
    for (cycle, bpp) in [32, 16, 32].into_iter().enumerate() {
        // RGB565 exceeds the 256KiB compositor quantum and splits within a
        // row, exercising resumed primary/compositor offsets independently.
        let width = if bpp == 16 { 520 } else { 64 };
        let height = if bpp == 16 { 128 } else { 32 };
        let stride = width * (bpp / 8);
        for (index, value) in [(4, 0), (1, width), (2, height), (3, bpp), (4, 0x41)] {
            qemu.command(&format!(
                "writew {:#x} {value:#x}",
                0xf0000500u32 + index * 2
            ));
        }
        qemu.command("outb 0x3c0 0x20");
        // Red RGB565 and red XRGB8888 primary, plus a sentinel past its extent.
        let pattern = if bpp == 16 { "00f8" } else { "0000ffff" };
        qemu.command(&format!(
            "write 0xe0000000 {} 0x{}",
            stride * height,
            pattern.repeat((width * height) as usize)
        ));
        qemu.command(&format!(
            "memset {:#x} 16 0xa5",
            0xe0000000u32 + stride * height
        ));
        let old = cpu_frame(&mut cpu, None, width, height);
        drop(old);
        seq += 1;
        qemu.batch(seq, &[(9, command(1, bpp, width, height, 0, stride))]);
        drive_desktop(
            &mut qemu,
            seq,
            1,
            true,
            &server,
            &mut canvas,
            &device,
            &queue,
            &ready,
        );
        seq += 1;
        let id = cycle as u32 + 1;
        qemu.batch_for(
            seq,
            1,
            id,
            id,
            2,
            &[
                (1, vec![0]),
                (3, vec![16, 16]),
                (5, vec![]),
                (6, vec![0x0a3, 0, 1f32.to_bits(), 0, 1f32.to_bits()]),
                (6, vec![0x09a, 0x4000]),
                (7, vec![]),
            ],
        );
        qemu.await_completion(seq);
        let mut blit = command(6, bpp, 16, 16, qemu.read(0x1140), 0);
        blit[1] = 1;
        blit[2] = 8;
        blit[3] = 8;
        for (word, reg) in [(12, 0x1144), (13, 0x1148), (14, 0x114c), (15, 0x1150)] {
            blit[word] = qemu.read(reg);
        }
        seq += 1;
        qemu.batch_for(seq, 1, id, id, 0, &[(9, blit)]);
        drive_desktop(
            &mut qemu,
            seq,
            2,
            true,
            &server,
            &mut canvas,
            &device,
            &queue,
            &ready,
        );
        let mut patch = command(2, bpp, 2, 2, 0x100000, 2 * (bpp / 8));
        patch[2] = 9;
        patch[3] = 9;
        let blue = if bpp == 16 { "1f00" } else { "ff0000ff" };
        qemu.command(&format!(
            "write 0xe0100000 {} 0x{}",
            4 * (bpp / 8),
            blue.repeat(4)
        ));
        seq += 1;
        qemu.batch(
            seq,
            &[(9, patch), (9, command(7, bpp, width, height, 0, stride))],
        );
        drive_desktop(
            &mut qemu,
            seq,
            4,
            true,
            &server,
            &mut canvas,
            &device,
            &queue,
            &ready,
        );
        let reply = qemu.command(&format!("read 0xe0000000 {}", stride * height + 16));
        let hex = reply
            .split_whitespace()
            .nth(1)
            .unwrap()
            .trim_start_matches("0x");
        let pixels: Vec<u8> = (0..hex.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).unwrap())
            .collect();
        for y in 0..height as usize {
            for x in 0..width as usize {
                let color = if (9..11).contains(&x) && (9..11).contains(&y) {
                    2
                } else if (8..24).contains(&x) && (8..24).contains(&y) {
                    1
                } else {
                    0
                };
                let at = y * stride as usize + x * (bpp / 8) as usize;
                if bpp == 16 {
                    assert_eq!(
                        &pixels[at..at + 2],
                        [0xf800u16, 0x07e0, 0x001f][color].to_le_bytes()
                    );
                } else {
                    assert_eq!(
                        &pixels[at..at + 4],
                        [[0, 0, 255, 255], [0, 255, 0, 255], [255, 0, 0, 255]][color]
                    );
                }
            }
        }
        assert_eq!(&pixels[(stride * height) as usize..], &[0xa5; 16]);
        seq += 1;
        qemu.batch(seq, &[(9, command(3, bpp, width, height, 0, stride))]);
        drive_desktop(
            &mut qemu,
            seq,
            5,
            false,
            &server,
            &mut canvas,
            &device,
            &queue,
            &ready,
        );
        let frame = cpu_frame(&mut cpu, Some(&server), width, height);
        assert_eq!((frame.width, frame.height), (width, height));
        drop(frame);
    }
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(dir).unwrap();
}
